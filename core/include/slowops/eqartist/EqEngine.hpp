// The 8-band stereo EQ engine. Standard library only, no allocation after prepare().
//
// setBand() / setOutputDb() / setBypass() give targets; process() gets there without clicks:
//   * Frequency, gain and Q glide (about 20 ms) and the filters are redesigned every 32 samples
//     while they move.
//   * A change that cannot glide (a band switched on or off, a new type or slope), or a whole
//     new set of bands (forceCrossfade(), used by Apply and Undo), is handled by building the
//     new settings in a second filter bank and crossfading to it over 30 ms.
//   * Bypass and Output are smoothed too.
//
// Beside the bands runs the live EQ (LiveEq: 32 bells that follow the drawing at once). setLive()
// crossfades (30 ms) between the two: live while you draw, the fitted bands once the fit has
// caught up. The live EQ costs nothing while it is off.
#pragma once

#include "Filters.hpp"
#include "LiveEq.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace slowops::eqartist {

class EqEngine
{
public:
    static constexpr int kNumBands = 8;
    static constexpr int kChunk = 32;

    // colHz: the drawing's column centres (LiveEq::kBands of them), for the live EQ.
    void prepare(double sampleRate, const double* colHz)
    {
        sampleRate_ = sampleRate;
        live_.prepare(sampleRate, colHz);
        liveMix_ = liveOn_ ? 1.0 : 0.0;
        glide_ = 1.0 - std::exp(-(double)kChunk / (0.02 * sampleRate));
        xfadeLen_ = std::max(1, (int)(0.03 * sampleRate));
        gainStep_ = 1.0 / (0.02 * sampleRate);
        liveStep_ = 1.0 / (0.03 * sampleRate);
        for (auto& b : banks_) { b.current = target_; b.redesign(sampleRate_); b.reset(); }
        active_ = 0;
        xfadePos_ = -1;
        outGain_ = outTarget_;
        wet_ = bypass_ ? 0.0 : 1.0;
    }

    void setBand(int i, const BandParams& p) { target_[(std::size_t)i] = p; }
    const BandParams& band(int i) const { return target_[(std::size_t)i]; }
    void setOutputDb(double db) { outTarget_ = std::pow(10.0, db / 20.0); }
    void setBypass(bool b) { bypass_ = b; }
    void forceCrossfade() { forceXfade_ = true; }
    void reset() { for (auto& b : banks_) b.reset(); live_.reset(); }

    // The live EQ: setLiveDrawing() solves its gains from the drawing (dB at the column centres);
    // setLive(true) crossfades the sound to it, setLive(false) back to the bands.
    void setLiveDrawing(const double* drawDb) { live_.setDrawing(drawDb); }
    void setLive(bool on)
    {
        if (on && !liveOn_ && liveMix_ == 0.0) live_.reset();   // starting from silence: no stale tail
        liveOn_ = on;
    }
    bool live() const { return liveOn_; }
    double liveResponseDb(int column) const { return live_.responseDb(column); }

    // In place is fine (l/r may alias the outputs).
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int start = 0; start < n; start += kChunk)
        {
            const int len = std::min(kChunk, n - start);
            control();
            const bool runLive = liveOn_ || liveMix_ > 0.0;
            if (runLive) { live_.control(); live_.flushTiny(); }
            for (int i = start; i < start + len; ++i)
            {
                const double dl = inL[i], dr = inR[i];
                double l = dl, r = dr;
                banks_[(std::size_t)active_].run(l, r);
                if (xfadePos_ >= 0)
                {
                    double l2 = dl, r2 = dr;
                    banks_[(std::size_t)(1 - active_)].run(l2, r2);
                    const double t = (double)xfadePos_ / xfadeLen_;   // 0 = old (active), 1 = new
                    l = l * (1.0 - t) + l2 * t;
                    r = r * (1.0 - t) + r2 * t;
                    if (++xfadePos_ >= xfadeLen_) { active_ = 1 - active_; xfadePos_ = -1; }
                }
                // Live EQ crossfade.
                if (runLive)
                {
                    liveMix_ += std::clamp((liveOn_ ? 1.0 : 0.0) - liveMix_, -liveStep_, liveStep_);
                    double ll = dl, lr = dr;
                    live_.run(ll, lr);
                    l += (ll - l) * liveMix_;
                    r += (lr - r) * liveMix_;
                }
                // Bypass and Output ramps.
                const double wetTarget = bypass_ ? 0.0 : 1.0;
                wet_ += std::clamp(wetTarget - wet_, -gainStep_, gainStep_);
                outGain_ += std::clamp(outTarget_ - outGain_, -gainStep_ * 4.0, gainStep_ * 4.0);
                outL[i] = (float)((dl + (l - dl) * wet_) * outGain_);
                outR[i] = (float)((dr + (r - dr) * wet_) * outGain_);
            }
        }
    }

    // Total digital response of the target settings, in dB, at n frequencies (for the display).
    // Designs each band once. No allocation.
    void responseDb(const double* hz, double* outDb, int n) const
    {
        for (int k = 0; k < n; ++k) outDb[k] = 0.0;
        for (int i = 0; i < kNumBands; ++i)
        {
            if (!target_[(std::size_t)i].on) continue;
            const DigitalBand d = design(target_[(std::size_t)i], sampleRate_);
            for (int k = 0; k < n; ++k) outDb[k] += digitalDb(d, hz[k], sampleRate_);
        }
    }

