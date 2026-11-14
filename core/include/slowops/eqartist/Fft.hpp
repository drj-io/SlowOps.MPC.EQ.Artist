// In-place radix-2 complex FFT. Standard library only.
//
// prepare() allocates the twiddle and bit-reversal tables; forward() does no allocation and
// is safe on the audio thread.
#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace slowops::eqartist {

class Fft
{
public:
    // n must be a power of two.
    void prepare(std::size_t n)
    {
        n_ = n;
        bits_ = 0;
        while ((std::size_t(1) << bits_) < n) ++bits_;
        twiddle_.resize(n / 2);
        for (std::size_t k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * M_PI * (double)k / (double)n;
            twiddle_[k] = {(float)std::cos(a), (float)std::sin(a)};
        }
        rev_.resize(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            std::size_t r = 0;
            for (unsigned b = 0; b < bits_; ++b) r |= ((i >> b) & 1u) << (bits_ - 1 - b);
            rev_[i] = r;
        }
    }

    std::size_t size() const { return n_; }

    // data must hold size() values.
    void forward(std::complex<float>* data) const
    {
        for (std::size_t i = 0; i < n_; ++i)
            if (rev_[i] > i) std::swap(data[i], data[rev_[i]]);
        for (std::size_t len = 2; len <= n_; len <<= 1)
        {
            const std::size_t half = len / 2, step = n_ / len;
            for (std::size_t i = 0; i < n_; i += len)
                for (std::size_t k = 0; k < half; ++k)
                {
                    const std::complex<float> t = twiddle_[k * step] * data[i + k + half];
                    data[i + k + half] = data[i + k] - t;
                    data[i + k] += t;
                }
        }
    }

private:
    std::size_t n_ = 0;
    unsigned bits_ = 0;
    std::vector<std::complex<float>> twiddle_;
    std::vector<std::size_t> rev_;
};

} // namespace slowops::eqartist
