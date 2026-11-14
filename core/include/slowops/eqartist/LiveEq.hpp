// The live EQ: what you hear while you draw. Standard library only.
//
// Fitting a drawing to 8 parametric bands takes a while (tens of milliseconds on the MPC), too
// long to follow a moving finger. So while you draw, the sound comes from this simpler EQ
// instead: one bell per drawing column (32 bells, fixed frequencies and Q), whose gains follow
// the drawing at once. When the stroke ends and the fit has caught up, EqEngine crossfades to the
// fitted bands.
//
// Neighbouring bells overlap, so a column's gain is not simply its drawn level. The gains come
// from a 32 x 32 matrix worked out once in prepare(): the least-squares solution (with a little
// regularisation, so neighbours never fight with huge opposite gains) of "the summed bells match
// the drawing, interpolated between the columns". That solution assumes the bells add linearly in
// dB, which is only nearly true, so setDrawing() then corrects the gains twice against their
// actual summed response at the column centres. All of that is a few thousand multiply-adds:
// cheap enough to run on the audio thread whenever the drawing changes.
//
// prepare() allocates; everything else is allocation-free and safe on the audio thread.
#pragma once

#include "Filters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace slowops::eqartist {

class LiveEq
{
public:
    static constexpr int kBands = 32;
    static constexpr double kQ = 2.0;          // ~0.7 octave: smooth sums, no ripple between columns
    static constexpr int kChunk = 32;

    // colHz: the drawing's column centres (kBands of them, ascending).
    void prepare(double sampleRate, const double* colHz)
    {
        sampleRate_ = sampleRate;
        for (int c = 0; c < kBands; ++c) hz_[(std::size_t)c] = colHz[c];
        glide_ = 1.0 - std::exp(-(double)kChunk / (0.015 * sampleRate));   // ~15 ms gain glide
        buildSolver();
        for (int b = 0; b < kBands; ++b)
        {
            cur_[(std::size_t)b] = target_[(std::size_t)b] = 0.0;
            coeffs_[(std::size_t)b] = Coeffs{};
        }
        reset();
    }

    void reset()
    {
        for (auto& s : stL_) s.reset();
        for (auto& s : stR_) s.reset();
    }

    // The drawing in dB at the column centres. Solves the bell gains (on the calling thread).
    void setDrawing(const double* drawDb)
    {
        std::array<double, kBands> g{}, r{};
        apply(drawDb, g.data());
        for (int iter = 0; iter < 2; ++iter)
        {
            responseAtColumns(g.data(), r.data());
            std::array<double, kBands> err{}, corr{};
            for (int c = 0; c < kBands; ++c) err[(std::size_t)c] = drawDb[c] - r[(std::size_t)c];
            apply(err.data(), corr.data());
            for (int b = 0; b < kBands; ++b) g[(std::size_t)b] = std::clamp(g[(std::size_t)b] + corr[(std::size_t)b], -30.0, 30.0);
        }
        target_ = g;
        responseAtColumns(g.data(), resp_.data());
    }

    // What the live EQ sounds like at the column centres, in dB (for the display).
    double responseDb(int column) const { return resp_[(std::size_t)column]; }
    double gainDb(int band) const { return target_[(std::size_t)band]; }

    // Once per kChunk samples: glide the gains and redesign the bells that moved.
    void control()
    {
        for (int b = 0; b < kBands; ++b)
        {
            double& c = cur_[(std::size_t)b];
            const double t = target_[(std::size_t)b];
            if (c == t) continue;
            c += (t - c) * glide_;
            if (std::fabs(t - c) < 0.01) c = t;
            coeffs_[(std::size_t)b] = c == 0.0 ? Coeffs{} : design(bell(b, c), sampleRate_).sec[0];
        }
    }

    void run(double& l, double& r)
    {
        for (int b = 0; b < kBands; ++b)
        {
            if (cur_[(std::size_t)b] == 0.0) continue;   // a flat bell is a wire
            const Coeffs& k = coeffs_[(std::size_t)b];
            l = stL_[(std::size_t)b].process(k, l);
            r = stR_[(std::size_t)b].process(k, r);
        }
    }

    void flushTiny()
    {
        for (auto* st : {&stL_, &stR_})
            for (auto& s : *st)
            {
                if (std::fabs(s.s1) < 1e-25) s.s1 = 0;
                if (std::fabs(s.s2) < 1e-25) s.s2 = 0;
            }
    }

private:
    BandParams bell(int b, double gainDb) const
    {
        BandParams p;
        p.on = true;
        p.type = BandType::Bell;
        p.freq = hz_[(std::size_t)b];
        p.gainDb = gainDb;
        p.q = kQ;
        return p;
    }

