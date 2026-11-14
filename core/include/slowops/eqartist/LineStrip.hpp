// Drawing EQ curves on the MPC screen with film strips. Standard library only.
//
// The MPC skin cannot draw a line that follows values (Path has fixed geometry, Plotter draws
// nothing; see CurveUITest/test-results/live3.md). So a curve is a row of film-strip columns, one
// parameter each, and the plugin picks each column's frame:
//
//   LineStrip   a connected polyline over 152 columns of 8 px. Its vertices sit on the column
//               boundaries, snapped to 0.125 dB; each column's frame is "left vertex x rise", so
//               neighbouring columns share their end points exactly. Each vertex takes the curve's
//               extreme within half a column, so narrow peaks and notches reach their full height.
//               Proven on the MPC Live III (Curve UI Test 0.4.0, check K7d).
//   ShadeStrip  a fill from 0 dB to the curve over 64 columns at 0.5 dB, each column showing the
//               extreme of its span (16 samples): for shading one band's shape.
//
// GraphMap converts between frequency / gain and the graph's 0..1 coordinates (x: log 20 Hz..20 kHz;
// y: -18..+18 dB, y = 1 at the top, as the MPC's XYPad reports it), and hitTest() finds the node a
// touch lands on. All allocation-free; safe on the audio thread.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace slowops::eqartist {

struct GraphMap
{
    static constexpr double kLoHz = 20.0, kHiHz = 20000.0, kRangeDb = 18.0;
    static double xToHz(double x) { return kLoHz * std::pow(kHiHz / kLoHz, std::clamp(x, 0.0, 1.0)); }
    static double hzToX(double hz) { return std::clamp(std::log(hz / kLoHz) / std::log(kHiHz / kLoHz), 0.0, 1.0); }
    static double yToDb(double y) { return (std::clamp(y, 0.0, 1.0) * 2.0 - 1.0) * kRangeDb; }
    static double dbToY(double db) { return std::clamp((db / kRangeDb + 1.0) * 0.5, 0.0, 1.0); }
};

struct LineStrip
{
    static constexpr int kCols = 152;
    static constexpr double kStepDb = 0.125;
    static constexpr int kLevels = 289;                       // -18..+18 dB in 0.125 dB steps
    static constexpr int kNumRises = 31;
    static constexpr std::array<int, kNumRises> kRises = {   // in kStepDb units; up to 10 dB per column
        -80, -64, -48, -40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0,
        1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80};
    static constexpr int kFrames = kLevels * kNumRises;      // 8,959

    // The response is sampled at quarter-column steps: x = i / (4 kCols), i = 0..kQuarterPoints-1.
    // Sampling once on this grid (sample()) and encoding from it (encodeSampled()) lets a caller cache each
    // band's samples and only recompute the band that changed.
    static constexpr int kQuarterPoints = 4 * kCols + 1;
    template <class Fn>
    static void sample(Fn responseDb, double* q)
    {
        for (int i = 0; i < kQuarterPoints; ++i) q[i] = responseDb(GraphMap::xToHz((double)i / (4.0 * kCols)));
    }

    // responseDb(hz) -> dB. Writes kCols normalised frame values (0..1) to out.
    template <class Fn>
    static void encode(Fn responseDb, float* out)
    {
        double q[kQuarterPoints];
        sample(responseDb, q);
        encodeSampled(q, out);
    }

    // The same, from quarter-column samples q[0..kQuarterPoints).
    static void encodeSampled(const double* q, float* out)
    {
        int v = 0;
        for (int b = 0; b <= kCols; ++b)
        {
            double ext = 0.0;
            for (int s = -2; s <= 2; ++s)
            {
                const double d = q[std::clamp(4 * b + s, 0, kQuarterPoints - 1)];
                if (std::fabs(d) > std::fabs(ext)) ext = d;
            }
            const double db = std::clamp(ext, -GraphMap::kRangeDb, GraphMap::kRangeDb);
            const int want = (int)std::lround((db + GraphMap::kRangeDb) / kStepDb);
            if (b == 0) { v = want; continue; }
            int best = -1;
            for (int k = 0; k < kNumRises; ++k)
            {
                const int to = v + kRises[(std::size_t)k];
                if (to < 0 || to >= kLevels) continue;
                if (best < 0 || std::abs(to - want) < std::abs(v + kRises[(std::size_t)best] - want)) best = k;
            }
            out[b - 1] = (float)(v * kNumRises + best) / (float)(kFrames - 1);
            v += kRises[(std::size_t)best];
        }
    }

    // The column's left and right vertex in dB (for tests and mock renders).
    static void decode(float norm, double& leftDb, double& rightDb)
    {
        const int idx = (int)std::lround(norm * (kFrames - 1));
        const int left = idx / kNumRises;
        leftDb = left * kStepDb - GraphMap::kRangeDb;
        rightDb = (left + kRises[(std::size_t)(idx % kNumRises)]) * kStepDb - GraphMap::kRangeDb;
    }
};

