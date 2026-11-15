// Slow Ops EQ Artist: one plugin class, built twice.
//
//   EQ Artist Dots   (plugins/dots)   curves drawn as dots: the fewest screen widgets, for speed
//   EQ Artist Lines  (plugins/lines)  curves drawn as connected lines
//
// The Variant (V) only chooses how curves are drawn, and with that which display parameters exist.
// Everything else is shared: one set of eight bands, edited from two pages.
//
//   PARA  drag band nodes, TYPE / Q / SLOPE, + / - bands (ParaPage.hpp)
//   DRAW  paint a curve with the pen; it is fitted to the same bands
//
// How the pages stay in step:
//   * A stroke on DRAW: while you draw, the audio comes from the live EQ (32 bells solved straight from
//     the drawing, LiveEq.hpp). Meanwhile the drawing is fitted to bands on the fitter's own thread; the
//     result is written to the band parameters and the band count, and once the stroke has ended the
//     sound crossfades to the bands. PARA then shows the fitted bands (nothing selected: a fit renumbers
//     them).
//   * An edit on PARA (or a Q-Link, or automation): the band parameter changes, the engine glides to it,
//     and after 0.3 s without changes the drawing on DRAW is redrawn from the EQ.
//   * Both pages show the same "EQ you hear" curve (ParaPage's combined curve).
//
// This file is the adapter between the MPC Plugin Builder SDK and the DSP in
// core/include/slowops/eqartist/ (standard library only). Parameter ids and order are permanent from 1.0:
// new parameters are appended after the last one.
#pragma once

#include "mpc_plugin.h"

#include "../core/include/slowops/eqartist/EqEngine.hpp"
#include "../core/include/slowops/eqartist/FitWorker.hpp"
#include "../core/include/slowops/eqartist/Spectrum.hpp"
#include "ParaPage.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eqartist {

namespace eq = slowops::eqartist;
using eqpara::ParaPage;

// V supplies: static constexpr bool kLines; const char* kName; int kUid (MPC_FOURCC); int kVersion.
template <class V>
class EqArtistPlugin : public mpc::Plugin
{
public:
    static constexpr int kCols = 32;                               // drawing and spectrum columns
    static constexpr int kBands = eq::EqEngine::kNumBands;
    static constexpr int kPerBand = ParaPage::kPerBand;
    static constexpr int kCurveCols = ParaPage::cols(V::kLines);   // 76 (line) or 64 (dots)
    static constexpr float kDrawRangeDb = 18.0f;
    static constexpr float kSpecTopDb = -6.0f, kSpecBottomDb = -66.0f;
    static constexpr int kSpecFrames = 36;    // must match spec_bar.png (scripts/skin_layout.py)

    enum
    {
        kOutput,      // index 0: the MPC nudges it on every prepare, so it is harmless
        kClear,       // trigger: flat drawing, one flat band
        kUndo,        // trigger: undo the last stroke or Clear (press again to redo)
        kMaxBands,    // 1..8: DRAW's band count (- n +, data wheel). Always equal to kBandCount; changing it sets the
                      // count and refits the drawing
        kAnalyzer,    // spectrum on / off
        kAnalyze,     // spectrum of the input (Pre) or the output (Post)
        kSpeed,       // analyser fall: Slow / Medium / Fast
        kBypass,
        kPenX,        // DRAW's pen XYPad (input only)
        kPenY,
        kFitInfo,     // read-only: its value text is DRAW's status
        kBandCount,   // 1..8: bands 1..count are on
        kSel,         // 0 = none, 1..8 = the selected band
        kParaPenX,    // PARA's pen XYPad (input only)
        kParaPenY,
        kParaInfo,    // read-only: its value text is PARA's status
        kSelNodeX,    // read-only: the selected node (the ring)
        kSelNodeY,
        kSelType, kSelFreq, kSelGain, kSelQ, kSelSlope,   // proxies of the selected band
        kAdd,         // trigger: add a band
        kRemove,      // trigger: remove the selected band
        kBand0,       // per band: On, Type, Freq, Gain, Q, Slope
        kNode0 = kBand0 + kBands * kPerBand,   // read-only, per band: Node X, Node Y
        kDraw0 = kNode0 + kBands * 2,          // the drawing (saved with the project)
        kSpec0 = kDraw0 + kCols,               // read-only: spectrum
        kFront0 = kSpec0 + kCols,              // read-only: the selected band's curve
        kCurve0 = kFront0 + kCurveCols,        // read-only: the EQ you hear (combined curve)
        kFill0 = kCurve0 + kCurveCols,         // read-only: the selected band's fill
        kMoreBands = kFill0 + kCurveCols,      // trigger: DRAW's + (one band more, refitted)
        kFewerBands,                           // trigger: DRAW's - (one band fewer, refitted)
        kNumParams
    };
    enum { kOn, kType, kFreq, kGain, kQ, kSlope };
    static int bandParam(int band, int field) { return kBand0 + band * kPerBand + field; }
    static constexpr int kNumBandParams = kBands * kPerBand;

