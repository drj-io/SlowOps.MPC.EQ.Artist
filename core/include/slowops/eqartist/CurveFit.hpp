// Turns a drawn curve into parametric bands. Standard library only.
//
// Input: gains in dB at a few log-spaced frequencies (the drawing's columns). Output: up to
// maxBands bands whose combined analog response follows the drawing.
//
//  1. The columns are interpolated (monotone cubic in log frequency) to kPoints log-spaced
//     points from 20 Hz to 20 kHz, then lightly smoothed. This is the target.
//  2. Steep edges become cuts: if the target falls well below 0 dB at either end at a rate a
//     shelf cannot make, a low or high cut is seeded with the nearest slope.
//  3. Greedy: while the error is too big and bands remain, look at the largest remaining error
//     and try a bell there (Q from the bump's width) plus a low and a high shelf. Each candidate
//     is refined on its own, and the one that leaves the least error is kept. A band that does
//     not cut the RMS error by at least kMinStepDb is not worth having: the fit stops there,
//     rather than spending narrow bands on the corners of a hand-drawn line.
//  4. All bands are refined together (Levenberg-Marquardt on the dB error, with frequency and Q
//     in octaves).
//  5. Bands with less than 0.3 dB of effect are dropped, and the rest are refined once more.
//
// Allocation-free after construction is NOT a goal: this runs on a worker thread, never on the
// audio thread. A typical fit takes a few milliseconds.
#pragma once

#include "Filters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace slowops::eqartist {

struct FitResult
{
    std::array<BandParams, 8> bands{};
    int count = 0;
    double rmsDb = 0.0;    // remaining RMS error over the target, dB
    double maxDb = 0.0;    // largest remaining error, dB
};

class CurveFit
{
public:
    static constexpr int kPoints = 160;
    static constexpr double kFLo = 20.0, kFHi = 20000.0;
    static constexpr double kStopRmsDb = 0.35;
    static constexpr double kMinStepDb = 0.05;  // a new band must cut the RMS error at least this much
    static constexpr double kMinGainDb = 0.3;

    // colHz / colDb: the drawing, frequencies ascending. maxBands: 1..8.
    FitResult fit(const std::vector<double>& colHz, const std::vector<double>& colDb, int maxBands)
    {
        maxBands = std::clamp(maxBands, 1, 8);
        makeTarget(colHz, colDb);
        bands_.clear();

        seedCuts(maxBands);
        globalRefine(30);

        while ((int)bands_.size() < maxBands)
        {
            std::vector<double> res = residual(bands_);
            const double r = rms(res);
            if (r < kStopRmsDb) break;
            const BandParams best = bestCandidate(res);
            bands_.push_back(best);
            globalRefine(25);
            if (rms(residual(bands_)) > r - kMinStepDb) { bands_.pop_back(); break; }   // not worth a band
        }

        globalRefine(60);
        dropWeak();
        globalRefine(40);
        dropWeak();

        FitResult out;
        std::sort(bands_.begin(), bands_.end(), [](const BandParams& a, const BandParams& b) { return a.freq < b.freq; });
        out.count = (int)bands_.size();
        for (int i = 0; i < out.count; ++i) out.bands[(std::size_t)i] = bands_[(std::size_t)i];
        const std::vector<double> res = residual(bands_);
        out.rmsDb = rms(res);
        for (double v : res) out.maxDb = std::max(out.maxDb, std::fabs(v));
        return out;
    }

