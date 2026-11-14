// core_tests.cpp - unit tests for core/include/slowops/eqartist (no SDK dependency).
// Build: c++ -std=c++17 -O2 -I core/include tests/core_tests.cpp -o build/core_tests

#include "slowops/eqartist/CurveFit.hpp"
#include "slowops/eqartist/EqEngine.hpp"
#include "slowops/eqartist/Fft.hpp"
#include "slowops/eqartist/Filters.hpp"
#include "slowops/eqartist/LineStrip.hpp"
#include "slowops/eqartist/LiveEq.hpp"
#include "slowops/eqartist/Spectrum.hpp"

#include <chrono>
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


static eq::BandParams band(eq::BandType t, double f, double g, double q, int slope = 1)
{
    eq::BandParams b; b.on = true; b.type = t; b.freq = f; b.gainDb = g; b.q = q; b.slope = slope; return b;
}

static void testFiltersMatchAnalog()
{
    std::printf("== Filters: digital response matches the analog shape\n");
    // Bands up to 9 kHz: up to half of Nyquist (about 11 kHz) the digital filters must follow the
    // analog shapes within 0.5 dB, and within 3 dB up to 0.8 x Nyquist (a zero-latency filter
    // cannot follow an analog shape that keeps going past Nyquist).
    // Bands at 16 kHz (three quarters of the way to Nyquist): within 3.5 dB everywhere.
    const char* names[] = {"Bell", "LowShelf", "HighShelf", "LowCut", "HighCut", "Notch", "BandPass", "Tilt"};
    double worstLo = 0, worstHi = 0, worstTop = 0;
    for (double sr : {44100.0, 48000.0})
        for (int t = 0; t < (int)eq::BandType::Count; ++t)
        {
            double lo = 0, hi = 0, top = 0, loF = 0, loF0 = 0;
            for (double f0 : {40.0, 300.0, 2500.0, 9000.0, 16000.0})
                for (double g : {-12.0, 6.0, 15.0})
                    for (double q : {0.5, 1.0, 4.0})
                    {
                        const auto type = (eq::BandType)t;
                        // Shelves and cuts are used up to Q 1.2 / 1.4 (CurveFit and the Q range).
                        if (q > 1.5 && !(type == eq::BandType::Bell || type == eq::BandType::Notch || type == eq::BandType::BandPass)) continue;
                        const eq::BandParams p = band(type, f0, g, q, 3);
                        const auto d = eq::design(p, sr);
                        for (int k = 0; k < 200; ++k)
                        {
                            const double f = 20.0 * std::pow(1000.0, k / 199.0);
                            if (f > 0.8 * sr / 2) break;
                            const double a = eq::analogDb(p, f), dd = eq::digitalDb(d, f, sr);
                            // A notch's centre and a cut's stop band are deep: compare only above -30 dB.
                            if (a < -30.0) continue;
                            const double e = std::fabs(a - dd);
                            if (f0 > 10000.0) top = std::fmax(top, e);
                            else if (f <= 0.5 * sr / 2) { if (e > lo) { lo = e; loF = f; loF0 = f0; } }
                            else hi = std::fmax(hi, e);
                        }
                    }
            worstLo = std::fmax(worstLo, lo);
            worstHi = std::fmax(worstHi, hi);
            worstTop = std::fmax(worstTop, top);
            CHECK(top < 3.5, "%s @ %.0f: a 16 kHz band is off by %.2f dB", names[t], sr, top);
            CHECK(lo < 0.5, "%s @ %.0f: off by %.2f dB at %.0f Hz (f0 %.0f)", names[t], sr, lo, loF, loF0);
            CHECK(hi < 3.0, "%s @ %.0f: off by %.2f dB between 0.5 and 0.8 x Nyquist", names[t], sr, hi);
        }
    std::printf("  worst deviation from analog: bands to 9 kHz %.2f dB up to 0.5 x Nyquist, %.2f dB up to 0.8 x Nyquist; 16 kHz bands %.2f dB\n",
                worstLo, worstHi, worstTop);
}

