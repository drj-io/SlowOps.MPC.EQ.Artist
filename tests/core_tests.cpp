// core_tests.cpp - unit tests for core/include/slowops/eqartist (no SDK dependency).
// Build: c++ -std=c++17 -O2 -I core/include tests/core_tests.cpp -o build/core_tests

#include "slowops/eqartist/Fft.hpp"
#include "slowops/eqartist/Spectrum.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>

namespace eq = slowops::eqartist;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void testFft()
{
    std::printf("== Fft\n");
    const std::size_t n = 64;
    eq::Fft fft;
    fft.prepare(n);
    std::vector<std::complex<float>> x(n), ref(n);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (auto& v : x) v = {u(rng), u(rng)};
    for (std::size_t k = 0; k < n; ++k)
    {
        std::complex<double> s = 0;
        for (std::size_t t = 0; t < n; ++t)
            s += std::complex<double>(x[t]) * std::polar(1.0, -2.0 * M_PI * (double)(k * t) / (double)n);
        ref[k] = std::complex<float>(s);
    }
    fft.forward(x.data());
    double err = 0;
    for (std::size_t k = 0; k < n; ++k) err = std::fmax(err, std::abs(x[k] - ref[k]));
    CHECK(err < 1e-4, "FFT differs from the direct DFT by %g", err);
}

// Run the analyser on a signal long enough to fill its window, return the column levels.
template <class Gen>
static std::vector<float> analyse(double sr, Gen gen, int cols = 32)
{
    eq::Spectrum s;
    s.prepare(sr, cols);
    for (int i = 0; i < (int)eq::Spectrum::kFftSize * 2; ++i) s.push(gen(i));
    s.analyse(24.0f, 0.033f);
    std::vector<float> out((std::size_t)cols);
    for (int c = 0; c < cols; ++c) out[(std::size_t)c] = s.columnDb(c);
    return out;
}

static void testSpectrumSine()
{
    std::printf("== Spectrum: sines land in the right column at about 0 dB\n");
    for (double sr : {44100.0, 48000.0})
    {
        eq::Spectrum ref;
        ref.prepare(sr, 32);
        for (double hz : {60.0, 250.0, 1000.0, 4000.0, 12000.0})
        {
            const auto db = analyse(sr, [&](int i) { return (float)std::sin(2.0 * M_PI * hz * i / sr); });
            int want = 0;
            while (want < 31 && ref.columnHiHz(want) <= hz) ++want;
            int best = 0;
            for (int c = 1; c < 32; ++c) if (db[(std::size_t)c] > db[(std::size_t)best]) best = c;
            CHECK(std::abs(best - want) <= 1, "%.0f Hz @ %.0f: loudest column %d, expected %d", hz, sr, best, want);
            // Columns narrower than an FFT bin (below about 100 Hz) read one bin's level, which
            // is up to about 3.2 dB low (Hann scalloping plus the main lobe's spread).
            const float floorDb = ref.columnHiHz(want) - ref.columnLoHz(want) < sr / 2048.0 ? -3.5f : -1.0f;
            CHECK(db[(std::size_t)best] > floorDb && db[(std::size_t)best] < 0.5f,
                  "%.0f Hz @ %.0f: full-scale sine reads %.2f dB", hz, sr, db[(std::size_t)best]);
            // Far from the sine, the columns are well below it.
            if (want > 6) CHECK(db[(std::size_t)(want - 6)] < db[(std::size_t)best] - 40.0f,
                                "%.0f Hz: leakage 6 columns below reads %.1f dB", hz, db[(std::size_t)(want - 6)]);
        }
    }
}

static void testSpectrumPink()
{
    std::printf("== Spectrum: pink noise reads flat\n");
    // Pink noise (Paul Kellet's filter), averaged over several analyses to tame the variance.
    const double sr = 44100.0;
    eq::Spectrum s;
    s.prepare(sr, 32);
    std::mt19937 rng(7);
    std::normal_distribution<float> g(0.0f, 0.1f);
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    std::vector<double> sum(32, 0.0);
    const int frames = 200;
    for (int f = 0; f < frames; ++f)
    {
        for (int i = 0; i < (int)eq::Spectrum::kFftSize; ++i)
        {
            const float w = g(rng);
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            s.push(pink);
        }
        s.analyse(1000.0f, 1.0f);   // effectively no release smoothing: each frame stands alone
        for (int c = 0; c < 32; ++c) sum[(std::size_t)c] += std::pow(10.0, s.columnDb(c) / 10.0);
    }
    // The lowest columns are narrower than an FFT bin, so they cannot resolve pink noise's slope;
    // check flatness from 60 Hz up.
    double lo = 1e9, hi = -1e9;
    for (int c = 0; c < 32; ++c)
    {
        if (s.columnLoHz(c) < 60.0) continue;
        const double d = 10.0 * std::log10(sum[(std::size_t)c] / frames);
        lo = std::fmin(lo, d); hi = std::fmax(hi, d);
    }
    CHECK(hi - lo < 3.0, "pink noise spread %.2f dB across 60 Hz..20 kHz columns", hi - lo);
}

static void testSpectrumRelease()
{
    std::printf("== Spectrum: release limits the fall\n");
    eq::Spectrum s;
    s.prepare(44100.0, 32);
    for (int i = 0; i < 4096; ++i) s.push((float)std::sin(2.0 * M_PI * 1000.0 * i / 44100.0));
    s.analyse(24.0f, 0.033f);
    float before = -200; int col = 0;
    for (int c = 0; c < 32; ++c) if (s.columnDb(c) > before) { before = s.columnDb(c); col = c; }
    for (int i = 0; i < 4096; ++i) s.push(0.0f);
    s.analyse(24.0f, 0.5f);   // half a second at 24 dB/s: may fall 12 dB
    CHECK(std::abs(s.columnDb(col) - (before - 12.0f)) < 0.01f, "fell to %.2f from %.2f", s.columnDb(col), before);
}

int main()
{
    testFft();
    testSpectrumSine();
    testSpectrumPink();
    testSpectrumRelease();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
