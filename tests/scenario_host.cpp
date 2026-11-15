// scenario_host.cpp - drives a real native plugin build (EQ Artist Dots or EQ Artist Lines) through the
// VST2 ABI, as the MPC does, and checks that the two pages edit one EQ:
//   * PARA: dragging a node, TYPE / Q, + / - change the measured audio; the drawing on DRAW follows.
//   * DRAW: a pen stroke changes the audio at once, is fitted to bands, and PARA shows those bands.
//   * Undo, Clear, Bypass, NONE, the curve displays, display-update budgets, the spectrum.
// Parameters are looked up by name, so the same test runs against both plugins.
//
// Build: c++ -std=c++17 -O2 -I third_party/mpc-plugin-builder-sdk/framework -I core/include tests/scenario_host.cpp -o build/scenario_host
// Run:   build/scenario_host plugins/<variant>/build/native/eq-artist-<variant>.dylib [out_dir]
//        (out_dir gets draw-demo.wav: pink noise, flat for 2 s, then with the drawn EQ)

#include "mpc_vst2_abi.h"
#include "slowops/eqartist/Filters.hpp"
#include "slowops/eqartist/LineStrip.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace eq = slowops::eqartist;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static long g_automate[2048] = {};

static VstIntPtr hostCallback(AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr, void* ptr, float)
{
    static VstTimeInfo ti{};
    switch (opcode)
    {
        case audioMasterAutomate: if (index >= 0 && index < 2048) ++g_automate[index]; return 0;
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
static constexpr int kCols = 32;

static double colHz(int c) { return 20.0 * std::pow(1000.0, (c + 0.5) / kCols); }

// The test drawing: a +9 dB bump around 200 Hz, a -8 dB dip around 3 kHz, flat elsewhere.
static double drawnDb(double hz)
{
    const double lo = std::log2(hz / 200.0), hi = std::log2(hz / 3000.0);
    return 9.0 * std::exp(-lo * lo / 0.5) - 8.0 * std::exp(-hi * hi / 0.3);
}

// Plain value -> normalised, for the plugin's parameter ranges.
static float nGain(double db) { return (float)((db + 18.0) / 36.0); }
static float nQ(double q) { return (float)(std::log(q / 0.1) / std::log(180.0)); }
static float nSel(int band) { return band / 8.0f; }        // 0 = none
static float nType(int t) { return t / 5.0f; }

struct Plug
{
    AEffect* e = nullptr;
    std::map<std::string, int> idx;
    double phase = 0.0;
    std::vector<float> recL, recR;
    bool recording = false;
    std::mt19937 rng{5};
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    VstIntPtr disp(VstInt32 op, VstInt32 i = 0, VstIntPtr v = 0, void* p = nullptr, float o = 0.0f) { return e->dispatcher(e, op, i, v, p, o); }
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
        for (int i = 0; i < e->numParams; ++i) { char name[256] = {0}; disp(effGetParamName, i, 0, name); idx[name] = i; }
    }
    ~Plug() { disp(effStopProcess); disp(effMainsChanged, 0, 0); disp(effClose); }

    int I(const std::string& name) const
    {
        const auto it = idx.find(name);
        if (it == idx.end()) { std::printf("  no parameter named '%s'\n", name.c_str()); std::exit(2); }
        return it->second;
    }
    std::string B(int band, const char* field) const { return "B" + std::to_string(band) + " " + field; }
    void set(const std::string& name, float v) { e->setParameter(e, I(name), v); }
    float get(const std::string& name) { return e->getParameter(e, I(name)); }
    float getI(int i) { return e->getParameter(e, i); }
    std::string text(const std::string& name) { char buf[256] = {0}; disp(effGetParamDisplay, I(name), 0, buf); return buf; }
    int bands() { return (int)std::lround(get("Bands") * 7) + 1; }
    int sel() { return (int)std::lround(get("Sel Band") * 8); }
    double freq(int band) { return eq::GraphMap::xToHz(get(B(band, "Freq"))); }
    double gain(int band) { return get(B(band, "Gain")) * 36.0 - 18.0; }
    double q(int band) { return 0.1 * std::pow(180.0, get(B(band, "Q"))); }
    float drawDb(int c) { return get("Draw " + std::to_string(c + 1)) * 36.0f - 18.0f; }
    int bandsOn() { int n = 0; for (int b = 1; b <= 8; ++b) n += get(B(b, "On")) > 0.5f; return n; }

    // The bands as the plugin has them (for the analog reference).
    std::vector<eq::BandParams> bandList()
    {
        std::vector<eq::BandParams> out;
        for (int b = 1; b <= bands(); ++b)
        {
            eq::BandParams p;
            p.on = true;
            p.type = (eq::BandType)std::lround(get(B(b, "Type")) * 5);
            p.freq = freq(b); p.gainDb = eq::typeHasGain(p.type) ? gain(b) : 0.0; p.q = q(b);
            p.slope = (int)std::lround(get(B(b, "Slope")) * 5);
            out.push_back(p);
        }
        return out;
    }

    // One block: a sine at hz, or pink noise when hz < 0, or silence when hz == 0.
    float lastPeak = 0.0f, maxStep = 0.0f, prevOut = 0.0f;
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

    // PARA's pen: a new touch (after a pause) at graph position x, y, then optional drag points.
    void paraTouch(double x, double y, double hz = 0.0)
    {
        run(0.4, hz);
        set("Para Pen X", (float)x); set("Para Pen Y", (float)y); block(hz);
    }
    void paraDrag(double x, double y, double hz = 0.0)
    {
        set("Para Pen X", (float)x); set("Para Pen Y", (float)y); block(hz);
    }
};

static double analogSum(const std::vector<eq::BandParams>& bands, double hz)
{
    double s = 0.0;
    for (const auto& b : bands) s += eq::analogDb(b, hz);
    return s;
}

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

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plugin binary> [out_dir]\n", argv[0]); return 2; }
    const std::string outDir = argc > 2 ? argv[2] : "";
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    g_main = (MainProc)dlsym(lib, "VSTPluginMain");
    if (!g_main) { std::fprintf(stderr, "no VSTPluginMain\n"); return 2; }
    {
        Plug p;
        int cols = 0;
        while (p.idx.count("Curve " + std::to_string(cols + 1))) ++cols;
        const bool lines = cols == eq::LineStrip76::kCols;
        std::printf("%d parameters; curves are %s (%d columns)\n", p.e->numParams, lines ? "lines" : "dots", cols);
        CHECK(cols == 76 || cols == 64, "unexpected curve column count %d", cols);
        const int curve0 = p.I("Curve 1"), front0 = p.I("Front 1"), fill0 = p.I("Fill 1");
        // What a curve display should hold for a response function.
        auto encode = [&](auto fn, float* out) {
            if (lines) eq::LineStrip76::encode(fn, out); else eq::ShadeStrip::encode(fn, out);
        };
        auto curveDiff = [&](int first, auto fn) {
            float want[76];
            encode(fn, want);
            int off = 0;
            for (int c = 0; c < cols; ++c) off += std::fabs(p.getI(first + c) - want[c]) > 1e-4f;
            return off;
        };

        std::printf("== a fresh instance: two flat bands, flat audio\n");
        for (double hz : {100.0, 1000.0, 8000.0})
        {
            const double g = p.gainDb(hz);
            CHECK(std::fabs(g) < 0.05, "%.0f Hz: %.2f dB on a fresh instance", hz, g);
        }
        CHECK(p.bands() == 2 && p.bandsOn() == 2, "starts with %d bands (%d on)", p.bands(), p.bandsOn());
        CHECK(p.text("Fit") == "Draw on the graph", "DRAW status reads '%s'", p.text("Fit").c_str());

        std::printf("== PARA: dragging a node changes the sound\n");
        const double n2x = eq::GraphMap::hzToX(p.freq(2));
        p.paraTouch(n2x + 15.0 / 1216.0, 0.5);          // 15 px right of node 2
        CHECK(p.sel() == 2, "touching node 2 selected band %d", p.sel());
        CHECK(std::fabs(p.gain(2)) < 0.05 && std::fabs(p.freq(2) / 2500.0 - 1.0) < 0.01, "grabbing made the node jump");
        p.paraDrag(n2x + 15.0 / 1216.0 + 0.1, 0.5 + 6.0 / 36.0);   // right, and up 6 dB
        const double f2 = eq::GraphMap::xToHz(n2x + 0.1);
        CHECK(std::fabs(p.freq(2) / f2 - 1.0) < 0.01, "drag: band 2 at %.0f Hz, expected %.0f", p.freq(2), f2);
        CHECK(std::fabs(p.gain(2) - 6.0) < 0.1, "drag: band 2 gain %.2f dB, expected +6", p.gain(2));
        const double heard = p.gainDb(f2);
        std::printf("  node at %.0f Hz, +6.0 dB: audio measures %+.2f dB there\n", f2, heard);
        CHECK(std::fabs(heard - 6.0) < 0.5, "the audio at the node is %.2f dB, expected +6", heard);
        CHECK(std::fabs(p.get("Sel Freq") - p.get("B2 Freq")) < 1e-6 && std::fabs(p.get("Sel Gain") - p.get("B2 Gain")) < 1e-6,
              "Sel Freq / Gain do not follow band 2");

        std::printf("== PARA: no click while a node is dragged\n");
        {
            p.run(0.3, 1000.0);
            p.maxStep = 0; p.run(0.3, 1000.0);
            const float steady = p.maxStep;
            p.maxStep = 0;
            p.paraTouch(eq::GraphMap::hzToX(p.freq(2)), eq::GraphMap::dbToY(p.gain(2)), 1000.0);   // the sine keeps playing
            for (int k = 1; k <= 30; ++k)   // down to 1 kHz and up to +12 dB, over 30 pen updates
            {
                const double t = k / 30.0;
                p.paraDrag(eq::GraphMap::hzToX(f2) + (eq::GraphMap::hzToX(1000.0) - eq::GraphMap::hzToX(f2)) * t,
                           eq::GraphMap::dbToY(6.0 + 6.0 * t), 1000.0);
                for (int b = 0; b < 4; ++b) p.block(1000.0);
            }
            p.run(0.3, 1000.0);
            CHECK(p.maxStep < steady * std::pow(10.0, 12.0 / 20.0) * 1.3, "step %.4f while dragging (steady %.4f)", p.maxStep, steady);
            CHECK(std::fabs(p.gainDb(1000.0) - 12.0) < 0.5, "after the drag: %.2f dB at 1 kHz, expected +12", p.gainDb(1000.0));
        }

        std::printf("== PARA: a touch on empty space moves nothing\n");
        {
            const float before1 = p.get("B1 Freq"), before2 = p.get("B2 Freq");
            p.paraTouch(0.97, 0.05);
            p.paraDrag(0.90, 0.10);
            CHECK(p.get("B1 Freq") == before1 && p.get("B2 Freq") == before2, "a touch on empty space moved a band");
        }

        std::printf("== PARA: Q and TYPE act on the selected band, and on the sound\n");
        p.set("Sel Q", nQ(4.0)); p.block(0.0); p.block(0.0);
        CHECK(std::fabs(p.q(2) - 4.0) < 0.01, "Sel Q did not reach band 2 (Q %.2f)", p.q(2));
        p.set("Sel Band", nSel(1)); p.block(0.0); p.block(0.0);
        CHECK(std::fabs(p.get("Sel Freq") - p.get("B1 Freq")) < 1e-6, "selecting band 1 did not load its frequency");
        p.set("Sel Type", nType(3)); p.block(0.0); p.block(0.0);   // Low Cut at 150 Hz, 12 dB/oct
        {
            const double g40 = p.gainDb(40.0);
            CHECK(g40 < -15.0, "band 1 as a low cut: %.1f dB at 40 Hz", g40);
            const std::string info = p.text("Para Info");
            CHECK(info.find("Lo Cut") != std::string::npos, "PARA status reads '%s'", info.c_str());
            // SLOPE: 12 -> 48 dB/oct changes the sound an octave below the cut, and the curves on screen.
            p.run(1.0);
            std::vector<float> before;
            for (int c = 0; c < cols; ++c) before.push_back(p.getI(curve0 + c));
            const double g75at12 = p.gainDb(75.0);
            p.set("Sel Slope", 5.0f / 5.0f); p.block(0.0); p.block(0.0);
            const double g75at48 = p.gainDb(75.0);
            std::printf("  low cut at 150 Hz, one octave below: %.1f dB at 12 dB/oct, %.1f dB at 48 dB/oct (slope param %.2f, band 1 slope %s)\n",
                        g75at12, g75at48, p.get("Sel Slope"), p.text("B1 Slope").c_str());
            CHECK(g75at48 < g75at12 - 20.0, "SLOPE 48 dB/oct did not steepen the cut (%.1f vs %.1f dB)", g75at48, g75at12);
            p.run(1.0);
            int changed = 0;
            for (int c = 0; c < cols; ++c) changed += p.getI(curve0 + c) != before[(std::size_t)c];
            CHECK(changed >= 3, "SLOPE changed only %d curve columns", changed);
            p.set("Sel Slope", 1.0f / 5.0f); p.block(0.0); p.block(0.0);
            // A cut's node: up / down is its resonance (Q). +6 dB on the graph = Q 1.41 (twice a plain cut's 0.71).
            p.paraTouch(eq::GraphMap::hzToX(p.freq(1)), p.get("Node 1 Y"));
            p.paraDrag(eq::GraphMap::hzToX(p.freq(1)), eq::GraphMap::dbToY(6.0));
            CHECK(std::fabs(p.q(1) / 1.4142 - 1.0) < 0.02, "dragging the cut's node to +6 dB gave Q %.2f, expected 1.41", p.q(1));
            CHECK(std::fabs(p.freq(1) / 150.0 - 1.0) < 0.01 && std::fabs(p.gain(1)) < 0.05, "dragging the cut's node changed its frequency or gain");
            p.run(0.3);
            CHECK(std::fabs(p.get("Node 1 Y") - (float)eq::GraphMap::dbToY(6.0)) < 1e-3, "the cut's node is not at +6 dB");
            CHECK(std::fabs(p.gainDb(150.0) - 3.0) < 0.5, "a 12 dB cut at Q 1.41 measures %.2f dB at its cutoff, expected +3", p.gainDb(150.0));
            p.set("Sel Q", nQ(1.0)); p.block(0.0); p.block(0.0);
            // NOTCH: chosen, it starts full; its gain (knob, or the node's height) is its depth.
            p.set("Sel Type", nType(5)); p.block(0.0); p.block(0.0);
            CHECK(p.gain(1) < -17.9 && p.text("Sel Gain") == "Full", "a new notch is at %.1f dB ('%s'), not full", p.gain(1), p.text("Sel Gain").c_str());
            CHECK(p.gainDb(150.0) < -30.0, "a full notch at 150 Hz measures %.1f dB there", p.gainDb(150.0));
            p.set("Sel Gain", nGain(-6.0)); p.block(0.0); p.block(0.0);
            CHECK(std::fabs(p.gainDb(150.0) + 6.0) < 0.5, "a -6 dB notch measures %.2f dB", p.gainDb(150.0));
            p.paraTouch(eq::GraphMap::hzToX(p.freq(1)), p.get("Node 1 Y"));
            p.paraDrag(eq::GraphMap::hzToX(p.freq(1)), eq::GraphMap::dbToY(-12.0));
            CHECK(std::fabs(p.gain(1) + 12.0) < 0.1, "dragging the notch's node to -12 dB gave %.1f dB", p.gain(1));
            p.paraDrag(eq::GraphMap::hzToX(p.freq(1)), eq::GraphMap::dbToY(9.0));
            CHECK(std::fabs(p.gain(1)) < 0.05, "a notch dragged above 0 dB is at %.1f dB, expected 0", p.gain(1));
            p.run(0.3);
            p.set("Sel Gain", nGain(0.0)); p.block(0.0); p.block(0.0);
        }
        p.set("Sel Type", nType(0)); p.block(0.0); p.block(0.0);

        std::printf("== PARA edits redraw the drawing on DRAW, and the curve displays show the bands\n");
        p.run(0.8);   // 0.3 s after the last edit the drawing follows; then the displays catch up
        {
            const auto bands = p.bandList();
            double worst = 0;
            for (int c = 0; c < kCols; ++c) worst = std::fmax(worst, std::fabs(p.drawDb(c) - std::fmax(-18.0, std::fmin(18.0, analogSum(bands, colHz(c))))));
            CHECK(worst < 1.0, "DRAW's drawing is %.2f dB away from the EQ made on PARA", worst);
            p.run(1.0);
            const int offCurve = curveDiff(curve0, [&](double hz) { return analogSum(bands, hz); });
            CHECK(offCurve == 0, "%d of %d combined-curve columns differ from the bands' response", offCurve, cols);
            const int offFront = curveDiff(front0, [&](double hz) { return eq::analogDb(bands[0], hz); });   // band 1 is selected
            CHECK(offFront == 0, "%d of %d front-curve columns differ from the selected band's curve", offFront, cols);
            CHECK(std::fabs(p.get("Node 2 X") - (float)eq::GraphMap::hzToX(p.freq(2))) < 1e-4 &&
                  std::fabs(p.get("Node 2 Y") - (float)eq::GraphMap::dbToY(p.gain(2))) < 1e-4, "node 2 is not on band 2");
            // The fill is band 1's colour (frames 0..144); band 1 is flat, so it is empty (level 72).
            int fillOff = 0;
            for (int c = 0; c < cols; ++c) fillOff += (int)std::lround(p.getI(fill0 + c) * (8 * 145 - 1)) != 72;
            CHECK(fillOff == 0, "%d fill columns are not band 1's empty fill", fillOff);
            // NONE: the front curve is the combined one.
            p.set("Sel Band", nSel(0)); p.run(1.0);
            int offNone = 0;
            for (int c = 0; c < cols; ++c) offNone += p.getI(front0 + c) != p.getI(curve0 + c);
            CHECK(offNone == 0, "with NONE selected, %d front-curve columns differ from the combined curve", offNone);
            CHECK(p.text("Para Info").find("none selected") != std::string::npos, "PARA status with NONE reads '%s'", p.text("Para Info").c_str());
        }

        std::printf("== PARA: + adds a band you can hear, - removes the selected one\n");
        {
            p.set("Add Band", 1.0f); p.block(0.0); p.block(0.0);
            CHECK(p.bands() == 3 && p.sel() == 3 && p.bandsOn() == 3, "+ gave %d bands, selected %d, %d on", p.bands(), p.sel(), p.bandsOn());
            CHECK(std::fabs(p.freq(3) - 400.0) < 1.0 && std::fabs(p.gain(3)) < 0.05, "new band 3 at %.0f Hz %.2f dB", p.freq(3), p.gain(3));
            CHECK(p.get("Add Band") < 0.5f, "+ did not reset itself");
            const double before = p.gainDb(400.0);
            p.set("Sel Gain", nGain(-6.0)); p.block(0.0); p.block(0.0);
            const double after = p.gainDb(400.0);
            CHECK(std::fabs((after - before) + 6.0) < 0.3, "band 3 at -6 dB changed 400 Hz by %.2f dB", after - before);
            p.set("Remove Band", 1.0f); p.block(0.0); p.block(0.0);
            CHECK(p.bands() == 2 && p.sel() == 0 && p.bandsOn() == 2, "- left %d bands, selected %d, %d on", p.bands(), p.sel(), p.bandsOn());
            CHECK(std::fabs(p.gainDb(400.0) - before) < 0.1, "removing band 3 did not restore 400 Hz");
            p.set("Add Band", 1.0f); p.block(0.0); p.block(0.0);
            p.set("Sel Band", nSel(0)); p.block(0.0); p.block(0.0);
            const float keep2 = p.get("B2 Freq");
            p.set("Remove Band", 1.0f); p.block(0.0); p.block(0.0);   // none selected: the last band goes
            CHECK(p.bands() == 2 && p.get("B2 Freq") == keep2 && p.sel() == 0, "- with NONE selected left %d bands", p.bands());
            CHECK(std::fabs(p.gainDb(400.0) - before) < 0.1, "removing the last band changed the sound");
            const float band2freq = p.get("B2 Freq");
            p.set("Sel Band", nSel(1)); p.block(0.0);
            p.set("Remove Band", 1.0f); p.block(0.0); p.block(0.0);
            CHECK(p.bands() == 1 && p.get("B1 Freq") == band2freq, "removing band 1 did not move band 2 down");
            CHECK(std::fabs(p.gainDb(1000.0) - 12.0) < 0.5, "the remaining band is not the +12 dB one at 1 kHz");
            // The data wheel on BANDS sets the count directly: the band it switches on starts flat at its default spot.
            p.set("B2 Gain", nGain(15.0)); p.set("B2 Freq", 0.3f); p.block(0.0);   // stale settings on the switched-off band 2
            p.run(0.5);
            p.set("Bands", 1.0f / 7.0f); p.block(0.0); p.block(0.0);
            CHECK(p.bands() == 2 && p.bandsOn() == 2, "the wheel left %d bands (%d on)", p.bands(), p.bandsOn());
            CHECK(std::fabs(p.gain(2)) < 0.05 && std::fabs(p.freq(2) - 2500.0) < 2.0, "the wheel's band 2 is at %.0f Hz %.1f dB, not flat at 2.5 kHz", p.freq(2), p.gain(2));
            CHECK(std::fabs(p.gainDb(1000.0) - 12.0) < 0.5, "switching on a flat band changed the sound");
            p.run(0.5);
            p.set("Undo", 1.0f); p.run(0.2);
            CHECK(p.bands() == 1, "UNDO after the wheel left %d bands", p.bands());
        }

        std::printf("== PARA: display updates while dragging stay within the budget\n");
        {
            auto total = [&] { long s = 0; for (int c = 0; c < 3 * cols; ++c) s += g_automate[front0 + c]; return s; };
            p.paraTouch(eq::GraphMap::hzToX(p.freq(1)), eq::GraphMap::dbToY(p.gain(1)));
            const long before = total();
            const auto t0 = std::chrono::steady_clock::now();
            int blocks = 0;
            for (int k = 0; k < 60; ++k)   // a 2-second drag across the graph
            {
                p.paraDrag(0.1 + 0.8 * k / 59.0, 0.5 + 0.3 * std::sin(k * 0.2));
                for (int b = 0; b < (int)(SR / 30 / 128); ++b) { p.block(0.0); ++blocks; }
            }
            const double usPerBlock = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / blocks;
            const double perSec = (total() - before) / 2.0;
            std::printf("  curve updates: %.0f per second; plugin time %.1f us per 128-sample block (desktop; a block lasts 2902 us)\n", perSec, usPerBlock);
            CHECK(perSec > 60 && perSec <= 500, "%.0f curve updates per second while dragging", perSec);
        }

        std::printf("== PARA: UNDO takes back a drag; UNDO again redoes it; an empty touch is not a step\n");
        {
            p.run(0.5);
            const float f0 = p.get("B1 Freq"), g0 = p.get("B1 Gain");
            const double heard0 = p.gainDb(p.freq(1));
            p.paraTouch(eq::GraphMap::hzToX(p.freq(1)), eq::GraphMap::dbToY(p.gain(1)));
            p.paraDrag(eq::GraphMap::hzToX(300.0), eq::GraphMap::dbToY(-9.0));
            p.run(0.5);
            const float f1 = p.get("B1 Freq"), g1 = p.get("B1 Gain");
            CHECK(std::fabs(p.freq(1) - 300.0) < 3.0 && std::fabs(p.gain(1) + 9.0) < 0.1, "the drag left band 1 at %.0f Hz %.1f dB", p.freq(1), p.gain(1));
            p.set("Undo", 1.0f); p.run(0.2);
            CHECK(p.get("B1 Freq") == f0 && p.get("B1 Gain") == g0, "UNDO did not take back the PARA drag");
            CHECK(std::fabs(p.gainDb(eq::GraphMap::xToHz(f0)) - heard0) < 0.1, "after UNDO the sound is not back");
            p.set("Undo", 1.0f); p.run(0.2);
            CHECK(p.get("B1 Freq") == f1 && p.get("B1 Gain") == g1, "UNDO again did not redo the drag");
            p.paraTouch(0.97, 0.05);
            p.run(0.5);
            p.set("Undo", 1.0f); p.run(0.2);
            CHECK(p.get("B1 Freq") == f0, "a touch on empty space became the undo step");
            p.set("Undo", 1.0f); p.run(0.2);
            CHECK(p.get("B1 Freq") == f1, "redo after the empty touch failed");
        }

        std::printf("== DRAW's BANDS knob and PARA's count are one count\n");
        {
            p.run(0.5);
            auto knob = [&] { return (int)std::lround(p.get("Max Bands") * 7) + 1; };
            CHECK(knob() == p.bands(), "DRAW's BANDS knob reads %d, PARA has %d bands", knob(), p.bands());
            p.set("Add Band", 1.0f); p.block(0.0); p.block(0.0);
            CHECK(knob() == p.bands(), "after + on PARA the knob reads %d, PARA has %d", knob(), p.bands());
            const double g1k = p.gainDb(1000.0);
            p.set("Max Bands", 3.0f / 7.0f); p.block(0.0);   // turn DRAW's knob to 4: the drawing is refitted with 4 bands
            p.settle(0.6);
            CHECK(p.bands() == 4 && p.bandsOn() == 4 && knob() == 4, "the knob at 4 left %d bands (%d on), knob %d", p.bands(), p.bandsOn(), knob());
            CHECK(std::fabs(p.gainDb(1000.0) - g1k) < 1.0, "refitting with 4 bands changed 1 kHz from %.1f to %.1f dB", g1k, p.gainDb(1000.0));
            // Turning the knob steadily (a drag: a value every 40 ms, 4 -> 8 -> 2) while refits run: the count follows the
            // knob step by step and never jumps back to an older value.
            std::vector<int> seen;
            auto track = [&] { const int b = p.bands(); if (seen.empty() || seen.back() != b) seen.push_back(b); };
            for (int k = 0; k <= 40; ++k)
            {
                const double t = k / 40.0;
                const double want = t < 0.5 ? 4.0 + 8.0 * t : 8.0 - 12.0 * (t - 0.5);   // 4 .. 8 .. 2
                p.set("Max Bands", (float)((want - 1.0) / 7.0));
                for (int b = 0; b < 14; ++b) { p.block(0.0); track(); }   // faster than real time: refits overlap the turning, as on the MPC
            }
            p.settle(0.8);
            track();
            std::string path;
            for (int b : seen) path += std::to_string(b) + " ";
            std::printf("  count while turning 4 -> 8 -> 2: %s\n", path.c_str());
            int reversals = 0;
            for (std::size_t i = 2; i < seen.size(); ++i)
                reversals += (seen[i] - seen[i - 1]) * (seen[i - 1] - seen[i - 2]) < 0;
            CHECK(reversals == 1 && seen.back() == 2 && knob() == 2, "the count jumped around: %s", path.c_str());
            // DRAW's + / - step the count by one, refitted.
            p.set("More Bands", 1.0f); p.block(0.0); p.settle(0.5);
            CHECK(p.bands() == 3 && knob() == 3 && p.get("More Bands") < 0.5f, "DRAW's + left %d bands (knob %d)", p.bands(), knob());
            p.set("Fewer Bands", 1.0f); p.block(0.0); p.settle(0.5);
            p.set("Fewer Bands", 1.0f); p.block(0.0); p.settle(0.5);
            CHECK(p.bands() == 1 && knob() == 1, "DRAW's - twice left %d bands (knob %d)", p.bands(), knob());
            p.set("Fewer Bands", 1.0f); p.block(0.0); p.settle(0.3);
            CHECK(p.bands() == 1, "DRAW's - went below one band");
            p.set("Max Bands", 3.0f / 7.0f); p.block(0.0); p.settle(0.6);   // back to 4 for the stroke below
        }

        std::printf("== DRAW: the sound follows the pen at once, then the stroke becomes bands PARA shows\n");
        p.run(0.5);
        const int bandsBefore = p.bands();
        const double g200Before = p.gainDb(200.0);
        p.recording = true;
        p.run(2.0, -1.0);   // pink noise, EQ as PARA left it
        p.run(0.3);         // a pause: the next touch is a new stroke
        for (int c = 0; c < kCols; ++c)   // one pen point per column
        {
            p.set("Pen X", (c + 0.5f) / kCols);
            p.set("Pen Y", (float)((drawnDb(colHz(c)) / 18.0 + 1.0) / 2.0));
            p.block(0.0);
        }
        {
            const auto t0 = std::chrono::steady_clock::now();
            const double g200 = p.gainDb(200.0), g3k = p.gainDb(3000.0);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("  right after the stroke (%.1f ms of real time): %.2f dB at 200 Hz, %.2f dB at 3 kHz (drew %.2f / %.2f)\n",
                        ms, g200, g3k, drawnDb(200.0), drawnDb(3000.0));
            CHECK(std::fabs(g200 - drawnDb(200.0)) < 1.0 && std::fabs(g3k - drawnDb(3000.0)) < 1.0, "the sound did not follow the stroke at once");
        }
        p.settle(1.0, -1.0);
        p.run(2.0, -1.0);   // pink noise, the drawn EQ
        p.recording = false;
        if (!outDir.empty()) writeWav(outDir + "/draw-demo.wav", p.recL, p.recR);
        {
            std::printf("  fit: %s\n", p.text("Fit").c_str());
            for (int b = 1; b <= p.bands(); ++b)
                std::printf("    B%d %-10s %-10s %-9s Q %s\n", b, p.text(p.B(b, "Type")).c_str(), p.text(p.B(b, "Freq")).c_str(),
                            p.text(p.B(b, "Gain")).c_str(), p.text(p.B(b, "Q")).c_str());
            double worst = 0, worstHz = 0;
            for (int c : {4, 8, 11, 13, 15, 18, 20, 22, 24, 26, 30})
            {
                const double hz = colHz(c), want = drawnDb(hz), got = p.gainDb(hz);
                if (std::fabs(got - want) > worst) { worst = std::fabs(got - want); worstHz = hz; }
            }
            std::printf("  measured output vs drawing: worst %.2f dB (at %.0f Hz)\n", worst, worstHz);
            CHECK(worst < 1.5, "the audio misses the drawing by %.2f dB at %.0f Hz", worst, worstHz);
            CHECK(p.bands() >= 2 && p.bandsOn() == p.bands(), "Bands = %d but %d bands are on", p.bands(), p.bandsOn());
            CHECK(p.sel() == 0, "band %d is still selected after a fit", p.sel());
            const auto bands = p.bandList();
            bool nodesOk = true;
            for (int b = 1; b <= p.bands(); ++b)
                nodesOk = nodesOk && std::fabs(p.get("Node " + std::to_string(b) + " X") - (float)eq::GraphMap::hzToX(p.freq(b))) < 1e-4;
            CHECK(nodesOk, "PARA's nodes are not on the fitted bands");
            p.run(1.0);
            const int off = curveDiff(curve0, [&](double hz) { return analogSum(bands, hz); });
            CHECK(off == 0, "%d of %d combined-curve columns differ from the fitted bands", off, cols);
        }

        std::printf("== DRAW: Undo takes back the stroke (sound, drawing, band count); Undo again redoes it\n");
        {
            const int fitted = p.bands();
            p.maxStep = 0;
            p.run(0.3, 1000.0);
            p.set("Undo", 1.0f); p.run(0.2, 1000.0);
            CHECK(p.maxStep < 0.25 * 2.0 * M_PI * 1000.0 / SR * std::pow(10.0, 12.0 / 20.0) * 1.5, "Undo clicked (step %.4f)", p.maxStep);
            p.settle(0.3);
            CHECK(p.bands() == bandsBefore, "Undo left %d bands, expected %d", p.bands(), bandsBefore);
            CHECK(std::fabs(p.gainDb(200.0) - g200Before) < 0.1, "Undo: 200 Hz at %.2f dB, was %.2f", p.gainDb(200.0), g200Before);
            p.set("Undo", 1.0f); p.block(0.0);
            p.settle(0.3);
            CHECK(p.bands() == fitted, "Redo left %d bands, expected %d", p.bands(), fitted);
            CHECK(std::fabs(p.gainDb(200.0) - drawnDb(200.0)) < 1.5, "Redo did not bring the bump back");
            CHECK(std::fabs(p.drawDb(10) - drawnDb(colHz(10))) < 1.0, "Redo did not bring the drawing back");
        }

        std::printf("== Bypass, and DRAW's status line says so\n");
        p.set("Bypass", 1.0f);
        CHECK(std::fabs(p.gainDb(200.0)) < 0.05, "bypassed but %.2f dB at 200 Hz", p.gainDb(200.0));
        CHECK(p.text("Fit") == "BYPASSED", "status reads '%s' while bypassed", p.text("Fit").c_str());
        p.set("Bypass", 0.0f);
        p.block(0.0);

        std::printf("== DRAW: Clear is a flat EQ; the band count stays\n");
        const int countBeforeClear = p.bands();
        p.set("Clear", 1.0f); p.block(0.0);
        p.settle(0.6);
        CHECK(p.bands() == countBeforeClear && p.bandsOn() == countBeforeClear, "Clear left %d bands (%d on), expected %d", p.bands(), p.bandsOn(), countBeforeClear);
        {
            bool flat = true;
            for (int b = 1; b <= p.bands(); ++b) flat = flat && std::fabs(p.gain(b)) < 0.05 && p.get(p.B(b, "Type")) == 0.0f;
            CHECK(flat, "after Clear the bands are not all flat bells");
        }
        CHECK(std::fabs(p.gainDb(200.0)) < 0.1 && std::fabs(p.gainDb(3000.0)) < 0.1, "Clear is not flat");
        CHECK(p.text("Fit") == "Flat", "DRAW status after Clear reads '%s'", p.text("Fit").c_str());
        CHECK(p.get("Clear") < 0.5f, "Clear did not reset itself");

        std::printf("== the spectrum display follows the input\n");
        p.run(0.5, 1000.0);
        const int spec0 = p.I("Spec 1");
        int best = 0;
        for (int c = 1; c < kCols; ++c) if (p.getI(spec0 + c) > p.getI(spec0 + best)) best = c;
        CHECK(best == 18, "1 kHz lights column %d, expected 19 (976..1210 Hz)", best + 1);
        p.set("Analyzer", 0.0f); p.run(0.3, 1000.0);
        bool dark = true;
        for (int c = 0; c < kCols; ++c) dark = dark && p.getI(spec0 + c) == 0.0f;
        CHECK(dark, "Analyzer off still shows a spectrum");
    }
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