static void testCutSlopes()
{
    std::printf("== Filters: cut slopes\n");
    for (int s = 0; s < eq::kNumSlopes; ++s)
    {
        const auto lc = eq::design(band(eq::BandType::LowCut, 2000.0, 0, M_SQRT1_2, s), 48000.0);
        const auto hc = eq::design(band(eq::BandType::HighCut, 500.0, 0, M_SQRT1_2, s), 48000.0);
        const double lSlope = eq::digitalDb(lc, 250.0, 48000.0) - eq::digitalDb(lc, 125.0, 48000.0);
        const double hSlope = eq::digitalDb(hc, 2000.0, 48000.0) - eq::digitalDb(hc, 4000.0, 48000.0);
        const double want = eq::kSlopesDbPerOct[(std::size_t)s];
        CHECK(std::fabs(lSlope - want) < 0.6, "low cut %d dB/oct measures %.2f", (int)want, lSlope);
        CHECK(std::fabs(hSlope - want) < 0.6, "high cut %d dB/oct measures %.2f", (int)want, hSlope);
        CHECK(std::fabs(eq::digitalDb(lc, 2000.0, 48000.0) + 3.01) < 0.2, "low cut %d: %.2f dB at its corner", (int)want,
              eq::digitalDb(lc, 2000.0, 48000.0));
    }
}

static std::vector<double> columnHz();

static void testEngine()
{
    std::printf("== Engine: response, stability under fast changes, bypass\n");
    const double sr = 48000.0;
    eq::EqEngine e;
    e.setBand(0, band(eq::BandType::Bell, 1000.0, 9.0, 1.0));
    const auto hzc = columnHz();
    e.prepare(sr, hzc.data());
    // A 1 kHz sine through a +9 dB bell at 1 kHz comes out 9 dB louder.
    const int n = 48000;
    std::vector<float> l((std::size_t)n), r((std::size_t)n);
    for (int i = 0; i < n; ++i) l[(std::size_t)i] = r[(std::size_t)i] = 0.1f * (float)std::sin(2 * M_PI * 1000.0 * i / sr);
    e.process(l.data(), r.data(), l.data(), r.data(), n);
    float peak = 0;
    for (int i = n / 2; i < n; ++i) peak = std::fmax(peak, std::fabs(l[(std::size_t)i]));
    CHECK(std::fabs(20 * std::log10(peak / 0.1) - 9.0) < 0.1, "bell gain measures %.2f dB", 20 * std::log10(peak / 0.1));

    // Random targets every 64 samples on noise: output must stay finite and bounded.
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(0, 1);
    bool finite = true; float maxOut = 0;
    std::vector<float> bl(64), br(64);
    for (int blk = 0; blk < 6000; ++blk)
    {
        if (blk % 1 == 0)
            for (int b = 0; b < 8; ++b)
                e.setBand(b, band((eq::BandType)(int)(u(rng) * 7.99), 20.0 * std::pow(1000.0, u(rng)), -18 + 36 * u(rng),
                                  0.2 * std::pow(60.0, u(rng)), (int)(u(rng) * 5.99)));
        for (int i = 0; i < 64; ++i) bl[(std::size_t)i] = br[(std::size_t)i] = (float)(u(rng) * 2 - 1) * 0.25f;
        e.process(bl.data(), br.data(), bl.data(), br.data(), 64);
        for (float v : bl) { finite = finite && std::isfinite(v); maxOut = std::fmax(maxOut, std::fabs(v)); }
    }
    CHECK(finite, "NaN/Inf under random parameter changes");
    CHECK(maxOut < 200.0f, "output blew up to %.1f under random changes", maxOut);

    // Bypass: dry signal back, after the ramp.
    for (int b = 0; b < 8; ++b) e.setBand(b, band(eq::BandType::Bell, 1000.0, 12.0, 1.0));
    e.setBypass(true);
    for (int i = 0; i < n; ++i) l[(std::size_t)i] = r[(std::size_t)i] = 0.1f * (float)std::sin(2 * M_PI * 1000.0 * i / sr);
    std::vector<float> dry = l;
    e.process(l.data(), r.data(), l.data(), r.data(), n);
    double diff = 0;
    for (int i = n / 2; i < n; ++i) diff = std::fmax(diff, std::fabs(l[(std::size_t)i] - dry[(std::size_t)i]));
    CHECK(diff < 1e-5, "bypass differs from dry by %g", diff);
}

