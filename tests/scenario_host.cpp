// scenario_host.cpp - drives the real native plugin build through the VST2 ABI (as the MPC does)
// and checks the spike's behaviour: pen painting, swipe counting, spectrum display, and how many
// display updates per second the plugin sends the host.
//
// Build: c++ -std=c++17 -O2 -I third_party/mpc-plugin-builder-sdk/framework tests/scenario_host.cpp -o build/scenario_host
// Run:   build/scenario_host plugin/build/native/eq-artist.dylib

#include "mpc_vst2_abi.h"

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// Parameter indices (plugin/src/EqArtist.cpp).
enum P { kOutput, kClear, kSource, kRate, kFlipY, kPenX, kPenY, kPenRate, kPenPts, kSwipeCols, kDraw0,
         kSpec0 = kDraw0 + 32, kNum = kSpec0 + 32 };
static constexpr int kCols = 32;

static long g_automate[512] = {};
static long automateTotal(int from, int to) { long s = 0; for (int i = from; i < to; ++i) s += g_automate[i]; return s; }

static VstIntPtr hostCallback(AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr, void* ptr, float)
{
    static VstTimeInfo ti{};
    switch (opcode)
    {
        case audioMasterAutomate: if (index >= 0 && index < 512) ++g_automate[index]; return 0;
        case audioMasterVersion: return 2400;
        case audioMasterGetTime: ti.sampleRate = 44100.0; ti.tempo = 120.0; ti.flags = kVstTempoValid; return (VstIntPtr)&ti;
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
    double phase = 0.0;
    VstIntPtr disp(VstInt32 op, VstInt32 idx = 0, VstIntPtr v = 0, void* p = nullptr, float o = 0.0f) { return e->dispatcher(e, op, idx, v, p, o); }
    Plug()
    {
        e = g_main(hostCallback);
        disp(effSetSampleRate, 0, 0, nullptr, (float)SR);
        disp(effSetBlockSize, 0, 512);
        disp(effOpen);
        disp(effSetProgram, 0, 0);
        disp(effMainsChanged, 0, 1);
        const float p0 = e->getParameter(e, 0);   // the MPC's parameter-0 nudge
        e->setParameter(e, 0, p0 >= 0.5f ? 0.0f : 1.0f);
        e->setParameter(e, 0, p0);
        disp(effStartProcess);
    }
    ~Plug() { disp(effStopProcess); disp(effMainsChanged, 0, 0); disp(effClose); }
    void set(int i, float v) { e->setParameter(e, i, v); }
    float get(int i) { return e->getParameter(e, i); }
    float drawDb(int c) { return get(kDraw0 + c) * 36.0f - 18.0f; }

    // One block of a sine at hz (0 = silence), processed in place like the MPC.
    float lastPeak = 0.0f;
    void block(double hz, int n = 128, float amp = 0.5f)
    {
        std::vector<float> l((size_t)n), r((size_t)n);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t)i] = r[(size_t)i] = hz > 0 ? amp * (float)std::sin(phase) : 0.0f;
            phase += 2.0 * M_PI * hz / SR;
        }
        float* io[2] = {l.data(), r.data()};
        e->processReplacing(e, io, io, n);
        lastPeak = 0.0f;
        for (float v : l) lastPeak = std::fmax(lastPeak, std::fabs(v));
    }
    void run(double sec, double hz = 0.0) { for (int k = 0; k < (int)(sec * SR) / 128; ++k) block(hz); }
};

static void testPassThrough(Plug& p)
{
    std::printf("== pass-through at 0 dB Output\n");
    p.run(0.2, 1000.0);
    CHECK(std::fabs(p.lastPeak - 0.5f) < 0.01f, "peak %.4f, expected 0.5", p.lastPeak);
}