    // Exposed for tests: the target after interpolation and smoothing.
    const std::vector<double>& target() const { return target_; }
    const std::vector<double>& freqs() const { return f_; }

private:
    // ---- target ---------------------------------------------------------------------------
    void makeTarget(const std::vector<double>& colHz, const std::vector<double>& colDb)
    {
        const int n = (int)colHz.size();
        f_.resize(kPoints);
        target_.resize(kPoints);
        std::vector<double> x((std::size_t)n), y = colDb, m((std::size_t)n, 0.0);
        for (int i = 0; i < n; ++i) x[(std::size_t)i] = std::log2(colHz[(std::size_t)i]);
        // Fritsch-Carlson monotone cubic slopes.
        std::vector<double> d((std::size_t)std::max(1, n - 1), 0.0);
        for (int i = 0; i + 1 < n; ++i) d[(std::size_t)i] = (y[(std::size_t)i + 1] - y[(std::size_t)i]) / (x[(std::size_t)i + 1] - x[(std::size_t)i]);
        for (int i = 0; i < n; ++i)
        {
            if (i == 0) m[0] = n > 1 ? d[0] : 0.0;
            else if (i == n - 1) m[(std::size_t)i] = d[(std::size_t)i - 1];
            else if (d[(std::size_t)i - 1] * d[(std::size_t)i] <= 0) m[(std::size_t)i] = 0.0;
            else
            {
                const double a = d[(std::size_t)i - 1], b = d[(std::size_t)i];
                m[(std::size_t)i] = 2.0 * a * b / (a + b);   // harmonic mean: monotone and smooth
            }
        }
        for (int k = 0; k < kPoints; ++k)
        {
            const double lf = std::log2(kFLo) + (std::log2(kFHi) - std::log2(kFLo)) * k / (kPoints - 1);
            f_[(std::size_t)k] = std::exp2(lf);
            double v;
            if (n == 0) v = 0.0;
            else if (lf <= x[0]) v = y[0];
            else if (lf >= x[(std::size_t)n - 1]) v = y[(std::size_t)n - 1];
            else
            {
                int i = 0;
                while (i + 1 < n && x[(std::size_t)i + 1] < lf) ++i;
                const double h = x[(std::size_t)i + 1] - x[(std::size_t)i], t = (lf - x[(std::size_t)i]) / h;
                const double h00 = (1 + 2 * t) * (1 - t) * (1 - t), h10 = t * (1 - t) * (1 - t);
                const double h01 = t * t * (3 - 2 * t), h11 = t * t * (t - 1);
                v = h00 * y[(std::size_t)i] + h10 * h * m[(std::size_t)i] + h01 * y[(std::size_t)i + 1] + h11 * h * m[(std::size_t)i + 1];
            }
            target_[(std::size_t)k] = v;
        }
        // Light smoothing (one pass of 1-2-1), ends kept.
        std::vector<double> s = target_;
        for (int k = 1; k + 1 < kPoints; ++k)
            s[(std::size_t)k] = 0.25 * target_[(std::size_t)k - 1] + 0.5 * target_[(std::size_t)k] + 0.25 * target_[(std::size_t)k + 1];
        target_ = s;
    }

    // ---- model ----------------------------------------------------------------------------
    std::vector<double> residual(const std::vector<BandParams>& bands) const
    {
        std::vector<double> r((std::size_t)kPoints);
        for (int k = 0; k < kPoints; ++k)
        {
            double m = 0.0;
            for (const BandParams& b : bands) m += analogDb(b, f_[(std::size_t)k]);
            r[(std::size_t)k] = m - target_[(std::size_t)k];
        }
        return r;
    }
    static double rms(const std::vector<double>& r)
    {
        double s = 0.0;
        for (double v : r) s += v * v;
        return std::sqrt(s / std::max<std::size_t>(1, r.size()));
    }

    // Free parameters of a band, in fitting units: log2 freq, gain dB, log2 Q.
    static int numFree(const BandParams& b) { return typeHasGain(b.type) ? 3 : 2; }
    static void getFree(const BandParams& b, double* v)
    {
        v[0] = std::log2(b.freq);
        if (typeHasGain(b.type)) { v[1] = b.gainDb; v[2] = std::log2(b.q); }
        else v[1] = std::log2(b.q);
    }
    static void setFree(BandParams& b, const double* v)
    {
        b.freq = std::exp2(std::clamp(v[0], std::log2(kFLo), std::log2(kFHi)));
        double lq;
        if (typeHasGain(b.type)) { b.gainDb = std::clamp(v[1], -24.0, 24.0); lq = v[2]; }
        else lq = v[1];
        // Shelves and cuts stay gentle; bells may be narrow.
        const double qLo = b.type == BandType::Bell ? 0.2 : 0.4;
        const double qHi = b.type == BandType::Bell ? 8.0 : (typeHasSlope(b.type) ? 1.4 : 1.2);
        b.q = std::exp2(std::clamp(lq, std::log2(qLo), std::log2(qHi)));
    }

