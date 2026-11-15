// PARA page: the interactive parametric view of the EQ. It edits the plugin's real bands (the same
// eight bands DRAW's curve fit writes), so what you do here is what you hear.
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
// Bands (1..8) is the band count: bands 1..Bands are switched on, the rest off.
// One set of controls (TYPE, Q, SLOPE and the Q-Links) serves all bands through proxy parameters:
// Sel Type / Freq / Gain / Q / Slope mirror the selected band both ways.
//
// It produces display targets for the host's queue:
//   nodes     Node n X / Y, and the selected node (for the ring)
//   front     the selected band's own curve (white); with no band selected, the combined curve
//   curve     the combined curve of all bands (grey on PARA; DRAW shows the same values as "the EQ you
//             hear"). While the live EQ is sounding (a stroke on DRAW), the host hands over the live
//             response and this curve shows that instead.
//   fill      a flat fill under the selected band's curve: frame = band colour x height
// as a line (LineStrip76, 76 columns) or as dots (ShadeStrip, 64 columns), chosen at construction.
#pragma once

#include "../core/include/slowops/eqartist/Filters.hpp"
#include "../core/include/slowops/eqartist/LineStrip.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eqpara {

namespace eq = slowops::eqartist;

// Where the page's parameters live in the host plugin (absolute indices).
struct ParaIdx
{
    int bands, sel, penX, penY, info, selNodeX, selNodeY;
    int proxy[5];      // Sel Type, Freq, Gain, Q, Slope
    int add, remove;
    int band0;         // per band: On, Type, Freq, Gain, Q, Slope
    int node0;         // per band: Node X, Node Y
};

class ParaPage
{
public:
    static constexpr int kBands = 8;
    static constexpr int kPerBand = 6;
    static constexpr int kFields = 5;
    enum Field { fType, fFreq, fGain, fQ, fSlope };
    using Line = eq::LineStrip76;
    static constexpr int kMaxCols = Line::kCols;                 // 76 (line) >= 64 (dots)
    static constexpr int kFillLevels = 145;                      // 0.25 dB heights per band colour
    static constexpr int kFillFrames = kBands * kFillLevels;
    static constexpr int kTypes = 6;              // Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch (= BandType 0..5)
    static constexpr double kNewTouchSec = 0.25;  // no release event: a pause this long ends a touch
    static constexpr double kGrabPx = 44.0;
    static constexpr double kGraphW = 1216.0, kGraphH = 408.0;   // the node travel area, px
    static constexpr float kDefaultHz[kBands] = {150, 2500, 400, 1000, 60, 5000, 9000, 14000};

    // Where a band's node sits vertically, in dB on the graph: its gain (bell, shelves; a notch's depth, 0 .. -18 =
    // full), or for a cut its resonance: Q 0.71 (a plain cut) on the 0 dB line, +6 dB per doubling of Q.
    static double nodeDb(const eq::BandParams& p)
    {
        if (eq::typeHasSlope(p.type)) return std::clamp(20.0 * std::log10(p.q / M_SQRT1_2), -18.0, 18.0);
        if (p.type == eq::BandType::Notch) return std::clamp(p.gainDb, eq::kNotchFullDb, 0.0);
        return p.gainDb;
    }
    static double cutQFromDb(double db) { return std::clamp(M_SQRT1_2 * std::pow(10.0, db / 20.0), 0.1, 18.0); }

    static constexpr int cols(bool lines) { return lines ? Line::kCols : eq::ShadeStrip::kCols; }
    static constexpr int curveFrames(bool lines) { return lines ? Line::kFrames : eq::ShadeStrip::kFrames; }

    ParaPage(const ParaIdx& idx, bool lines) : i_(idx), lines_(lines) {}

    int cols() const { return cols(lines_); }

    // From the host's parameterChanged (any thread): only counts pen updates.
    void parameterChanged(int index)
    {
        if (index == i_.penX || index == i_.penY) penSeq_.fetch_add(1, std::memory_order_relaxed);
    }

    // Host interface H: getN(i), get(i), getInt(i), setValue(i, norm), setPlain(i, plain), sampleRate(),
    // paramText(i), beginEdit() (called before PARA changes a band: the host's undo step).
    template <class H>
    void reset(H& h)
    {
        for (int t : {i_.add, i_.remove}) if (h.getN(t) >= 0.5f) h.setValue(t, 0.0f);
        lastPenSeq_ = penSeq_.load(std::memory_order_relaxed);
        sincePen_ = 1 << 30;
        grabbed_ = -1;
        lastSel_ = -1;   // proxies reload from the (restored) selected band
        dirtyAll_ = true;
    }