private:
    struct Bank
    {
        std::array<BandParams, kNumBands> current{};
        std::array<DigitalBand, kNumBands> coeffs{};
        std::array<std::array<BiquadState, kMaxSections>, kNumBands> stL{}, stR{};

        void redesign(double sr, int only = -1)
        {
            for (int b = 0; b < kNumBands; ++b)
                if (only < 0 || only == b) coeffs[(std::size_t)b] = design(current[(std::size_t)b], sr);
        }
        void reset()
        {
            for (auto& band : stL) for (auto& s : band) s.reset();
            for (auto& band : stR) for (auto& s : band) s.reset();
        }
        void run(double& l, double& r)
        {
            for (int b = 0; b < kNumBands; ++b)
            {
                const DigitalBand& d = coeffs[(std::size_t)b];
                for (int k = 0; k < d.count; ++k)
                {
                    l = stL[(std::size_t)b][(std::size_t)k].process(d.sec[(std::size_t)k], l);
                    r = stR[(std::size_t)b][(std::size_t)k].process(d.sec[(std::size_t)k], r);
                }
            }
        }
        // Clear tiny filter states (denormal insurance where flush-to-zero is not on). Once per
        // chunk, not per sample.
        void flushTiny()
        {
            for (auto* st : {&stL, &stR})
                for (auto& band : *st) for (auto& s : band)
                {
                    if (std::fabs(s.s1) < 1e-25) s.s1 = 0;
                    if (std::fabs(s.s2) < 1e-25) s.s2 = 0;
                }
        }
    };

    static bool structural(const BandParams& a, const BandParams& b)
    {
        return a.on != b.on || (a.on && (a.type != b.type || (typeHasSlope(a.type) && a.slope != b.slope)));
    }

    // Once per chunk: start a crossfade if needed, otherwise glide the active bank.
    void control()
    {
        for (auto& b : banks_) b.flushTiny();
        Bank& cur = banks_[(std::size_t)active_];
        if (xfadePos_ < 0)
        {
            bool needXfade = forceXfade_;
            for (int b = 0; b < kNumBands && !needXfade; ++b)
                needXfade = structural(cur.current[(std::size_t)b], target_[(std::size_t)b]);
            if (needXfade)
            {
                forceXfade_ = false;
                Bank& next = banks_[(std::size_t)(1 - active_)];
                next.current = target_;
                next.redesign(sampleRate_);
                next.reset();
                xfadePos_ = 0;
                return;
            }
        }
        // Glide freq (in octaves), gain and Q (in octaves) towards the target.
        for (int b = 0; b < kNumBands; ++b)
        {
            BandParams& c = cur.current[(std::size_t)b];
            const BandParams& t = target_[(std::size_t)b];
            if (c == t || structural(c, t)) continue;
            const double lf = std::log2(c.freq), lq = std::log2(c.q);
            double nf = lf + (std::log2(t.freq) - lf) * glide_;
            double ng = c.gainDb + (t.gainDb - c.gainDb) * glide_;
            double nq = lq + (std::log2(t.q) - lq) * glide_;
            if (std::fabs(nf - std::log2(t.freq)) < 1e-4) nf = std::log2(t.freq);
            if (std::fabs(ng - t.gainDb) < 1e-3) ng = t.gainDb;
            if (std::fabs(nq - std::log2(t.q)) < 1e-4) nq = std::log2(t.q);
            c.freq = nf == std::log2(t.freq) ? t.freq : std::exp2(nf);
            c.gainDb = ng;
            c.q = nq == std::log2(t.q) ? t.q : std::exp2(nq);
            c.slope = t.slope;
            cur.redesign(sampleRate_, b);
        }
    }

    double sampleRate_ = 44100.0, glide_ = 0.1, gainStep_ = 1e-3;
    std::array<BandParams, kNumBands> target_{};
    std::array<Bank, 2> banks_{};
    int active_ = 0, xfadePos_ = -1, xfadeLen_ = 1323;
    bool forceXfade_ = false, bypass_ = false;
    double outTarget_ = 1.0, outGain_ = 1.0, wet_ = 1.0;
    LiveEq live_;
    bool liveOn_ = false;
    double liveMix_ = 0.0, liveStep_ = 1e-3;
};

} // namespace slowops::eqartist
