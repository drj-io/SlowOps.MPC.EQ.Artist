// scenario_host.cpp - drives the real Curve UI Test binary through the VST2 ABI (as the MPC does) and
// checks that its diagnostics report what they claim. It cannot test the touchscreen itself: that
// needs the MPC (see test-results/live3.md).
//
// Build: c++ -std=c++17 -O2 -I ../third_party/mpc-plugin-builder-sdk/framework tests/scenario_host.cpp -o build/scenario_host
// Run:   build/scenario_host build/native/curve-ui-test.dylib

#include "mpc_vst2_abi.h"

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

enum P { kLevel, kPadAX, kPadAY, kPadBX, kPadBY, kPadCX, kPadCY, kPadDX, kPadDY, kPadEX, kPadEY,
         kAutoMove, kReset, kRate, kLastPad, kGap, kMinX, kMaxX, kMinY, kMaxY, kSlider1, kSlider2,
         kBellX, kBellY, kBellQ, kStrip, kRefresh, kSweep, kCurveRate, kCurve0, kShade0 = kCurve0 + 64,
         kLineValue = kShade0 + 32, kLineSweep, kLine0, kEnvA = kLine0 + 152, kEnvD, kEnvS, kEnvR, kEnvShA, kEnvShD,
         kEnvShR, kPit0, kAdA = kPit0 + 9, kAdD, kAdEnd, kEnvSweep, kWidth, kBend, kRul0, kNum = kRul0 + 9 };

// The analog bell (same prototype as core/Filters.hpp), for checking the curve columns.
static double bellDb(double f0, double g, double q, double f)
{
    const double A = std::pow(10.0, g / 40.0), x = f / f0;
    const double nr = 1 - x * x, ni = A / q * x, dr = 1 - x * x, di = x / (A * q);
    return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
}


static VstIntPtr hostCallback(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void* ptr, float)
{
    switch (opcode)
    {
        case audioMasterVersion: return 2400;
        case audioMasterGetVendorString: if (ptr) std::strcpy((char*)ptr, "MPC 3"); return 1;
        case audioMasterGetProductString: if (ptr) std::strcpy((char*)ptr, "MPC 3"); return 1;
        default: return 0;
    }
}

typedef AEffect* (*MainProc)(audioMasterCallback);
static MainProc g_main = nullptr;
static constexpr double SR = 44100.0;

struct Plug
{
    AEffect* e = nullptr;
    VstIntPtr disp(VstInt32 op, VstInt32 idx = 0, VstIntPtr v = 0, void* p = nullptr, float o = 0.0f) { return e->dispatcher(e, op, idx, v, p, o); }
    Plug()
    {
        e = g_main(hostCallback);
        disp(effSetSampleRate, 0, 0, nullptr, (float)SR);
        disp(effSetBlockSize, 0, 512);
        disp(effOpen);
        disp(effMainsChanged, 0, 1);
        const float p0 = e->getParameter(e, 0);
        e->setParameter(e, 0, p0 >= 0.5f ? 0.0f : 1.0f);
        e->setParameter(e, 0, p0);
        disp(effStartProcess);
    }
    ~Plug() { disp(effStopProcess); disp(effMainsChanged, 0, 0); disp(effClose); }
    void set(int i, float v) { e->setParameter(e, i, v); }
    float get(int i) { return e->getParameter(e, i); }
    std::string text(int i) { char buf[256] = {0}; disp(effGetParamDisplay, i, 0, buf); return buf; }
    float peak = 0;
    void block(int n = 128)
    {
        std::vector<float> l((size_t)n, 0.5f), r((size_t)n, 0.5f);
        float* io[2] = {l.data(), r.data()};
        e->processReplacing(e, io, io, n);
        peak = l[0];
    }
    void run(double sec) { for (int k = 0; k < (int)(sec * SR) / 128; ++k) block(); }
};