    // The live EQ's response at the 32 drawing columns (centres at (c + 0.5) / 32), or nullptr when the
    // bands are sounding.
    void setLive(const double* columnsDb)
    {
        live_ = columnsDb != nullptr;
        if (live_) for (int c = 0; c < 32; ++c) liveDb_[(std::size_t)c] = columnsDb[c];
        curvesPending_ = true;
    }

    template <class H>
    void process(H& h, int n)
    {
        sincePen_ = std::min(sincePen_ + n, 1 << 30);
        const int gap = (int)(kNewTouchSec * h.sampleRate());
        handleAddRemove(h);
        const int bands = std::clamp(h.getInt(i_.bands), 1, kBands);
        if (h.getInt(i_.sel) > bands) h.setPlain(i_.sel, (float)bands);
        for (int b = 0; b < kBands; ++b)   // the band count switches the bands on and off
            if ((h.getN(on(b)) >= 0.5f) != (b < bands)) h.setPlain(on(b), b < bands ? 1.0f : 0.0f);

        handlePen(h, gap, bands);
        syncProxies(h);

        // The display targets. Node positions follow every change at once (the node under the finger must
        // stay instant). The curves are recomputed at most kCurveHz times a second, and only the bands that
        // changed are re-sampled (each band's response is cached on the curve's sample grid; the combined
        // curve is the sum of the cached bands).
        std::array<eq::BandParams, kBands> now{};
        for (int b = 0; b < kBands; ++b) now[(std::size_t)b] = readBand(h, b, b < bands);
        const int sel = std::clamp(h.getInt(i_.sel), 0, bands) - 1;   // -1 = none
        if (dirtyAll_ || now != shown_ || sel != shownSel_)
        {
            curvesPending_ = true;
            shown_ = now;
            shownSel_ = sel;
            for (int b = 0; b < kBands; ++b)
            {
                nodeWant_[(std::size_t)(2 * b)] = (float)eq::GraphMap::hzToX(now[(std::size_t)b].freq);
                nodeWant_[(std::size_t)(2 * b + 1)] = (float)eq::GraphMap::dbToY(nodeDb(now[(std::size_t)b]));
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

    // Display targets (normalised values); the curves hold cols() values.
    const std::array<float, kBands * 2>& nodeTargets() const { return nodeWant_; }
    const std::array<float, 2>& selNodeTargets() const { return selNodeWant_; }
    const std::array<float, kMaxCols>& frontTargets() const { return frontWant_; }
    const std::array<float, kMaxCols>& curveTargets() const { return curveWant_; }
    const std::array<float, kMaxCols>& fillTargets() const { return fillWant_; }

    // "B2 Bell 2.80kHz +6.0dB Q1.20" for the status label (any thread; reads parameters only).
    template <class H>
    std::string info(H& h) const
    {
        const int bands = std::clamp(h.getInt(i_.bands), 1, kBands);
        const int s = std::clamp(h.getInt(i_.sel), 0, bands) - 1;
        if (s < 0)
        {
            char nb[48];
            std::snprintf(nb, sizeof(nb), "%d band%s, none selected", bands, bands == 1 ? "" : "s");
            return nb;
        }
        static const char* names[kTypes] = {"Bell", "Lo Shelf", "Hi Shelf", "Lo Cut", "Hi Cut", "Notch"};
        const int t = std::clamp(h.getInt(field(s, fType)), 0, kTypes - 1);
        const double hz = h.get(field(s, fFreq));
        char f[24], buf[96];
        if (hz < 1000.0) std::snprintf(f, sizeof(f), "%.0fHz", hz);
        else std::snprintf(f, sizeof(f), "%.2fkHz", hz / 1000.0);
        if (t == 3 || t == 4)
            std::snprintf(buf, sizeof(buf), "B%d %s %s %s", s + 1, names[t], f, h.paramText(field(s, fSlope)).c_str());
        else if (t == 5 && eq::notchIsFull(h.get(field(s, fGain))))
            std::snprintf(buf, sizeof(buf), "B%d %s %s full Q%.2f", s + 1, names[t], f, (double)h.get(field(s, fQ)));
        else
            std::snprintf(buf, sizeof(buf), "B%d %s %s %+.1fdB Q%.2f", s + 1, names[t], f,
                          (double)h.get(field(s, fGain)), (double)h.get(field(s, fQ)));
        return buf;
    }

private:
    int on(int b) const { return i_.band0 + b * kPerBand; }
    int field(int b, int f) const { return i_.band0 + b * kPerBand + 1 + f; }

    // The live response (32 columns) at graph position x (0..1), by straight lines between column centres.
    double liveAt(double x) const
    {
        const double pos = std::clamp(x * 32.0 - 0.5, 0.0, 31.0);
        const int i = std::min(30, (int)pos);
        const double t = pos - i;
        return liveDb_[(std::size_t)i] * (1.0 - t) + liveDb_[(std::size_t)i + 1] * t;
    }

    void recomputeCurves(const std::array<eq::BandParams, kBands>& now, int sel)
    {
        dirtyAll_ = false;
        const int n = lines_ ? eq::LineStrip::kQuarterPoints : eq::ShadeStrip::kPoints;
        for (int b = 0; b < kBands; ++b)
        {
            const eq::BandParams& p = now[(std::size_t)b];
            if (cacheValid_[(std::size_t)b] && p == cached_[(std::size_t)b]) continue;
            cached_[(std::size_t)b] = p;
            cacheValid_[(std::size_t)b] = true;
            auto own = [&](double hz) { return eq::analogDb(p, hz); };   // 0 dB when the band is off
            if (lines_) eq::LineStrip::sample(own, samples_[(std::size_t)b].data());
            else eq::ShadeStrip::sample(own, samples_[(std::size_t)b].data());
        }
        if (live_)
        {
            auto live = [&](double hz) { return liveAt(eq::GraphMap::hzToX(hz)); };
            if (lines_) eq::LineStrip::sample(live, sum_.data());
            else eq::ShadeStrip::sample(live, sum_.data());
        }
        else
        {
            for (int k = 0; k < n; ++k)
            {
                double t = 0.0;
                for (int b = 0; b < kBands; ++b) t += samples_[(std::size_t)b][(std::size_t)k];
                sum_[(std::size_t)k] = t;
            }
        }
        encode(sum_.data(), curveWant_.data());
        if (sel >= 0)
        {
            const double* own = samples_[(std::size_t)sel].data();
            encode(own, frontWant_.data());
            for (int c = 0; c < cols(); ++c)
            {
                // The fill's flat top: the line's level at the column centre, or exactly the dot's level.
                const double db = lines_ ? std::clamp(own[8 * c + 4], -eq::GraphMap::kRangeDb, eq::GraphMap::kRangeDb)
                                         : frontWant_[(std::size_t)c] * 2.0 * eq::GraphMap::kRangeDb - eq::GraphMap::kRangeDb;
                const int level = (int)std::lround((db + eq::GraphMap::kRangeDb) / 0.25);
                fillWant_[(std::size_t)c] = (float)(sel * kFillLevels + level) / (float)(kFillFrames - 1);
            }
        }
        else
        {
            frontWant_ = curveWant_;   // none selected: the front curve is the combined one; no fill
            fillWant_.fill((float)((kFillLevels - 1) / 2) / (float)(kFillFrames - 1));
        }
    }

    void encode(const double* samples, float* out) const
    {
        if (lines_) Line::encodeSampled(samples, out);
        else eq::ShadeStrip::encodeSampled(samples, out);
    }

    template <class H>
    eq::BandParams readBand(H& h, int b, bool isOn) const
    {
        eq::BandParams p;
        p.on = isOn;
        p.type = (eq::BandType)std::clamp(h.getInt(field(b, fType)), 0, kTypes - 1);
        p.freq = h.get(field(b, fFreq));
        p.gainDb = eq::typeHasGain(p.type) ? h.get(field(b, fGain)) : 0.0;
        p.q = h.get(field(b, fQ));
        p.slope = std::clamp(h.getInt(field(b, fSlope)), 0, eq::kNumSlopes - 1);
        return p;
    }

    // + adds a band (up to 8) at its default spot, flat, and selects it; - removes the selected band (with none
    // selected, the last band), moving the bands above it down one place, and leaves none selected (at least one
    // band stays).
    template <class H>
    void handleAddRemove(H& h)
    {
        const int bands = std::clamp(h.getInt(i_.bands), 1, kBands);
        if (h.getN(i_.add) >= 0.5f)
        {
            h.setValue(i_.add, 0.0f);
            if (bands < kBands)
            {
                h.beginEdit();
                const int b = bands;   // the new band's index
                h.setPlain(field(b, fType), 0.0f);
                h.setPlain(field(b, fFreq), kDefaultHz[b]);
                h.setPlain(field(b, fGain), 0.0f);
                h.setPlain(field(b, fQ), 1.0f);
                h.setPlain(field(b, fSlope), 1.0f);
                h.setPlain(i_.bands, (float)(bands + 1));
                h.setPlain(i_.sel, (float)(b + 1));
            }
        }
        if (h.getN(i_.remove) >= 0.5f)
        {
            h.setValue(i_.remove, 0.0f);
            int s = std::clamp(h.getInt(i_.sel), 0, bands) - 1;
            if (s < 0) s = bands - 1;   // none selected: the last band
            if (bands > 1)
            {
                h.beginEdit();
                for (int b = s; b < bands - 1; ++b)
                    for (int f = 0; f < kFields; ++f) h.setValue(field(b, f), h.getN(field(b + 1, f)));
                h.setPlain(i_.bands, (float)(bands - 1));
                h.setPlain(i_.sel, 0.0f);
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
        const double x = h.getN(i_.penX), y = h.getN(i_.penY);
        if (newTouch)
        {
            double nx[kBands], ny[kBands];
            bool shown[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const eq::BandParams p = readBand(h, b, true);
                nx[b] = eq::GraphMap::hzToX(p.freq);
                ny[b] = eq::GraphMap::dbToY(nodeDb(p));
                shown[b] = b < bands;
            }
            grabbed_ = eq::hitTest(nx, ny, shown, kBands, x, y, kGraphW, kGraphH, kGrabPx);
            if (grabbed_ >= 0)
            {
                offX_ = nx[grabbed_] - x;
                offY_ = ny[grabbed_] - y;
                h.setPlain(i_.sel, (float)(grabbed_ + 1));
            }
        }
        if (grabbed_ < 0) return;
        h.beginEdit();
        const double gx = std::clamp(x + offX_, 0.0, 1.0), gy = std::clamp(y + offY_, 0.0, 1.0);
        h.setPlain(field(grabbed_, fFreq), (float)eq::GraphMap::xToHz(gx));
        // Up / down: the gain (a notch: its depth, 0 dB at most), or a cut's resonance (Q).
        const auto type = (eq::BandType)std::clamp(h.getInt(field(grabbed_, fType)), 0, kTypes - 1);
        const double db = eq::GraphMap::yToDb(gy);
        if (eq::typeHasSlope(type)) h.setPlain(field(grabbed_, fQ), (float)cutQFromDb(db));
        else if (type == eq::BandType::Notch) h.setPlain(field(grabbed_, fGain), (float)std::min(db, 0.0));
        else h.setPlain(field(grabbed_, fGain), (float)db);
    }

    // The proxies mirror the selected band: a proxy turned by the user (knob, switch, Q-Link) writes
    // the band; a band changed otherwise (drag, a new fit) refreshes the proxy.
    template <class H>
    void syncProxies(H& h)
    {
        const int bands = std::clamp(h.getInt(i_.bands), 1, kBands);
        const int s = std::clamp(h.getInt(i_.sel), 0, bands) - 1;
        if (s < 0) { lastSel_ = -1; return; }   // none selected: the controls have nothing to edit
        if (s != lastSel_)
        {
            lastSel_ = s;
            for (int f = 0; f < kFields; ++f)
            {
                const float v = h.getN(field(s, f));
                h.setValue(i_.proxy[f], v);
                lastProxy_[(std::size_t)f] = v;
            }
            return;
        }
        for (int f = 0; f < kFields; ++f)
        {
            const float pv = h.getN(i_.proxy[f]);
            if (pv != lastProxy_[(std::size_t)f])   // the user turned it
            {
                h.beginEdit();
                h.setValue(field(s, f), pv);
                // A band turned into a notch starts as a full notch (unless it already cuts).
                if (f == fType && h.getInt(field(s, fType)) == (int)eq::BandType::Notch && h.get(field(s, fGain)) > -0.05f)
                    h.setPlain(field(s, fGain), (float)eq::kNotchFullDb);
            }
            const float bv = h.getN(field(s, f));
            if (bv != pv) h.setValue(i_.proxy[f], bv);                          // the band moved
            lastProxy_[(std::size_t)f] = bv;
        }
    }

    const ParaIdx i_;
    const bool lines_;
    std::atomic<uint32_t> penSeq_{0};
    uint32_t lastPenSeq_ = 0;
    int sincePen_ = 1 << 30, dragHoldSamples_ = 11025, grabbed_ = -1, lastSel_ = -1, shownSel_ = -2;
    double offX_ = 0.0, offY_ = 0.0;
    std::array<float, kFields> lastProxy_{};
    std::array<eq::BandParams, kBands> shown_{};
    bool dirtyAll_ = true, infoDirty_ = true, live_ = false;
    std::array<double, 32> liveDb_{};
    std::array<float, kBands * 2> nodeWant_{};
    std::array<float, 2> selNodeWant_{};
    std::array<float, kMaxCols> frontWant_{}, curveWant_{}, fillWant_{};

    // Per-band response caches on the curve's sample grid (the line's 609 quarter-column points, or the
    // dots' 1,024 points), and their sum.
    static constexpr double kCurveHz = 30.0;
    static constexpr int kMaxSamples = eq::ShadeStrip::kPoints > eq::LineStrip::kQuarterPoints ? eq::ShadeStrip::kPoints
                                                                                               : eq::LineStrip::kQuarterPoints;
    std::array<std::array<double, kMaxSamples>, kBands> samples_{};
    std::array<double, kMaxSamples> sum_{};
    std::array<eq::BandParams, kBands> cached_{};
    std::array<bool, kBands> cacheValid_{};
    bool curvesPending_ = true;
    int sinceCurves_ = 1 << 30;
};

} // namespace eqpara
