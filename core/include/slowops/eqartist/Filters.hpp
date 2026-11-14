// EQ band shapes and zero-latency digital filters whose response matches the analog shapes.
// Standard library only.
//
// Every band is a cascade of up to four sections. Each section is written as an analog
// prototype in normalised s (s = j f / f0):
//
//     H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0)
//
// That one description serves two purposes:
//   * analogDb() evaluates it directly. The curve fitter uses this, and it is the reference
//     the digital filters are tested against.
//   * design() turns it into a digital biquad ("magnitude-matched" design):
//       - The poles are the analog poles mapped exactly, z = exp(s T). Unlike the bilinear
//         transform, nothing is squeezed towards Nyquist, so a bell at 15 kHz keeps its width.
//       - The zeros are chosen so the digital magnitude equals the analog magnitude at three
//         frequencies: DC, Nyquist, and the band's own frequency.
//     For a biquad numerator N(z) = b0 + b1 z^-1 + b2 z^-2,
//         |N(e^jw)|^2 = B0 p0 + B1 p1 + B2 p2
//     with p1 = sin^2(w/2), p0 = 1 - p1, p2 = 4 p0 p1, B0 = (b0+b1+b2)^2, B1 = (b0-b1+b2)^2 and
//     B2 = -4 b0 b2. The same holds for the denominator. Matching three magnitudes gives
//     B0, B1, B2 in closed form, and from them b0, b1, b2 (minimum phase).
//
// Cuts (6..48 dB/oct) are Butterworth cascades. The section with the highest Q takes the band's
// Q as resonance (Q 0.707 = plain Butterworth).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace slowops::eqartist {

enum class BandType { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, Count };

inline constexpr int kNumSlopes = 6;
inline constexpr std::array<int, kNumSlopes> kSlopesDbPerOct = {6, 12, 18, 24, 36, 48};
inline constexpr int kMaxSections = 4;

inline bool typeHasGain(BandType t)
{
    return t == BandType::Bell || t == BandType::LowShelf || t == BandType::HighShelf || t == BandType::TiltShelf;
}
inline bool typeHasSlope(BandType t) { return t == BandType::LowCut || t == BandType::HighCut; }

struct BandParams
{
    bool on = false;
    BandType type = BandType::Bell;
    double freq = 1000.0;   // Hz
    double gainDb = 0.0;
    double q = 1.0;
    int slope = 1;          // index into kSlopesDbPerOct (cuts only)

    bool operator==(const BandParams& o) const
    {
        return on == o.on && type == o.type && freq == o.freq && gainDb == o.gainDb && q == o.q && slope == o.slope;
    }
};

// How a section's digital zeros are fitted (see designSection()).
enum class Match
{
    Peak,       // bell, notch: DC, plus the level and flat top at f0
    Shelf,      // shelves, tilt: DC, plus the level and slope at f0
    LowPass,    // b2 = 0: DC, and the level at f0
    HighPass,   // double zero at DC; level at f0
    BandPass,   // zero at DC; level and flat top at f0
    LowPass1,   // first order: DC, level at f0
    HighPass1,  // first order: zero at DC, level at f0
};

// One analog section in normalised s.
struct AnalogSection
{
    double n2 = 0, n1 = 0, n0 = 1, d2 = 0, d1 = 0, d0 = 1;
    Match match = Match::Shelf;

    std::complex<double> at(double x) const   // x = f / f0
    {
        const std::complex<double> s(0.0, x);
        return (n2 * s * s + n1 * s + n0) / (d2 * s * s + d1 * s + d0);
    }
};

struct AnalogBand
{
    std::array<AnalogSection, kMaxSections> sec{};
    int count = 0;
    double f0 = 1000.0;
};