static double colDb(Plug& p, int c) { return p.get(kCurve0 + c) * 36.0 - 18.0; }

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plugin binary>\n", argv[0]); return 2; }
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    g_main = (MainProc)dlsym(lib, "VSTPluginMain");
    if (!g_main) { std::fprintf(stderr, "no VSTPluginMain\n"); return 2; }
    {
        Plug p;
        CHECK(p.e->numParams == kNum, "%d params, expected %d", p.e->numParams, kNum);

        std::printf("== audio passes through\n");
        p.block();
        CHECK(std::fabs(p.peak - 0.5f) < 1e-6f, "output %.6f, input 0.5", p.peak);

        std::printf("== a drag on Pad A: Updates/s, Last Pad, Gap, Min/Max\n");
        // 60 touch updates over one second, x from 0.1 to 0.9, y from 0.2 to 0.8.
        for (int k = 0; k < 60; ++k)
        {
            p.set(kPadAX, 0.1f + 0.8f * k / 59.0f);
            p.set(kPadAY, 0.2f + 0.6f * k / 59.0f);
            for (int b = 0; b < (int)(SR / 60 / 128); ++b) p.block();
        }
        p.run(0.06);
        CHECK(p.text(kLastPad) == "A", "Last Pad reads '%s'", p.text(kLastPad).c_str());
        CHECK(std::fabs(p.get(kMinX) - 0.1f) < 0.002f && std::fabs(p.get(kMaxX) - 0.9f) < 0.002f, "X range %.3f..%.3f", p.get(kMinX), p.get(kMaxX));
        CHECK(std::fabs(p.get(kMinY) - 0.2f) < 0.002f && std::fabs(p.get(kMaxY) - 0.8f) < 0.002f, "Y range %.3f..%.3f", p.get(kMinY), p.get(kMaxY));
        const int gapNow = std::atoi(p.text(kGap).c_str());
        p.run(0.5);   // past the 1 s mark, when Updates/s is published
        const int gapLater = std::atoi(p.text(kGap).c_str());
        CHECK(gapNow < 120 && gapLater >= 450 && gapLater <= 600, "Gap ms %d right after, %d 0.5 s later", gapNow, gapLater);
        const int rate = std::atoi(p.text(kRate).c_str());
        CHECK(rate >= 100 && rate <= 120, "Updates/s reads %d, expected ~120 (60 X + 60 Y changes)", rate);

        std::printf("== other pads report themselves\n");
        p.set(kPadCX, 0.3f); p.run(0.06);
        CHECK(p.text(kLastPad) == "C", "Last Pad reads '%s' after Pad C", p.text(kLastPad).c_str());
        p.set(kPadEY, 0.3f); p.run(0.06);
        CHECK(p.text(kLastPad) == "E", "Last Pad reads '%s' after Pad E", p.text(kLastPad).c_str());

        std::printf("== Reset clears Min/Max\n");
        p.set(kReset, 1.0f); p.run(0.06);
        CHECK(p.get(kReset) < 0.5f, "Reset did not reset itself");
        CHECK(p.get(kMinX) == 1.0f && p.get(kMaxX) == 0.0f, "Min/Max not cleared");

        std::printf("== Auto Move moves Pad A and is not counted as touches\n");
        p.run(1.1);   // let Updates/s settle at 0
        p.set(kAutoMove, 1.0f);
        float lo = 1, hi = 0;
        for (int k = 0; k < (int)(2.2 * SR / 128); ++k) { p.block(); lo = std::fmin(lo, p.get(kPadAX)); hi = std::fmax(hi, p.get(kPadAX)); }
        CHECK(hi - lo > 0.5f, "Auto Move swept Pad A X only %.3f..%.3f", lo, hi);
        CHECK(std::atoi(p.text(kRate).c_str()) <= 1, "Auto Move counted as touches (Updates/s %s)", p.text(kRate).c_str());
        CHECK(p.get(kMinX) == 1.0f, "Auto Move changed Min/Max");
        p.set(kAutoMove, 0.0f);
        const float stopped = p.get(kPadAX);
        p.run(0.3);
        CHECK(p.get(kPadAX) == stopped, "Pad A still moving after Auto Move off");

        std::printf("== CURVE: the 64 columns follow the bell (extreme over each column's span, 0.5 dB steps)\n");
        p.set(kRefresh, 1.0f);   // All
        p.run(0.5);
        const double f0 = 20.0 * std::pow(1000.0, 0.5), g = (0.7 * 2 - 1) * 18, q = 1.5;
        double worst = 0, peak = -100;
        for (int c = 0; c < 64; ++c)
        {
            double ext = 0;
            for (int s = 0; s < 4; ++s)
            {
                const double d = bellDb(f0, g, q, 20.0 * std::pow(1000.0, (c + (s + 0.5) / 4.0) / 64.0));
                if (std::fabs(d) > std::fabs(ext)) ext = d;
            }
            worst = std::fmax(worst, std::fabs(colDb(p, c) - ext));
            peak = std::fmax(peak, colDb(p, c));
        }
        CHECK(worst <= 0.26, "curve columns off the bell by %.2f dB", worst);
        CHECK(std::fabs(peak - g) < 0.3, "curve peak %.2f dB, bell gain %.2f", peak, g);
        const double before = colDb(p, 10);
        p.set(kBellX, 0.15f); p.run(0.3);
        CHECK(std::fabs(colDb(p, 10) - before) > 3.0, "moving the bell did not change the curve");
        CHECK(std::fabs(p.get(kShade0 + 5) * 36.0 - 18.0 - colDb(p, 10)) < 1.5, "shade does not line up with the curve");

        std::printf("== CURVE: Refresh caps the updates sent (Sweep on)\n");
        p.set(kSweep, 1.0f);
        const char* names[] = {"8", "16", "24", "32", "All"};
        for (int r = 0; r < 5; ++r)
        {
            p.set(kRefresh, r / 4.0f);
            p.run(2.05);
            const int rate = std::atoi(p.text(kCurveRate).c_str());
            std::printf("  Refresh %-3s: %d curve updates per second\n", names[r], rate);
            static const int cap[5] = {8, 16, 24, 32, 96};
            CHECK(rate > 0 && rate <= cap[r] * 31, "Refresh %s: %d per second", names[r], rate);
        }
        p.set(kSweep, 0.0f);

        std::printf("== CURVE, LINE mode: a connected polyline through the bell\n");
        p.set(kRefresh, 1.0f);              // All
        p.set(kBellX, 0.5f); p.set(kBellY, 0.8f); p.set(kBellQ, 0.6f);   // 632 Hz, +10.8 dB
        p.set(kStrip, 1.0f);                // Line (3 choices)
        p.run(0.5);
        {
            const double f0 = 20.0 * std::pow(1000.0, 0.5), g = (0.8 * 2 - 1) * 18;
            const double q = 0.3 * std::pow(10.0 / 0.3, 0.6);   // log Q range 0.3..10
            double worstLevel = 0;
            bool joined = true;
            int prevRight = -1;
            static const int rises[31] = {-80, -64, -48, -40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80};
            int topLevel = 0;
            for (int c = 0; c < 152; ++c)
            {
                const int idx = (int)std::lround(p.get(kLine0 + c) * (289 * 31 - 1));
                const int left = idx / 31, right = left + rises[idx % 31];
                if (c > 0 && left != prevRight) joined = false;
                prevRight = right;
                topLevel = std::max(topLevel, std::max(left, right));
                // The vertex is the curve's extreme within half a column of the boundary.
                double ext = 0;
                for (int s2 = -2; s2 <= 2; ++s2)
                {
                    const double d = bellDb(f0, g, q, 20.0 * std::pow(1000.0, std::fmin(1.0, std::fmax(0.0, (c + s2 * 0.25) / 152.0))));
                    if (std::fabs(d) > std::fabs(ext)) ext = d;
                }
                worstLevel = std::fmax(worstLevel, std::fabs((left * 0.125 - 18.0) - std::fmax(-18.0, std::fmin(18.0, ext))));
            }
            CHECK(joined, "LINE segments do not share their end points");
            // Gentle slopes use 0.25 dB rises, steep ones coarser rises (up to 2 dB apart), so a vertex on
            // a steep side may sit up to half a rise step (1 dB) off; the next column corrects it.
            CHECK(worstLevel <= 1.01, "LINE vertices off the bell by %.2f dB", worstLevel);
            CHECK(std::fabs(topLevel * 0.125 - 18.0 - g) < 0.3, "LINE peak %.2f dB, bell gain %.2f", topLevel * 0.125 - 18.0, g);
            std::printf("  152 segments joined end to end; vertices within %.2f dB of the bell\n", worstLevel);
        }
        std::printf("== CURVE, LINE mode: a narrow +18 dB bell (the 0.3.2 screenshot case) reaches its peak\n");
        {
            // 282 Hz, +18 dB, Q 4.08: x = log1000(282/20), y = 1, Q normalised on the log 0.3..10 range.
            p.set(kBellX, (float)(std::log(282.0 / 20.0) / std::log(1000.0)));
            p.set(kBellY, 1.0f);
            p.set(kBellQ, (float)(std::log(4.08 / 0.3) / std::log(10.0 / 0.3)));
            p.run(0.5);
            static const int rises[31] = {-80, -64, -48, -40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80};
            int top = 0;
            for (int c = 0; c < 152; ++c)
            {
                const int idx = (int)std::lround(p.get(kLine0 + c) * (289 * 31 - 1));
                top = std::max(top, std::max(idx / 31, idx / 31 + rises[idx % 31]));
            }
            std::printf("  line peak %.2f dB (bell +18.0)\n", top * 0.125 - 18.0);
            CHECK(top * 0.125 - 18.0 >= 17.0, "the narrow bell's line peak is only %.2f dB (0.3.2 drew about +14)", top * 0.125 - 18.0);
        }
        p.set(kStrip, 0.0f);

        std::printf("== ENV: Env Sweep moves every envelope point\n");
        {
            float lo[kEnvSweep - kEnvA], hi[kEnvSweep - kEnvA];
            for (int i = 0; i < kEnvSweep - kEnvA; ++i) { lo[i] = 1; hi[i] = 0; }
            p.set(kEnvSweep, 1.0f);
            for (int k = 0; k < (int)(8.0 * SR / 128); ++k)
            {
                p.block();
                for (int i = 0; i < kEnvSweep - kEnvA; ++i) { lo[i] = std::fmin(lo[i], p.get(kEnvA + i)); hi[i] = std::fmax(hi[i], p.get(kEnvA + i)); }
            }
            p.set(kEnvSweep, 0.0f);
            int moved = 0;
            for (int i = 0; i < kEnvSweep - kEnvA; ++i) moved += hi[i] - lo[i] > 0.5f;
            CHECK(moved == kEnvSweep - kEnvA, "only %d of %d envelope points swept", moved, kEnvSweep - kEnvA);
        }

        std::printf("== ENV2: the ruler envelope follows Width\n");
        p.set(kWidth, 0.6f); p.block();
        CHECK(p.get(kRul0 + 1) == 0.6f && p.get(kRul0 + 7) == 0.6f && p.get(kRul0 + 2) == 0.9f, "ruler stages do not follow Width");

        std::printf("== LINES: Line Sweep moves Line Value\n");
        p.set(kLineSweep, 1.0f);
        float lo2 = 1, hi2 = 0;
        for (int k = 0; k < (int)(4.2 * SR / 128); ++k) { p.block(); lo2 = std::fmin(lo2, p.get(kLineValue)); hi2 = std::fmax(hi2, p.get(kLineValue)); }
        CHECK(hi2 - lo2 > 0.8f, "Line Value swept only %.2f..%.2f", lo2, hi2);
        p.set(kLineSweep, 0.0f);
    }
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
