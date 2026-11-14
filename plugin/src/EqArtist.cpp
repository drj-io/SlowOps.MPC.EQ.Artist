// Slow Ops EQ Artist: draw an EQ curve over a live spectrum, then Apply it as real bands.
//
// This file is the thin adapter between the MPC Plugin Builder SDK and the DSP in
// core/include/slowops/eqartist/ (standard library only, no SDK dependency). It:
//   * declares the parameters (ids are permanent once released; see docs/CONTROLS.md),
//   * paints the 32 Draw columns from the pen (an XYPad over the graph, bound to Pen X / Pen Y),
//   * turns the self-resetting Apply / Clear / Undo buttons into actions,
//   * sends the drawing to the curve fitter (on its own thread) and writes the fitted bands
//     into the band parameters, so knobs, Q-Links, automation and the project all see them,
//   * runs the EQ engine, and writes the spectrum and the EQ's response curve into read-only
//     parameters about 30 times a second.

#include "mpc_plugin.h"

#include "../../core/include/slowops/eqartist/EqEngine.hpp"
#include "../../core/include/slowops/eqartist/FitWorker.hpp"
#include "../../core/include/slowops/eqartist/Spectrum.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eq = slowops::eqartist;

class EqArtist : public mpc::Plugin
{
public:
    static constexpr int kCols = 32;
    static constexpr int kBands = eq::EqEngine::kNumBands;
    static constexpr int kPerBand = 6;
    static constexpr float kDrawRangeDb = 18.0f;
    static constexpr float kSpecTopDb = -6.0f, kSpecBottomDb = -66.0f;
    static constexpr int kSpecFrames = 36;    // must match spec_bar.png (scripts/skin_layout.py)
    static constexpr int kDotFrames = 37;     // must match dot.png: -18..+18 dB in 1 dB steps

    // Parameter indices. The ids are permanent once released: only ever append.
    enum
    {
        kOutput,      // index 0: the MPC nudges it on every prepare, so it is harmless
        kDraw,        // Draw mode: the pen paints the curve
        kApply,       // trigger: fit the drawing to bands
        kClear,       // trigger: flatten the drawing
        kUndo,        // trigger: back to the bands before the last Apply (press again to redo)
        kMaxBands,    // 1..8: how many bands Apply may use
        kAnalyzer,    // Off / Pre / Post
        kSpeed,       // analyser fall: Slow / Medium / Fast
        kBypass,
        kPenX,        // the pen XYPad (input only)
        kPenY,
        kFitInfo,     // read-only: its value text is the fit status
        kBand0,       // per band: On, Type, Freq, Gain, Q, Slope
        kDraw0 = kBand0 + kBands * kPerBand,
        kSpec0 = kDraw0 + kCols,
        kResp0 = kSpec0 + kCols,
        kNumParams = kResp0 + kCols
    };
    enum { kOn, kType, kFreq, kGain, kQ, kSlope };
    static int bandParam(int band, int field) { return kBand0 + band * kPerBand + field; }

