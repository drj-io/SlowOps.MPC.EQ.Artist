// PARA page: an interactive parametric EQ, UI only for now (its bands are silent test bands; they
// never reach the EQ engine).
//
// How it takes touches (all verified on the MPC Live III with Curve UI Test, see
// CurveUITest/research/recommendation.md):
//   * One transparent XYPad over the whole graph is the pen (Para Pen X / Y). Stacked pads cannot be
//     separate nodes (the top pad takes every touch), so the plugin hit-tests: the first pen update
//     after a pause of kNewTouchSec (there is no release event) grabs the nearest shown node within
//     kGrabPx, keeping the finger-to-node offset; later updates drag it. Touching empty space does
//     nothing.
//   * The nodes are display-only XYPads bound to Node n X / Y; the plugin writes them.
//   * Dragging sets the band's frequency (x, log) and gain (y); cuts and notches have no gain and
//     only move in frequency.
// One set of controls (TYPE, Q, SLOPE and the Q-Links) serves all bands through proxy parameters:
// Sel Type / Q / Slope / Freq / Gain mirror the selected band both ways.
// It produces display targets for the host's queue: node positions; in front, the SELECTED band's own
// curve (LineStrip, 152 columns, white) over a flat fill in the band's colour (152 columns, one film strip
// for all colours); behind, the COMBINED curve of all shown bands in grey (a second LineStrip). STYLE
// switches both curves between the line and dots (64 columns each).
#pragma once

#include "mpc_plugin.h"

#include "../../core/include/slowops/eqartist/Filters.hpp"
#include "../../core/include/slowops/eqartist/LineStrip.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eqpara {

namespace eq = slowops::eqartist;

class ParaPage
{
public:
    static constexpr int kBands = 8;
    using Line = eq::LineStrip76;                           // the PARA line (76 columns; see LineStrip.hpp)
    static constexpr int kLineCols = Line::kCols;
    static constexpr int kFields = 5;   // Type, Freq, Gain, Q, Slope
    enum Field { fType, fFreq, fGain, fQ, fSlope };
    // Parameter offsets inside the PARA block (the block's ids are permanent once released).
    enum
    {
        pBands, pSel, pPenX, pPenY, pInfo, pSelNodeX, pSelNodeY,
        pSelType, pSelQ, pSelSlope, pSelFreq, pSelGain,      // proxies of the selected band
        pBand0,                                             // per band: Type, Freq, Gain, Q, Slope
        pNode0 = pBand0 + kBands * kFields,                 // per band: Node X, Node Y
        pLine0 = pNode0 + kBands * 2,
        pShade0 = pLine0 + kLineCols,
        pBgLine0 = pShade0 + eq::ShadeStrip::kCols,         // the combined curve, behind (grey)
        pBgDot0 = pBgLine0 + kLineCols,                     // the combined curve as dots (DOTS style)
        pStyle = pBgDot0 + eq::ShadeStrip::kCols,           // DOTS / LINE
        pAdd,                                               // trigger: add a band and select it
        pRemove,                                            // trigger: remove the selected band
        pFill0,                                             // the selected band's fill: 76 columns (colour x height)
        kNumParams = pFill0 + kLineCols
    };
    // The band fill: one film strip holds every band colour; frame = colour x height (0.25 dB, flat top at
    // the band's response at the column centre). Height 0 dB is an empty frame.
    static constexpr int kFillLevels = 145;
    static constexpr int kFillFrames = kBands * kFillLevels;
    static constexpr int kTypes = 6;              // Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch (= BandType 0..5)
    static constexpr double kNewTouchSec = 0.25;  // no release event: a pause this long ends a touch
    static constexpr double kGrabPx = 44.0;
    static constexpr double kGraphW = 1216.0, kGraphH = 408.0;   // the node travel area, px

    static int band(int b, int field) { return pBand0 + b * kFields + field; }
    static int proxyOf(int field)
    {
        switch (field) { case fType: return pSelType; case fFreq: return pSelFreq; case fGain: return pSelGain;
                         case fQ: return pSelQ; default: return pSelSlope; }
    }

