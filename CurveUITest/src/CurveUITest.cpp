// Slow Ops Curve UI Test: a diagnostic plugin for the MPC's touchscreen widgets.
// Experiment A of docs MPC_Curve_UI_Research_and_Test_Plan.md: does the native XYPad drive two plugin
// parameters reliably, and how does it behave?
//
// Audio passes straight through (with a Level control), so audio dropouts stay audible during tests.
//
// Five pads, each bound to its own X / Y parameter pair:
//   Pad A   FULL tab: one large pad (edge reach, speed, tap vs drag, release, Q-Links, Auto Move)
//   Pad B/C SPLIT tab: two pads side by side
//   Pad D/E STACK tab: two pads in the same rectangle, E on top
// Readouts (read-only parameters shown as Value labels, updated at most 20 times a second):
//   Updates/s  pad parameter changes from the screen in the last second
//   Last Pad   which pad sent the last change
//   Gap ms     time since the last pad change (does input stop when the finger lifts? stays still?)
//   X/Y Min/Max  the extremes Pad A has reported since Reset (edge reach)
// Auto Move: the plugin itself moves Pad A in a circle, to see whether the puck follows
// plugin-side changes. Its own writes are not counted as touches.
//
// CURVE tab (does a film-strip EQ curve render well?): one bell, moved by a directly bound XYPad
// (Bell X = log frequency, Bell Y = gain, Y = 1 at the top) and a Q knob. The plugin draws:
//   * the curve: 64 columns at 0.5 dB, each frame a fill from 0 dB plus a dot, each column showing the
//     extreme of its frequency span; drawn either as upper / lower half strips (HALVES, 15,768 px) or
//     one full-height strip (TALL, 31,536 px: past the 16,384 px texture limit);
//   * the band shading: 32 columns at 1 dB;
// sending at most Refresh (8 / 16 / 24 / 32 / all) changed columns per 30 Hz refresh. Sweep moves the
// bell by itself. Curve Rate reads the curve updates sent per second.
// LINES tab: native Path (static, and bound to Line Value) and Plotter (bound to Line Value, with
// history); Line Sweep moves Line Value by itself.

#include "mpc_plugin.h"

#include "../../core/include/slowops/eqartist/Filters.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

class CurveUITest : public mpc::Plugin
{
public:
    enum
    {
        kLevel,                     // index 0: the MPC nudges it on every prepare; harmless
        kPadAX, kPadAY, kPadBX, kPadBY, kPadCX, kPadCY, kPadDX, kPadDY, kPadEX, kPadEY,
        kAutoMove,                  // toggle: the plugin moves Pad A in a circle
        kReset,                     // trigger: reset Min/Max and the counters
        kRate, kLastPad, kGap,      // read-only readouts
        kMinX, kMaxX, kMinY, kMaxY, // read-only: Pad A extremes since Reset
        kSlider1, kSlider2,         // SAFE tab: plain sliders
        kBellX, kBellY, kBellQ,     // CURVE tab: the test bell
        kStrip,                     // Halves / Tall
        kRefresh,                   // 8 / 16 / 24 / 32 / All curve updates per refresh
        kSweep,                     // the plugin moves the bell
        kCurveRate,                 // read-only: curve updates sent in the last second
        kCurve0,                    // read-only: 64 curve columns, then 32 shade columns
        kShade0 = kCurve0 + 64,
        kLineValue = kShade0 + 32,  // LINES tab: drives the bound Path and the Plotter
        kLineSweep,
        kLine0,                     // read-only: 152 LINE-mode columns (height and slope in one value)
        // ENV tab (Experiment B): native Envelope widgets, every point bound to a parameter.
        kEnvA = kLine0 + 152, kEnvD, kEnvS, kEnvR,      // ADSR stages
        kEnvShA, kEnvShD, kEnvShR,                      // curve shapes (attack, decay, release)
        kPit0,                                          // Pitch: L1, Delay, L2, Attack, L3, Decay, Sustain, Release, L5
        kAdA = kPit0 + 9, kAdD, kAdEnd,                 // AD: attack, decay, decay-from-end
        kEnvSweep,                                      // the plugin moves every point by itself
        // ENV2 tab: measuring the Pitch envelope's geometry, and whether curves can bend.
        kWidth,                                         // all ruler stage durations (slider)
        kBend,                                          // every shape handle on ENV2 (slider)
        kRul0,                                          // ruler Pitch points (set from Width): L1, Delay, L2, Attack, L3, Decay, Sustain, Release, L5
        kNumParams = kRul0 + 9
    };
    static constexpr int kNumPads = 5;
    static constexpr int kCurveCols = 64, kShadeCols = 32;
    static constexpr float kRangeDb = 18.0f;
    static constexpr int kCurveFrames = 73, kShadeFrames = 37;   // 0.5 dB and 1 dB steps over +-18 dB
    // LINE mode: a connected polyline over kLineCols columns of 8 px. Its vertices sit on the column
    // boundaries, snapped to 0.125 dB (289 levels); each column draws the segment from its left vertex
    // to its right vertex (and the fill under it), so neighbouring columns share their end points
    // exactly. Frame = left level x rise, the rise (in 0.125 dB steps) taken from kRises: fine for
    // gentle slopes, coarser for steep ones, up to 10 dB per column. Must match scripts/layout.py
    // (LINE_COLS, LINE_STEP_DB, LINE_RISES).
    static constexpr int kLineCols = 152;
    static constexpr int kLineLevels = 289;
    static constexpr int kNumRises = 31;
    static constexpr int kRises[kNumRises] = {-80, -64, -48, -40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0,
                                              1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80};
    static constexpr int kLineFrames = kLineLevels * kNumRises;