    EqArtistPlugin() : mpc::Plugin(makeInfo())
    {
        const std::vector<std::string> types = {"Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch"};
        const std::vector<std::string> slopes = {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB"};
        addParam(mpc::floatParam("output", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(mpc::boolParam("clear", "Clear", false));
        addParam(mpc::boolParam("undo", "Undo", false));
        addParam(mpc::intParam("maxbands", "Max Bands", 1, kBands, kBands));
        addParam(mpc::boolParam("analyzer", "Analyzer", true));
        addParam(mpc::choiceParam("analyze", "Analyze", {"Pre", "Post"}, 1));   // default Post: the spectrum of what you hear
        addParam(mpc::choiceParam("speed", "Speed", {"Slow", "Medium", "Fast"}, 1));
        addParam(mpc::boolParam("bypass", "Bypass", false));
        addDisplay(mpc::floatParam("penx", "Pen X", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::floatParam("peny", "Pen Y", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::intParam("fitinfo", "Fit", 0, 99, 0));
        addParam(mpc::intParam("bands", "Bands", 1, kBands, 2));
        addParam(mpc::intParam("sel", "Sel Band", 0, kBands, 1));
        addDisplay(mpc::floatParam("ppenx", "Para Pen X", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::floatParam("ppeny", "Para Pen Y", 0.0f, 1.0f, 0.5f, "", 3));
        addDisplay(mpc::intParam("pinfo", "Para Info", 0, 99, 0));
        addDisplay(mpc::floatParam("selx", "Sel Node X", 0.0f, 1.0f, 0.5f, "", 4));
        addDisplay(mpc::floatParam("sely", "Sel Node Y", 0.0f, 1.0f, 0.5f, "", 4));
        addParam(mpc::choiceParam("seltype", "Sel Type", types, 0));
        addParam(mpc::logParam("selfreq", "Sel Freq", 20.0f, 20000.0f, 1000.0f, "Hz", 0));
        addParam(mpc::floatParam("selgain", "Sel Gain", -kDrawRangeDb, kDrawRangeDb, 0.0f, "dB", 1));
        addParam(mpc::logParam("selq", "Sel Q", 0.1f, 18.0f, 1.0f, "", 2));
        addParam(mpc::choiceParam("selslope", "Sel Slope", slopes, 1));
        addParam(mpc::boolParam("add", "Add Band", false));
        addParam(mpc::boolParam("remove", "Remove Band", false));
        for (int b = 0; b < kBands; ++b)
        {
            const std::string n = std::to_string(b + 1), id = "b" + n, name = "B" + n + " ";
            addParam(mpc::boolParam((id + "on").c_str(), (name + "On").c_str(), b < 2));
            addParam(mpc::choiceParam((id + "type").c_str(), (name + "Type").c_str(), types, 0));
            addParam(mpc::logParam((id + "freq").c_str(), (name + "Freq").c_str(), 20.0f, 20000.0f, ParaPage::kDefaultHz[b], "Hz", 0));
            addParam(mpc::floatParam((id + "gain").c_str(), (name + "Gain").c_str(), -kDrawRangeDb, kDrawRangeDb, 0.0f, "dB", 1));
            addParam(mpc::logParam((id + "q").c_str(), (name + "Q").c_str(), 0.1f, 18.0f, 1.0f, "", 2));
            addParam(mpc::choiceParam((id + "slope").c_str(), (name + "Slope").c_str(), slopes, 1));
        }
        for (int b = 0; b < kBands; ++b)
        {
            const std::string n = std::to_string(b + 1);
            addDisplay(mpc::floatParam(("node" + n + "x").c_str(), ("Node " + n + " X").c_str(), 0.0f, 1.0f, 0.5f, "", 4));
            addDisplay(mpc::floatParam(("node" + n + "y").c_str(), ("Node " + n + " Y").c_str(), 0.0f, 1.0f, 0.5f, "", 4));
        }
        // Not automatable: the drawing is edited with the pen and saved with the project.
        addColumns("draw", "Draw ", kCols, -kDrawRangeDb, kDrawRangeDb, 0.0f, 1);
        addColumns("spec", "Spec ", kCols, 0.0f, 1.0f, 0.0f, 3);
        addColumns("front", "Front ", kCurveCols, 0.0f, 1.0f, 0.5f, 5);
        addColumns("curve", "Curve ", kCurveCols, 0.0f, 1.0f, 0.5f, 5);
        addColumns("fill", "Fill ", kCurveCols, 0.0f, 1.0f, 0.0f, 5);
        addParam(mpc::boolParam("drawmore", "More Bands", false));
        addParam(mpc::boolParam("drawfewer", "Fewer Bands", false));
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
    }

    // Also called after a state load. Restoring the pens or the bands from a project must not count as a
    // pen stroke or a hand edit, and the triggers must not fire.
    void reset() override
    {
        for (int t : {kClear, kUndo, kMoreBands, kFewerBands}) if (getBool(t)) setNormalised(t, 0.0f);
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
        para_.setLive(nullptr);
        countSeen_ = std::clamp(getInt(kBandCount), 1, kBands);
        maxSeen_ = -1;   // a loaded Max Bands is not a turn of the knob; the next block sets it to the count
        paraLive_ = false;
    }

    void parameterChanged(int index, float plain) override
    {
        (void)plain;
        if (index == kPenX || index == kPenY) penSeq_.fetch_add(1, std::memory_order_relaxed);
        else para_.parameterChanged(index);
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

        followDrawBandsKnob();
        initBandsAddedFromOutside();
        handleTriggers();
        para_.process(*this, n);   // PARA's pen, + / -, proxies, band count: all write the band parameters
        handlePen(n);
        updateLive();
        handleFitResult();
        requestFitIfNeeded();
        pullBands();
        syncDrawingToHandEdits(n);
        sinceEdit_ = std::min(sinceEdit_ + n, 1 << 30);
        if (editOpen_ && sinceEdit_ >= syncDelaySamples_) editOpen_ = false;   // the PARA edit has settled
        countSeen_ = std::clamp(getInt(kBandCount), 1, kBands);
        if (getInt(kMaxBands) != countSeen_) setPlain(kMaxBands, (float)countSeen_);   // DRAW's knob shows the count
        maxSeen_ = countSeen_;
        leaveLiveWhenFitted();
        showLiveOnCurves();

        const int analyzer = !getBool(kAnalyzer) ? 0 : (getInt(kAnalyze) == 0 ? 1 : 2);   // off / pre / post
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
        if (index == kParaInfo) { out = para_.info(*this); return true; }
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
        const int field = index >= kBand0 && index < kNode0 ? (index - kBand0) % kPerBand : -1;
        if (field == kFreq || index == kSelFreq)
        {
            if (plain < 1000.0f) std::snprintf(buf, sizeof(buf), "%.0f Hz", (double)plain);
            else std::snprintf(buf, sizeof(buf), "%.2f kHz", (double)plain / 1000.0);
            out = buf;
            return true;
        }
        if (field == kGain || index == kSelGain)
        {
            const int band = index == kSelGain ? getInt(kSel) - 1 : (index - kBand0) / kPerBand;
            const bool notch = band >= 0 && band < kBands && getInt(bandParam(band, kType)) == (int)eq::BandType::Notch;
            if (notch && eq::notchIsFull(plain)) out = "Full";
            else std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)(notch ? std::min(plain, 0.0f) : plain)), out = buf;
            return true;
        }
        return false;
    }

    // ---- host interface for ParaPage -------------------------------------------------------------
    float getN(int i) const { return getNormalised(i); }
    std::string paramText(int i) const { return param(i).display(get(i)); }
    void setValue(int i, float norm)
    {
        if (getNormalised(i) == norm) return;
        setNormalised(i, norm);
        notifyHostParameterChanged(i);
    }
    void setPlain(int i, float plain) { setValue(i, param(i).toNormalised(plain)); }

    // PARA is about to change a band (a grab, + / -, a control). The state before it becomes the undo step,
    // once a band actually changes (pullBands); changes until 0.3 s of stillness count as one edit.
    void beginEdit()
    {
        sinceEdit_ = 0;
        if (editOpen_) return;
        editOpen_ = true;
        editCommitted_ = false;
        capture(pendingUndo_);
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = V::kName;
        info.vendor = "Slow Ops";
        info.uniqueId = V::kUid;
        info.version = V::kVersion;
        info.isInstrument = false;
        info.numInputs = 2;
        info.numOutputs = 2;
        info.tailSamples = 0;
        return info;
    }

    static eqpara::ParaIdx paraIdx()
    {
        eqpara::ParaIdx i{};
        i.bands = kBandCount; i.sel = kSel; i.penX = kParaPenX; i.penY = kParaPenY; i.info = kParaInfo;
        i.selNodeX = kSelNodeX; i.selNodeY = kSelNodeY;
        i.proxy[ParaPage::fType] = kSelType; i.proxy[ParaPage::fFreq] = kSelFreq; i.proxy[ParaPage::fGain] = kSelGain;
        i.proxy[ParaPage::fQ] = kSelQ; i.proxy[ParaPage::fSlope] = kSelSlope;
        i.add = kAdd; i.remove = kRemove; i.band0 = kBand0; i.node0 = kNode0;
        return i;
    }

    void addDisplay(mpc::Param p)
    {
        p.automatable = false;
        addParam(p);
    }
    void addColumns(const char* id, const char* name, int count, float lo, float hi, float def, int decimals)
    {
        for (int c = 0; c < count; ++c)
        {
            const std::string n = std::to_string(c + 1);
            addDisplay(mpc::floatParam((id + n).c_str(), (name + n).c_str(), lo, hi, def, "", decimals));
        }
    }

    // ---- bands <-> parameters ---------------------------------------------------------------
    eq::BandParams readBand(int b) const
    {
        eq::BandParams p;
        p.on = getBool(bandParam(b, kOn));
        p.type = (eq::BandType)std::clamp(getInt(bandParam(b, kType)), 0, ParaPage::kTypes - 1);
        p.freq = get(bandParam(b, kFreq));
        p.gainDb = get(bandParam(b, kGain));
        p.q = get(bandParam(b, kQ));
        p.slope = std::clamp(getInt(bandParam(b, kSlope)), 0, eq::kNumSlopes - 1);
        return p;
    }

    // Engine targets from the band parameters. A change the fit (or Undo) did not make is a hand edit
    // (PARA, a Q-Link, automation): the drawing follows it once the edit has settled.
    void pullBands()
    {
        bool changed = false;
        for (int b = 0; b < kBands; ++b)
        {
            const eq::BandParams p = readBand(b);
            if (!(p == engine_.band(b))) { engine_.setBand(b, p); changed = true; }
        }
        if (changed && !selfBandWrite_)
        {
            if (editOpen_ && !editCommitted_) { undo_ = pendingUndo_; hasUndo_ = true; editCommitted_ = true; }
            handEdited_ = true;
            sinceHandEdit_ = 0;
            engine_.setLive(false);   // hear the bands
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

    // DRAW's BANDS knob (kMaxBands) turned: it becomes the band count, and the drawing is refitted with that many
    // bands (one undo step). Other count changes (PARA, a fit, Undo) are copied to the knob at the end of the block.
    void followDrawBandsKnob()
    {
        // DRAW's - / + step the knob by one band.
        for (int t : {kMoreBands, kFewerBands})
        {
            if (!getBool(t)) continue;
            setValue(t, 0.0f);
            if (maxSeen_ >= 0)
                setPlain(kMaxBands, (float)std::clamp(maxSeen_ + (t == kMoreBands ? 1 : -1), 1, kBands));
        }
        const int want = std::clamp(getInt(kMaxBands), 1, kBands);
        if (maxSeen_ < 0 || want == maxSeen_) return;
        saveUndo();
        setPlain(kBandCount, (float)want);
        countSeen_ = want;    // the refit writes every band; no flat start needed
        maxSeen_ = want;
        drawDirty_ = true;    // refit the drawing as it is (it always shows the EQ you hear)
        ++drawSeq_;
        handEdited_ = false;
    }

    // The band count raised from outside the plugin's own paths (the data wheel on BANDS, a Q-Link, automation):
    // + and the fit set up the bands they switch on, but these would switch on a band with its old settings. Start
    // each such band flat at its default spot, as + does, as one undo step.
    void initBandsAddedFromOutside()
    {
        const int count = std::clamp(getInt(kBandCount), 1, kBands);
        if (count <= countSeen_) return;
        const bool fresh = !editOpen_;
        beginEdit();
        if (fresh) pendingUndo_.count = param(kBandCount).toNormalised((float)countSeen_);   // the count before the wheel
        for (int b = countSeen_; b < count; ++b)
        {
            setPlain(bandParam(b, kType), 0.0f);
            setPlain(bandParam(b, kFreq), ParaPage::kDefaultHz[b]);
            setPlain(bandParam(b, kGain), 0.0f);
            setPlain(bandParam(b, kQ), 1.0f);
            setPlain(bandParam(b, kSlope), 1.0f);
        }
        countSeen_ = count;
    }

    // ---- Undo: one snapshot of the drawing, the bands and the band count, before each stroke, Clear or
    // PARA edit. Undo swaps it with the current state, so pressing again redoes. ----
    struct UndoState
    {
        std::array<float, kNumBandParams> bands{};
        std::array<float, kCols> draw{};
        float count = 0.0f;
        std::pair<int, float> fit{-1, 0.0f};
    };

    void capture(UndoState& u) const
    {
        for (int i = 0; i < kNumBandParams; ++i) u.bands[(std::size_t)i] = getNormalised(kBand0 + i);
        for (int c = 0; c < kCols; ++c) u.draw[(std::size_t)c] = getNormalised(kDraw0 + c);
        u.count = getNormalised(kBandCount);
        u.fit = {fitBands_.load(), fitRms_.load()};
    }

    void saveUndo()
    {
        capture(undo_);
        hasUndo_ = true;
        editOpen_ = false;
    }

    void swapUndo()
    {
        UndoState cur;
        capture(cur);
        for (int i = 0; i < kNumBandParams; ++i) setValue(kBand0 + i, undo_.bands[(std::size_t)i]);
        for (int c = 0; c < kCols; ++c) setValue(kDraw0 + c, undo_.draw[(std::size_t)c]);
        setValue(kBandCount, undo_.count);
        setPlain(kSel, 0.0f);
        fitBands_.store(undo_.fit.first);
        fitRms_.store(undo_.fit.second);
        undo_ = cur;
        editOpen_ = false;
        selfBandWrite_ = true;
        drawDirty_ = false;
        liveDirty_ = false;
        discardFit_ = fit_.busy();   // a fit still running belongs to the undone drawing
        fittedSeq_ = ++drawSeq_;     // the restored bands belong to the restored drawing
        engine_.forceCrossfade();
        engine_.setLive(false);
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
        // Bypass shows in DRAW's status line.
        const bool byp = getBool(kBypass);
        if (byp != lastBypass_) { lastBypass_ = byp; bumpFitInfo(); }
    }

    // ---- the fit: rerun whenever the drawing has changed and the fitter is free --------------
    void requestFitIfNeeded()
    {
        if (!drawDirty_ || fit_.busy()) return;
        double db[kCols];
        for (int c = 0; c < kCols; ++c) db[c] = get(kDraw0 + c);
        requestedCount_ = std::clamp(getInt(kBandCount), 1, kBands);
        if (fit_.request(colHz_.data(), db, kCols, requestedCount_)) { drawDirty_ = false; requestedSeq_ = drawSeq_; }
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
        liveShown_ = false;
    }

    // Back to the fitted bands once the stroke has ended and the bands fit the drawing as it is now.
    void leaveLiveWhenFitted()
    {
        if (!engine_.live() || drawDirty_ || liveDirty_ || fittedSeq_ != drawSeq_ || sincePen_ <= penGapSamples_) return;
        engine_.setLive(false);
    }

    // The "EQ you hear" curve shows the live EQ's response while it sounds, the bands' otherwise.
    void showLiveOnCurves()
    {
        const bool live = engine_.live();
        if (live == paraLive_ && (!live || liveShown_)) return;
        paraLive_ = live;
        liveShown_ = true;
        if (!live) { para_.setLive(nullptr); return; }
        double db[kCols];
        for (int c = 0; c < kCols; ++c) db[c] = engine_.liveResponseDb(c);
        para_.setLive(db);
    }

    void handleFitResult()
    {
        eq::FitResult r;
        if (!fit_.take(r)) return;
        if (discardFit_) { discardFit_ = false; return; }
        // The band count changed while this fit ran (DRAW's BANDS knob being turned): it is out of date, and
        // applying it would snap the count back. The knob already asked for a refit with the new count.
        if (requestedCount_ != std::clamp(getInt(kBandCount), 1, kBands)) { drawDirty_ = true; return; }
        fittedSeq_ = requestedSeq_;
        // The fit may use up to the band count; the bands it does not need sit flat (0 dB bells at their default
        // spots), so the count stays what you set and PARA has them to grab.
        const int count = requestedCount_;
        for (int b = 0; b < kBands; ++b)
        {
            eq::BandParams p;
            if (b < r.count) p = r.bands[(std::size_t)b];
            else if (b < count)
            {
                p.type = eq::BandType::Bell;
                p.freq = ParaPage::kDefaultHz[b];
                p.gainDb = 0.0;
                p.q = 1.0;
                p.slope = 1;
            }
            p.on = b < count;
            writeBand(b, p);
        }
        setPlain(kBandCount, (float)count);
        setPlain(kSel, 0.0f);   // the fit renumbers the bands: nothing stays selected
        selfBandWrite_ = true;
        handEdited_ = false;
        // Each fit may number the bands differently, so switch the whole set over (30 ms) rather
        // than letting band 1 glide from one frequency to another.
        engine_.forceCrossfade();
        fitBands_.store(r.count > 0 ? count : 0);
        fitRms_.store((float)r.rmsDb);
        bumpFitInfo();
    }

    // After a hand edit has been still for syncDelaySamples_, redraw the drawing from the EQ you
    // hear, so DRAW shows what PARA made and the next stroke edits that curve.
    void syncDrawingToHandEdits(int n)
    {
        sinceHandEdit_ = std::min(sinceHandEdit_ + n, 1 << 30);
        if (!handEdited_ || sinceHandEdit_ < syncDelaySamples_ || drawDirty_ || fit_.busy()) return;
        handEdited_ = false;
        double db[kCols];
        engine_.responseDb(colHz_.data(), db, kCols);
        for (int c = 0; c < kCols; ++c) paint(c, (float)db[c]);
        fittedSeq_ = ++drawSeq_;   // the bands are the source of this drawing
        fitBands_.store(getInt(kBandCount));
        fitRms_.store(0.0f);
        bumpFitInfo();
    }

    // A status line is the value text of a counter parameter: changing the value makes the MPC ask for
    // the text again.
    void bumpFitInfo()
    {
        fitCounter_ = (fitCounter_ + 1) % 100;
        setValue(kFitInfo, (float)fitCounter_ / 99.0f);
    }
    void bumpParaInfo()
    {
        paraInfoCounter_ = (paraInfoCounter_ + 1) % 100;
        setValue(kParaInfo, (float)paraInfoCounter_ / 99.0f);
    }

    // ---- DRAW's pen ---------------------------------------------------------------------------
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

    // ---- displays -----------------------------------------------------------------------------
    // Every display value is a read-only parameter, and every change is a message to the MPC's screen.
    // If they come faster than the screen draws them they queue up and the picture lags, so each refresh
    // (30 a second) sends at most a budget of them, most important first: the nodes, the selected band's
    // curve, the combined curve, the fill, then the spectrum (rises before falls, so a hit shows at once).
    // While a PARA node is dragged only the nodes and the dragged band's curve are sent; the rest catches
    // up when the finger stops. (Sending everything during a drag swamped the MPC's screen.)
    static constexpr int kDisplayBudget = 8;   // per refresh: <= 240 a second (verified on the MPC)
    static constexpr int kSpectrumShare = 2;   // spectrum updates per refresh kept while curves catch up
    static constexpr int kCatchUpBudget = 16;  // while curves catch up, and while dragging dots
    static constexpr int kDragBudgetLine = 12; // while dragging a line (each column is 3 image tiles)

    void updateDisplays(int analyzer, float secs)
    {
        static const float kFall[3] = {20.0f, 40.0f, 80.0f};   // dB per second
        if (analyzer != 0) spectrum_.analyse(kFall[std::clamp(getInt(kSpeed), 0, 2)], secs);
        const bool drag = para_.dragging();
        if (para_.takeInfoDirty()) bumpParaInfo();
        int count = 0;
        auto add = [&](int idx, float want, float frames, float base, bool spectrum) {
            items_[(std::size_t)count++] = {idx, want, frames, base, spectrum};
        };
        const float curveFrames = (float)(ParaPage::curveFrames(V::kLines) - 1);
        for (int k = 0; k < 2 * kBands; ++k) add(kNode0 + k, para_.nodeTargets()[(std::size_t)k], 4000.0f, 4000.0f, false);
        add(kSelNodeX, para_.selNodeTargets()[0], 4000.0f, 4000.0f, false);
        add(kSelNodeY, para_.selNodeTargets()[1], 4000.0f, 4000.0f, false);
        for (int k = 0; k < kCurveCols; ++k) add(kFront0 + k, para_.frontTargets()[(std::size_t)k], curveFrames, 3000.0f, false);
        if (!drag)
        {
            for (int k = 0; k < kCurveCols; ++k) add(kCurve0 + k, para_.curveTargets()[(std::size_t)k], curveFrames, 2000.0f, false);
            for (int k = 0; k < kCurveCols; ++k) add(kFill0 + k, para_.fillTargets()[(std::size_t)k], (float)(ParaPage::kFillFrames - 1), 1500.0f, false);
            for (int c = 0; c < kCols; ++c)
            {
                float v = 0.0f;
                if (analyzer != 0)
                {
                    v = (spectrum_.columnDb(c) - kSpecBottomDb) / (kSpecTopDb - kSpecBottomDb);
                    v = std::round(std::clamp(v, 0.0f, 1.0f) * (kSpecFrames - 1)) / (float)(kSpecFrames - 1);
                }
                add(kSpec0 + c, v, (float)(kSpecFrames - 1), 0.0f, true);
            }
        }
        bool curvesBehind = drag || para_.curvesPending();
        for (int k = 0; k < count && !curvesBehind; ++k)
            curvesBehind = !items_[(std::size_t)k].spectrum &&
                           std::fabs(items_[(std::size_t)k].want - getNormalised(items_[(std::size_t)k].idx)) * items_[(std::size_t)k].frames >= 0.5f;
        const int budget = drag ? (V::kLines ? kDragBudgetLine : kCatchUpBudget) : (curvesBehind ? kCatchUpBudget : kDisplayBudget);
        sendBest(count, budget, false);
        // While curves catch up (not during a drag, when the spectrum pauses on purpose), the spectrum still
        // gets a share, so it never freezes behind a big redraw.
        if (curvesBehind && !drag) sendBest(count, kSpectrumShare, true);
    }

    void sendBest(int count, int budget, bool spectrumOnly)
    {
        for (int sent = 0; sent < budget; ++sent)
        {
            int best = -1;
            float bestScore = 0.0f;
            for (int k = 0; k < count; ++k)
            {
                const DisplayItem& it = items_[(std::size_t)k];
                if (spectrumOnly && !it.spectrum) continue;
                const float diff = (it.want - getNormalised(it.idx)) * it.frames;   // in frames
                if (std::fabs(diff) < 0.5f) continue;
                float score;
                if (!it.spectrum) score = it.base + std::min(std::fabs(diff), 999.0f);
                else if (diff > 0.0f) score = 2.0f * diff;          // spectrum rising: a hit shows at once
                else if (diff <= -1.5f || spectrumOnly) score = -diff;   // spectrum falling 2+ steps
                else score = 0.1f;                                  // a 1-step fall: only with budget to spare
                if (score > bestScore) { bestScore = score; best = k; }
            }
            if (best < 0) break;
            setValue(items_[(std::size_t)best].idx, items_[(std::size_t)best].want);
        }
    }

    struct DisplayItem { int idx; float want, frames, base; bool spectrum; };
    std::array<DisplayItem, 2 * kBands + 2 + 3 * kCurveCols + kCols> items_{};
    int paraInfoCounter_ = 0;
    ParaPage para_{paraIdx(), V::kLines};
    bool paraLive_ = false, liveShown_ = false;

    eq::EqEngine engine_;
    eq::Spectrum spectrum_;
    eq::FitWorker fit_;
    std::array<double, kCols> colHz_{};
    int displayAge_ = 0, displayEvery_ = 1470;
    bool drawDirty_ = false, liveDirty_ = false, discardFit_ = false, lastBypass_ = false;
    uint32_t drawSeq_ = 0, requestedSeq_ = 0, fittedSeq_ = 0;   // which drawing the bands were fitted to

    std::atomic<uint32_t> penSeq_{0};
    uint32_t lastPenSeq_ = 0;
    int sincePen_ = 1 << 30, penGapSamples_ = 6615, lastCol_ = -1;
    float lastDb_ = 0.0f;

    bool selfBandWrite_ = true, handEdited_ = false;
    int sinceHandEdit_ = 1 << 30, syncDelaySamples_ = 13230;

    UndoState undo_, pendingUndo_;
    bool hasUndo_ = false, editOpen_ = false, editCommitted_ = false;
    int countSeen_ = 2;   // the band count at the end of the last block
    int maxSeen_ = -1;    // DRAW's BANDS knob at the end of the last block (-1: just loaded)
    int requestedCount_ = 2;   // the band count the running fit was asked for
    int sinceEdit_ = 1 << 30;
    std::atomic<int> fitBands_{-1};   // -1: nothing fitted yet
    std::atomic<float> fitRms_{0.0f};
    int fitCounter_ = 0;
};

} // namespace eqartist
