// Runs CurveFit on a background thread so the audio thread never waits for it.
//
// Lock-free hand-off through one atomic state:
//   Idle --(audio: request())--> Requested --(worker picks it up)--> Working
//        --(worker done)--> Done --(audio: take())--> Idle
// The audio thread only writes the request while the state is Idle, and only reads the
// result while it is Done; the worker only touches them in between. The worker polls every
// 2 ms rather than waiting on a condition variable, so the audio thread never makes a call
// that could block.
#pragma once

#include "CurveFit.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace slowops::eqartist {

class FitWorker
{
public:
    static constexpr int kMaxColumns = 64;

    FitWorker() : thread_([this] { loop(); }) {}
    ~FitWorker()
    {
        quit_.store(true);
        thread_.join();
    }
    FitWorker(const FitWorker&) = delete;
    FitWorker& operator=(const FitWorker&) = delete;

    bool busy() const { return state_.load(std::memory_order_acquire) != kIdle; }

    // Audio thread. Returns false (and does nothing) if a fit is already running.
    bool request(const double* hz, const double* db, int columns, int maxBands)
    {
        if (state_.load(std::memory_order_acquire) != kIdle) return false;
        columns_ = std::min(columns, kMaxColumns);
        for (int c = 0; c < columns_; ++c) { hz_[(std::size_t)c] = hz[c]; db_[(std::size_t)c] = db[c]; }
        maxBands_ = maxBands;
        state_.store(kRequested, std::memory_order_release);
        return true;
    }

    // Audio thread. True once, when a result is ready; copies it out.
    bool take(FitResult& out)
    {
        if (state_.load(std::memory_order_acquire) != kDone) return false;
        out = result_;
        state_.store(kIdle, std::memory_order_release);
        return true;
    }

private:
    enum : int { kIdle, kRequested, kWorking, kDone };

    void loop()
    {
        // Allocated here, on the worker, never on the audio thread.
        std::vector<double> hz, db;
        CurveFit fit;
        while (!quit_.load())
        {
            int expected = kRequested;
            if (state_.compare_exchange_strong(expected, kWorking, std::memory_order_acq_rel))
            {
                hz.assign(hz_.begin(), hz_.begin() + columns_);
                db.assign(db_.begin(), db_.begin() + columns_);
                result_ = fit.fit(hz, db, maxBands_);
                state_.store(kDone, std::memory_order_release);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::atomic<int> state_{kIdle};
    std::atomic<bool> quit_{false};
    std::array<double, kMaxColumns> hz_{}, db_{};
    int columns_ = 0, maxBands_ = 8;
    FitResult result_;
    std::thread thread_;   // last: starts after every member above is constructed
};

} // namespace slowops::eqartist