// Column centres matching the plugin's 32 columns.
static std::vector<double> columnHz()
{
    std::vector<double> hz(32);
    for (int c = 0; c < 32; ++c) hz[(std::size_t)c] = 20.0 * std::pow(1000.0, (c + 0.5) / 32.0);
    return hz;
}

static void testFitKnownCurves()
{
    std::printf("== CurveFit: recovers curves made of real bands\n");
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u(0, 1);
    const auto hz = columnHz();
    double worstRms = 0, totalMs = 0;
    int runs = 0;
    for (int trial = 0; trial < 40; ++trial)
    {
        std::vector<eq::BandParams> truth;
        const int nb = 1 + trial % 6;
        for (int b = 0; b < nb; ++b)
        {
            const double pick = u(rng);
            const auto t = pick < 0.7 ? eq::BandType::Bell : (pick < 0.85 ? eq::BandType::LowShelf : eq::BandType::HighShelf);
            truth.push_back(band(t, 40.0 * std::pow(400.0, u(rng)), (u(rng) < 0.5 ? -1 : 1) * (3 + 9 * u(rng)),
                                 t == eq::BandType::Bell ? 0.5 * std::pow(6.0, u(rng)) : 0.71));
        }
        auto truthDb = [&](double f) { double s = 0; for (auto& b : truth) s += eq::analogDb(b, f); return s; };
        std::vector<double> db(32);
        bool inRange = true;
        for (int c = 0; c < 32; ++c) { db[(std::size_t)c] = truthDb(hz[(std::size_t)c]); inRange = inRange && std::fabs(db[(std::size_t)c]) <= 18; }
        if (!inRange) continue;   // the drawing is limited to +-18 dB
        eq::CurveFit fit;
        const auto t0 = std::chrono::steady_clock::now();
        const eq::FitResult res = fit.fit(hz, db, 8);
        totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++runs;
        // Compare the fit with the drawing at the columns (what the user drew).
        double s = 0;
        for (int c = 0; c < 32; ++c)
        {
            double m = 0;
            for (int b = 0; b < res.count; ++b) m += eq::analogDb(res.bands[(std::size_t)b], hz[(std::size_t)c]);
            s += std::pow(m - db[(std::size_t)c], 2);
        }
        const double e = std::sqrt(s / 32);
        worstRms = std::fmax(worstRms, e);
        CHECK(e < 0.75, "trial %d (%d true bands): fit misses the drawing by %.2f dB rms with %d bands", trial, nb, e, res.count);
    }
    std::printf("  %d curves, worst %.2f dB rms, average fit time %.1f ms\n", runs, worstRms, totalMs / std::max(1, runs));
    CHECK(totalMs / std::max(1, runs) < 50.0, "fit too slow: %.1f ms average", totalMs / std::max(1, runs));
}

