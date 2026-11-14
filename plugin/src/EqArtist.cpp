// Slow Ops EQ Artist: draw an EQ curve over a live spectrum; the sound follows as you draw.
//
// This file is the thin adapter between the MPC Plugin Builder SDK and the DSP in
// core/include/slowops/eqartist/ (standard library only, no SDK dependency). It:
//   * declares the parameters (ids are permanent once released; see docs/CONTROLS.md),
//   * paints the 32 Draw columns from the pen (an XYPad over the graph, bound to Pen X / Pen Y),
//   * makes the sound follow the drawing at once: while you draw, the audio comes from the live
//     EQ (32 bells solved straight from the drawing, see LiveEq.hpp),
//   * meanwhile keeps fitting the drawing to bands (on the fitter's own thread) and writes each
//     result into the band parameters, so Q-Links, automation and the project all see them; once
//     the stroke has ended and the fit matches the drawing, the sound crossfades to the bands,
//   * keeps the drawing in step when a band is edited by hand (Q-Link, automation),
//   * turns the self-resetting Clear / Undo buttons into actions (Undo = the last stroke),
//   * runs the EQ engine, and writes the spectrum and the EQ's response curve into read-only
//     parameters about 30 times a second.

#include "mpc_plugin.h"

#include "../../core/include/slowops/eqartist/EqEngine.hpp"
#include "../../core/include/slowops/eqartist/FitWorker.hpp"
#include "../../core/include/slowops/eqartist/Spectrum.hpp"
#include "ParaPage.hpp"

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
    static constexpr int kDotFrames = 37;     // must match dot.png / draw_dot.png: -18..+18 dB in 1 dB steps

    // Parameter indices. The ids are permanent once released (1.0): from then on, only append.
    enum
    {
        kOutput,      // index 0: the MPC nudges it on every prepare, so it is harmless
        kClear,       // trigger: flat drawing, no bands
        kUndo,        // trigger: undo the last stroke or Clear (press again to redo)
        kMaxBands,    // 1..8: how many bands the fit may use
        kAnalyzer,    // spectrum on / off
        kAnalyze,     // spectrum of the input (Pre) or the output (Post)
        kSpeed,       // analyser fall: Slow / Medium / Fast
        kBypass,
        kPenX,        // the pen XYPad (input only)
        kPenY,
        kFitInfo,     // read-only: its value text is the fit status
        kBand0,       // per band: On, Type, Freq, Gain, Q, Slope
        kDraw0 = kBand0 + kBands * kPerBand,
        kSpec0 = kDraw0 + kCols,
        kResp0 = kSpec0 + kCols,
        kPara0 = kResp0 + kCols,      // the PARA page's block (see ParaPage.hpp)
        kNumParams = kPara0 + eqpara::ParaPage::kNumParams
    };
    enum { kOn, kType, kFreq, kGain, kQ, kSlope };
    static int bandParam(int band, int field) { return kBand0 + band * kPerBand + field; }
    static constexpr int kNumBandParams = kBands * kPerBand;

    EqArtist() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("output", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(mpc::boolParam("clear", "Clear", false));
        addParam(mpc::boolParam("undo", "Undo", false));
        addParam(mpc::intParam("maxbands", "Max Bands", 1, kBands, kBands));
        addParam(mpc::boolParam("analyzer", "Analyzer", true));
        addParam(mpc::choiceParam("analyze", "Analyze", {"Pre", "Post"}, 0));
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
        eqpara::ParaPage::addParams(*this);
        if (numParams() != kNumParams) mpc::log("EqArtist: parameter count mismatch");
        for (int c = 0; c < kCols; ++c) colHz_[(std::size_t)c] = 20.0 * std::pow(1000.0, (c + 0.5) / kCols);
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        spectrum_.prepare(sampleRate, kCols);   // allocates
        pullBands();
        engine_.prepare(sampleRate, colHz_.data());
        penGapSamples_ = (int)(0.15 * sampleRate);
        syncDelaySamples_ = (int)(0.3 * sampleRate);
        displayEvery_ = (int)(sampleRate / 30.0);
        respDirty_ = true;
    }

    // Also called after a state load. Restoring Pen X / Pen Y or the bands from a project must
    // not count as a pen stroke or a hand edit, and the triggers must not fire.
    void reset() override
    {
        for (int t : {kClear, kUndo}) if (getBool(t)) setNormalised(t, 0.0f);
        lastPenSeq_ = penSeq_.load(std::memory_order_relaxed);
        lastCol_ = -1;
        sincePen_ = 1 << 30;
        drawDirty_ = false;
        selfBandWrite_ = true;   // the next pullBands() takes the loaded bands as they are
        handEdited_ = false;
        liveDirty_ = false;
        engine_.setLive(false);  // a loaded project plays its bands
        engine_.reset();
        para_.reset(*this);
        respDirty_ = true;
    }

    void parameterChanged(int index, float plain) override
    {
        (void)plain;
        if (index >= kPara0) { para_.parameterChanged(index - kPara0); return; }
        if (index == kPenX || index == kPenY) penSeq_.fetch_add(1, std::memory_order_relaxed);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        mpc::ScopedDenormalsOff noDenormals;   // flush-to-zero: the filters' tails never slow the CPU
        const int n = ctx.numFrames;
        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (!inL) { for (int i = 0; i < n; ++i) outL[i] = outR[i] = 0.0f; return; }

        handleTriggers();
        handlePen(n);
        updateLive();
        handleFitResult();
        requestFitIfNeeded();
        pullBands();
        syncDrawingToHandEdits(n);
        leaveLiveWhenFitted();

        const int analyzer = !getBool(kAnalyzer) ? 0 : (getInt(kAnalyze) == 0 ? 1 : 2);   // off / pre / post
        if (analyzer == 1) for (int i = 0; i < n; ++i) spectrum_.push(0.5f * (inL[i] + inR[i]));
        engine_.setOutputDb(get(kOutput));
        engine_.setBypass(getBool(kBypass));
        engine_.process(inL, inR, outL, outR, n);   // in place is fine
        if (analyzer == 2) for (int i = 0; i < n; ++i) spectrum_.push(0.5f * (outL[i] + outR[i]));

        para_.process(*this, n);   // PARA: silent test bands, never reach the engine

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
        if (index == kPara0 + eqpara::ParaPage::pInfo) { out = para_.info(*this); return true; }
        if (index >= kPara0)
        {
            const int rel = index - kPara0;
            const bool freq = rel == eqpara::ParaPage::pSelFreq ||
                              (rel >= eqpara::ParaPage::pBand0 && rel < eqpara::ParaPage::pNode0 &&
                               (rel - eqpara::ParaPage::pBand0) % eqpara::ParaPage::kFields == eqpara::ParaPage::fFreq);
            if (freq)
            {
                if (plain < 1000.0f) std::snprintf(buf, sizeof(buf), "%.0f Hz", (double)plain);
                else std::snprintf(buf, sizeof(buf), "%.2f kHz", (double)plain / 1000.0);
                out = buf;
                return true;
            }
            return false;
        }
        if (index == kFitInfo)
        {
            if (getBool(kBypass)) { out = "BYPASSED"; return true; }
            const int nb = fitBands_.load();
            if (nb < 0) out = "Draw on the graph";
            else if (nb == 0) out = "Flat";
            else
            {
                std::snprintf(buf, sizeof(buf), "%d band%s, %.1f dB off", nb, nb == 1 ? "" : "s", (double)fitRms_.load());
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
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "EQ Artist";
        info.vendor = "Slow Ops";
        info.uniqueId = MPC_FOURCC('S', 'o', 'E', 'q');
        info.version = 560;   // 0.5.6
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

    // Engine targets from the band parameters. A change the plugin did not make itself is a hand
    // edit (knob, Q-Link, automation): the drawing follows it once the edit has settled.
    void pullBands()
    {
        bool changed = false;
        for (int b = 0; b < kBands; ++b)
        {
            const eq::BandParams p = readBand(b);
            if (!(p == engine_.band(b))) { engine_.setBand(b, p); changed = true; }
        }
        if (changed)
        {
            respDirty_ = true;
            if (!selfBandWrite_)
            {
                handEdited_ = true;
                sinceHandEdit_ = 0;
                if (engine_.live()) { engine_.setLive(false); respDirty_ = true; }   // a knob turn: hear the bands
            }
        }
        selfBandWrite_ = false;
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

    // ---- Undo: one snapshot of the drawing and the bands, taken before each stroke or Clear --------
    void saveUndo()
    {
        for (int i = 0; i < kNumBandParams; ++i) undoBands_[(std::size_t)i] = getNormalised(kBand0 + i);
        for (int c = 0; c < kCols; ++c) undoDraw_[(std::size_t)c] = getNormalised(kDraw0 + c);
        undoFit_ = {fitBands_.load(), fitRms_.load()};
        hasUndo_ = true;
    }

    void swapUndo()
    {
        for (int i = 0; i < kNumBandParams; ++i)
        {
            const float cur = getNormalised(kBand0 + i);
            setValue(kBand0 + i, undoBands_[(std::size_t)i]);
            undoBands_[(std::size_t)i] = cur;
        }
        for (int c = 0; c < kCols; ++c)
        {
            const float cur = getNormalised(kDraw0 + c);
            setValue(kDraw0 + c, undoDraw_[(std::size_t)c]);
            undoDraw_[(std::size_t)c] = cur;
        }
        const std::pair<int, float> fit = {fitBands_.load(), fitRms_.load()};
        fitBands_.store(undoFit_.first);
        fitRms_.store(undoFit_.second);
        undoFit_ = fit;
        selfBandWrite_ = true;
        drawDirty_ = false;
        liveDirty_ = false;
        discardFit_ = fit_.busy();   // a fit still running belongs to the undone drawing
        fittedSeq_ = ++drawSeq_;     // the restored bands belong to the restored drawing
        engine_.forceCrossfade();
        engine_.setLive(false);
        respDirty_ = true;
        bumpFitInfo();
    }

    void handleTriggers()
    {
        if (getBool(kClear))
        {
            setValue(kClear, 0.0f);
            saveUndo();
            for (int c = 0; c < kCols; ++c) paint(c, 0.0f);
            drawDirty_ = liveDirty_ = true;
            ++drawSeq_;
        }
        if (getBool(kUndo))
        {
            setValue(kUndo, 0.0f);
            if (hasUndo_) swapUndo();
        }
        // Bypass shows in the status line.
        const bool byp = getBool(kBypass);
        if (byp != lastBypass_) { lastBypass_ = byp; bumpFitInfo(); }
    }

    // ---- the fit: rerun whenever the drawing has changed and the fitter is free --------------
    void requestFitIfNeeded()
    {
        if (!drawDirty_ || fit_.busy()) return;
        double db[kCols];
        for (int c = 0; c < kCols; ++c) db[c] = get(kDraw0 + c);
        if (fit_.request(colHz_.data(), db, kCols, getInt(kMaxBands))) { drawDirty_ = false; requestedSeq_ = drawSeq_; }
    }

    // The live EQ follows every change of the drawing at once.
    void updateLive()
    {
        if (!liveDirty_) return;
        liveDirty_ = false;
        double db[kCols];
        for (int c = 0; c < kCols; ++c) db[c] = get(kDraw0 + c);
        engine_.setLiveDrawing(db);
        engine_.setLive(true);
        respDirty_ = true;
    }

    // Back to the fitted bands once the stroke has ended and the bands fit the drawing as it is now.
    void leaveLiveWhenFitted()
    {
        if (!engine_.live() || drawDirty_ || liveDirty_ || fittedSeq_ != drawSeq_ || sincePen_ <= penGapSamples_) return;
        engine_.setLive(false);
        respDirty_ = true;
    }

    void handleFitResult()
    {
        eq::FitResult r;
        if (!fit_.take(r)) return;
        if (discardFit_) { discardFit_ = false; return; }
        fittedSeq_ = requestedSeq_;
        for (int b = 0; b < kBands; ++b)
        {
            eq::BandParams p = b < r.count ? r.bands[(std::size_t)b] : eq::BandParams{};
            if (b >= r.count) p.on = false;
            writeBand(b, p);
        }
        selfBandWrite_ = true;
        handEdited_ = false;
        // Each fit may number the bands differently, so switch the whole set over (30 ms) rather
        // than letting band 1 glide from one frequency to another.
        engine_.forceCrossfade();
        fitBands_.store(r.count);
        fitRms_.store((float)r.rmsDb);
        bumpFitInfo();
    }

    // After a hand edit has been still for syncDelaySamples_, redraw the drawing from the EQ you
    // hear, so the next stroke edits that curve instead of snapping back to the old drawing.
    void syncDrawingToHandEdits(int n)
    {
        sinceHandEdit_ = std::min(sinceHandEdit_ + n, 1 << 30);
        if (!handEdited_ || sinceHandEdit_ < syncDelaySamples_ || drawDirty_ || fit_.busy()) return;
        handEdited_ = false;
        double db[kCols];
        engine_.responseDb(colHz_.data(), db, kCols);
        for (int c = 0; c < kCols; ++c) paint(c, (float)db[c]);
        fittedSeq_ = ++drawSeq_;   // the bands are the source of this drawing
        int on = 0;
        for (int b = 0; b < kBands; ++b) on += getBool(bandParam(b, kOn)) ? 1 : 0;
        fitBands_.store(on);
        fitRms_.store(0.0f);
        bumpFitInfo();
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
    // than penGapSamples_ starts a new stroke; each stroke can be undone.
    void handlePen(int n)
    {
        sincePen_ = std::min(sincePen_ + n, 1 << 30);
        const uint32_t seq = penSeq_.load(std::memory_order_relaxed);
        if (seq == lastPenSeq_) return;
        lastPenSeq_ = seq;

        const bool newStroke = sincePen_ > penGapSamples_ || lastCol_ < 0;
        sincePen_ = 0;
        if (newStroke) saveUndo();
        const float x = getNormalised(kPenX), y = getNormalised(kPenY);
        const int col = std::clamp((int)(x * kCols), 0, kCols - 1);
        const float db = (y * 2.0f - 1.0f) * kDrawRangeDb;
        if (newStroke)
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
        drawDirty_ = liveDirty_ = true;
        ++drawSeq_;
        handEdited_ = false;   // the pen wins over a pending hand-edit sync
    }

    void paint(int col, float db) { setPlain(kDraw0 + col, std::clamp(db, -kDrawRangeDb, kDrawRangeDb)); }

    // ---- host interface for ParaPage (absolute parameter indices) --------------------------------
public:
    float getN(int i) const { return getNormalised(i); }
    std::string paramText(int i) const { std::string t; return const_cast<EqArtist*>(this)->formatValue(i, get(i), t) ? t : param(i).display(get(i)); }
    void setValue(int i, float norm)
    {
        if (getNormalised(i) == norm) return;
        setNormalised(i, norm);
        notifyHostParameterChanged(i);
    }
    void setPlain(int i, float plain) { setValue(i, param(i).toNormalised(plain)); }

private:
    // ---- displays -----------------------------------------------------------------------------
    // The spectrum and the response curve are 64 read-only parameters. Every change is a message
    // to the MPC's screen, and if they come faster than the screen draws them they queue up and
    // the picture lags the sound. So each tick sends at most kDisplayBudget of them: the curve
    // first (it rarely changes), then the biggest spectrum changes, rises before falls (a hit
    // shows at once); a spectrum bar that has fallen just one step waits for spare budget.
    static constexpr int kDisplayBudget = 8;   // per tick, at 30 ticks a second: <= 240 per second
    static constexpr int kSpectrumShare = 2;   // spectrum updates per refresh kept while PARA catches up
    static constexpr int kDragBudgetLine = 12; // while dragging in LINE (Curve UI Test's LINE mode on the MPC: ~146/s at 16)
    static constexpr int kDragBudget = 16;     // while PARA curves catch up (proven on the MPC: ~146 line updates/s at 16)

    void updateDisplays(int analyzer, float secs)
    {
        static const float kFall[3] = {20.0f, 40.0f, 80.0f};   // dB per second
        if (analyzer != 0) spectrum_.analyse(kFall[std::clamp(getInt(kSpeed), 0, 2)], secs);
        for (int c = 0; c < kCols; ++c)
        {
            float v = 0.0f;
            if (analyzer != 0)
            {
                v = (spectrum_.columnDb(c) - kSpecBottomDb) / (kSpecTopDb - kSpecBottomDb);
                v = std::round(std::clamp(v, 0.0f, 1.0f) * (kSpecFrames - 1)) / (float)(kSpecFrames - 1);
            }
            want_[(std::size_t)c] = v;
        }
        if (respDirty_)
        {
            respDirty_ = false;
            double db[kCols];
            if (engine_.live()) for (int c = 0; c < kCols; ++c) db[c] = engine_.liveResponseDb(c);   // what you hear
            else engine_.responseDb(colHz_.data(), db, kCols);
            for (int c = 0; c < kCols; ++c)
            {
                // Snap to the dot film strip's 1 dB frames.
                const float d = std::clamp((float)db[c], -kDrawRangeDb, kDrawRangeDb);
                want_[(std::size_t)(kCols + c)] =
                    std::round((d + kDrawRangeDb) / (2.0f * kDrawRangeDb) * (kDotFrames - 1)) / (float)(kDotFrames - 1);
            }
        }
        // One queue for both pages (the plugin cannot tell which tab is showing). Priority: PARA's
        // node markers, PARA's line, PARA's band shading, DRAW's response, then the spectrum. While a
        // PARA node is held the budget rises to kDragBudget and the spectrum waits.
        const bool drag = para_.dragging();
        if (para_.takeInfoDirty()) bumpParaInfo();
        int count = 0;
        auto add = [&](int idx, float want, float frames, float base, bool spectrum) {
            items_[(std::size_t)count++] = {idx, want, frames, base, spectrum};
        };
        const int P = kPara0;
        for (int k = 0; k < 2 * eqpara::ParaPage::kBands; ++k) add(P + eqpara::ParaPage::pNode0 + k, para_.nodeTargets()[(std::size_t)k], 4000.0f, 4000.0f, false);
        add(P + eqpara::ParaPage::pSelNodeX, para_.selNodeTargets()[0], 4000.0f, 4000.0f, false);
        add(P + eqpara::ParaPage::pSelNodeY, para_.selNodeTargets()[1], 4000.0f, 4000.0f, false);
        // PARA curves. While a node is dragged only the nodes and the dragged band's own curve are sent (the
        // grey combined curve and the fill catch up when the finger stops): in LINE every column is 6 image
        // tiles, and sending everything during a drag swamped the MPC's screen until touches stopped.
        // Only the visible style's curves are sent.
        const bool dots = getInt(P + eqpara::ParaPage::pStyle) == 0;   // DOTS is the first (default) style
        if (dots)
            for (int k = 0; k < eq::ShadeStrip::kCols; ++k) add(P + eqpara::ParaPage::pShade0 + k, para_.shadeTargets()[(std::size_t)k], (float)(eq::ShadeStrip::kFrames - 1), 3000.0f, false);
        else
            for (int k = 0; k < eqpara::ParaPage::kLineCols; ++k) add(P + eqpara::ParaPage::pLine0 + k, para_.lineTargets()[(std::size_t)k], (float)(eqpara::ParaPage::Line::kFrames - 1), 3000.0f, false);
        if (!drag)
        {
            if (dots)
                for (int k = 0; k < eq::ShadeStrip::kCols; ++k) add(P + eqpara::ParaPage::pBgDot0 + k, para_.bgDotTargets()[(std::size_t)k], (float)(eq::ShadeStrip::kFrames - 1), 2000.0f, false);
            else
                for (int k = 0; k < eqpara::ParaPage::kLineCols; ++k) add(P + eqpara::ParaPage::pBgLine0 + k, para_.bgLineTargets()[(std::size_t)k], (float)(eqpara::ParaPage::Line::kFrames - 1), 2000.0f, false);
            for (int k = 0; k < eqpara::ParaPage::kLineCols; ++k) add(P + eqpara::ParaPage::pFill0 + k, para_.fillTargets()[(std::size_t)k], (float)(eqpara::ParaPage::kFillFrames - 1), 1500.0f, false);
        }
        for (int k = 0; k < kCols; ++k) add(kResp0 + k, want_[(std::size_t)(kCols + k)], (float)(kDotFrames - 1), 1000.0f, false);
        if (!drag) for (int k = 0; k < kCols; ++k) add(kSpec0 + k, want_[(std::size_t)k], (float)(kSpecFrames - 1), 0.0f, true);
        // The higher budget whenever PARA curves are still catching up (a drag, a knob, a band change), so
        // a change settles in well under a second; otherwise the proven 8 per refresh.
        bool paraPending = drag || para_.curvesPending();
        for (int k = 0; k < count && !paraPending; ++k)
            paraPending = !items_[(std::size_t)k].spectrum && items_[(std::size_t)k].idx >= kPara0 &&
                          std::fabs(items_[(std::size_t)k].want - getNormalised(items_[(std::size_t)k].idx)) * items_[(std::size_t)k].frames >= 0.5f;
        // LINE columns are 6 tiles each: a lower budget while dragging in LINE.
        const int budget = drag ? (dots ? kDragBudget : kDragBudgetLine) : (paraPending ? kDragBudget : kDisplayBudget);
        for (int sent = 0; sent < budget; ++sent)
        {
            int best = -1;
            float bestScore = 0.0f;
            for (int k = 0; k < count; ++k)
            {
                const DisplayItem& it = items_[(std::size_t)k];
                const float diff = (it.want - getNormalised(it.idx)) * it.frames;   // in frames
                if (std::fabs(diff) < 0.5f) continue;
                float score;
                if (!it.spectrum) score = it.base + std::min(std::fabs(diff), 999.0f);
                else if (diff > 0.0f) score = 2.0f * diff;          // spectrum rising: a hit shows at once
                else if (diff <= -1.5f) score = -diff;              // spectrum falling 2+ steps
                else score = 0.1f;                                  // a 1-step fall: only with budget to spare
                if (score > bestScore) { bestScore = score; best = k; }
            }
            if (best < 0) break;
            setValue(items_[(std::size_t)best].idx, items_[(std::size_t)best].want);
        }
        // While PARA curves catch up (not during a drag, when the spectrum pauses on purpose), the spectrum
        // still gets kSpectrumShare updates per refresh, so it never freezes behind a big redraw.
        if (paraPending && !drag)
            for (int sent = 0; sent < kSpectrumShare; ++sent)
            {
                int best = -1;
                float bestScore = 0.0f;
                for (int k = 0; k < count; ++k)
                {
                    const DisplayItem& it = items_[(std::size_t)k];
                    if (!it.spectrum) continue;
                    const float diff = (it.want - getNormalised(it.idx)) * it.frames;
                    if (std::fabs(diff) < 0.5f) continue;
                    const float score = diff > 0.0f ? 2.0f * diff : -diff;
                    if (score > bestScore) { bestScore = score; best = k; }
                }
                if (best < 0) break;
                setValue(items_[(std::size_t)best].idx, items_[(std::size_t)best].want);
            }
    }

    void bumpParaInfo()
    {
        paraInfoCounter_ = (paraInfoCounter_ + 1) % 100;
        setValue(kPara0 + eqpara::ParaPage::pInfo, (float)paraInfoCounter_ / 99.0f);
    }

    struct DisplayItem { int idx; float want, frames, base; bool spectrum; };
    std::array<DisplayItem, 2 * eqpara::ParaPage::kBands + 2 + 3 * eq::LineStrip::kCols + eq::ShadeStrip::kCols + 2 * kCols> items_{};
    int paraInfoCounter_ = 0;
    eqpara::ParaPage para_{kPara0};

    eq::EqEngine engine_;
    eq::Spectrum spectrum_;
    eq::FitWorker fit_;
    std::array<double, kCols> colHz_{};
    std::array<float, 2 * kCols> want_{};   // display targets: spectrum, then response
    int displayAge_ = 0, displayEvery_ = 1470;
    bool respDirty_ = true, drawDirty_ = false, liveDirty_ = false, discardFit_ = false, lastBypass_ = false;
    uint32_t drawSeq_ = 0, requestedSeq_ = 0, fittedSeq_ = 0;   // which drawing the bands were fitted to

    std::atomic<uint32_t> penSeq_{0};
    uint32_t lastPenSeq_ = 0;
    int sincePen_ = 1 << 30, penGapSamples_ = 6615, lastCol_ = -1;
    float lastDb_ = 0.0f;

    bool selfBandWrite_ = true, handEdited_ = false;
    int sinceHandEdit_ = 1 << 30, syncDelaySamples_ = 13230;

    std::array<float, kNumBandParams> undoBands_{};
    std::array<float, kCols> undoDraw_{};
    std::pair<int, float> undoFit_{-1, 0.0f};
    bool hasUndo_ = false;
    std::atomic<int> fitBands_{-1};   // -1: nothing fitted yet
    std::atomic<float> fitRms_{0.0f};
    int fitCounter_ = 0;
};

MPC_PLUGIN_ENTRY(EqArtist)
