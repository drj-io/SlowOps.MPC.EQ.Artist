// scenario_host.cpp - drives the real native plugin build through the VST2 ABI (as the MPC does):
// draws a curve with the pen and checks that the audio follows the drawing on its own (no Apply).
// Also Undo / Redo of a stroke, Clear, Bypass, hand edits redrawing the curve, the response and
// spectrum displays, and clicks.
//
// Build: c++ -std=c++17 -O2 -I third_party/mpc-plugin-builder-sdk/framework tests/scenario_host.cpp -o build/scenario_host
// Run:   build/scenario_host plugin/build/native/eq-artist.dylib [out_dir]
//        (out_dir gets draw-demo.wav: pink noise, flat for 2 s, then with the drawn EQ)

#include "mpc_vst2_abi.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// Parameter indices (plugin/src/EqArtist.cpp).
enum P { kOutput, kClear, kUndo, kMaxBands, kAnalyzer, kAnalyze, kSpeed, kBypass, kPenX, kPenY, kFitInfo,
         kBand0, kDraw0 = kBand0 + 48, kSpec0 = kDraw0 + 32, kResp0 = kSpec0 + 32, kDispMode = kResp0 + 32, kNum };
static constexpr int kCols = 32;
static int bandOn(int b) { return kBand0 + b * 6; }

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

static double colHz(int c) { return 20.0 * std::pow(1000.0, (c + 0.5) / kCols); }

// The test drawing: a +9 dB bump around 200 Hz, a -8 dB dip around 3 kHz, flat elsewhere.
static double drawnDb(double hz)
{
    const double lo = std::log2(hz / 200.0), hi = std::log2(hz / 3000.0);
    return 9.0 * std::exp(-lo * lo / 0.5) - 8.0 * std::exp(-hi * hi / 0.3);
}

struct Plug
{
    AEffect* e = nullptr;
    double phase = 0.0;
    std::vector<float> recL, recR;
    bool recording = false;
    std::mt19937 rng{5};
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

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
    std::string text(int i) { char buf[256] = {0}; disp(effGetParamDisplay, i, 0, buf); return buf; }
    float drawDb(int c) { return get(kDraw0 + c) * 36.0f - 18.0f; }
    float respDb(int c) { return get(kResp0 + c) * 36.0f - 18.0f; }

    // A drum-like test signal: a pink-noise burst with a fast decay four times a second, plus a
    // bass note, so every spectrum column keeps rising and falling as with music.
    long beatT = 0;
    void beatBlock(int n = 128)
    {
        std::vector<float> l((size_t)n), r((size_t)n);
        std::normal_distribution<float> g(0.0f, 0.3f);
        for (int i = 0; i < n; ++i, ++beatT)
        {
            const double t = std::fmod((double)beatT / SR, 0.25);
            const float env = (float)std::exp(-t * 30.0);
            const float w = g(rng);
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            l[(size_t)i] = r[(size_t)i] = env * (b0 + b1 + b2 + b3 + w * 0.5f) * 0.3f
                                         + 0.2f * (float)std::sin(2.0 * M_PI * 55.0 * (double)beatT / SR);
        }
        float* io[2] = {l.data(), r.data()};
        e->processReplacing(e, io, io, n);
    }

