// Slow Ops EQ Artist: SPIKE BUILD (Phase 0, drawing-surface hardware test).
//
// Audio passes through (with an Output gain). The point of this build is to answer, on the MPC:
//   (a) Does dragging an XYPad deliver Pen X / Pen Y smoothly, and how many updates per second?
//       The PEN tab paints the 32 Draw columns from the pen, joining up skipped columns.
//   (b) Does one swipe across a row of vertical sliders move several of them, or only the first?
//       The COLUMNS tab counts how many different columns one gesture changed ("Swipe cols").
//   (c) When an XYPad sits on top of the sliders, which one gets the touch? (OVERLAP tab.)
//   (d) Is the redraw smooth with 64 read-only/drawn params updating at 15 / 30 / 60 Hz?
//       Source = Sweep moves a bump across all 32 spectrum columns without any audio.
// See docs/SPIKE.md for the checklist.
//
// DSP lives in core/include/slowops/eqartist/ (standard library only, no SDK dependency).

#include "mpc_plugin.h"

#include "../../core/include/slowops/eqartist/Spectrum.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace eq = slowops::eqartist;

class EqArtist : public mpc::Plugin
{
public:
    static constexpr int kCols = 32;
    static constexpr float kDrawRangeDb = 18.0f;
    static constexpr float kSpecTopDb = -6.0f, kSpecBottomDb = -66.0f;
    static constexpr int kSpecFrames = 36;    // must match the spec_bar.png film strip

    // Parameter indices. Ids are permanent once released; this is a spike, so they may still change.
    enum
    {
        kOutput,     // index 0: the MPC nudges it on every prepare, so it is harmless
        kClear,      // trigger: flatten the drawing
        kSource,     // Input / Sweep
        kRate,       // display rate: 30 / 15 / 60 Hz
        kFlipY,      // invert the pen's vertical direction
        kPenX,       // XYPad position (input, not automatable)
        kPenY,
        kPenRate,    // read-only: pen updates in the last second
        kPenPts,     // read-only: pen points in the current stroke
        kSwipeCols,  // read-only: columns changed by the last slider gesture
        kDraw0,
        kSpec0 = kDraw0 + kCols,
        kNumParams = kSpec0 + kCols
    };