    // Levenberg-Marquardt over the free parameters of bands[first .. first+count).
    void refine(std::vector<BandParams>& bands, int first, int count, int iterations) const
    {
        std::vector<int> offs;
        int np = 0;
        for (int i = first; i < first + count; ++i) { offs.push_back(np); np += numFree(bands[(std::size_t)i]); }
        if (np == 0) return;
        std::vector<double> theta((std::size_t)np);
        for (int i = 0; i < count; ++i) getFree(bands[(std::size_t)(first + i)], &theta[(std::size_t)offs[(std::size_t)i]]);

        auto apply = [&](std::vector<BandParams>& bs, const std::vector<double>& th) {
            for (int i = 0; i < count; ++i) setFree(bs[(std::size_t)(first + i)], &th[(std::size_t)offs[(std::size_t)i]]);
        };
        auto cost = [&](const std::vector<double>& r) { double s = 0; for (double v : r) s += v * v; return s; };

        std::vector<BandParams> trial = bands;
        std::vector<double> r = residual(bands);
        double c = cost(r), lambda = 1e-2;
        std::vector<double> J((std::size_t)kPoints * np), JtJ((std::size_t)np * np), Jtr((std::size_t)np), step((std::size_t)np);
        for (int it = 0; it < iterations; ++it)
        {
            // Numeric Jacobian, one band parameter at a time.
            for (int p = 0; p < np; ++p)
            {
                std::vector<double> th = theta;
                const double h = 1e-3;
                th[(std::size_t)p] += h;
                trial = bands;
                apply(trial, th);
                const std::vector<double> r2 = residual(trial);
                for (int k = 0; k < kPoints; ++k) J[(std::size_t)k * np + p] = (r2[(std::size_t)k] - r[(std::size_t)k]) / h;
            }
            for (int a = 0; a < np; ++a)
            {
                double g = 0;
                for (int k = 0; k < kPoints; ++k) g += J[(std::size_t)k * np + a] * r[(std::size_t)k];
                Jtr[(std::size_t)a] = g;
                for (int b = 0; b <= a; ++b)
                {
                    double s = 0;
                    for (int k = 0; k < kPoints; ++k) s += J[(std::size_t)k * np + a] * J[(std::size_t)k * np + b];
                    JtJ[(std::size_t)a * np + b] = JtJ[(std::size_t)b * np + a] = s;
                }
            }
            bool improved = false;
            for (int tries = 0; tries < 8 && !improved; ++tries)
            {
                if (!solve(JtJ, Jtr, lambda, np, step)) { lambda *= 10; continue; }
                std::vector<double> th = theta;
                for (int p = 0; p < np; ++p) th[(std::size_t)p] -= step[(std::size_t)p];
                trial = bands;
                apply(trial, th);
                const std::vector<double> r2 = residual(trial);
                const double c2 = cost(r2);
                if (c2 < c)
                {
                    const double gain = c - c2;
                    bands = trial;
                    for (int i = 0; i < count; ++i) getFree(bands[(std::size_t)(first + i)], &theta[(std::size_t)offs[(std::size_t)i]]);
                    r = r2;
                    c = c2;
                    lambda = std::max(1e-7, lambda / 3);
                    improved = true;
                    if (gain < 1e-6 * (1 + c)) it = iterations;   // converged
                }
                else lambda *= 4;
            }
            if (!improved) break;
        }
    }