    // One block: a sine at hz, or pink noise when hz < 0, or silence when hz == 0.
    float lastPeak = 0.0f, maxStep = 0.0f;
    float prevOut = 0.0f;
    void block(double hz, int n = 128, float amp = 0.25f)
    {
        std::vector<float> l((size_t)n), r((size_t)n);
        std::normal_distribution<float> g(0.0f, 0.05f);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            if (hz > 0) { v = amp * (float)std::sin(phase); phase += 2.0 * M_PI * hz / SR; }
            else if (hz < 0)
            {
                const float w = g(rng);
                b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
                b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
                b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
                v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
                b6 = w * 0.115926f;
            }
            l[(size_t)i] = r[(size_t)i] = v;
        }
        float* io[2] = {l.data(), r.data()};
        e->processReplacing(e, io, io, n);
        lastPeak = 0.0f;
        for (float v : l)
        {
            lastPeak = std::fmax(lastPeak, std::fabs(v));
            maxStep = std::fmax(maxStep, std::fabs(v - prevOut));
            prevOut = v;
        }
        if (recording) { recL.insert(recL.end(), l.begin(), l.end()); recR.insert(recR.end(), r.begin(), r.end()); }
    }
    void run(double sec, double hz = 0.0) { for (int k = 0; k < (int)(sec * SR) / 128; ++k) block(hz); }

    // Steady-state gain at hz, in dB.
    double gainDb(double hz)
    {
        phase = 0.0;
        run(0.25, hz);
        float peak = 0.0f;
        for (int k = 0; k < 40; ++k) { block(hz); peak = std::fmax(peak, lastPeak); }
        return 20.0 * std::log10(peak / 0.25);
    }

    // Let the fitter (its own thread) catch up: keep processing in real time for a while.
    void settle(double seconds = 1.0, double hz = 0.0)
    {
        for (int k = 0; k < (int)(seconds * 200); ++k) { block(hz); usleep(5000); }
    }
    int bandsOn() { int on = 0; for (int b = 0; b < 8; ++b) on += get(bandOn(b)) > 0.5f; return on; }
};

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t n = (uint32_t)l.size(), data = n * 4, rate = 44100;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(data);
    for (uint32_t i = 0; i < n; ++i)
        for (float s : {l[i], r[i]}) { const float c = s > 1 ? 1 : (s < -1 ? -1 : s); u16((uint16_t)(int16_t)std::lround(c * 32767.0f)); }
    std::fclose(f);
}