    EqArtist() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("output", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(mpc::boolParam("clear", "Clear", false));
        addParam(mpc::choiceParam("source", "Source", {"Input", "Sweep"}, 0));
        addParam(mpc::choiceParam("rate", "Disp Rate", {"30 Hz", "15 Hz", "60 Hz"}, 0));
        addParam(mpc::boolParam("flipy", "Flip Y", false));
        addDisplay(mpc::floatParam("penx", "Pen X", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::floatParam("peny", "Pen Y", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::intParam("penrate", "Pen Rate", 0, 1000, 0, "/s"));
        addDisplay(mpc::intParam("penpts", "Pen Pts", 0, 9999, 0));
        addDisplay(mpc::intParam("swipecols", "Swipe Cols", 0, kCols, 0));
        for (int c = 0; c < kCols; ++c)
        {
            const std::string n = std::to_string(c + 1);
            addParam(mpc::floatParam(("draw" + n).c_str(), ("Draw " + n).c_str(), -kDrawRangeDb, kDrawRangeDb, 0.0f, "dB", 1));
        }
        for (int c = 0; c < kCols; ++c)
        {
            const std::string n = std::to_string(c + 1);
            addDisplay(mpc::floatParam(("spec" + n).c_str(), ("Spec " + n).c_str(), 0.0f, 1.0f, 0.0f, "", 3));
        }
        if (numParams() != kNumParams) mpc::log("EqArtist: parameter count mismatch");
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        spectrum_.prepare(sampleRate, kCols);   // the only allocation
        penGapSamples_ = (int)(0.15 * sampleRate);
        swipeGapSamples_ = (int)(0.4 * sampleRate);
        oneSecond_ = (int)sampleRate;
        gain_ = targetGain();
    }

    // Also called after a state load. Restoring Pen X / Pen Y or the Draw columns from a project
    // must not count as a pen stroke or a slider gesture, so drop the pending events.
    void reset() override
    {
        if (getBool(kClear)) setNormalised(kClear, 0.0f);
        lastPenSeq_ = penSeq_.load(std::memory_order_relaxed);
        lastSwipeSeq_ = uiDrawSeq_.load(std::memory_order_relaxed);
        uiDrawMask_.store(0, std::memory_order_relaxed);
        lastCol_ = -1;
        sincePen_ = sinceSwipe_ = 1 << 30;
    }

    void parameterChanged(int index, float plain) override
    {
        (void)plain;
        if (index == kPenX || index == kPenY)
        {
            penSeq_.fetch_add(1, std::memory_order_relaxed);
        }
        else if (index >= kDraw0 && index < kDraw0 + kCols && !selfWrite_)
        {
            uiDrawMask_.fetch_or(1u << (index - kDraw0), std::memory_order_relaxed);
            uiDrawSeq_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];

        handleTriggers();
        handlePen(n);
        handleSwipe(n);

        // Pass-through with a smoothed output gain; the analyser sees the input.
        const float target = targetGain();
        const float step = (target - gain_) / (float)std::max(1, n);
        for (int i = 0; i < n; ++i)
        {
            const float l = inL ? inL[i] : 0.0f;
            const float r = inR ? inR[i] : 0.0f;
            spectrum_.push(0.5f * (l + r));
            gain_ += step;
            outL[i] = l * gain_;
            if (outR != outL) outR[i] = r * gain_;
        }
        gain_ = target;

        displayAge_ += n;
        sweepT_ = (sweepT_ + n) % (int64_t)(sampleRate() * 4.0);   // one sweep pass = 4 s
        const int every = (int)(sampleRate() / displayHz());
        if (displayAge_ >= every)
        {
            const float secs = (float)displayAge_ / (float)sampleRate();
            displayAge_ = 0;
            updateSpectrum(secs);
        }
        rateAge_ += n;
        if (rateAge_ >= oneSecond_)
        {
            rateAge_ = 0;
            setDisplay(kPenRate, param(kPenRate).toNormalised((float)std::min(1000, penEventsThisSecond_)));
            penEventsThisSecond_ = 0;
        }
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "EQ Artist";
        info.vendor = "Slow Ops";
        info.uniqueId = MPC_FOURCC('S', 'o', 'E', 'q');
        info.version = 100;   // 0.1.0
        info.isInstrument = false;
        info.numInputs = 2;
        info.numOutputs = 2;
        info.tailSamples = 1;
        return info;
    }

    void addDisplay(mpc::Param p)
    {
        p.automatable = false;
        addParam(p);
    }

    // Write a parameter from the plugin and tell the host, only when it changed.
    void setDisplay(int i, float norm)
    {
        if (getNormalised(i) == norm) return;
        selfWrite_ = true;
        setNormalised(i, norm);
        selfWrite_ = false;
        notifyHostParameterChanged(i);
    }

    float targetGain() const { return std::pow(10.0f, get(kOutput) / 20.0f); }

    double displayHz() const
    {
        switch (getInt(kRate)) { case 1: return 15.0; case 2: return 60.0; default: return 30.0; }
    }

    void handleTriggers()
    {
        if (getBool(kClear))
        {
            setDisplay(kClear, 0.0f);
            for (int c = 0; c < kCols; ++c) setDisplay(kDraw0 + c, param(kDraw0 + c).toNormalised(0.0f));
        }
    }

    // Pen: each new X/Y pair paints the column under X with the gain at Y. Columns skipped since
    // the previous point of the same stroke are filled by straight-line interpolation. A pause
    // longer than penGapSamples_ starts a new stroke.
    void handlePen(int n)
    {
        sincePen_ = std::min(sincePen_ + n, 1 << 30);
        const uint32_t seq = penSeq_.load(std::memory_order_relaxed);
        if (seq == lastPenSeq_) return;
        penEventsThisSecond_ += (int)(seq - lastPenSeq_);
        lastPenSeq_ = seq;

        const bool newStroke = sincePen_ > penGapSamples_;
        sincePen_ = 0;
        const float x = getNormalised(kPenX);
        float y = getNormalised(kPenY);
        if (getBool(kFlipY)) y = 1.0f - y;
        const int col = std::clamp((int)(x * kCols), 0, kCols - 1);
        const float db = (y * 2.0f - 1.0f) * kDrawRangeDb;

        if (newStroke || lastCol_ < 0)
        {
            strokePts_ = 0;
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
        ++strokePts_;
        setDisplay(kPenPts, param(kPenPts).toNormalised((float)std::min(9999, strokePts_)));
    }

    void paint(int col, float db)
    {
        setDisplay(kDraw0 + col, param(kDraw0 + col).toNormalised(std::clamp(db, -kDrawRangeDb, kDrawRangeDb)));
    }

    // Slider gestures: collect the columns the UI changed until it has been quiet for 0.4 s.
    // The live count shows during the gesture; it stays on screen after it.
    void handleSwipe(int n)
    {
        sinceSwipe_ = std::min(sinceSwipe_ + n, 1 << 30);
        const uint32_t seq = uiDrawSeq_.load(std::memory_order_relaxed);
        if (seq != lastSwipeSeq_)
        {
            lastSwipeSeq_ = seq;
            if (sinceSwipe_ > swipeGapSamples_) swipeMask_ = 0;
            sinceSwipe_ = 0;
            swipeMask_ |= uiDrawMask_.exchange(0, std::memory_order_relaxed);
            int count = 0;
            for (uint32_t m = swipeMask_; m; m &= m - 1) ++count;
            setDisplay(kSwipeCols, param(kSwipeCols).toNormalised((float)count));
        }
    }

    void updateSpectrum(float secs)
    {
        const bool sweep = getInt(kSource) == 1;
        if (!sweep) spectrum_.analyse(24.0f, secs);
        const double t = (double)sweepT_ / sampleRate();
        const double centre = t / 4.0 * (kCols + 8) - 4.0;
        for (int c = 0; c < kCols; ++c)
        {
            float db;
            if (sweep)
            {
                const double d = (c - centre) / 3.0;
                db = (float)(kSpecBottomDb + 6.0 + 54.0 * std::exp(-d * d));
            }
            else
            {
                db = spectrum_.columnDb(c);
            }
            float v = (db - kSpecBottomDb) / (kSpecTopDb - kSpecBottomDb);
            v = std::clamp(v, 0.0f, 1.0f);
            v = std::round(v * (kSpecFrames - 1)) / (float)(kSpecFrames - 1);   // only notify on a frame change
            setDisplay(kSpec0 + c, v);
        }
    }

    eq::Spectrum spectrum_;
    float gain_ = 1.0f;
    int displayAge_ = 0, rateAge_ = 0, oneSecond_ = 44100;
    int64_t sweepT_ = 0;

    std::atomic<uint32_t> penSeq_{0};
    uint32_t lastPenSeq_ = 0;
    int penEventsThisSecond_ = 0, sincePen_ = 1 << 30, penGapSamples_ = 6615;
    int lastCol_ = -1, strokePts_ = 0;
    float lastDb_ = 0.0f;

    std::atomic<uint32_t> uiDrawMask_{0}, uiDrawSeq_{0};
    uint32_t lastSwipeSeq_ = 0, swipeMask_ = 0;
    int sinceSwipe_ = 1 << 30, swipeGapSamples_ = 17640;

    bool selfWrite_ = false;
};

MPC_PLUGIN_ENTRY(EqArtist)