    void responseAtColumns(const double* g, double* out) const
    {
        for (int c = 0; c < kBands; ++c) out[c] = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            if (g[b] == 0.0) continue;
            const BandParams p = bell(b, g[b]);
            // Only nearby columns are affected noticeably; skip the far ones (> 3 octaves away).
            for (int c = std::max(0, b - 10); c < std::min(kBands, b + 11); ++c) out[c] += analogDb(p, hz_[(std::size_t)c]);
        }
    }

    // gains = G * columns
    void apply(const double* in, double* out) const
    {
        for (int b = 0; b < kBands; ++b)
        {
            double s = 0.0;
            for (int c = 0; c < kBands; ++c) s += G_[(std::size_t)(b * kBands + c)] * in[c];
            out[b] = s;
        }
    }

    // G = (M^T M + lambda I)^-1 M^T T, where M (points x bands) is each bell's dB response per dB
    // of gain at a fine log-spaced grid, and T (points x columns) interpolates the columns onto
    // that grid (straight lines in log frequency, flat beyond the end columns).
    void buildSolver()
    {
        const int P = 4 * kBands;
        std::vector<double> M((std::size_t)P * kBands), T((std::size_t)P * kBands, 0.0);
        const double lo = std::log2(hz_[0]), hi = std::log2(hz_[kBands - 1]);
        for (int i = 0; i < P; ++i)
        {
            const double lf = lo + (hi - lo) * i / (P - 1), f = std::exp2(lf);
            for (int b = 0; b < kBands; ++b) M[(std::size_t)(i * kBands + b)] = analogDb(bell(b, 6.0), f) / 6.0;
            int c = 0;
            while (c < kBands - 2 && std::log2(hz_[(std::size_t)c + 1]) < lf) ++c;
            const double l0 = std::log2(hz_[(std::size_t)c]), l1 = std::log2(hz_[(std::size_t)c + 1]);
            const double t = std::clamp((lf - l0) / (l1 - l0), 0.0, 1.0);
            T[(std::size_t)(i * kBands + c)] += 1.0 - t;
            T[(std::size_t)(i * kBands + c + 1)] += t;
        }
        // A = M^T M + lambda I, B = M^T T; then G = A^-1 B (Gauss-Jordan).
        std::vector<double> A((std::size_t)kBands * kBands, 0.0), B((std::size_t)kBands * kBands, 0.0);
        for (int a = 0; a < kBands; ++a)
            for (int b = 0; b < kBands; ++b)
            {
                double sa = 0, sb = 0;
                for (int i = 0; i < P; ++i)
                {
                    sa += M[(std::size_t)(i * kBands + a)] * M[(std::size_t)(i * kBands + b)];
                    sb += M[(std::size_t)(i * kBands + a)] * T[(std::size_t)(i * kBands + b)];
                }
                A[(std::size_t)(a * kBands + b)] = sa;
                B[(std::size_t)(a * kBands + b)] = sb;
            }
        double trace = 0;
        for (int a = 0; a < kBands; ++a) trace += A[(std::size_t)(a * kBands + a)];
        const double lambda = 0.02 * trace / kBands;
        for (int a = 0; a < kBands; ++a) A[(std::size_t)(a * kBands + a)] += lambda;
        for (int c = 0; c < kBands; ++c)
        {
            int piv = c;
            for (int r = c + 1; r < kBands; ++r)
                if (std::fabs(A[(std::size_t)(r * kBands + c)]) > std::fabs(A[(std::size_t)(piv * kBands + c)])) piv = r;
            for (int j = 0; j < kBands; ++j)
            {
                std::swap(A[(std::size_t)(c * kBands + j)], A[(std::size_t)(piv * kBands + j)]);
                std::swap(B[(std::size_t)(c * kBands + j)], B[(std::size_t)(piv * kBands + j)]);
            }
            const double d = A[(std::size_t)(c * kBands + c)];
            for (int j = 0; j < kBands; ++j) { A[(std::size_t)(c * kBands + j)] /= d; B[(std::size_t)(c * kBands + j)] /= d; }
            for (int r = 0; r < kBands; ++r)
            {
                if (r == c) continue;
                const double f = A[(std::size_t)(r * kBands + c)];
                if (f == 0.0) continue;
                for (int j = 0; j < kBands; ++j)
                {
                    A[(std::size_t)(r * kBands + j)] -= f * A[(std::size_t)(c * kBands + j)];
                    B[(std::size_t)(r * kBands + j)] -= f * B[(std::size_t)(c * kBands + j)];
                }
            }
        }
        for (int i = 0; i < kBands * kBands; ++i) G_[(std::size_t)i] = B[(std::size_t)i];
    }

    double sampleRate_ = 44100.0, glide_ = 0.05;
    std::array<double, kBands> hz_{}, cur_{}, target_{}, resp_{};
    std::array<Coeffs, kBands> coeffs_{};
    std::array<BiquadState, kBands> stL_{}, stR_{};
    std::array<double, kBands * kBands> G_{};
};

} // namespace slowops::eqartist