// Draw the test curve with the pen: one pen point per column, a block between points.
static void penDraw(Plug& p)
{
    for (int c = 0; c < kCols; ++c)
    {
        const double db = drawnDb(colHz(c));
        p.set(kPenX, (c + 0.5f) / kCols);
        p.set(kPenY, (float)((db / 18.0 + 1.0) / 2.0));
        p.block(0.0);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plugin binary> [out_dir]\n", argv[0]); return 2; }
    const std::string outDir = argc > 2 ? argv[2] : "";
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    g_main = (MainProc)dlsym(lib, "VSTPluginMain");
    if (!g_main) g_main = (MainProc)dlsym(lib, "main");
    if (!g_main) { std::fprintf(stderr, "no VSTPluginMain\n"); return 2; }
    {
        Plug p;
        CHECK(p.e->numParams == kNum, "plugin has %d params, this host expects %d", p.e->numParams, kNum);

        std::printf("== a fresh instance is flat\n");
        for (double hz : {100.0, 1000.0, 8000.0})
        {
            const double g = p.gainDb(hz);
            CHECK(std::fabs(g) < 0.05, "%.0f Hz: %.2f dB with no bands", hz, g);
        }
        CHECK(p.text(kFitInfo) == "Draw on the graph", "fit status reads '%s'", p.text(kFitInfo).c_str());

        std::printf("== the sound follows the drawing while you draw (no Apply)\n");
        // Draw the left half, then hold still: the bump at 200 Hz is already in the sound.
        for (int c = 0; c < 16; ++c)
        {
            p.set(kPenX, (c + 0.5f) / kCols);
            p.set(kPenY, (float)((drawnDb(colHz(c)) / 18.0 + 1.0) / 2.0));
            p.block(0.0);
            usleep(2000);
        }
        p.settle(0.3);
        CHECK(p.bandsOn() >= 1, "no bands while drawing");
        CHECK(std::fabs(p.gainDb(200.0) - drawnDb(200.0)) < 1.5, "mid-drawing: %.2f dB at 200 Hz, drew %.2f",
              p.gainDb(200.0), drawnDb(200.0));
        p.set(kClear, 1.0f); p.block(0.0);
        p.settle(0.5);
        CHECK(p.bandsOn() == 0, "Clear left %d bands on", p.bandsOn());
        CHECK(std::fabs(p.gainDb(200.0)) < 0.1, "Clear is not flat");
        CHECK(p.text(kFitInfo) == "Flat", "fit status after Clear reads '%s'", p.text(kFitInfo).c_str());

        std::printf("== one stroke across the graph: the audio matches the drawing\n");
        p.recording = true;
        p.run(2.0, -1.0);   // pink noise, flat
        p.run(0.3);         // a pause: the next touch is a new stroke
        penDraw(p);
        p.settle(1.0, -1.0);
        p.run(2.0, -1.0);   // pink noise, EQ'd
        p.recording = false;
        double worstDraw = 0;
        for (int c = 0; c < kCols; ++c) worstDraw = std::fmax(worstDraw, std::fabs(p.drawDb(c) - drawnDb(colHz(c))));
        CHECK(worstDraw < 1.0, "the pen drawing is off the intended curve by %.2f dB", worstDraw);
        std::printf("  fit: %s\n", p.text(kFitInfo).c_str());
        for (int b = 0; b < 8; ++b)
            if (p.get(bandOn(b)) > 0.5f)
                std::printf("    B%d %-10s %-10s %-9s Q %s\n", b + 1, p.text(kBand0 + b * 6 + 1).c_str(), p.text(kBand0 + b * 6 + 2).c_str(),
                            p.text(kBand0 + b * 6 + 3).c_str(), p.text(kBand0 + b * 6 + 4).c_str());
        CHECK(p.bandsOn() >= 2, "%d bands on", p.bandsOn());
        double worst = 0, worstHz = 0;
        for (int c : {4, 8, 11, 13, 15, 18, 20, 22, 24, 26, 30})
        {
            const double hz = colHz(c), want = drawnDb(hz), got = p.gainDb(hz);
            if (std::fabs(got - want) > worst) { worst = std::fabs(got - want); worstHz = hz; }
        }
        std::printf("  measured output vs drawing: worst %.2f dB (at %.0f Hz)\n", worst, worstHz);
        CHECK(worst < 1.5, "the audio misses the drawing by %.2f dB at %.0f Hz", worst, worstHz);
        double worstResp = 0;
        for (int c = 0; c < kCols; ++c) worstResp = std::fmax(worstResp, std::fabs(p.respDb(c) - drawnDb(colHz(c))));
        CHECK(worstResp < 1.6, "the response display misses the drawing by %.2f dB", worstResp);
        if (!outDir.empty()) writeWav(outDir + "/draw-demo.wav", p.recL, p.recR);

        std::printf("== no click when Undo / Redo swap the EQ\n");
        p.run(0.3, 1000.0);
        p.maxStep = 0; p.run(0.3, 1000.0);
        const float steady = p.maxStep;
        p.maxStep = 0;
        p.set(kUndo, 1.0f); p.run(0.2, 1000.0);
        p.set(kUndo, 1.0f); p.run(0.2, 1000.0);
        const double ceiling = steady * std::pow(10.0, std::fabs(drawnDb(1000.0)) / 20.0) * 1.3;
        CHECK(p.maxStep < ceiling, "step %.4f during Undo/Redo, steady-state step %.4f", p.maxStep, steady);

        std::printf("== Undo takes back the stroke (drawing and sound); Undo again redoes it\n");
        p.set(kUndo, 1.0f); p.block(0.0);
        p.settle(0.3);
        CHECK(p.bandsOn() == 0, "%d bands on after undoing the stroke", p.bandsOn());
        CHECK(std::fabs(p.gainDb(200.0)) < 0.1, "not flat after Undo");
        CHECK(std::fabs(p.drawDb(10)) < 0.01f, "the drawing was not undone");
        p.set(kUndo, 1.0f); p.block(0.0);
        p.settle(0.3);
        CHECK(std::fabs(p.gainDb(200.0) - drawnDb(200.0)) < 1.5, "Redo did not bring the bump back");
        CHECK(std::fabs(p.drawDb(10) - drawnDb(colHz(10))) < 1.0, "Redo did not bring the drawing back");

        std::printf("== Bypass, and the status line says so\n");
        p.set(kBypass, 1.0f);
        CHECK(std::fabs(p.gainDb(200.0)) < 0.05, "bypassed but %.2f dB at 200 Hz", p.gainDb(200.0));
        CHECK(p.text(kFitInfo) == "BYPASSED", "status reads '%s' while bypassed", p.text(kFitInfo).c_str());
        p.set(kBypass, 0.0f);
        p.block(0.0);

        std::printf("== turning a band knob redraws the drawing from the EQ you hear\n");
        int bell = -1;
        for (int b = 0; b < 8 && bell < 0; ++b) if (p.get(bandOn(b)) > 0.5f && p.text(kBand0 + b * 6 + 1) == "Bell") bell = b;
        CHECK(bell >= 0, "no bell band to edit");
        if (bell >= 0)
        {
            p.set(kBand0 + bell * 6 + 3, (0.0f + 24.0f) / 48.0f);   // gain to 0 dB
            p.settle(0.6);
            double worstSync = 0;
            for (int c = 0; c < kCols; ++c) worstSync = std::fmax(worstSync, std::fabs(p.drawDb(c) - p.respDb(c)));
            CHECK(worstSync < 1.0, "the drawing is %.2f dB away from the EQ after a knob edit", worstSync);
            CHECK(p.bandsOn() >= 1, "the knob edit was overwritten by a refit");
        }

        std::printf("== display updates sent to the MPC with music-like input\n");
        {
            p.set(kAnalyzer, 1.0f); p.set(kAnalyze, 0.0f);
            for (int k = 0; k < (int)(1.0 * SR / 128); ++k) p.beatBlock();
            const long before = automateTotal(kSpec0, kResp0 + kCols);
            for (int k = 0; k < (int)(4.0 * SR / 128); ++k) p.beatBlock();
            const double perSec = (automateTotal(kSpec0, kResp0 + kCols) - before) / 4.0;
            std::printf("  spectrum + curve: %.0f updates per second\n", perSec);
            CHECK(perSec <= 400.0, "%.0f display updates per second (budget 360)", perSec);
            CHECK(perSec > 50.0, "the spectrum hardly moves (%.0f updates per second)", perSec);
            // TEST tab modes: Light sends a quarter; Silent sends nothing but still moves the values.
            p.set(kDispMode, 0.5f);
            long b2 = automateTotal(kSpec0, kResp0 + kCols);
            for (int k = 0; k < (int)(4.0 * SR / 128); ++k) p.beatBlock();
            const double light = (automateTotal(kSpec0, kResp0 + kCols) - b2) / 4.0;
            std::printf("  Light: %.0f updates per second\n", light);
            CHECK(light <= 61.0 && light > 10.0, "Light sends %.0f per second", light);
            p.set(kDispMode, 1.0f);
            b2 = automateTotal(kSpec0, kResp0 + kCols);
            float moved = 0, last = p.get(kSpec0 + 10);
            for (int k = 0; k < (int)(2.0 * SR / 128); ++k) { p.beatBlock(); moved += std::fabs(p.get(kSpec0 + 10) - last); last = p.get(kSpec0 + 10); }
            CHECK(automateTotal(kSpec0, kResp0 + kCols) == b2, "Silent still sent updates");
            CHECK(moved > 0.5f, "Silent: the spectrum values did not move");
            p.set(kDispMode, 0.0f);
        }

        std::printf("== the spectrum display follows the input\n");
        p.run(0.5, 1000.0);
        int best = 0;
        for (int c = 1; c < kCols; ++c) if (p.get(kSpec0 + c) > p.get(kSpec0 + best)) best = c;
        CHECK(best == 18, "1 kHz lights column %d, expected 19 (976..1210 Hz)", best + 1);
        p.set(kAnalyze, 1.0f); p.run(0.5, 1000.0);   // Post: same column (the EQ is nearly flat at 1 kHz)
        best = 0;
        for (int c = 1; c < kCols; ++c) if (p.get(kSpec0 + c) > p.get(kSpec0 + best)) best = c;
        CHECK(best == 18, "Post: 1 kHz lights column %d", best + 1);
        p.set(kAnalyzer, 0.0f); p.run(0.2, 1000.0);
        bool dark = true;
        for (int c = 0; c < kCols; ++c) dark = dark && p.get(kSpec0 + c) == 0.0f;
        CHECK(dark, "Analyzer off still shows a spectrum");
    }
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