    CurveUITest() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("level", "Level", -24.0f, 6.0f, 0.0f, "dB", 1));
        const char* names = "ABCDE";
        for (int p = 0; p < kNumPads; ++p)
            for (const char* axis : {"X", "Y"})
            {
                const std::string id = std::string("pad") + (char)(names[p] + 32) + (char)(axis[0] + 32);
                const std::string name = std::string("Pad ") + names[p] + " " + axis;
                // Pad E starts right of centre so it does not hide Pad D (same rectangle, STACK tab).
                const float def = (p == 4 && axis[0] == 'X') ? 0.7f : 0.5f;
                addParam(mpc::floatParam(id.c_str(), name.c_str(), 0.0f, 1.0f, def, "", 3));
            }
        addParam(mpc::boolParam("automove", "Auto Move", false));
        addParam(mpc::boolParam("reset", "Reset", false));
        addDisplay(mpc::intParam("rate", "Updates/s", 0, 999, 0));
        addDisplay(mpc::choiceParam("lastpad", "Last Pad", {"-", "A", "B", "C", "D", "E"}, 0));
        addDisplay(mpc::intParam("gap", "Gap ms", 0, 9999, 9999));
        addDisplay(mpc::floatParam("minx", "X Min", 0.0f, 1.0f, 1.0f, "", 3));
        addDisplay(mpc::floatParam("maxx", "X Max", 0.0f, 1.0f, 0.0f, "", 3));
        addDisplay(mpc::floatParam("miny", "Y Min", 0.0f, 1.0f, 1.0f, "", 3));
        addDisplay(mpc::floatParam("maxy", "Y Max", 0.0f, 1.0f, 0.0f, "", 3));
        addParam(mpc::floatParam("slider1", "Slider 1", 0.0f, 1.0f, 0.5f, "", 2));
        addParam(mpc::floatParam("slider2", "Slider 2", 0.0f, 1.0f, 0.5f, "", 2));
        addParam(mpc::floatParam("bellx", "Bell X", 0.0f, 1.0f, 0.5f, "", 3));
        addParam(mpc::floatParam("belly", "Bell Y", 0.0f, 1.0f, 0.7f, "", 3));
        addParam(mpc::logParam("bellq", "Bell Q", 0.3f, 10.0f, 1.5f, "", 2));
        addParam(mpc::choiceParam("strip", "Strip", {"Halves", "Tall", "Line"}, 0));
        addParam(mpc::choiceParam("refresh", "Refresh", {"8", "16", "24", "32", "All"}, 1));
        addParam(mpc::boolParam("sweep", "Sweep", false));
        addDisplay(mpc::intParam("curverate", "Curve Rate", 0, 9999, 0));
        for (int c = 0; c < kCurveCols; ++c)
            addDisplay(mpc::floatParam(("curve" + std::to_string(c + 1)).c_str(), ("Curve " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.5f, "", 3));
        for (int c = 0; c < kShadeCols; ++c)
            addDisplay(mpc::floatParam(("shade" + std::to_string(c + 1)).c_str(), ("Shade " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.5f, "", 3));
        addParam(mpc::floatParam("linevalue", "Line Value", 0.0f, 1.0f, 0.5f, "", 3));
        addParam(mpc::boolParam("linesweep", "Line Sweep", false));
        for (int c = 0; c < kLineCols; ++c)
            addDisplay(mpc::floatParam(("line" + std::to_string(c + 1)).c_str(), ("Line " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.0f, "", 4));
        // ENV tab: plain 0..1 parameters, one per envelope point (the widget draws from them).
        const char* adsr[] = {"Env A", "Env D", "Env S", "Env R", "Env Shape A", "Env Shape D", "Env Shape R"};
        const float adsrDef[] = {0.25f, 0.35f, 0.6f, 0.4f, 0.5f, 0.5f, 0.5f};
        for (int k = 0; k < 7; ++k) addParam(mpc::floatParam(("env" + std::to_string(k)).c_str(), adsr[k], 0.0f, 1.0f, adsrDef[k], "", 3));
        const char* pitch[] = {"Pitch L1", "Pitch Delay", "Pitch L2", "Pitch Attack", "Pitch L3", "Pitch Decay", "Pitch Sustain", "Pitch Release", "Pitch L5"};
        const float pitchDef[] = {0.5f, 0.15f, 0.8f, 0.2f, 0.3f, 0.25f, 0.6f, 0.3f, 0.5f};
        for (int k = 0; k < 9; ++k) addParam(mpc::floatParam(("pitch" + std::to_string(k)).c_str(), pitch[k], 0.0f, 1.0f, pitchDef[k], "", 3));
        addParam(mpc::floatParam("ada", "AD Attack", 0.0f, 1.0f, 0.4f, "", 3));
        addParam(mpc::floatParam("add", "AD Decay", 0.0f, 1.0f, 0.4f, "", 3));
        addParam(mpc::floatParam("adend", "AD End", 0.0f, 1.0f, 0.5f, "", 3));
        addParam(mpc::boolParam("envsweep", "Env Sweep", false));
        addParam(mpc::floatParam("width", "Width", 0.0f, 1.0f, 0.25f, "", 2));
        addParam(mpc::floatParam("bend", "Bend", 0.0f, 1.0f, 0.5f, "", 2));
        const char* rul[] = {"Ruler L1", "Ruler Delay", "Ruler L2", "Ruler Attack", "Ruler L3", "Ruler Decay", "Ruler Sustain", "Ruler Release", "Ruler L5"};
        for (int k = 0; k < 9; ++k) addDisplay(mpc::floatParam(("ruler" + std::to_string(k)).c_str(), rul[k], 0.0f, 1.0f, 0.5f, "", 3));
        if (numParams() != kNumParams) mpc::log("CurveUITest: parameter count mismatch");
    }

    void prepare(double sampleRate, int) override
    {
        displayEvery_ = (int)(sampleRate / 20.0);
        oneSecond_ = (int)sampleRate;
    }

    // Also called after a state load: restoring pad values must not count as touches.
    void reset() override
    {
        if (getBool(kReset)) setNormalised(kReset, 0.0f);
        for (int p = 0; p < kNumPads; ++p) lastSeq_[p] = seq_[p].load(std::memory_order_relaxed);
    }

    // From the host, between process calls. Counts touches; the plugin's own Auto Move writes
    // happen inside process() with selfWrite_ set and are not counted.
    void parameterChanged(int index, float) override
    {
        if (selfWrite_) return;
        if (index >= kPadAX && index <= kPadEY) seq_[(index - kPadAX) / 2].fetch_add(1, std::memory_order_relaxed);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        const float g = std::pow(10.0f, get(kLevel) / 20.0f);
        for (int c = 0; c < ctx.numOutputs; ++c)
        {
            const float* in = c < ctx.numInputs ? ctx.inputs[c] : nullptr;
            float* out = ctx.outputs[c];
            for (int i = 0; i < n; ++i) out[i] = in ? in[i] * g : 0.0f;
        }

        updateCurve(n);
        updateLineSweep(n);
        updateEnvSweep(n);
        updateRuler();

        if (getBool(kReset))
        {
            setValue(kReset, 0.0f);
            minX_ = minY_ = 1.0f;
            maxX_ = maxY_ = 0.0f;
            eventsThisSecond_ = 0;
            setValue(kRate, 0.0f);
            setValue(kLastPad, 0.0f);
            minMaxDirty_ = true;
        }

        // Touches since the last block.
        for (int p = 0; p < kNumPads; ++p)
        {
            const uint32_t s = seq_[p].load(std::memory_order_relaxed);
            if (s == lastSeq_[p]) continue;
            eventsThisSecond_ += (int)(s - lastSeq_[p]);
            lastSeq_[p] = s;
            lastPad_ = p + 1;
            sinceTouch_ = 0;
            if (p == 0)
            {
                const float x = getNormalised(kPadAX), y = getNormalised(kPadAY);
                minX_ = std::min(minX_, x); maxX_ = std::max(maxX_, x);
                minY_ = std::min(minY_, y); maxY_ = std::max(maxY_, y);
                minMaxDirty_ = true;
            }
        }
        sinceTouch_ = std::min(sinceTouch_ + n, 1 << 30);

        // Auto Move: Pad A in a circle, one turn every 4 s.
        if (getBool(kAutoMove))
        {
            autoPhase_ += (double)n / (4.0 * sampleRate());
            autoPhase_ -= std::floor(autoPhase_);
            autoAge_ += n;
            if (autoAge_ >= displayEvery_ / 2)   // 40 moves per second
            {
                autoAge_ = 0;
                selfWrite_ = true;
                setValue(kPadAX, (float)(0.5 + 0.4 * std::cos(2.0 * M_PI * autoPhase_)));
                setValue(kPadAY, (float)(0.5 + 0.4 * std::sin(2.0 * M_PI * autoPhase_)));
                selfWrite_ = false;
            }
        }

        rateAge_ += n;
        if (rateAge_ >= oneSecond_)
        {
            rateAge_ = 0;
            setPlain(kRate, (float)std::min(999, eventsThisSecond_));
            eventsThisSecond_ = 0;
            setPlain(kCurveRate, (float)std::min(9999, curveSentThisSecond_));
            curveSentThisSecond_ = 0;
        }
        displayAge_ += n;
        if (displayAge_ >= displayEvery_)
        {
            displayAge_ = 0;
            setPlain(kLastPad, (float)lastPad_);
            const int ms = (int)std::min(9999.0, std::round(sinceTouch_ * 1000.0 / sampleRate() / 10.0) * 10.0);
            setPlain(kGap, (float)ms);
            if (minMaxDirty_)
            {
                minMaxDirty_ = false;
                setValue(kMinX, minX_ <= maxX_ ? minX_ : 1.0f);
                setValue(kMaxX, minX_ <= maxX_ ? maxX_ : 0.0f);
                setValue(kMinY, minY_ <= maxY_ ? minY_ : 1.0f);
                setValue(kMaxY, minY_ <= maxY_ ? maxY_ : 0.0f);
            }
        }
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[48];
        if (index == kBellX)
        {
            const double hz = freqOf(plain);
            if (hz < 1000.0) std::snprintf(buf, sizeof(buf), "%.0f Hz", hz);
            else std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0);
            out = buf;
            return true;
        }
        if (index == kBellY) { std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)gainOf(plain)); out = buf; return true; }
        return false;
    }