// The band's analog prototype: sections in normalised s, all sharing f0.
inline AnalogBand analogBand(const BandParams& p)
{
    AnalogBand b;
    b.f0 = p.freq;
    const double q = std::max(0.025, p.q);
    const double A = std::pow(10.0, p.gainDb / 40.0);   // sqrt of the linear gain
    auto add = [&](AnalogSection s) { if (b.count < kMaxSections) b.sec[(std::size_t)b.count++] = s; };
    switch (p.type)
    {
        case BandType::Bell:
            add({1, A / q, 1, 1, 1 / (A * q), 1, Match::Peak});
            break;
        case BandType::LowShelf:
        {
            const double r = std::sqrt(A) / q;
            add({A, A * r, A * A, A, r, 1, Match::Shelf});
            break;
        }
        case BandType::HighShelf:
        {
            const double r = std::sqrt(A) / q;
            add({A * A, A * r, A, 1, r, A, Match::Shelf});
            break;
        }
        case BandType::TiltShelf:
        {
            // Lows at -gain/2, highs at +gain/2: a low shelf of -gain, lifted by gain/2.
            const double a = 1.0 / A, r = std::sqrt(a) / q, lift = A;
            add({a * lift, a * r * lift, a * a * lift, a, r, 1, Match::Shelf});
            break;
        }
        case BandType::Notch:
            add({1, 0, 1, 1, 1 / q, 1, Match::Peak});
            break;
        case BandType::BandPass:
            add({0, 1 / q, 0, 1, 1 / q, 1, Match::BandPass});
            break;
        case BandType::LowCut:
        case BandType::HighCut:
        {
            const bool hp = p.type == BandType::LowCut;
            const int order = kSlopesDbPerOct[(std::size_t)std::clamp(p.slope, 0, kNumSlopes - 1)] / 6;
            // Butterworth section Qs, highest first; the highest takes the resonance.
            std::array<double, kMaxSections> qs{};
            int nq = 0;
            for (int k = 0; k < order / 2; ++k)
            {
                const double angle = (order % 2 == 0) ? (2.0 * k + 1.0) * M_PI / (2.0 * order)
                                                      : (k + 1.0) * M_PI / order;
                qs[(std::size_t)nq++] = 1.0 / (2.0 * std::cos(angle));
            }
            std::sort(qs.begin(), qs.begin() + nq, std::greater<double>());
            if (nq > 0) qs[0] *= q / M_SQRT1_2;
            for (int k = 0; k < nq; ++k)
                add(hp ? AnalogSection{1, 0, 0, 1, 1 / qs[(std::size_t)k], 1, Match::HighPass}
                       : AnalogSection{0, 0, 1, 1, 1 / qs[(std::size_t)k], 1, Match::LowPass});
            if (order % 2 == 1)
                add(hp ? AnalogSection{0, 1, 0, 0, 1, 1, Match::HighPass1} : AnalogSection{0, 0, 1, 0, 1, 1, Match::LowPass1});
            break;
        }
        default:
            break;
    }
    return b;
}

// Magnitude of the analog prototype at f Hz, in dB. An "off" band is 0 dB.
inline double analogDb(const BandParams& p, double f)
{
    if (!p.on) return 0.0;
    const AnalogBand b = analogBand(p);
    double mag2 = 1.0;
    for (int k = 0; k < b.count; ++k) mag2 *= std::norm(b.sec[(std::size_t)k].at(f / b.f0));
    return 10.0 * std::log10(std::max(mag2, 1e-30));
}

// ---- digital ---------------------------------------------------------------------------------

struct Coeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    // Magnitude response at f Hz.
    double magnitude(double f, double sampleRate) const
    {
        const std::complex<double> z1 = std::polar(1.0, -2.0 * M_PI * f / sampleRate);
        const std::complex<double> num = b0 + b1 * z1 + b2 * z1 * z1;
        const std::complex<double> den = 1.0 + a1 * z1 + a2 * z1 * z1;
        return std::abs(num / den);
    }
};

struct DigitalBand
{
    std::array<Coeffs, kMaxSections> sec{};
    int count = 0;
};

// The highest frequency a band is designed at: the matching point must stay clear of Nyquist.
inline double maxDesignFreq(double sampleRate) { return 0.45 * sampleRate; }