static void testPen(Plug& p)
{
    std::printf("== pen paints the columns and joins up skipped ones\n");
    p.set(kClear, 1.0f); p.run(0.05);
    // A fast stroke left to right: 8 points, each jumping 4 columns, from -12 dB to +12 dB.
    for (int k = 0; k < 8; ++k)
    {
        const float x = (k * 4 + 0.5f) / kCols;
        const float y = (float)k / 7.0f * (24.0f / 36.0f) + (6.0f / 36.0f);   // -12..+12 dB
        p.set(kPenX, x); p.set(kPenY, y);
        p.block(0.0);
    }
    int painted = 0;
    for (int c = 0; c <= 28; ++c) if (std::fabs(p.drawDb(c)) > 0.01f) ++painted;
    CHECK(painted >= 28, "only %d of columns 0..28 painted (gaps not filled)", painted);
    bool monotonic = true;
    for (int c = 1; c <= 28; ++c) if (p.drawDb(c) < p.drawDb(c - 1) - 0.06f) monotonic = false;
    CHECK(monotonic, "a rising stroke should paint a rising curve");
    CHECK(std::fabs(p.drawDb(0) + 12.0f) < 0.2f && std::fabs(p.drawDb(28) - 12.0f) < 0.2f,
          "ends at %.2f / %.2f dB, expected -12 / +12", p.drawDb(0), p.drawDb(28));
    CHECK(std::lround(p.get(kPenPts) * 9999.0f) == 8, "Pen Pts reads %ld, expected 8", std::lround(p.get(kPenPts) * 9999.0f));
    // Column 31 was never reached.
    CHECK(std::fabs(p.drawDb(31)) < 0.01f, "column 32 painted (%.2f dB) but the pen never got there", p.drawDb(31));

    std::printf("== a pause starts a new stroke (no line joining the strokes)\n");
    p.run(0.3);
    p.set(kPenX, 31.5f / kCols); p.set(kPenY, 1.0f); p.block(0.0);
    CHECK(std::fabs(p.drawDb(30) - p.drawDb(29)) < 2.0f, "a new stroke joined itself to the old one");
    CHECK(std::fabs(p.drawDb(31) - 18.0f) < 0.2f, "new stroke's point reads %.2f dB, expected +18", p.drawDb(31));

    std::printf("== Flip Y inverts the pen\n");
    p.set(kFlipY, 1.0f); p.run(0.3);
    p.set(kPenX, 10.5f / kCols); p.set(kPenY, 1.0f); p.block(0.0);
    CHECK(std::fabs(p.drawDb(10) + 18.0f) < 0.2f, "flipped top reads %.2f dB, expected -18", p.drawDb(10));
    p.set(kFlipY, 0.0f);

    std::printf("== Clear flattens and resets itself\n");
    p.set(kClear, 1.0f); p.block(0.0);
    bool flat = true;
    for (int c = 0; c < kCols; ++c) if (std::fabs(p.drawDb(c)) > 0.01f) flat = false;
    CHECK(flat, "Clear left a column off 0 dB");
    CHECK(p.get(kClear) < 0.5f, "Clear did not reset itself");
}

static void testSwipe(Plug& p)
{
    std::printf("== Swipe Cols counts the columns one slider gesture changed\n");
    p.run(0.5);
    for (int c = 5; c < 12; ++c) { p.set(kDraw0 + c, 0.7f); p.block(0.0); }   // 7 columns, back to back
    CHECK(std::lround(p.get(kSwipeCols) * 32.0f) == 7, "reads %ld, expected 7", std::lround(p.get(kSwipeCols) * 32.0f));
    p.run(0.5);
    p.set(kDraw0 + 20, 0.2f); p.block(0.0);   // a new gesture after a pause
    CHECK(std::lround(p.get(kSwipeCols) * 32.0f) == 1, "reads %ld after a single tap, expected 1", std::lround(p.get(kSwipeCols) * 32.0f));
}

static void testSpectrumAndRate(Plug& p)
{
    std::printf("== spectrum display follows the input\n");
    p.set(kSource, 0.0f);
    p.run(0.5, 1000.0);
    int best = 0;
    for (int c = 1; c < kCols; ++c) if (p.get(kSpec0 + c) > p.get(kSpec0 + best)) best = c;
    // 1 kHz sits in column 17 of 32 (20 Hz * 1000^(c/32)).
    CHECK(best >= 16 && best <= 18, "1 kHz lights column %d", best + 1);
    CHECK(p.get(kSpec0 + 3) < p.get(kSpec0 + best) - 0.3f, "low columns are lit by a 1 kHz sine");

    std::printf("== display updates sent to the host (Source = Sweep, every column moving)\n");
    for (int rate = 0; rate < 3; ++rate)
    {
        p.set(kSource, 1.0f);
        p.set(kRate, rate / 2.0f);
        p.run(0.2);
        const long before = automateTotal(kSpec0, kSpec0 + kCols);
        p.run(4.0);   // one full sweep pass
        const double perSec = (automateTotal(kSpec0, kSpec0 + kCols) - before) / 4.0;
        const char* names[] = {"30 Hz", "15 Hz", "60 Hz"};
        std::printf("  Disp Rate %s: %.0f spectrum notifications per second\n", names[rate], perSec);
        CHECK(perSec > 10.0, "the sweep did not move the display");
    }
    p.set(kRate, 0.0f);
    p.set(kSource, 0.0f);
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plugin binary>\n", argv[0]); return 2; }
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    g_main = (MainProc)dlsym(lib, "VSTPluginMain");
    if (!g_main) g_main = (MainProc)dlsym(lib, "main");
    if (!g_main) { std::fprintf(stderr, "no VSTPluginMain\n"); return 2; }
    {
        Plug p;
        CHECK(p.e->numParams == kNum, "plugin has %d params, this host expects %d", p.e->numParams, kNum);
        testPassThrough(p);
        testPen(p);
        testSwipe(p);
        testSpectrumAndRate(p);
    }
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