private:
    static double freqOf(float x) { return 20.0 * std::pow(1000.0, (double)std::clamp(x, 0.0f, 1.0f)); }
    static float gainOf(float y) { return (std::clamp(y, 0.0f, 1.0f) * 2.0f - 1.0f) * kRangeDb; }

    // CURVE tab: recompute the targets when the bell moves, then send at most Refresh changed
    // columns per refresh (curve first: it is what the eye follows), counting what was sent.
    void updateCurve(int n)
    {
        if (getBool(kSweep))
        {
            sweepPhase_ += (double)n / (6.0 * sampleRate());   // there and back in 6 s
            sweepPhase_ -= std::floor(sweepPhase_);
            const double tri = sweepPhase_ < 0.5 ? sweepPhase_ * 2.0 : 2.0 - sweepPhase_ * 2.0;
            sweepAge_ += n;
            if (sweepAge_ >= displayEvery_ / 2)
            {
                sweepAge_ = 0;
                selfWrite_ = true;
                setValue(kBellX, (float)(0.08 + 0.84 * tri));
                selfWrite_ = false;
            }
        }
        slowops::eqartist::BandParams b;
        b.on = true;
        b.type = slowops::eqartist::BandType::Bell;
        b.freq = freqOf(getNormalised(kBellX));
        b.gainDb = gainOf(getNormalised(kBellY));
        b.q = get(kBellQ);
        if (!(b == bell_))
        {
            bell_ = b;
            columns(kCurveCols, kCurveFrames, &want_[0]);
            columns(kShadeCols, kShadeFrames, &want_[kCurveCols]);
            lineColumns(&want_[kCurveCols + kShadeCols]);
        }
        // The queue serves the curve (the 64 dot columns, or the 76 line columns in LINE mode) first,
        // then the 32 shade columns. want_ holds curve [0, 64), shade [64, 96), line [96, 172).
        const bool line = getInt(kStrip) == 2;
        const int nCurve = line ? kLineCols : kCurveCols;
        auto item = [&](int k, int& idx, float& want, float& frames) {
            if (k < nCurve)
            {
                idx = (line ? kLine0 : kCurve0) + k;
                want = want_[(line ? kCurveCols + kShadeCols : 0) + k];
                frames = (float)((line ? kLineFrames : kCurveFrames) - 1);
            }
            else
            {
                idx = kShade0 + (k - nCurve);
                want = want_[kCurveCols + (k - nCurve)];
                frames = (float)(kShadeFrames - 1);
            }
        };
        curveAge_ += n;
        if (curveAge_ < oneSecond_ / 30) return;
        curveAge_ = 0;
        static const int kBudget[5] = {8, 16, 24, 32, kLineCols + kShadeCols};
        const int budget = kBudget[std::clamp(getInt(kRefresh), 0, 4)];
        for (int sent = 0; sent < budget; ++sent)
        {
            int best = -1;
            float bestScore = 0.0f;
            for (int k = 0; k < nCurve + kShadeCols; ++k)
            {
                int idx; float want, frames;
                item(k, idx, want, frames);
                const float diff = std::fabs(want - getNormalised(idx)) * frames;
                if (diff < 0.5f) continue;
                const float score = std::min(diff, 100.0f) + (k < nCurve ? 1000.0f : 0.0f);
                if (score > bestScore) { bestScore = score; best = k; }
            }
            if (best < 0) break;
            int idx; float want, frames;
            item(best, idx, want, frames);
            setValue(idx, want);
            ++curveSentThisSecond_;
        }
    }

    // Each column shows the extreme (largest |dB|) over its own frequency span, so a narrow notch
    // or peak between column centres still shows. Snapped to the strip's frames.
    void columns(int cols, int frames, float* out) const
    {
        for (int c = 0; c < cols; ++c)
        {
            double ext = 0.0;
            for (int s = 0; s < 16; ++s)   // 16 samples: a Q 10 peak is narrower than a shade column
            {
                const double f = 20.0 * std::pow(1000.0, (c + (s + 0.5) / 16.0) / cols);
                const double d = slowops::eqartist::analogDb(bell_, f);
                if (std::fabs(d) > std::fabs(ext)) ext = d;
            }
            const float v = (float)std::clamp(ext, (double)-kRangeDb, (double)kRangeDb);
            out[c] = std::round((v + kRangeDb) / (2.0f * kRangeDb) * (frames - 1)) / (float)(frames - 1);
        }
    }

    // LINE mode targets: the polyline's vertices at the kLineCols + 1 column boundaries, in 0.125 dB
    // levels (0 = -18 dB .. 288 = +18 dB). Each vertex takes the extreme (largest |dB|) of the curve
    // within half a column either side, so a narrow peak or notch between boundaries still reaches its
    // full height (shifted sideways by at most half a column). Walking left to right, each column takes
    // the allowed rise (kRises) that lands nearest that value, so the vertices stay shared.
    void lineColumns(float* out) const
    {
        int v = 0;
        for (int b = 0; b <= kLineCols; ++b)
        {
            double ext = 0.0;
            for (int s = -2; s <= 2; ++s)
            {
                const double pos = std::clamp((b + s * 0.25) / kLineCols, 0.0, 1.0);
                const double d = slowops::eqartist::analogDb(bell_, 20.0 * std::pow(1000.0, pos));
                if (std::fabs(d) > std::fabs(ext)) ext = d;
            }
            const double db = std::clamp(ext, (double)-kRangeDb, (double)kRangeDb);
            const int want = (int)std::lround((db + kRangeDb) / (2.0 * kRangeDb) * (kLineLevels - 1));
            if (b == 0) { v = want; continue; }
            int best = -1;
            for (int k = 0; k < kNumRises; ++k)
            {
                const int to = v + kRises[k];
                if (to < 0 || to >= kLineLevels) continue;
                if (best < 0 || std::abs(to - want) < std::abs(v + kRises[best] - want)) best = k;
            }
            out[b - 1] = (float)(v * kNumRises + best) / (float)(kLineFrames - 1);
            v += kRises[best];
        }
    }

    // ENV tab: move every envelope point slowly (each at its own rate), 40 times a second, to see
    // whether the native Envelope widgets follow plugin-set values smoothly.
    void updateEnvSweep(int n)
    {
        if (!getBool(kEnvSweep)) return;
        envPhase_ += (double)n / sampleRate();
        envAge_ += n;
        if (envAge_ < displayEvery_ / 2) return;
        envAge_ = 0;
        for (int i = kEnvA; i < kEnvSweep; ++i)
        {
            const double rate = 0.15 + 0.05 * (i - kEnvA);   // Hz, different per point
            setValue(i, (float)(0.5 + 0.4 * std::sin(2.0 * M_PI * rate * envPhase_ + i)));
        }
    }

    // ENV2: the ruler envelope zigzags between 10 % and 90 % (L1, L2, L3, Sustain, L5) with every stage
    // duration set to Width, so a screenshot shows where each point lands for a given duration.
    void updateRuler()
    {
        const float w = getNormalised(kWidth);
        const float v[9] = {0.1f, w, 0.9f, w, 0.1f, w, 0.9f, w, 0.1f};
        for (int k = 0; k < 9; ++k) setValue(kRul0 + k, v[k]);
    }

    void updateLineSweep(int n)
    {
        if (!getBool(kLineSweep)) return;
        linePhase_ += (double)n / (4.0 * sampleRate());
        linePhase_ -= std::floor(linePhase_);
        lineAge_ += n;
        if (lineAge_ < displayEvery_ / 2) return;
        lineAge_ = 0;
        setValue(kLineValue, (float)(0.5 + 0.45 * std::sin(2.0 * M_PI * linePhase_)));
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "Curve UI Test";
        info.vendor = "Slow Ops";
        info.uniqueId = MPC_FOURCC('S', 'o', 'C', 't');
        info.version = 510;   // 0.5.1
        info.isInstrument = false;
        info.numInputs = 2;
        info.numOutputs = 2;
        info.tailSamples = 0;
        return info;
    }

    void addDisplay(mpc::Param p)
    {
        p.automatable = false;
        addParam(p);
    }
    void setValue(int i, float norm)
    {
        if (getNormalised(i) == norm) return;
        setNormalised(i, norm);
        notifyHostParameterChanged(i);
    }
    void setPlain(int i, float plain) { setValue(i, param(i).toNormalised(plain)); }

    std::atomic<uint32_t> seq_[kNumPads] = {};
    uint32_t lastSeq_[kNumPads] = {};
    bool selfWrite_ = false, minMaxDirty_ = true;
    int lastPad_ = 0, eventsThisSecond_ = 0, sinceTouch_ = 1 << 30;
    int displayAge_ = 0, displayEvery_ = 2205, rateAge_ = 0, oneSecond_ = 44100, autoAge_ = 0;
    float minX_ = 1.0f, maxX_ = 0.0f, minY_ = 1.0f, maxY_ = 0.0f;
    double autoPhase_ = 0.0;

    slowops::eqartist::BandParams bell_{};
    float want_[kCurveCols + kShadeCols + kLineCols] = {};   // curve, shade, line
    int curveAge_ = 0, sweepAge_ = 0, lineAge_ = 0, curveSentThisSecond_ = 0;
    double sweepPhase_ = 0.0, linePhase_ = 0.0, envPhase_ = 0.0;
    int envAge_ = 0;
};

MPC_PLUGIN_ENTRY(CurveUITest)