inline Coeffs designSection(const AnalogSection& s, double f0, double sampleRate)
{
    Coeffs c;
    const double w0 = 2.0 * M_PI * f0 / sampleRate;   // band frequency, radians per sample
    const bool second = s.d2 != 0.0;

    // Poles: the analog poles mapped with z = exp(s T). In normalised s, a root r is the analog
    // pole r * 2 pi f0, so z = exp(r * w0).
    if (second)
    {
        const std::complex<double> disc = std::sqrt(std::complex<double>(s.d1 * s.d1 - 4.0 * s.d2 * s.d0, 0.0));
        const std::complex<double> r1 = (-s.d1 + disc) / (2.0 * s.d2), r2 = (-s.d1 - disc) / (2.0 * s.d2);
        const std::complex<double> z1 = std::exp(r1 * w0), z2 = std::exp(r2 * w0);
        c.a1 = -(z1 + z2).real();
        c.a2 = (z1 * z2).real();
    }
    else
    {
        c.a1 = -std::exp(-s.d0 / s.d1 * w0);
        c.a2 = 0.0;
    }

    // Zeros. With u = sin^2(w/2), a biquad's squared magnitude is a quadratic in u:
    //   |N|^2 = B0 (1-u) + B1 u + 4 B2 u (1-u), and likewise |D|^2 with A0, A1, A2.
    // Each Match mode fixes B0, B1, B2 from the analog shape, then b0, b1, b2 follow.
    const double A0 = (1 + c.a1 + c.a2) * (1 + c.a1 + c.a2);
    const double A1 = (1 - c.a1 + c.a2) * (1 - c.a1 + c.a2);
    const double A2 = -4.0 * c.a2;
    const double u0 = std::pow(std::sin(0.5 * w0), 2.0);
    const double D0 = A0 * (1 - u0) + A1 * u0 + 4 * A2 * u0 * (1 - u0);   // |D|^2 at f0
    const double dD0 = -A0 + A1 + 4 * A2 * (1 - 2 * u0);                   // d|D|^2/du at f0
    const double h0 = std::norm(s.at(0.0)), h1 = std::norm(s.at(1.0));
    const double T0 = h1 * D0;                                              // |N|^2 wanted at f0

    double B0 = 0, B1 = 0, B2 = 0;
    switch (s.match)
    {
        case Match::HighPass:   // N = k (1 - z^-1)^2: |N|^2 = 16 k^2 u^2
        {
            const double k = std::sqrt(T0) / (4.0 * u0);
            c.b0 = k; c.b1 = -2.0 * k; c.b2 = k;
            return c;
        }
        case Match::HighPass1:  // N = k (1 - z^-1): |N|^2 = 4 k^2 u
        {
            const double k = std::sqrt(T0 / (4.0 * u0));
            c.b0 = k; c.b1 = -k; c.b2 = 0;
            return c;
        }
        case Match::LowPass:
        case Match::LowPass1:   // b2 = 0 (B2 = 0): DC and f0
            B0 = A0 * h0;
            B1 = std::max(0.0, (T0 - B0 * (1 - u0)) / u0);
            break;
        case Match::Peak:
        case Match::BandPass:
        {
            // DC level, and at f0 the level and a flat top (the analog shape peaks or dips exactly
            // at f0, so the derivative of |H|^2 is 0 there and d|N|^2/du = |H|^2 d|D|^2/du).
            B0 = s.match == Match::Peak ? A0 * h0 : 0.0;
            const double r1 = T0 - B0 * (1 - u0);   // = B1 u0 + 4 B2 u0 (1 - u0)
            const double r2 = h1 * dD0 + B0;        // = B1 + 4 B2 (1 - 2 u0)
            // Solve the 2 x 2 system for B1, B2.
            const double det = u0 * 4 * (1 - 2 * u0) - 4 * u0 * (1 - u0);
            B1 = (r1 * 4 * (1 - 2 * u0) - 4 * u0 * (1 - u0) * r2) / det;
            B2 = (u0 * r2 - r1) / det;
            B1 = std::max(0.0, B1);
            break;
        }
        case Match::Shelf:
        default:
        {
            // DC level, and at f0 the level and the slope of the analog shape. (Matching at
            // Nyquist instead fails for shelves close to Nyquist: no real zeros fit all three.)
            const double e = 1e-4;
            const double dhdx = (std::norm(s.at(1.0 + e)) - std::norm(s.at(1.0 - e))) / (2.0 * e);
            const double dxdu = sampleRate / (2.0 * M_PI * f0 * std::sqrt(u0 * (1.0 - u0)));
            B0 = A0 * h0;
            const double r1 = T0 - B0 * (1 - u0);
            const double r2 = dhdx * dxdu * D0 + h1 * dD0 + B0;
            const double det = u0 * 4 * (1 - 2 * u0) - 4 * u0 * (1 - u0);
            B1 = std::max(0.0, (r1 * 4 * (1 - 2 * u0) - 4 * u0 * (1 - u0) * r2) / det);
            B2 = (u0 * r2 - r1) / det;
            break;
        }
    }
    const double sq0 = std::sqrt(B0), sq1 = std::sqrt(B1);
    if (!second)
    {
        c.b0 = 0.5 * (sq0 + sq1);
        c.b1 = 0.5 * (sq0 - sq1);
        c.b2 = 0.0;
        return c;
    }
    // b0 + b1 + b2 = sqrt(B0), b0 - b1 + b2 = sqrt(B1), b0 b2 = -B2 / 4; b0 the larger root
    // (zeros inside the unit circle: minimum phase).
    const double W = 0.5 * (sq0 + sq1);
    c.b1 = 0.5 * (sq0 - sq1);
    const double d = std::sqrt(std::max(0.0, W * W + B2));
    c.b0 = 0.5 * (W + d);
    c.b2 = W - c.b0;
    return c;
}

inline DigitalBand design(const BandParams& p, double sampleRate)
{
    DigitalBand d;
    if (!p.on) return d;
    BandParams q = p;
    q.freq = std::clamp(p.freq, 10.0, maxDesignFreq(sampleRate));
    const AnalogBand a = analogBand(q);
    for (int k = 0; k < a.count; ++k) d.sec[(std::size_t)k] = designSection(a.sec[(std::size_t)k], a.f0, sampleRate);
    d.count = a.count;
    return d;
}

inline double digitalDb(const DigitalBand& d, double f, double sampleRate)
{
    double mag = 1.0;
    for (int k = 0; k < d.count; ++k) mag *= d.sec[(std::size_t)k].magnitude(f, sampleRate);
    return 20.0 * std::log10(std::max(mag, 1e-15));
}

// Transposed direct form II, double-precision state, one channel.
struct BiquadState
{
    double s1 = 0, s2 = 0;
    double process(const Coeffs& c, double x)
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { s1 = s2 = 0; }
};

} // namespace slowops::eqartist