static void testFitDrawnShapes()
{
    std::printf("== CurveFit: hand-drawn shapes\n");
    const auto hz = columnHz();
    struct Shape { const char* name; double (*fn)(double lf); double maxRms; };
    // lf = log10(f / 20) / 3: 0 at 20 Hz, 1 at 20 kHz.
    const Shape shapes[] = {
        {"smile", [](double lf) { return 10.0 * std::pow(2 * lf - 1, 2) - 3.0; }, 1.0},
        {"tilt", [](double lf) { return -8.0 + 16.0 * lf; }, 1.0},
        {"steep low cut", [](double lf) { return lf < 0.2 ? std::fmax(-18.0, (lf - 0.2) * 3 * 3.3219 * 24.0) : 0.0; }, 1.5},
        {"single dip", [](double lf) { const double d = (lf - 0.55) / 0.04; return -12.0 * std::exp(-d * d); }, 1.0},
        {"flat", [](double) { return 0.0; }, 0.01},
        {"bumpy", [](double lf) { return 6.0 * std::sin(lf * 2 * M_PI * 3.0); }, 1.5},
    };
    for (const Shape& s : shapes)
    {
        std::vector<double> db(32);
        for (int c = 0; c < 32; ++c) db[(std::size_t)c] = s.fn(std::log10(hz[(std::size_t)c] / 20.0) / 3.0);
        eq::CurveFit fit;
        const eq::FitResult res = fit.fit(hz, db, 8);
        double e = 0;
        for (int c = 0; c < 32; ++c)
        {
            double m = 0;
            for (int b = 0; b < res.count; ++b) m += eq::analogDb(res.bands[(std::size_t)b], hz[(std::size_t)c]);
            e += std::pow(m - db[(std::size_t)c], 2);
        }
        e = std::sqrt(e / 32);
        std::printf("  %-14s %d bands, %.2f dB rms at the columns\n", s.name, res.count, e);
        CHECK(e < s.maxRms, "%s: %.2f dB rms", s.name, e);
    }
    // Flat drawing -> no bands at all.
    eq::CurveFit fit;
    CHECK(fit.fit(hz, std::vector<double>(32, 0.0), 8).count == 0, "a flat drawing made bands");
    // Max Bands is respected.
    std::vector<double> db(32);
    for (int c = 0; c < 32; ++c) db[(std::size_t)c] = 6.0 * std::sin(c * 0.9);
    CHECK(fit.fit(hz, db, 3).count <= 3, "Max Bands 3 ignored");
}

static void testLiveEq()
{
    std::printf("== LiveEq: the 32 live bells land on the drawing\n");
    const auto hz = columnHz();
    struct Shape { const char* name; double (*fn)(double lf); double maxErr; };
    const Shape shapes[] = {
        {"smile", [](double lf) { return 10.0 * std::pow(2 * lf - 1, 2) - 3.0; }, 0.6},
        {"tilt", [](double lf) { return -8.0 + 16.0 * lf; }, 0.6},
        {"bump + dip", [](double lf) { const double a = (lf - 0.3) / 0.08, b = (lf - 0.7) / 0.06;
                                      return 9.0 * std::exp(-a * a) - 8.0 * std::exp(-b * b); }, 0.8},
        {"steep step", [](double lf) { return lf < 0.5 ? -12.0 : 12.0; }, 4.0},   // a cliff: smoothed, by design
        {"flat", [](double) { return 0.0; }, 0.01},
    };
    for (double sr : {44100.0, 48000.0})
    {
        eq::LiveEq live;
        live.prepare(sr, hz.data());
        for (const Shape& sh : shapes)
        {
            std::vector<double> db(32);
            for (int c = 0; c < 32; ++c) db[(std::size_t)c] = sh.fn(std::log10(hz[(std::size_t)c] / 20.0) / 3.0);
            live.setDrawing(db.data());
            double worst = 0, worstDigital = 0;
            for (int c = 0; c < 32; ++c) worst = std::fmax(worst, std::fabs(live.responseDb(c) - db[(std::size_t)c]));
            // The digital filters, after the gain glide has settled.
            for (int k = 0; k < 400; ++k) live.control();
            for (int c = 0; c < 32; ++c)
            {
                double dsum = 0;
                for (int b = 0; b < 32; ++b)
                {
                    eq::BandParams p; p.on = live.gainDb(b) != 0.0; p.type = eq::BandType::Bell;
                    p.freq = hz[(std::size_t)b]; p.gainDb = live.gainDb(b); p.q = eq::LiveEq::kQ;
                    if (p.on) dsum += eq::digitalDb(eq::design(p, sr), hz[(std::size_t)c], sr);
                }
                if (hz[(std::size_t)c] < 0.25 * sr) worstDigital = std::fmax(worstDigital, std::fabs(dsum - db[(std::size_t)c]));
            }
            if (sr == 44100.0) std::printf("  %-11s %.2f dB off at the columns (digital, to 11 kHz: %.2f dB)\n", sh.name, worst, worstDigital);
            CHECK(worst < sh.maxErr, "%s @ %.0f: live EQ %.2f dB off the drawing", sh.name, sr, worst);
            CHECK(worstDigital < sh.maxErr + 0.3, "%s @ %.0f: digital live EQ %.2f dB off", sh.name, sr, worstDigital);
        }
    }
    // Cost: setDrawing must be cheap enough for the audio thread.
    eq::LiveEq live;
    live.prepare(44100.0, hz.data());
    std::vector<double> db(32);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 1000; ++k) { for (int c = 0; c < 32; ++c) db[(std::size_t)c] = 10.0 * std::sin(c * 0.3 + k); live.setDrawing(db.data()); }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 1000.0;
    std::printf("  setDrawing: %.0f us per call (desktop)\n", us);
    CHECK(us < 500.0, "setDrawing takes %.0f us", us);
}