    // Solve (JtJ + lambda diag(JtJ)) x = Jtr by Gaussian elimination with partial pivoting.
    static bool solve(const std::vector<double>& JtJ, const std::vector<double>& Jtr, double lambda, int n, std::vector<double>& x)
    {
        std::vector<double> A((std::size_t)n * (n + 1));
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < n; ++j) A[(std::size_t)i * (n + 1) + j] = JtJ[(std::size_t)i * n + j];
            A[(std::size_t)i * (n + 1) + i] += lambda * (JtJ[(std::size_t)i * n + i] + 1e-9);
            A[(std::size_t)i * (n + 1) + n] = Jtr[(std::size_t)i];
        }
        for (int c = 0; c < n; ++c)
        {
            int piv = c;
            for (int r = c + 1; r < n; ++r)
                if (std::fabs(A[(std::size_t)r * (n + 1) + c]) > std::fabs(A[(std::size_t)piv * (n + 1) + c])) piv = r;
            if (std::fabs(A[(std::size_t)piv * (n + 1) + c]) < 1e-14) return false;
            if (piv != c) for (int j = 0; j <= n; ++j) std::swap(A[(std::size_t)c * (n + 1) + j], A[(std::size_t)piv * (n + 1) + j]);
            for (int r = c + 1; r < n; ++r)
            {
                const double f = A[(std::size_t)r * (n + 1) + c] / A[(std::size_t)c * (n + 1) + c];
                for (int j = c; j <= n; ++j) A[(std::size_t)r * (n + 1) + j] -= f * A[(std::size_t)c * (n + 1) + j];
            }
        }
        for (int r = n - 1; r >= 0; --r)
        {
            double s = A[(std::size_t)r * (n + 1) + n];
            for (int j = r + 1; j < n; ++j) s -= A[(std::size_t)r * (n + 1) + j] * x[(std::size_t)j];
            x[(std::size_t)r] = s / A[(std::size_t)r * (n + 1) + r];
            if (!std::isfinite(x[(std::size_t)r])) return false;
        }
        return true;
    }

    void globalRefine(int iterations)
    {
        if (!bands_.empty()) refine(bands_, 0, (int)bands_.size(), iterations);
    }

    // ---- seeding --------------------------------------------------------------------------
    // A cut at one end: the target falls below -9 dB at the edge, and over the last octave it
    // falls faster than 9 dB/oct (steeper than a shelf can go).
    void seedCuts(int maxBands)
    {
        const double perOct = (kPoints - 1) / std::log2(kFHi / kFLo);
        const int oct = (int)std::lround(perOct);
        for (int end = 0; end < 2 && (int)bands_.size() < maxBands; ++end)
        {
            const bool low = end == 0;
            const int e = low ? 0 : kPoints - 1, in = low ? oct : kPoints - 1 - oct;
            const double edge = target_[(std::size_t)e];
            const double slope = target_[(std::size_t)in] - edge;   // dB per octave, towards the edge
            if (edge > -9.0 || slope < 9.0) continue;
            // Corner: where the target, walking in from the edge, first comes back within 3 dB
            // of the level just inside the roll-off.
            int k = in;
            const double plateau = target_[(std::size_t)in];
            while (low ? (k > 0 && target_[(std::size_t)k] > plateau - 3.0) : (k < kPoints - 1 && target_[(std::size_t)k] > plateau - 3.0))
                k += low ? -1 : 1;
            BandParams b;
            b.on = true;
            b.type = low ? BandType::LowCut : BandType::HighCut;
            b.freq = f_[(std::size_t)k];
            b.q = M_SQRT1_2;
            int best = 0;
            for (int s = 0; s < kNumSlopes; ++s)
                if (std::fabs(kSlopesDbPerOct[(std::size_t)s] - slope) < std::fabs(kSlopesDbPerOct[(std::size_t)best] - slope)) best = s;
            b.slope = best;
            bands_.push_back(b);
            refine(bands_, (int)bands_.size() - 1, 1, 30);
        }
    }

    BandParams bestCandidate(const std::vector<double>& res) const
    {
        // The largest remaining error (the target minus the model).
        int peak = 0;
        for (int k = 1; k < kPoints; ++k) if (std::fabs(res[(std::size_t)k]) > std::fabs(res[(std::size_t)peak])) peak = k;
        const double want = -res[(std::size_t)peak];

        std::vector<BandParams> cands;
        {
            // Bell: Q from the width at half the error.
            int lo = peak, hi = peak;
            while (lo > 0 && -res[(std::size_t)lo] * want > 0.25 * want * want) --lo;
            while (hi < kPoints - 1 && -res[(std::size_t)hi] * want > 0.25 * want * want) ++hi;
            const double bwOct = std::max(0.2, std::log2(f_[(std::size_t)hi] / f_[(std::size_t)lo]));
            const double r = std::exp2(bwOct);
            BandParams b;
            b.on = true; b.type = BandType::Bell; b.freq = f_[(std::size_t)peak]; b.gainDb = want;
            b.q = std::clamp(std::sqrt(r) / (r - 1.0), 0.2, 8.0);
            cands.push_back(b);
        }
        for (int end = 0; end < 2; ++end)
        {
            const bool low = end == 0;
            // The shelf's level: the mean error over the outer octave at that end.
            const int oct = (int)std::lround((kPoints - 1) / std::log2(kFHi / kFLo));
            double s = 0;
            for (int k = 0; k < oct; ++k) s += -res[(std::size_t)(low ? k : kPoints - 1 - k)];
            const double level = s / oct;
            if (std::fabs(level) < 0.5) continue;
            // Corner: where the error first falls to half the level, walking in from the end.
            int k = low ? 0 : kPoints - 1;
            while (low ? k < kPoints - 1 : k > 0)
            {
                if (-res[(std::size_t)k] * level < 0.25 * level * level) break;
                k += low ? 1 : -1;
            }
            BandParams b;
            b.on = true; b.type = low ? BandType::LowShelf : BandType::HighShelf;
            b.freq = f_[(std::size_t)k]; b.gainDb = level; b.q = M_SQRT1_2;
            cands.push_back(b);
        }

        BandParams best = cands[0];
        double bestErr = 1e300;
        for (BandParams c : cands)
        {
            std::vector<BandParams> trial = bands_;
            trial.push_back(c);
            refine(trial, (int)trial.size() - 1, 1, 25);
            const double e = rms(residual(trial));
            if (e < bestErr) { bestErr = e; best = trial.back(); }
        }
        return best;
    }

    void dropWeak()
    {
        // A band's effect: its largest |dB| over the target range.
        bands_.erase(std::remove_if(bands_.begin(), bands_.end(), [&](const BandParams& b) {
            double m = 0;
            for (double f : f_) m = std::max(m, std::fabs(analogDb(b, f)));
            return m < kMinGainDb;
        }), bands_.end());
    }

    std::vector<double> f_, target_;
    std::vector<BandParams> bands_;
};

} // namespace slowops::eqartist