// The PARA page's line: 76 columns of 16 px, vertices at 0.25 dB, rises up to 10 dB per column. Half the
// columns of LineStrip, so half the screen widgets: with 152 columns (and 6, then 3 tiles each) dragging
// a node in LINE did not work on the MPC Live III, while the 76-column line moved smoothly in Curve UI Test.
// It encodes from LineStrip's quarter-column samples (every second one), so the same cached samples serve
// the line, the dots and the fill.
struct LineStrip76
{
    static constexpr int kCols = 76;
    static constexpr double kStepDb = 0.25;
    static constexpr int kLevels = 145;
    static constexpr int kNumRises = 25;
    static constexpr std::array<int, kNumRises> kRises = {
        -40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40};
    static constexpr int kFrames = kLevels * kNumRises;      // 3,625

    // q: LineStrip quarter-column samples (LineStrip::kQuarterPoints); this line's quarter step is 2 of them.
    static void encodeSampled(const double* q, float* out)
    {
        int v = 0;
        for (int b = 0; b <= kCols; ++b)
        {
            double ext = 0.0;
            for (int s = -2; s <= 2; ++s)
            {
                const double d = q[std::clamp(2 * (4 * b + s), 0, LineStrip::kQuarterPoints - 1)];
                if (std::fabs(d) > std::fabs(ext)) ext = d;
            }
            const double db = std::clamp(ext, -GraphMap::kRangeDb, GraphMap::kRangeDb);
            const int want = (int)std::lround((db + GraphMap::kRangeDb) / kStepDb);
            if (b == 0) { v = want; continue; }
            int best = -1;
            for (int k = 0; k < kNumRises; ++k)
            {
                const int to = v + kRises[(std::size_t)k];
                if (to < 0 || to >= kLevels) continue;
                if (best < 0 || std::abs(to - want) < std::abs(v + kRises[(std::size_t)best] - want)) best = k;
            }
            out[b - 1] = (float)(v * kNumRises + best) / (float)(kFrames - 1);
            v += kRises[(std::size_t)best];
        }
    }

    template <class Fn>
    static void encode(Fn responseDb, float* out)
    {
        double q[LineStrip::kQuarterPoints];
        LineStrip::sample(responseDb, q);
        encodeSampled(q, out);
    }

    static void decode(float norm, double& leftDb, double& rightDb)
    {
        const int idx = (int)std::lround(norm * (kFrames - 1));
        const int left = idx / kNumRises;
        leftDb = left * kStepDb - GraphMap::kRangeDb;
        rightDb = (left + kRises[(std::size_t)(idx % kNumRises)]) * kStepDb - GraphMap::kRangeDb;
    }
};

struct ShadeStrip
{
    static constexpr int kCols = 64;
    static constexpr int kFrames = 73;   // -18..+18 dB in 0.5 dB steps

    static constexpr int kSamples = 16;                      // per column
    static constexpr int kPoints = kCols * kSamples;
    template <class Fn>
    static void sample(Fn responseDb, double* s16)
    {
        for (int c = 0; c < kCols; ++c)
            for (int s = 0; s < kSamples; ++s) s16[c * kSamples + s] = responseDb(GraphMap::xToHz((c + (s + 0.5) / kSamples) / kCols));
    }

    template <class Fn>
    static void encode(Fn responseDb, float* out)
    {
        double s16[kPoints];
        sample(responseDb, s16);
        encodeSampled(s16, out);
    }

    static void encodeSampled(const double* s16, float* out)
    {
        for (int c = 0; c < kCols; ++c)
        {
            double ext = 0.0;
            for (int s = 0; s < kSamples; ++s)
            {
                const double d = s16[c * kSamples + s];
                if (std::fabs(d) > std::fabs(ext)) ext = d;
            }
            const double db = std::clamp(ext, -GraphMap::kRangeDb, GraphMap::kRangeDb);
            out[c] = (float)(std::round((db + GraphMap::kRangeDb) / (2.0 * GraphMap::kRangeDb) * (kFrames - 1)) / (kFrames - 1));
        }
    }
};

// The nearest of n nodes (0..1 graph coordinates) to a touch, within radiusPx on a graph of widthPx x
// heightPx; shown[i] false skips node i. Returns -1 when none is close enough.
inline int hitTest(const double* nodeX, const double* nodeY, const bool* shown, int n, double x, double y,
                   double widthPx, double heightPx, double radiusPx)
{
    int best = -1;
    double bestD = radiusPx * radiusPx;
    for (int i = 0; i < n; ++i)
    {
        if (!shown[i]) continue;
        const double dx = (nodeX[i] - x) * widthPx, dy = (nodeY[i] - y) * heightPx;
        const double d = dx * dx + dy * dy;
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

} // namespace slowops::eqartist