static void testLineStrip()
{
    std::printf("== LineStrip / ShadeStrip / GraphMap / hitTest (the PARA page's drawing)\n");
    // A narrow +18 dB bell (Q 10) and a wide cut: segments join, vertices sit on the curve, peaks reach.
    for (auto bandSpec : {std::array<double, 3>{365.0, 18.0, 10.0}, std::array<double, 3>{3000.0, -9.0, 0.7}})
    {
        const eq::BandParams b = band(eq::BandType::Bell, bandSpec[0], bandSpec[1], bandSpec[2]);
        float cols[eq::LineStrip::kCols];
        eq::LineStrip::encode([&](double hz) { return eq::analogDb(b, hz); }, cols);
        bool joined = true;
        double prevRight = 0, top = -100, bottom = 100, worst = 0;
        for (int c = 0; c < eq::LineStrip::kCols; ++c)
        {
            double l, r;
            eq::LineStrip::decode(cols[c], l, r);
            if (c > 0 && std::fabs(l - prevRight) > 1e-9) joined = false;
            prevRight = r;
            top = std::fmax(top, std::fmax(l, r));
            bottom = std::fmin(bottom, std::fmin(l, r));
            double ext = 0;
            for (int s2 = -2; s2 <= 2; ++s2)
            {
                const double d = eq::analogDb(b, eq::GraphMap::xToHz((c + s2 * 0.25) / eq::LineStrip::kCols));
                if (std::fabs(d) > std::fabs(ext)) ext = d;
            }
            worst = std::fmax(worst, std::fabs(l - std::fmax(-18.0, std::fmin(18.0, ext))));
        }
        CHECK(joined, "LineStrip segments do not join");
        CHECK(worst <= 1.01, "LineStrip vertices off the curve by %.2f dB", worst);
        if (bandSpec[1] > 0) CHECK(top >= 17.5, "Q 10 +18 dB bell: line peak %.2f dB", top);
        else CHECK(bottom <= bandSpec[1] + 0.2, "cut: line bottom %.2f dB, curve %.1f", bottom, bandSpec[1]);
        float shade[eq::ShadeStrip::kCols];
        eq::ShadeStrip::encode([&](double hz) { return eq::analogDb(b, hz); }, shade);
        double sTop = 0;
        for (float v : shade) sTop = std::fabs(v * 36.0 - 18.0) > std::fabs(sTop) ? v * 36.0 - 18.0 : sTop;
        CHECK(std::fabs(std::fabs(sTop) - std::fabs(std::fmax(-18.0, std::fmin(18.0, bandSpec[1])))) < 0.6,
              "shade extreme %.2f dB, band %.1f", sTop, bandSpec[1]);
    }
    // Encoding from cached samples gives exactly what encoding the function does (the PARA cache relies on it),
    // including a summed response built from per-band samples.
    {
        const eq::BandParams b1 = band(eq::BandType::Bell, 300.0, 6.0, 2.0), b2 = band(eq::BandType::HighShelf, 4000.0, -5.0, 0.7);
        auto sum = [&](double hz) { return eq::analogDb(b1, hz) + eq::analogDb(b2, hz); };
        static double q1[eq::LineStrip::kQuarterPoints], q2[eq::LineStrip::kQuarterPoints], qs[eq::LineStrip::kQuarterPoints];
        eq::LineStrip::sample([&](double hz) { return eq::analogDb(b1, hz); }, q1);
        eq::LineStrip::sample([&](double hz) { return eq::analogDb(b2, hz); }, q2);
        for (int i = 0; i < eq::LineStrip::kQuarterPoints; ++i) qs[i] = q1[i] + q2[i];
        float direct[eq::LineStrip::kCols], cached[eq::LineStrip::kCols];
        eq::LineStrip::encode(sum, direct);
        eq::LineStrip::encodeSampled(qs, cached);
        int diff = 0;
        for (int c = 0; c < eq::LineStrip::kCols; ++c) diff += direct[c] != cached[c];
        CHECK(diff == 0, "LineStrip: %d columns differ between direct and cached-sample encoding", diff);
        static double s1[eq::ShadeStrip::kPoints];
        eq::ShadeStrip::sample(sum, s1);
        float sd[eq::ShadeStrip::kCols], sc[eq::ShadeStrip::kCols];
        eq::ShadeStrip::encode(sum, sd);
        eq::ShadeStrip::encodeSampled(s1, sc);
        int sdiff = 0;
        for (int c = 0; c < eq::ShadeStrip::kCols; ++c) sdiff += sd[c] != sc[c];
        CHECK(sdiff == 0, "ShadeStrip: %d columns differ between direct and cached-sample encoding", sdiff);
    }
    // LineStrip76 (the PARA line): joined, on the curve, peaks reach; and cached encoding equals direct.
    {
        const eq::BandParams b = band(eq::BandType::Bell, 365.0, 18.0, 10.0);
        float cols[eq::LineStrip76::kCols];
        eq::LineStrip76::encode([&](double hz) { return eq::analogDb(b, hz); }, cols);
        bool joined = true;
        double prevRight = 0, top = -100;
        for (int c = 0; c < eq::LineStrip76::kCols; ++c)
        {
            double l, r;
            eq::LineStrip76::decode(cols[c], l, r);
            if (c > 0 && std::fabs(l - prevRight) > 1e-9) joined = false;
            prevRight = r;
            top = std::fmax(top, std::fmax(l, r));
        }
        CHECK(joined, "LineStrip76 segments do not join");
        CHECK(top >= 17.0, "LineStrip76: Q 10 +18 dB bell peaks at %.2f dB", top);
    }
    // Mapping round trips.
    for (double hz : {20.0, 100.0, 1234.0, 20000.0})
        CHECK(std::fabs(eq::GraphMap::xToHz(eq::GraphMap::hzToX(hz)) - hz) < hz * 1e-9, "x round trip at %.0f Hz", hz);
    for (double db : {-18.0, -3.5, 0.0, 12.0, 18.0})
        CHECK(std::fabs(eq::GraphMap::yToDb(eq::GraphMap::dbToY(db)) - db) < 1e-9, "y round trip at %.1f dB", db);
    CHECK(eq::GraphMap::dbToY(18.0) == 1.0, "Y = 1 must be +18 dB (the top: the MPC's XYPad)");
    // Hit test: nearest shown node within the radius.
    const double nx[3] = {0.20, 0.25, 0.70}, ny[3] = {0.5, 0.5, 0.8};
    const bool all[3] = {true, true, true}, hide1[3] = {true, false, true};
    CHECK(eq::hitTest(nx, ny, all, 3, 0.235, 0.5, 1216, 408, 44) == 1, "nearest node not picked");
    CHECK(eq::hitTest(nx, ny, hide1, 3, 0.235, 0.5, 1216, 408, 44) == 0, "a hidden node was picked");
    CHECK(eq::hitTest(nx, ny, all, 3, 0.45, 0.1, 1216, 408, 44) == -1, "a far touch picked a node");
}

int main()
{
    testFft();
    testSpectrumSine();
    testSpectrumPink();
    testSpectrumRelease();
    testFiltersMatchAnalog();
    testCutSlopes();
    testEngine();
    testFitKnownCurves();
    testFitDrawnShapes();
    testLiveEq();
    testLineStrip();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