    // ---- parameters ------------------------------------------------------------------------------
    static void addParams(mpc::Plugin& p)
    {
        auto display = [&p](mpc::Param q) { q.automatable = false; p.addParam(q); };
        const std::vector<std::string> types = {"Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch"};
        const std::vector<std::string> slopes = {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB"};
        p.addParam(mpc::intParam("pbands", "Para Bands", 1, kBands, 2));
        p.addParam(mpc::intParam("psel", "Para Sel", 0, kBands, 1));   // 0 = none: only the combined curve
        display(mpc::floatParam("ppenx", "Para Pen X", 0.0f, 1.0f, 0.5f, "", 3));
        display(mpc::floatParam("ppeny", "Para Pen Y", 0.0f, 1.0f, 0.5f, "", 3));
        display(mpc::intParam("pinfo", "Para Info", 0, 99, 0));
        display(mpc::floatParam("pselx", "Sel Node X", 0.0f, 1.0f, 0.5f, "", 4));
        display(mpc::floatParam("psely", "Sel Node Y", 0.0f, 1.0f, 0.5f, "", 4));
        p.addParam(mpc::choiceParam("pseltype", "Sel Type", types, 0));
        p.addParam(mpc::logParam("pselq", "Sel Q", 0.1f, 18.0f, 1.0f, "", 2));
        p.addParam(mpc::choiceParam("pselslope", "Sel Slope", slopes, 1));
        p.addParam(mpc::logParam("pselfreq", "Sel Freq", 20.0f, 20000.0f, 1000.0f, "Hz", 0));
        p.addParam(mpc::floatParam("pselgain", "Sel Gain", -18.0f, 18.0f, 0.0f, "dB", 1));
        static const float hz[kBands] = {150, 2500, 400, 1000, 60, 5000, 9000, 14000};
        static const float db[kBands] = {4, -3, 0, 0, 0, 0, 0, 0};
        for (int b = 0; b < kBands; ++b)
        {
            const std::string n = std::to_string(b + 1), id = "p" + n, name = "P" + n + " ";
            p.addParam(mpc::choiceParam((id + "type").c_str(), (name + "Type").c_str(), types, 0));
            p.addParam(mpc::logParam((id + "freq").c_str(), (name + "Freq").c_str(), 20.0f, 20000.0f, hz[b], "Hz", 0));
            p.addParam(mpc::floatParam((id + "gain").c_str(), (name + "Gain").c_str(), -18.0f, 18.0f, db[b], "dB", 1));
            p.addParam(mpc::logParam((id + "q").c_str(), (name + "Q").c_str(), 0.1f, 18.0f, 1.0f, "", 2));
            p.addParam(mpc::choiceParam((id + "slope").c_str(), (name + "Slope").c_str(), slopes, 1));
        }
        for (int b = 0; b < kBands; ++b)
        {
            const std::string n = std::to_string(b + 1);
            display(mpc::floatParam(("pnode" + n + "x").c_str(), ("Node " + n + " X").c_str(), 0.0f, 1.0f, 0.5f, "", 4));
            display(mpc::floatParam(("pnode" + n + "y").c_str(), ("Node " + n + " Y").c_str(), 0.0f, 1.0f, 0.5f, "", 4));
        }
        for (int c = 0; c < kLineCols; ++c)
            display(mpc::floatParam(("pline" + std::to_string(c + 1)).c_str(), ("PLine " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.0f, "", 5));
        for (int c = 0; c < eq::ShadeStrip::kCols; ++c)
            display(mpc::floatParam(("pshade" + std::to_string(c + 1)).c_str(), ("PShade " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.5f, "", 4));
        for (int c = 0; c < kLineCols; ++c)
            display(mpc::floatParam(("pbgline" + std::to_string(c + 1)).c_str(), ("PBgLine " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.0f, "", 5));
        for (int c = 0; c < eq::ShadeStrip::kCols; ++c)
            display(mpc::floatParam(("pbgdot" + std::to_string(c + 1)).c_str(), ("PBgDot " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.5f, "", 4));
        p.addParam(mpc::choiceParam("pstyle", "Para Style", {"Dots", "Line"}, 0));
        p.addParam(mpc::boolParam("padd", "Para Add", false));
        p.addParam(mpc::boolParam("premove", "Para Remove", false));
        for (int c = 0; c < kLineCols; ++c)
            display(mpc::floatParam(("pfill" + std::to_string(c + 1)).c_str(), ("PFill " + std::to_string(c + 1)).c_str(), 0.0f, 1.0f, 0.0f, "", 5));
    }

    explicit ParaPage(int base) : base_(base) {}
    int base() const { return base_; }

    // From the host's parameterChanged (any thread): only counts pen updates.
    void parameterChanged(int rel)
    {
        if (rel == pPenX || rel == pPenY) penSeq_.fetch_add(1, std::memory_order_relaxed);
    }

    // Host interface H: getN(i), get(i), getInt(i), setValue(i, norm), setPlain(i, plain), sampleRate(),
    // with absolute parameter indices.
    template <class H>
    void reset(H& h)
    {
        for (int t : {pAdd, pRemove}) if (h.getN(abs(t)) >= 0.5f) h.setValue(abs(t), 0.0f);
        lastPenSeq_ = penSeq_.load(std::memory_order_relaxed);
        sincePen_ = 1 << 30;
        grabbed_ = -1;
        lastSel_ = -1;   // proxies reload from the (restored) selected band
        dirtyAll_ = true;
        (void)h;
    }

    template <class H>
    void process(H& h, int n)
    {
        sincePen_ = std::min(sincePen_ + n, 1 << 30);
        const int gap = (int)(kNewTouchSec * h.sampleRate());
        handleAddRemove(h);
        const int bands = std::clamp(h.getInt(abs(pBands)), 1, kBands);
        if (h.getInt(abs(pSel)) > bands) h.setPlain(abs(pSel), (float)bands);

        handlePen(h, gap, bands);
        syncProxies(h);

        // The display targets. Node positions follow every change at once (the node under the finger must
        // stay instant). The curves are recomputed at most kCurveHz times a second, and only the bands that
        // changed are re-sampled (each band's response is cached on the curves' sample grids; the combined
        // curve is the sum of the cached bands).
        std::array<eq::BandParams, kBands> now{};
        for (int b = 0; b < kBands; ++b) now[(std::size_t)b] = readBand(h, b, b < bands);
        const int sel = std::clamp(h.getInt(abs(pSel)), 0, bands) - 1;   // -1 = none
        if (dirtyAll_ || now != shown_ || sel != shadeSel_)
        {
            curvesPending_ = true;
            shown_ = now;
            for (int b = 0; b < kBands; ++b)
            {
                nodeWant_[(std::size_t)(2 * b)] = (float)eq::GraphMap::hzToX(now[(std::size_t)b].freq);
                nodeWant_[(std::size_t)(2 * b + 1)] = (float)eq::GraphMap::dbToY(eq::typeHasGain(now[(std::size_t)b].type) ? now[(std::size_t)b].gainDb : 0.0);
            }
            if (sel >= 0)
            {
                selNodeWant_[0] = nodeWant_[(std::size_t)(2 * sel)];
                selNodeWant_[1] = nodeWant_[(std::size_t)(2 * sel + 1)];
            }
            infoDirty_ = true;
        }
        sinceCurves_ = std::min(sinceCurves_ + n, 1 << 30);
        if (curvesPending_ && (dirtyAll_ || sinceCurves_ >= (int)(h.sampleRate() / kCurveHz)))
        {
            curvesPending_ = false;
            sinceCurves_ = 0;
            recomputeCurves(now, sel);
        }
    }

    bool curvesPending() const { return curvesPending_; }

    bool dragging() const { return grabbed_ >= 0 && sincePen_ < dragHoldSamples_; }
    bool takeInfoDirty() { const bool d = infoDirty_; infoDirty_ = false; return d; }

    // Display targets (normalised values), in priority order for the host's queue.
    const std::array<float, kBands * 2>& nodeTargets() const { return nodeWant_; }
    const std::array<float, 2>& selNodeTargets() const { return selNodeWant_; }
    const std::array<float, kLineCols>& lineTargets() const { return lineWant_; }
    const std::array<float, eq::ShadeStrip::kCols>& shadeTargets() const { return shadeWant_; }
    const std::array<float, kLineCols>& bgLineTargets() const { return bgLineWant_; }
    const std::array<float, eq::ShadeStrip::kCols>& bgDotTargets() const { return bgDotWant_; }
    const std::array<float, kLineCols>& fillTargets() const { return fillWant_; }

    // "B2 Bell 2.80 kHz +6.0 dB Q 1.20" for the status label (any thread; reads parameters only).
    template <class H>
    std::string info(H& h) const
    {
        const int bands = std::clamp(h.getInt(abs(pBands)), 1, kBands);
        const int s = std::clamp(h.getInt(abs(pSel)), 0, bands) - 1;
        if (s < 0)
        {
            char nb[48];
            std::snprintf(nb, sizeof(nb), "%d band%s, none selected", bands, bands == 1 ? "" : "s");
            return nb;
        }
        static const char* names[kTypes] = {"Bell", "Lo Shelf", "Hi Shelf", "Lo Cut", "Hi Cut", "Notch"};
        const int t = std::clamp(h.getInt(abs(band(s, fType))), 0, kTypes - 1);
        const double hz = h.get(abs(band(s, fFreq)));
        char f[24], buf[96];
        if (hz < 1000.0) std::snprintf(f, sizeof(f), "%.0fHz", hz);
        else std::snprintf(f, sizeof(f), "%.2fkHz", hz / 1000.0);
        if (t == 3 || t == 4)
            std::snprintf(buf, sizeof(buf), "B%d %s %s %s", s + 1, names[t], f, h.paramText(abs(band(s, fSlope))).c_str());
        else if (t == 5)
            std::snprintf(buf, sizeof(buf), "B%d %s %s Q%.2f", s + 1, names[t], f, (double)h.get(abs(band(s, fQ))));
        else
            std::snprintf(buf, sizeof(buf), "B%d %s %s %+.1fdB Q%.2f", s + 1, names[t], f,
                          (double)h.get(abs(band(s, fGain))), (double)h.get(abs(band(s, fQ))));
        return buf;
    }

private:
    int abs(int rel) const { return base_ + rel; }

    void recomputeCurves(const std::array<eq::BandParams, kBands>& now, int sel)
    {
        dirtyAll_ = false;
        for (int b = 0; b < kBands; ++b)
        {
            const eq::BandParams& p = now[(std::size_t)b];
            if (cacheValid_[(std::size_t)b] && p == cached_[(std::size_t)b]) continue;
            cached_[(std::size_t)b] = p;
            cacheValid_[(std::size_t)b] = true;
            auto own = [&](double hz) { return eq::analogDb(p, hz); };   // 0 dB when the band is off
            eq::LineStrip::sample(own, q_[(std::size_t)b].data());
            eq::ShadeStrip::sample(own, s_[(std::size_t)b].data());
        }
        for (int i = 0; i < eq::LineStrip::kQuarterPoints; ++i)
        {
            double t = 0.0;
            for (int b = 0; b < kBands; ++b) t += q_[(std::size_t)b][(std::size_t)i];
            qSum_[(std::size_t)i] = t;
        }
        for (int i = 0; i < eq::ShadeStrip::kPoints; ++i)
        {
            double t = 0.0;
            for (int b = 0; b < kBands; ++b) t += s_[(std::size_t)b][(std::size_t)i];
            sSum_[(std::size_t)i] = t;
        }
        Line::encodeSampled(qSum_.data(), bgLineWant_.data());
        eq::ShadeStrip::encodeSampled(sSum_.data(), bgDotWant_.data());
        shadeSel_ = sel;
        if (sel >= 0)
        {
            const auto& q = q_[(std::size_t)sel];
            Line::encodeSampled(q.data(), lineWant_.data());
            eq::ShadeStrip::encodeSampled(s_[(std::size_t)sel].data(), shadeWant_.data());   // the band's dots
            for (int c = 0; c < kLineCols; ++c)
            {
                const double db = std::clamp(q[(std::size_t)(8 * c + 4)], -eq::GraphMap::kRangeDb, eq::GraphMap::kRangeDb);   // column centre
                const int level = (int)std::lround((db + eq::GraphMap::kRangeDb) / 0.25);
                fillWant_[(std::size_t)c] = (float)(sel * kFillLevels + level) / (float)(kFillFrames - 1);
            }
        }
        else
        {
            // None selected: the front curve (white line, or white dots) is the combined one; no fill.
            lineWant_ = bgLineWant_;
            shadeWant_ = bgDotWant_;
            fillWant_.fill((float)((kFillLevels - 1) / 2) / (float)(kFillFrames - 1));   // 0 dB: no fill
        }
    }

    template <class H>
    eq::BandParams readBand(H& h, int b, bool on) const
    {
        eq::BandParams p;
        p.on = on;
        p.type = (eq::BandType)std::clamp(h.getInt(abs(band(b, fType))), 0, kTypes - 1);
        p.freq = h.get(abs(band(b, fFreq)));
        p.gainDb = eq::typeHasGain(p.type) ? h.get(abs(band(b, fGain))) : 0.0;
        p.q = h.get(abs(band(b, fQ)));
        p.slope = std::clamp(h.getInt(abs(band(b, fSlope))), 0, eq::kNumSlopes - 1);
        return p;
    }

    // + adds a band (up to 8) at its default spot, flat, and selects it; - removes the selected band, moving
    // the bands above it down one place, and leaves none selected (at least one band stays).
    template <class H>
    void handleAddRemove(H& h)
    {
        static const float hz[kBands] = {150, 2500, 400, 1000, 60, 5000, 9000, 14000};
        const int bands = std::clamp(h.getInt(abs(pBands)), 1, kBands);
        if (h.getN(abs(pAdd)) >= 0.5f)
        {
            h.setValue(abs(pAdd), 0.0f);
            if (bands < kBands)
            {
                const int b = bands;   // the new band's index
                h.setPlain(abs(band(b, fType)), 0.0f);
                h.setPlain(abs(band(b, fFreq)), hz[b]);
                h.setPlain(abs(band(b, fGain)), 0.0f);
                h.setPlain(abs(band(b, fQ)), 1.0f);
                h.setPlain(abs(band(b, fSlope)), 1.0f);
                h.setPlain(abs(pBands), (float)(bands + 1));
                h.setPlain(abs(pSel), (float)(b + 1));
            }
        }
        if (h.getN(abs(pRemove)) >= 0.5f)
        {
            h.setValue(abs(pRemove), 0.0f);
            const int s = std::clamp(h.getInt(abs(pSel)), 0, bands) - 1;
            if (s >= 0 && bands > 1)
            {
                for (int b = s; b < bands - 1; ++b)
                    for (int f = 0; f < kFields; ++f) h.setValue(abs(band(b, f)), h.getN(abs(band(b + 1, f))));
                h.setPlain(abs(pBands), (float)(bands - 1));
                h.setPlain(abs(pSel), 0.0f);
                grabbed_ = -1;
            }
        }
    }

    template <class H>
    void handlePen(H& h, int gap, int bands)
    {
        dragHoldSamples_ = gap;
        const uint32_t seq = penSeq_.load(std::memory_order_relaxed);
        if (seq == lastPenSeq_) return;
        lastPenSeq_ = seq;
        const bool newTouch = sincePen_ > gap;
        sincePen_ = 0;
        const double x = h.getN(abs(pPenX)), y = h.getN(abs(pPenY));
        if (newTouch)
        {
            double nx[kBands], ny[kBands];
            bool shown[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const eq::BandParams p = readBand(h, b, true);
                nx[b] = eq::GraphMap::hzToX(p.freq);
                ny[b] = eq::GraphMap::dbToY(eq::typeHasGain(p.type) ? p.gainDb : 0.0);
                shown[b] = b < bands;
            }
            grabbed_ = eq::hitTest(nx, ny, shown, kBands, x, y, kGraphW, kGraphH, kGrabPx);
            if (grabbed_ >= 0)
            {
                offX_ = nx[grabbed_] - x;
                offY_ = ny[grabbed_] - y;
                h.setPlain(abs(pSel), (float)(grabbed_ + 1));
            }
        }
        if (grabbed_ < 0) return;
        const double gx = std::clamp(x + offX_, 0.0, 1.0), gy = std::clamp(y + offY_, 0.0, 1.0);
        h.setPlain(abs(band(grabbed_, fFreq)), (float)eq::GraphMap::xToHz(gx));
        const auto type = (eq::BandType)std::clamp(h.getInt(abs(band(grabbed_, fType))), 0, kTypes - 1);
        if (eq::typeHasGain(type)) h.setPlain(abs(band(grabbed_, fGain)), (float)eq::GraphMap::yToDb(gy));
    }

    // The proxies mirror the selected band: a proxy turned by the user (knob, switch, Q-Link) writes
    // the band; a band changed otherwise (drag) refreshes the proxy.
    template <class H>
    void syncProxies(H& h)
    {
        const int bands = std::clamp(h.getInt(abs(pBands)), 1, kBands);
        const int s = std::clamp(h.getInt(abs(pSel)), 0, bands) - 1;
        if (s < 0) { lastSel_ = -1; return; }   // none selected: the controls have nothing to edit
        if (s != lastSel_)
        {
            lastSel_ = s;
            for (int f = 0; f < kFields; ++f)
            {
                const float v = h.getN(abs(band(s, f)));
                h.setValue(abs(proxyOf(f)), v);
                lastProxy_[(std::size_t)f] = v;
            }
            return;
        }
        for (int f = 0; f < kFields; ++f)
        {
            const float pv = h.getN(abs(proxyOf(f)));
            if (pv != lastProxy_[(std::size_t)f]) h.setValue(abs(band(s, f)), pv);   // the user turned it
            const float bv = h.getN(abs(band(s, f)));
            if (bv != pv) h.setValue(abs(proxyOf(f)), bv);                          // the band moved
            lastProxy_[(std::size_t)f] = bv;
        }
    }

    int base_;
    std::atomic<uint32_t> penSeq_{0};
    uint32_t lastPenSeq_ = 0;
    int sincePen_ = 1 << 30, dragHoldSamples_ = 11025, grabbed_ = -1, lastSel_ = -1, shadeSel_ = -1;
    double offX_ = 0.0, offY_ = 0.0;
    std::array<float, kFields> lastProxy_{};
    std::array<eq::BandParams, kBands> shown_{};
    bool dirtyAll_ = true, infoDirty_ = true;
    std::array<float, kBands * 2> nodeWant_{};
    std::array<float, 2> selNodeWant_{};
    std::array<float, kLineCols> lineWant_{};
    std::array<float, eq::ShadeStrip::kCols> shadeWant_{};
    std::array<float, kLineCols> bgLineWant_{};
    std::array<float, eq::ShadeStrip::kCols> bgDotWant_{};
    std::array<float, kLineCols> fillWant_{};

    // Per-band response caches on the curves' sample grids, and their sums.
    static constexpr double kCurveHz = 30.0;
    std::array<std::array<double, eq::LineStrip::kQuarterPoints>, kBands> q_{};
    std::array<std::array<double, eq::ShadeStrip::kPoints>, kBands> s_{};
    std::array<double, eq::LineStrip::kQuarterPoints> qSum_{};
    std::array<double, eq::ShadeStrip::kPoints> sSum_{};
    std::array<eq::BandParams, kBands> cached_{};
    std::array<bool, kBands> cacheValid_{};
    bool curvesPending_ = true;
    int sinceCurves_ = 1 << 30;
};

} // namespace eqpara
