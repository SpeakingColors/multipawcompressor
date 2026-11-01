// MultiPaw core tests: no JUCE, no GUI. Run: sh tests/run.sh
#include "../src/core/MultiPawCore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
using namespace multipaw;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<float> noise(int n, unsigned seed, float amp) {
    std::mt19937 g(seed); std::uniform_real_distribution<float> d(-1, 1); std::vector<float> v(n);
    for (auto& x : v) x = d(g) * amp;
    return v;
}
static double rmsDb(const std::vector<double>& v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += v[i] * v[i]; return 10 * std::log10(s / (b - a) + 1e-30); }

// 1. FIR band sum equals the delayed input, for many settings
static void testSplitFlat(bool quick) {
    const double fs = 48000; int cases = 0; double worst = -999;
    for (int nb = 1; nb <= 4; nb++)
        for (int slope = 0; slope <= 8; slope += (quick ? 4 : 1)) {
            FirSplit f; f.prepare(); float xf[3] = {110, 500, 5000}; if (nb == 2) { xf[0] = 1000; } if (nb == 3) { xf[0] = 200; xf[1] = 2500; }
            f.setSplit(fs, nb, xf, slope);
            int lat = f.latency(); int N = lat + 20000; auto x = noise(N, 7 + nb * 13 + slope, 0.5f);
            std::vector<double> err, ref; double bl[NB], br[NB];
            for (int i = 0; i < N; i++) { f.push(x[i], x[i], bl, br); double sum = 0; for (int b = 0; b < nb; b++) sum += bl[b]; double d = i >= lat ? x[i - lat] : 0; err.push_back(sum - d); ref.push_back(d); }
            double e = rmsDb(err, lat, N) - rmsDb(ref, lat, N); worst = std::max(worst, e); cases++;
            CHECK(e < -110, "split sum nb=%d slope=%d error %.1f dB", nb, slope, e);
        }
    std::printf("split flat: %d cases, worst error %.1f dB relative\n", cases, worst);
}
// 2. band selectivity: a tone in band 0 must not leak into band 3 (and vice versa)
static void testSplitSelect() {
    const double fs = 48000; FirSplit f; f.prepare(); float xf[3] = {110, 500, 5000}; f.setSplit(fs, 4, xf, 5);
    int N = 40000, lat = f.latency(); double bl[NB], br[NB]; double e[2][4] = {};
    double freqs[2] = {40, 12000};
    for (int t = 0; t < 2; t++) {
        FirSplit g; g.prepare(); g.setSplit(fs, 4, xf, 5);
        for (int i = 0; i < N; i++) { double x = 0.5 * std::sin(2 * M_PI * freqs[t] * i / fs); g.push(x, x, bl, br); if (i > lat + 2000) for (int b = 0; b < 4; b++) e[t][b] += bl[b] * bl[b]; }
    }
    double lowInTop = 10 * std::log10(e[0][3] / e[0][0] + 1e-30), highInLow = 10 * std::log10(e[1][0] / e[1][3] + 1e-30);
    std::printf("selectivity: 40 Hz in top band %.1f dB, 12 kHz in bottom band %.1f dB\n", lowInTop, highInLow);
    CHECK(lowInTop < -60 && highInLow < -60, "band leakage too high");
}
// 3. changing the crossover while running keeps the sum flat and finite
static void testSplitMove() {
    const double fs = 48000; FirSplit f; f.prepare(); float xf[3] = {110, 500, 5000}; f.setSplit(fs, 4, xf, 5);
    int lat = f.latency(); int N = 60000; auto x = noise(N, 3, 0.4f); std::vector<double> err, ref; double bl[NB], br[NB];
    for (int i = 0; i < N; i++) {
        if (i % 777 == 0) { xf[0] = 90 + (i / 777 % 7) * 30; f.setSplit(fs, 4, xf, 5); lat = f.latency(); }
        f.push(x[i], x[i], bl, br); double sum = 0; for (int b = 0; b < 4; b++) sum += bl[b]; err.push_back(sum); ref.push_back(i >= lat ? x[i - lat] : 0);
        CHECK(std::isfinite(sum), "nan while moving");
    }
    double worst = -999; for (int i = lat + 2000; i < N; i += 1) worst = std::max(worst, std::fabs(err[i] - ref[i]));
    std::printf("crossover moving: worst sample error %.2e\n", worst);
    CHECK(worst < 1e-3, "moving crossover glitch %.2e", worst);
}
static std::vector<float> run(Engine& e, const std::vector<float>& x, int blk) {
    std::vector<float> L(x), R(x); HostInfo h; h.playing = true; double ppq = 0;
    for (size_t i = 0; i < x.size(); i += blk) { int n = (int)std::min<size_t>(blk, x.size() - i); h.ppq = ppq; e.process(L.data() + i, R.data() + i, n, nullptr, nullptr, h); ppq += n * h.bpm / 60.0 / e.fs; }
    return L;
}
// 4. Mix 0 is bit identical to Bypass, in both crossover modes, and latency is reported correctly
static void testMix0() {
    for (int lin = 0; lin < 2; lin++) {
        Engine a, b; MbcP m; m.mix = 0; m.linearPhase = lin; m.slope = 4; a.setMBC(m); b.setMBC(m); a.prepare(48000); b.prepare(48000); b.bypass = true;
        auto x = noise(30000, 5, 0.6f); auto ya = run(a, x, 333), yb = run(b, x, 333);
        int diff = 0; for (size_t i = 0; i < x.size(); i++) if (ya[i] != yb[i]) diff++;
        CHECK(diff == 0, "mix 0 differs from bypass in %d samples (linear=%d)", diff, lin);
        // latency: impulse appears at reported latency
        std::vector<float> imp(30000, 0.f); imp[100] = 1; Engine c; c.setMBC(m); c.prepare(48000); c.bypass = true; auto yi = run(c, imp, 480);
        int pos = (int)(std::max_element(yi.begin(), yi.end()) - yi.begin()); CHECK(pos - 100 == c.latencySamples(), "latency %d reported %d", pos - 100, c.latencySamples());
        std::printf("mix0 identical (linear=%d), latency %d samples = %.2f ms\n", lin, c.latencySamples(), 1000.0 * c.latencySamples() / 48000);
    }
}
// 5. With every compressor and limiter neutral and Mix 100, the linear-phase plugin is a pure delay (flat, no colour)
static void testNeutral() {
    Engine e; MbcP m; m.mix = 100; m.linearPhase = true; m.slope = 4; m.limOn = false; m.autoGain = false;
    for (auto& b : m.b) { b.cOn = false; b.lOn = false; }
    e.setMBC(m); e.prepare(48000); auto x = noise(40000, 9, 0.4f); auto y = run(e, x, 512); int lat = e.latencySamples();
    std::vector<double> err, ref; for (size_t i = lat + 3000; i < x.size(); i++) { err.push_back(y[i] - x[i - lat]); ref.push_back(x[i - lat]); }
    double e1 = rmsDb(err, 0, (int)err.size()) - rmsDb(ref, 0, (int)ref.size());
    std::printf("neutral linear-phase chain vs delayed input: %.1f dB\n", e1);
    CHECK(e1 < -100, "neutral chain not flat: %.1f", e1);
}
// 6. the master limiter holds its ceiling
static void testLimiter() {
    Engine e; MbcP m; m.mix = 100; m.ceil = -3; m.postG = 0; e.setMBC(m); e.prepare(48000);
    auto x = noise(60000, 11, 1.0f); for (auto& v : x) v *= 4; auto y = run(e, x, 256); float pk = 0; for (float v : y) pk = std::max(pk, std::fabs(v));
    std::printf("limiter: peak %.3f dBFS at ceiling -3\n", 20 * std::log10(pk));
    CHECK(pk <= D2L(-3) * 1.0001, "ceiling exceeded %.4f", pk);
}
// 7. fuzz: random parameters, no NaN, bounded output
static void testFuzz(bool quick) {
    std::mt19937 g(1234); auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(g); };
    int runs = quick ? 6 : 40; double worst = 0;
    for (int r = 0; r < runs; r++) {
        Engine e; MbcP m; m.bands = 1 + g() % 4; m.slope = g() % 9; m.linearPhase = g() % 2; m.mix = (float)U(0, 100); m.inG = (float)U(-24, 24); m.outG = (float)U(-24, 24);
        m.x[0] = (float)U(30, 300); m.x[1] = (float)U(300, 3000); m.x[2] = (float)U(3000, 15000); m.limOn = g() % 2; m.ceil = (float)U(-12, 0); m.sysLA = (float)U(1, 7.5); m.rms = g() % 2; m.autoGain = g() % 2;
        for (auto& b : m.b) { b.thr = (float)U(-48, 0); b.u = (float)U(0, 1); b.atk = (float)U(0.1, 100); b.rel = (float)U(0, 1000); b.boost = (float)U(0, 24); b.lRel = (float)U(0, 1000); b.lOut = (float)U(-12, 0); b.inG = (float)U(-24, 8); b.outG = (float)U(-24, 8); b.mix = (float)U(0, 100); b.dOn = g() % 2; b.dStr = (float)U(0, 100); b.mute = g() % 8 == 0; b.solo = g() % 8 == 0; for (auto& v : b.lut) v = (float)U(0, 1); }
        e.setMBC(m); ScP s; s.on = g() % 2; s.trig = g() % 3; s.sens = (float)U(0, 100); s.ftype = g() % 3; s.feed = g() % 2; e.setSC(s); e.prepare(g() % 2 ? 44100 : 96000);
        auto x = noise(quick ? 9000 : 30000, r, 0.8f); std::vector<float> L(x), R(x); HostInfo h; h.playing = g() % 2; int midi[2] = {5, 50}; h.midiOn = midi; h.nMidiOn = 2;
        std::vector<float> sc = noise(x.size(), r + 99, 0.5f);
        for (size_t i = 0; i < x.size(); i += 200) { int n = (int)std::min<size_t>(200, x.size() - i); if (r % 3 == 0 && i % 2000 == 0) e.setMBC([&] { MbcP q = m; q.x[0] = (float)U(30, 300); return q; }()); e.process(L.data() + i, R.data() + i, n, sc.data() + i, sc.data() + i, h); }
        for (size_t i = 0; i < x.size(); i++) { CHECK(std::isfinite(L[i]) && std::isfinite(R[i]), "nan in fuzz run %d", r); worst = std::max<double>(worst, std::fabs(L[i])); }
    }
    std::printf("fuzz: %d runs, no NaN, largest sample %.2f\n", runs, worst);
}
int main(int argc, char** argv) {
    bool quick = argc > 1 && !std::strcmp(argv[1], "quick");
    testSplitFlat(quick); testSplitSelect(); testSplitMove(); testMix0(); testNeutral(); testLimiter(); testFuzz(quick);
    std::printf(fails ? "FAILED %d\n" : "ALL OK\n", fails); return fails ? 1 : 0;
}