    EqArtist() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("output", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(mpc::boolParam("draw", "Draw", false));
        addParam(mpc::boolParam("apply", "Apply", false));
        addParam(mpc::boolParam("clear", "Clear", false));
        addParam(mpc::boolParam("undo", "Undo", false));
        addParam(mpc::intParam("maxbands", "Max Bands", 1, kBands, kBands));
        addParam(mpc::choiceParam("analyzer", "Analyzer", {"Off", "Pre", "Post"}, 1));
        addParam(mpc::choiceParam("speed", "Speed", {"Slow", "Medium", "Fast"}, 1));
        addParam(mpc::boolParam("bypass", "Bypass", false));
        addDisplay(mpc::floatParam("penx", "Pen X", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::floatParam("peny", "Pen Y", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::intParam("fitinfo", "Fit", 0, 99, 0));
        static const float defaultHz[kBands] = {60, 150, 400, 1000, 2500, 5000, 9000, 14000};
        for (int b = 0; b < kBands; ++b)
        {
            const std::string n = std::to_string(b + 1), id = "b" + n, name = "B" + n + " ";
            addParam(mpc::boolParam((id + "on").c_str(), (name + "On").c_str(), false));
            addParam(mpc::choiceParam((id + "type").c_str(), (name + "Type").c_str(),
                                      {"Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt"}, 0));
            addParam(mpc::logParam((id + "freq").c_str(), (name + "Freq").c_str(), 20.0f, 20000.0f, defaultHz[b], "Hz", 0));
            addParam(mpc::floatParam((id + "gain").c_str(), (name + "Gain").c_str(), -24.0f, 24.0f, 0.0f, "dB", 1));
            addParam(mpc::logParam((id + "q").c_str(), (name + "Q").c_str(), 0.1f, 18.0f, 1.0f, "", 2));
            addParam(mpc::choiceParam((id + "slope").c_str(), (name + "Slope").c_str(),
                                      {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB"}, 1));
        }
        for (int c = 0; c < kCols; ++c)
        {
            const std::string n = std::to_string(c + 1);
            // Not automatable: the drawing is edited with the pen and saved with the project.
            addDisplay(mpc::floatParam(("draw" + n).c_str(), ("Draw " + n).c_str(), -kDrawRangeDb, kDrawRangeDb, 0.0f, "dB", 1));
        }
        for (int c = 0; c < kCols; ++c)
        {
            const std::string n = std::to_string(c + 1);
            addDisplay(mpc::floatParam(("spec" + n).c_str(), ("Spec " + n).c_str(), 0.0f, 1.0f, 0.0f, "", 3));
        }
        for (int c = 0; c < kCols; ++c)
        {
            const std::string n = std::to_string(c + 1);
            addDisplay(mpc::floatParam(("resp" + n).c_str(), ("Resp " + n).c_str(), -kDrawRangeDb, kDrawRangeDb, 0.0f, "dB", 1));
        }
        if (numParams() != kNumParams) mpc::log("EqArtist: parameter count mismatch");
        for (int c = 0; c < kCols; ++c) colHz_[(std::size_t)c] = 20.0 * std::pow(1000.0, (c + 0.5) / kCols);
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        spectrum_.prepare(sampleRate, kCols);   // allocates
        pullBands();
        engine_.prepare(sampleRate);
        penGapSamples_ = (int)(0.15 * sampleRate);
        displayEvery_ = (int)(sampleRate / 30.0);
        respDirty_ = true;
    }

    // Also called after a state load. Restoring Pen X / Pen Y from a project must not count as a
    // pen stroke, and the triggers must not fire.
    void reset() override
    {
        for (int t : {kApply, kClear, kUndo}) if (getBool(t)) setNormalised(t, 0.0f);
        lastPenSeq_ = penSeq_.load(std::memory_order_relaxed);
        lastCol_ = -1;
        sincePen_ = 1 << 30;
        wasDraw_ = getBool(kDraw);
        engine_.reset();
        respDirty_ = true;
    }

    void parameterChanged(int index, float plain) override
    {
        (void)plain;
        if (index == kPenX || index == kPenY) penSeq_.fetch_add(1, std::memory_order_relaxed);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (!inL) { for (int i = 0; i < n; ++i) outL[i] = outR[i] = 0.0f; return; }

        handleDrawToggle();
        handleTriggers();
        handlePen(n);
        handleFitResult();
        pullBands();

        const int analyzer = getInt(kAnalyzer);
        if (analyzer == 1) for (int i = 0; i < n; ++i) spectrum_.push(0.5f * (inL[i] + inR[i]));
        engine_.setOutputDb(get(kOutput));
        engine_.setBypass(getBool(kBypass));
        engine_.process(inL, inR, outL, outR, n);   // in place is fine
        if (analyzer == 2) for (int i = 0; i < n; ++i) spectrum_.push(0.5f * (outL[i] + outR[i]));

        displayAge_ += n;
        if (displayAge_ >= displayEvery_)
        {
            const float secs = (float)displayAge_ / (float)sampleRate();
            displayAge_ = 0;
            updateDisplays(analyzer, secs);
        }
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[64];
        if (index == kFitInfo)
        {
            const int st = fitState_.load();
            if (st == kFitBusy) out = "Fitting...";
            else if (st == kFitNone) out = "Draw, then Apply";
            else
            {
                const int nb = fitBands_.load();
                if (nb == 0) std::snprintf(buf, sizeof(buf), "Flat: no bands");
                else std::snprintf(buf, sizeof(buf), "%d band%s, %.1f dB off", nb, nb == 1 ? "" : "s", (double)fitRms_.load());
                out = buf;
            }
            return true;
        }
        if (index >= kBand0 && index < kDraw0)
        {
            const int field = (index - kBand0) % kPerBand;
            if (field == kFreq)
            {
                if (plain < 1000.0f) std::snprintf(buf, sizeof(buf), "%.0f Hz", (double)plain);
                else std::snprintf(buf, sizeof(buf), "%.2f kHz", (double)plain / 1000.0);
                out = buf;
                return true;
            }
            if (field == kGain) { std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)plain); out = buf; return true; }
        }
        return false;
    }

private:
    enum { kFitNone, kFitBusy, kFitDone };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "EQ Artist";
        info.vendor = "Slow Ops";
        info.uniqueId = MPC_FOURCC('S', 'o', 'E', 'q');
        info.version = 200;   // 0.2.0
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

    // Write a parameter from the plugin and tell the host, only when it changed.
    void setValue(int i, float norm)
    {
        if (getNormalised(i) == norm) return;
        setNormalised(i, norm);
        notifyHostParameterChanged(i);
    }
    void setPlain(int i, float plain) { setValue(i, param(i).toNormalised(plain)); }

    // ---- bands <-> parameters ---------------------------------------------------------------
    eq::BandParams readBand(int b) const
    {
        eq::BandParams p;
        p.on = getBool(bandParam(b, kOn));
        p.type = (eq::BandType)std::clamp(getInt(bandParam(b, kType)), 0, (int)eq::BandType::Count - 1);
        p.freq = get(bandParam(b, kFreq));
        p.gainDb = get(bandParam(b, kGain));
        p.q = get(bandParam(b, kQ));
        p.slope = std::clamp(getInt(bandParam(b, kSlope)), 0, eq::kNumSlopes - 1);
        return p;
    }

    void pullBands()
    {
        for (int b = 0; b < kBands; ++b)
        {
            const eq::BandParams p = readBand(b);
            if (!(p == engine_.band(b))) { engine_.setBand(b, p); respDirty_ = true; }
        }
    }

    void writeBand(int b, const eq::BandParams& p)
    {
        setPlain(bandParam(b, kOn), p.on ? 1.0f : 0.0f);
        if (!p.on) return;   // an unused band keeps its last settings, switched off
        setPlain(bandParam(b, kType), (float)(int)p.type);
        setPlain(bandParam(b, kFreq), (float)p.freq);
        setPlain(bandParam(b, kGain), eq::typeHasGain(p.type) ? (float)p.gainDb : 0.0f);
        setPlain(bandParam(b, kQ), (float)p.q);
        setPlain(bandParam(b, kSlope), (float)p.slope);
    }

    // ---- Draw / Apply / Clear / Undo -------------------------------------------------------------
    // Turning Draw on starts the drawing from the curve you hear, so a redraw edits it.
    void handleDrawToggle()
    {
        const bool d = getBool(kDraw);
        if (d && !wasDraw_)
        {
            double db[kCols];
            engine_.responseDb(colHz_.data(), db, kCols);
            for (int c = 0; c < kCols; ++c) paint(c, (float)db[c]);
            lastCol_ = -1;
        }
        wasDraw_ = d;
    }

    void handleTriggers()
    {
        if (getBool(kClear))
        {
            setValue(kClear, 0.0f);
            for (int c = 0; c < kCols; ++c) paint(c, 0.0f);
        }
        if (getBool(kApply))
        {
            setValue(kApply, 0.0f);
            double db[kCols];
            for (int c = 0; c < kCols; ++c) db[c] = get(kDraw0 + c);
            if (fit_.request(colHz_.data(), db, kCols, getInt(kMaxBands)))
            {
                fitState_.store(kFitBusy);
                bumpFitInfo();
            }
        }
        if (getBool(kUndo))
        {
            setValue(kUndo, 0.0f);
            if (hasUndo_)
            {
                // Swap the current bands with the saved ones, so Undo again is Redo.
                for (int i = 0; i < kBands * kPerBand; ++i)
                {
                    const float cur = getNormalised(kBand0 + i);
                    setValue(kBand0 + i, undo_[(std::size_t)i]);
                    undo_[(std::size_t)i] = cur;
                }
                engine_.forceCrossfade();
            }
        }
    }

    void handleFitResult()
    {
        eq::FitResult r;
        if (!fit_.take(r)) return;
        for (int i = 0; i < kBands * kPerBand; ++i) undo_[(std::size_t)i] = getNormalised(kBand0 + i);
        hasUndo_ = true;
        for (int b = 0; b < kBands; ++b)
        {
            eq::BandParams p = b < r.count ? r.bands[(std::size_t)b] : eq::BandParams{};
            if (b >= r.count) p.on = false;
            writeBand(b, p);
        }
        engine_.forceCrossfade();
        fitBands_.store(r.count);
        fitRms_.store((float)r.rmsDb);
        fitState_.store(kFitDone);
        bumpFitInfo();
        setValue(kDraw, 0.0f);   // show the result
        wasDraw_ = false;
    }

    // The fit status is the value text of a counter parameter: changing the value makes the MPC
    // ask for the text again.
    void bumpFitInfo()
    {
        fitCounter_ = (fitCounter_ + 1) % 100;
        setValue(kFitInfo, (float)fitCounter_ / 99.0f);
    }

    // ---- pen ----------------------------------------------------------------------------------
    // Each new X/Y pair paints the column under X with the gain at Y. Columns skipped since the
    // previous point of the same stroke are filled by straight-line interpolation. A pause longer
    // than penGapSamples_ starts a new stroke. The pen only paints in Draw mode.
    void handlePen(int n)
    {
        sincePen_ = std::min(sincePen_ + n, 1 << 30);
        const uint32_t seq = penSeq_.load(std::memory_order_relaxed);
        if (seq == lastPenSeq_) return;
        lastPenSeq_ = seq;
        if (!getBool(kDraw)) return;

        const bool newStroke = sincePen_ > penGapSamples_;
        sincePen_ = 0;
        const float x = getNormalised(kPenX), y = getNormalised(kPenY);
        const int col = std::clamp((int)(x * kCols), 0, kCols - 1);
        const float db = (y * 2.0f - 1.0f) * kDrawRangeDb;
        if (newStroke || lastCol_ < 0)
        {
            paint(col, db);
        }
        else
        {
            const int dir = col >= lastCol_ ? 1 : -1;
            const int span = std::abs(col - lastCol_);
            for (int k = 1; k <= span; ++k)
                paint(lastCol_ + dir * k, lastDb_ + (db - lastDb_) * (float)k / (float)span);
            if (span == 0) paint(col, db);
        }
        lastCol_ = col;
        lastDb_ = db;
    }

    void paint(int col, float db) { setPlain(kDraw0 + col, std::clamp(db, -kDrawRangeDb, kDrawRangeDb)); }

    // ---- displays -----------------------------------------------------------------------------
    void updateDisplays(int analyzer, float secs)
    {
        static const float kFall[3] = {12.0f, 24.0f, 48.0f};
        if (analyzer != 0) spectrum_.analyse(kFall[std::clamp(getInt(kSpeed), 0, 2)], secs);
        for (int c = 0; c < kCols; ++c)
        {
            float v = 0.0f;
            if (analyzer != 0)
            {
                v = (spectrum_.columnDb(c) - kSpecBottomDb) / (kSpecTopDb - kSpecBottomDb);
                v = std::round(std::clamp(v, 0.0f, 1.0f) * (kSpecFrames - 1)) / (float)(kSpecFrames - 1);
            }
            setValue(kSpec0 + c, v);   // only notifies when the bar height changes
        }
        if (respDirty_)
        {
            respDirty_ = false;
            double db[kCols];
            engine_.responseDb(colHz_.data(), db, kCols);
            for (int c = 0; c < kCols; ++c)
            {
                // Snap to the dot film strip's 1 dB frames.
                const float d = std::clamp((float)db[c], -kDrawRangeDb, kDrawRangeDb);
                const float norm = std::round((d + kDrawRangeDb) / (2.0f * kDrawRangeDb) * (kDotFrames - 1)) / (float)(kDotFrames - 1);
                setValue(kResp0 + c, norm);
            }
        }
    }

    eq::EqEngine engine_;
    eq::Spectrum spectrum_;
    eq::FitWorker fit_;
    std::array<double, kCols> colHz_{};
    int displayAge_ = 0, displayEvery_ = 1470;
    bool respDirty_ = true, wasDraw_ = false;

    std::atomic<uint32_t> penSeq_{0};
    uint32_t lastPenSeq_ = 0;
    int sincePen_ = 1 << 30, penGapSamples_ = 6615, lastCol_ = -1;
    float lastDb_ = 0.0f;

    std::array<float, kBands * kPerBand> undo_{};
    bool hasUndo_ = false;
    std::atomic<int> fitState_{kFitNone}, fitBands_{0};
    std::atomic<float> fitRms_{0.0f};
    int fitCounter_ = 0;
};

MPC_PLUGIN_ENTRY(EqArtist)
