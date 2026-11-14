// Real-time spectrum analyser for the graph display. Standard library only.
//
// Feed mono samples with push(). Call analyse() at the display rate (about 30 Hz). It runs one
// 2048-point Hann-windowed FFT on the newest samples and reduces it to the graph's log-spaced
// columns.
//
// Each column reads the TOTAL power in its band, so:
//   * a full-scale sine reads 0 dB in the column that holds it;
//   * pink noise reads flat, because log-spaced bands each hold the same pink-noise power.
//     That is why no extra display tilt is needed.
//
// A column's power is the exact integral, over its frequency range, of the bin powers joined
// by straight lines. Wide columns therefore sum every bin they hold (a narrow peak is never
// missed), and columns narrower than one bin (the lowest ones) read the interpolated level at
// their position, which keeps the bottom end smooth instead of stepped.
//
// Ballistics: a rise shows at once; a fall is limited to releaseDbPerSec.
//
// prepare() allocates. push() and analyse() do not, and are safe on the audio thread.
#pragma once

#include "Fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace slowops::eqartist {

class Spectrum
{
public:
    static constexpr std::size_t kFftSize = 2048;
    static constexpr float kFloorDb = -120.0f;

    void prepare(double sampleRate, int numColumns, double fLo = 20.0, double fHi = 20000.0)
    {
        sampleRate_ = sampleRate;
        numColumns_ = numColumns;
        fft_.prepare(kFftSize);
        ring_.assign(kFftSize, 0.0f);
        buf_.assign(kFftSize, {});
        power_.assign(kFftSize / 2 + 1, 0.0f);
        window_.resize(kFftSize);
        for (std::size_t i = 0; i < kFftSize; ++i)
            window_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)kFftSize));
        // Hann: coherent gain 0.5, noise bandwidth 1.5 bins. A full-scale sine's power summed
        // over its bins is (N/4)^2 * 1.5; dividing by that reads it as 0 dB.
        norm_ = 1.0 / (std::pow((double)kFftSize / 4.0, 2.0) * 1.5);
        binHz_ = sampleRate / (double)kFftSize;

        // Column edges in Hz and in bins.
        edges_.resize((std::size_t)numColumns + 1);
        edgeBins_.resize((std::size_t)numColumns + 1);
        for (int c = 0; c <= numColumns; ++c)
        {
            edges_[(std::size_t)c] = fLo * std::pow(fHi / fLo, (double)c / numColumns);
            edgeBins_[(std::size_t)c] = std::min(edges_[(std::size_t)c] / binHz_, (double)(kFftSize / 2));
        }
        prefix_.assign(kFftSize / 2 + 1, 0.0);
        db_.assign((std::size_t)numColumns, kFloorDb);
        write_ = 0;
    }

    void push(float x)
    {
        ring_[write_] = x;
        write_ = (write_ + 1) & (kFftSize - 1);
    }

    // seconds = time since the previous analyse() call, for the release limit.
    void analyse(float releaseDbPerSec, float seconds)
    {
        for (std::size_t i = 0; i < kFftSize; ++i)
            buf_[i] = {ring_[(write_ + i) & (kFftSize - 1)] * window_[i], 0.0f};
        fft_.forward(buf_.data());
        for (std::size_t k = 0; k <= kFftSize / 2; ++k) power_[k] = std::norm(buf_[k]);
        prefix_[0] = 0.0;   // prefix_[k] = integral of the interpolated power from bin 0 to bin k
        for (std::size_t k = 1; k <= kFftSize / 2; ++k)
            prefix_[k] = prefix_[k - 1] + 0.5 * ((double)power_[k - 1] + (double)power_[k]);

        const float fall = releaseDbPerSec * seconds;
        for (int c = 0; c < numColumns_; ++c)
        {
            // The band's total power. A column narrower than one bin is scaled up to one bin's
            // worth, so a sine that falls in it still reads close to its level.
            const double a = edgeBins_[(std::size_t)c], b = edgeBins_[(std::size_t)c + 1];
            const double width = b - a;
            double p = width > 0.0 ? integral(b) - integral(a) : 0.0;
            if (width > 0.0 && width < 1.0) p /= width;
            p *= norm_;
            const float d = p > 1e-12 ? (float)(10.0 * std::log10(p)) : kFloorDb;
            float& cur = db_[(std::size_t)c];
            cur = d >= cur ? d : std::max(d, cur - fall);
        }
    }

    int numColumns() const { return numColumns_; }
    float columnDb(int c) const { return db_[(std::size_t)c]; }
    double columnLoHz(int c) const { return edges_[(std::size_t)c]; }
    double columnHiHz(int c) const { return edges_[(std::size_t)c + 1]; }
    double columnCentreHz(int c) const { return std::sqrt(edges_[(std::size_t)c] * edges_[(std::size_t)c + 1]); }

private:
    // Integral of the straight-line-interpolated power from bin 0 to bin position x.
    double integral(double x) const
    {
        const std::size_t last = kFftSize / 2;
        if (x <= 0.0) return 0.0;
        if (x >= (double)last) return prefix_[last];
        const std::size_t i = (std::size_t)x;
        const double t = x - (double)i, p0 = power_[i], p1 = power_[i + 1];
        return prefix_[i] + t * p0 + 0.5 * t * t * (p1 - p0);
    }

    double sampleRate_ = 44100.0, binHz_ = 0.0, norm_ = 1.0;
    int numColumns_ = 0;
    Fft fft_;
    std::vector<float> ring_, window_, power_, db_;
    std::vector<std::complex<float>> buf_;
    std::vector<double> edges_, edgeBins_, prefix_;
    std::size_t write_ = 0;
};

} // namespace slowops::eqartist
