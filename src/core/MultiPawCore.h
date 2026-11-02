// MultiPaw Compressor core: pure C++, no JUCE. Same maths as the approved mockup (dsp.js),
// except that page 1 can split the bands with LINEAR-PHASE FIR crossovers (default) or with
// the low-latency Linkwitz-Riley IIR crossovers of the mockup.
#pragma once
#include <algorithm>
#include <cmath>
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace multipaw {

constexpr int NB = 4;
constexpr int DPC_LUT = 513;
inline double D2L(double db) { return std::pow(10.0, db * 0.05); }
inline double L2D(double x) { return 20.0 * std::log10(std::max(x, 1e-9)); }
template <class T> inline T clampv(T v, T a, T b) { return std::min(b, std::max(a, v)); }

// ---------------------------------------------------------------- parameters
struct BandP {
    bool on = true, cOn = true;
    float thr = -18, u = 0.5f;      // threshold dB, ratio position 0..1 (ratio = 1/(1-u), inf at 1)
    bool knee = true;
    float atk = 20, rel = 150, boost = 0;   // ms, ms (<1 = Auto), dB
    bool lOn = true;
    float lRel = 50, lOut = 0;       // ms (<1 = Auto), dB
    float inG = 0, outG = 0, mix = 100;
    bool mute = false, solo = false;
    bool dOn = false, dX2 = false, dFlip = false, dPre = true;
    float dStr = 50;
    float lut[DPC_LUT];
    BandP() { for (float& v : lut) v = 1.0f; }
};
struct MbcP {
    bool on = true;
    int bands = 4, slope = 3;        // slope index 0..8 = 6..96 dB/oct (IIR) or FIR length (linear phase)
    float x[3] = {110, 500, 5000};
    float mix = 50, inG = 0, inPan = 0, outG = 0, outPan = 0, postG = 0;
    bool limOn = true;
    float ceil = -0.3f;
    bool autoGain = true, ratioSteps = false, rms = false;
    bool linearPhase = true;         // false = low latency IIR Linkwitz-Riley
    float sysLA = 7.5f;              // lookahead of the limiters in ms
    BandP b[NB];
};
struct ScP {
    bool on = false;
    int trig = 2;                    // 0 audio, 1 MIDI, 2 host beat grid
    float sens = 59, inG = 0, outG = 0, split = 1050;
    int ftype = 0, listen = 0, feed = 0;   // feed 0 = sidechain input, 1 = the signal itself
    float lowVol = 0, hiVol = 0;
    float lStr = 92, lAtk = 0, lAtkC = 0, lHold = 4, lRel = 600, lRelC = 33;
    float hStr = 52, hAtk = 0, hAtkC = 0, hHold = 20, hRel = 300, hRelC = 33;
    float e1f = 200, e1g = 0, e1w = 1, e2f = 4000, e2g = 0, e2w = 1;
    int grid = 2;
    float offset = 0;
};
struct HostInfo {
    bool playing = false;
    double ppq = 0;                  // quarter note position at the first sample of the block
    double bpm = 120;
    const int* midiOn = nullptr;     // sample offsets of MIDI note-ons in this block
    int nMidiOn = 0;
};

// ---------------------------------------------------------------- filters
struct Biq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double proc(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void lp1(double fs, double f) { double K = std::tan(M_PI * f / fs), n = 1 / (1 + K); b0 = K * n; b1 = K * n; b2 = 0; a1 = (K - 1) * n; a2 = 0; }
    void hp1(double fs, double f) { double K = std::tan(M_PI * f / fs), n = 1 / (1 + K); b0 = n; b1 = -n; b2 = 0; a1 = (K - 1) * n; a2 = 0; }
    void lp(double fs, double f, double q) { double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al; b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void hp(double fs, double f, double q) { double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void peak(double fs, double f, double g, double q) { double A = std::pow(10.0, g / 40), w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al / A; b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0; }
};

// Linkwitz-Riley crossover of order N (slope 6N dB/oct). Same as the mockup.
struct XO {
    double lo = 0, hi = 0; int N = -1; bool inv = false;
    struct Spec { int o; double q; };
    std::vector<Biq> lps, hps; std::vector<Spec> specs;
    void shape(int n) {
        if (n == N) return;
        N = n; lps.clear(); hps.clear(); specs.clear(); inv = false;
        if (n == 1) { lps.resize(1); return; }
        int m = n / 2; inv = (m % 2 == 1);
        std::vector<Spec> one;
        if (m % 2 == 1) one.push_back({1, 0});
        for (int k = 1; k <= m / 2; k++) one.push_back({2, 1.0 / (2 * std::sin((2 * k - 1) * M_PI / (2.0 * m)))});
        specs = one; specs.insert(specs.end(), one.begin(), one.end());
        lps.resize(specs.size()); hps.resize(specs.size());
    }
    void set(double fs, double fc, int n) {
        shape(n); fc = std::min(fc, fs * 0.45);
        if (n == 1) { lps[0].lp1(fs, fc); return; }
        for (size_t i = 0; i < specs.size(); i++) {
            if (specs[i].o == 1) { lps[i].lp1(fs, fc); hps[i].hp1(fs, fc); }
            else { lps[i].lp(fs, fc, specs[i].q); hps[i].hp(fs, fc, specs[i].q); }
        }
    }
    void proc(double x) {
        if (N == 1) { double l = lps[0].proc(x); lo = l; hi = x - l; return; }
        double l = x, h = x;
        for (size_t i = 0; i < lps.size(); i++) { l = lps[i].proc(l); h = hps[i].proc(h); }
        lo = l; hi = inv ? -h : h;
    }
    void reset() { for (auto& f : lps) f.reset(); for (auto& f : hps) f.reset(); }
};

// ---------------------------------------------------------------- limiter and delay
struct LL {
    int M = 0, w = 0, la = 0; int64_t h = 0, t = 0, n = 0;
    std::vector<float> bl, br, dv, avg; std::vector<int64_t> dq;
    double sum = 0, g = 1, gr = 0, ol = 0, orr = 0;
    void init(int maxLA) { M = maxLA + 4; bl.assign(M, 0); br.assign(M, 0); dv.assign(M, 1); avg.assign(M, 1); dq.assign(M, 0); la = 0; setLA(std::max(1, maxLA / 2)); }
    void setLA(int l) {
        l = std::max(1, std::min(M - 4, l)); if (l == la) return; la = l;
        std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f); std::fill(avg.begin(), avg.end(), 1.f);
        sum = la; h = t = 0; n = 0; g = 1; w = 0;
    }
    void proc(double l, double r, double ceil, double kr, bool on) {
        bl[w] = (float)l; br[w] = (float)r;
        double p = std::max(std::fabs(l), std::fabs(r)), gt = (on && p > ceil) ? ceil / p : 1.0;
        int64_t idx = n++;
        while (t > h && dv[(t - 1) % M] >= gt) t--;
        dq[t % M] = idx; dv[t % M] = (float)gt; t++;
        while (dq[h % M] <= idx - la) h++;
        double gm = dv[h % M];
        int ai = (int)(idx % la); sum += gm - avg[ai]; avg[ai] = (float)gm;
        double ga = std::min(1.0, sum / la);
        g = ga < g ? ga : g + (ga - g) * kr;
        int rd = (w - (la - 1) + M) % M; ol = bl[rd] * g; orr = br[rd] * g; w = (w + 1) % M;
        gr = -20 * std::log10(g + 1e-9);
    }
};
struct DL {
    int M = 2, w = 0; std::vector<float> b;
    void init(int maxD) { M = maxD + 2; b.assign(M, 0.f); w = 0; }
    float proc(float x, int d) { b[w] = x; int r = (w - d + M) % M; w = (w + 1) % M; return b[r]; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); }
};

// ---------------------------------------------------------------- FFT (radix 2, double)
struct FFT {
    int n = 0; std::vector<std::complex<double>> tw; std::vector<int> rev;
    void init(int N) {
        n = N; tw.resize(N / 2); rev.resize(N);
        for (int i = 0; i < N / 2; i++) tw[i] = std::polar(1.0, -2 * M_PI * i / N);
        int lg = 0; while ((1 << lg) < N) lg++;
        for (int i = 0; i < N; i++) { int r = 0; for (int b = 0; b < lg; b++) if (i & (1 << b)) r |= 1 << (lg - 1 - b); rev[i] = r; }
    }
    void run(std::complex<double>* a, bool inverse) const {
        for (int i = 0; i < n; i++) if (i < rev[i]) std::swap(a[i], a[rev[i]]);
        for (int len = 2; len <= n; len <<= 1) {
            int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < len / 2; k++) {
                    std::complex<double> w = tw[k * step]; if (inverse) w = std::conj(w);
                    std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v; a[i + k + len / 2] = u - v;
                }
        }
        if (inverse) for (int i = 0; i < n; i++) a[i] /= n;
    }
};

// ---------------------------------------------------------------- linear-phase band splitter
// Bands are differences of windowed-sinc low-passes (top band = delta - last low-pass), so the
// band sum equals the input delayed by D samples EXACTLY, whatever the frequencies are.
// Uniform partitioned convolution, block B. Total latency = B + D.
class FirSplit {
public:
    static constexpr int B = 256, FN = 512, PMAX = 16;
    static int partitionsFor(int slope) { static const int P[9] = {1, 2, 3, 4, 6, 8, 10, 12, 16}; return P[clampv(slope, 0, 8)]; }
    static int delayFor(int slope) { return (partitionsFor(slope) * B - 1) / 2; }
    static int latencyFor(int slope) { return B + delayFor(slope); }

    void prepare() {
        fft.init(FN); reset();
        spec.assign(PMAX * 2, std::vector<std::complex<double>>(FN));
        for (int s = 0; s < 2; s++) for (int b = 0; b < NB; b++) kern[s][b].assign(PMAX, std::vector<std::complex<double>>(FN));
        cur = 0; haveKern = false;
    }
    void reset() {
        head = 0; idx = 0; inPrev[0].assign(B, 0); inPrev[1].assign(B, 0);
        for (int c = 0; c < 2; c++) { inBlk[c].assign(B, 0); for (int b = 0; b < NB; b++) { outA[c][b].assign(B, 0); outB[c][b].assign(B, 0); } }
        for (auto& v : spec) std::fill(v.begin(), v.end(), std::complex<double>(0, 0));
    }
    // Set the split (nb bands, nb-1 crossover frequencies). Cheap enough for the audio thread.
    void setSplit(double fs, int nb, const float* xf, int slope) {
        int P = partitionsFor(slope), L = P * B - 1, D = (L - 1) / 2;
        bool same = haveKern && nb == nbands && P == parts && std::fabs(fs - sr) < 1e-6;
        for (int i = 0; same && i < nb - 1; i++) if (xf[i] != fx[i]) same = false;
        if (same) return;
        std::vector<double> lp[NB]; // lowpass kernels
        std::vector<std::vector<double>> k(NB, std::vector<double>(L, 0.0));
        std::vector<double> prev(L, 0.0);
        for (int c = 0; c < nb - 1; c++) {
            double fc = std::min<double>(xf[c], fs * 0.45) / fs;      // cycles per sample
            std::vector<double> h(L);
            for (int n = 0; n < L; n++) {
                double m = n - D, s = (m == 0) ? 2 * fc : std::sin(2 * M_PI * fc * m) / (M_PI * m);
                double w = 0.42 - 0.5 * std::cos(2 * M_PI * n / (L - 1)) + 0.08 * std::cos(4 * M_PI * n / (L - 1));   // Blackman
                if (L == 1) w = 1;
                h[n] = s * w;
            }
            for (int n = 0; n < L; n++) k[c][n] = h[n] - prev[n];
            prev = h;
        }
        for (int n = 0; n < L; n++) k[nb - 1][n] = (n == D ? 1.0 : 0.0) - prev[n];
        if (nb == 1) for (int n = 0; n < L; n++) k[0][n] = (n == D ? 1.0 : 0.0);
        int ns = 1 - cur;
        for (int b = 0; b < nb; b++)
            for (int p = 0; p < P; p++) {
                auto& a = kern[ns][b][p];
                for (int i = 0; i < FN; i++) a[i] = 0;
                for (int i = 0; i < B; i++) { int n = p * B + i; if (n < L) a[i] = k[b][n]; }
                fft.run(a.data(), false);
            }
        fadeFrom = haveKern ? cur : -1; fadeParts = parts; fadeNb = nbands;
        cur = ns; nbands = nb; parts = P; sr = fs; for (int i = 0; i < nb - 1; i++) fx[i] = xf[i]; haveKern = true;
    }
    int latency() const { return B + (parts * B - 2) / 2; }
    // One stereo sample in; nb band outputs per channel are written to bl/br (arrays of NB).
    void push(double xl, double xr, double* bl, double* br) {
        inBlk[0][idx] = xl; inBlk[1][idx] = xr;
        for (int b = 0; b < nbands; b++) { bl[b] = outA[0][b][idx]; br[b] = outA[1][b][idx]; }
        if (++idx == B) { idx = 0; block(); }
    }
private:
    FFT fft; std::vector<std::vector<std::complex<double>>> spec;
    std::vector<std::vector<std::complex<double>>> kern[2][NB];
    std::vector<double> inPrev[2], inBlk[2], outA[2][NB], outB[2][NB];
    std::complex<double> tmp[FN], acc[FN];
    int head = 0, idx = 0, cur = 0, parts = 1, nbands = 1, fadeFrom = -1, fadeParts = 1, fadeNb = 1; bool haveKern = false;
    double sr = 0, fx[3] = {0, 0, 0};
    void block() {
        // frame = [previous block, this block]; push spectrum into the history ring (channel c uses slots c*PMAX..)
        for (int c = 0; c < 2; c++) {
            for (int i = 0; i < B; i++) { tmp[i] = inPrev[c][i]; tmp[B + i] = inBlk[c][i]; }
            inPrev[c] = inBlk[c];
            fft.run(tmp, false);
            auto& slot = spec[c * PMAX + (head % PMAX)]; std::copy(tmp, tmp + FN, slot.begin());
        }
        for (int c = 0; c < 2; c++)
            for (int b = 0; b < nbands; b++) {
                mac(c, b, cur, parts, acc);
                for (int i = 0; i < FN; i++) tmp[i] = acc[i];
                fft.run(tmp, true);
                if (fadeFrom >= 0 && b < fadeNb) {
                    mac(c, b, fadeFrom, fadeParts, acc);
                    std::complex<double> t2[FN]; for (int i = 0; i < FN; i++) t2[i] = acc[i];
                    fft.run(t2, true);
                    for (int i = 0; i < B; i++) { double w = (i + 0.5) / B; outA[c][b][i] = t2[B + i].real() * (1 - w) + tmp[B + i].real() * w; }
                } else for (int i = 0; i < B; i++) outA[c][b][i] = tmp[B + i].real();
            }
        fadeFrom = -1; head++;
    }
    void mac(int c, int b, int set, int P, std::complex<double>* out) {
        for (int i = 0; i < FN; i++) out[i] = 0;
        for (int p = 0; p < P; p++) {
            const auto& X = spec[c * PMAX + ((head - p) % PMAX + PMAX) % PMAX]; const auto& H = kern[set][b][p];
            for (int i = 0; i < FN; i++) out[i] += X[i] * H[i];
        }
    }
};

// ---------------------------------------------------------------- curves
inline double ratioOf(double u) { return u >= 0.998 ? INFINITY : 1.0 / (1.0 - u); }
inline double snapRatio(double u) { double r = ratioOf(u); if (!std::isfinite(r) || r > 20.5) return 1; return 1 - 1.0 / std::max(1.0, std::round(r)); }
inline double grCurve(double over, double R, bool knee, double w) {
    double s = std::isfinite(R) ? 1.0 / R : 0.0;
    if (knee) { if (2 * over < -w) return 0; if (2 * std::fabs(over) <= w) { double u = over + w / 2; return (1 - s) * u * u / (2 * w); } }
    return over > 0 ? over * (1 - s) : 0;
}
struct Env { double str, atk, hold, rel, ac, rc; };
inline double envGain(const Env& e, double t) {
    auto cf = [](double x, double c) { return std::pow(clampv(x, 0.0, 1.0), std::pow(2.0, -c / 33)); };
    double k = 0;
    if (t < 0) return 1;
    if (e.atk > 0 && t < e.atk) k = cf(t / e.atk, e.ac);
    else if (t < e.atk + e.hold) k = 1;
    else if (e.rel > 0 && t < e.atk + e.hold + e.rel) k = 1 - cf((t - e.atk - e.hold) / e.rel, e.rc);
    else k = 0;
    return 1 - e.str * k;
}

struct BandState { double agr = 0, env = 0, pw = 0, cur = 0, gr = 0, lgr = 0, lvl = -90, dpcG = 1; };

// ---------------------------------------------------------------- engine
class Engine {
public:
    double fs = 48000;
    // meters for the editor (written by process, read by the GUI; plain floats, benign races)
    float prePk[2] = {0, 0}, postPk[2] = {0, 0}, preMs[2] = {0, 0}, postMs[2] = {0, 0}, mGr = 0, bandGr[NB] = {0, 0, 0, 0}, bandLvl[NB] = {-90, -90, -90, -90}, bandLimGr[NB] = {0, 0, 0, 0};
    float scLow = 1, scHigh = 1;
    bool bypass = false;

    void prepare(double sampleRate) {
        fs = sampleRate; maxLA = (int)std::ceil(0.0075 * fs) + 2;
        split.prepare();
        for (int c = 0; c < NB - 1; c++) for (int ch = 0; ch < 2; ch++) xo[c][ch] = XO();
        for (int b = 0; b < NB; b++) { for (int j = 0; j < 2; j++) { ap[b][j][0] = XO(); ap[b][j][1] = XO(); } }
        maxLat = FirSplit::latencyFor(8) + 2 * maxLA + 16;
        for (int b = 0; b < NB; b++) { bandLL[b].init(maxLA); bandDry[b][0].init(maxLA); bandDry[b][1].init(maxLA); bs[b] = BandState(); }
        mLL.init(maxLA);
        for (int c = 0; c < 2; c++) { dryTot[c].init(maxLat); mixDry[c].init(maxLat); }
        for (int c = 0; c < 2; c++) for (int j = 0; j < 2; j++) scEq[c][j] = Biq();
        scXo[0] = XO(); scXo[1] = XO();
        scEnvF = 0; scArm = false; scLast = -1e9; scT[0] = scT[1] = 1e9; scGain[0] = scGain[1] = 1; free = 0; prevBeat = -1;
        prepared = true; { MbcP m0 = p; setMBC(m0); } { ScP s0 = s; setSC(s0); }
    }
    int latencySamples() const { return xoLat + 2 * (LA - 1); }
    void setMBC(const MbcP& m) {
        p = m; if (!prepared) return;
        static const int Nmap[9] = {1, 2, 4, 6, 8, 10, 12, 14, 16}; const int N = Nmap[clampv(m.slope, 0, 8)];
        nb = clampv(m.bands, 1, NB); iirN = N;
        LA = std::max(1, (int)std::lround(m.sysLA * 0.001 * fs)); LA = std::min(LA, maxLA - 4);
        for (int b = 0; b < NB; b++) bandLL[b].setLA(LA);
        mLL.setLA(LA);
        linear = m.linearPhase;
        if (linear) { split.setSplit(fs, nb, m.x, m.slope); xoLat = split.latency(); }
        else {
            xoLat = 0;
            for (int c = 0; c < nb - 1; c++) for (int ch = 0; ch < 2; ch++) xo[c][ch].set(fs, m.x[c], N);
            for (int b = 2; b < nb; b++) for (int j = 0; j < b - 1; j++) for (int ch = 0; ch < 2; ch++) ap[b][j][ch].set(fs, m.x[j], N);
        }
        auto mk = [&](double ms) { return 1 - std::exp(-1 / (std::max(0.02, ms) * 0.001 * fs)); };
        for (int i = 0; i < NB; i++) {
            const BandP& b = m.b[i]; Q& q = bp[i];
            q.on = b.on; q.cOn = b.cOn; q.thr = b.thr; q.R = m.ratioSteps ? ratioOf(snapRatio(b.u)) : ratioOf(b.u); q.knee = b.knee;
            q.aA = mk(b.atk); q.relMs = b.rel; q.autoR = b.rel < 1; q.aR = mk(q.autoR ? 150 : b.rel);
            q.boost = D2L(b.boost); q.agm = D2L(std::min(24.0, -b.thr * (std::isfinite(q.R) ? 1 - 1 / q.R : 1.0)));
            q.lOn = b.lOn; q.lRel = 1 - std::exp(-1 / (std::max(1.0f, b.lRel) * 0.001 * fs)); q.lAuto = b.lRel < 1; q.ceil = D2L(b.lOut);
            q.inG = D2L(b.inG); q.outG = D2L(b.outG); q.mix = b.mix / 100.0; q.mute = b.mute; q.solo = b.solo;
            q.dOn = b.dOn; q.dStr = b.dStr / 100.0; q.dX2 = b.dX2; q.dFlip = b.dFlip; q.dPre = b.dPre;
        }
        anySolo = false; for (int i = 0; i < nb; i++) if (bp[i].solo) anySolo = true;
        mMix = m.mix / 100.0; mCeil = D2L(m.ceil); mKr = 1 - std::exp(-1 / (0.08 * fs));
        auto pan = [](double pp, double* o) { double a = (pp + 100) / 100.0 * M_PI / 4; o[0] = std::cos(a) * M_SQRT2; o[1] = std::sin(a) * M_SQRT2; };
        gIn = D2L(m.inG); gOut = D2L(m.outG); gPost = D2L(m.postG); pan(m.inPan, pIn); pan(m.outPan, pOut);
        kag = 1 / (0.4 * fs); kdet = 1 - std::exp(-1 / (0.0015 * fs)); krms = 1 - std::exp(-1 / (0.003 * fs));
    }
    void setSC(const ScP& sc) {
        s = sc; if (!prepared) return;
        scInG = D2L(sc.inG); scOutG = D2L(sc.outG);
        auto mkE = [&](int ft, int i, double f, double g, double q) {
            Biq& a = scEq[i][0]; Biq& b = scEq[i][1];
            if (ft == 1) { double gg = -std::max(6.0, std::fabs(g)) * 1.5; a.peak(fs, f, gg, std::max(q, 2.0)); b.peak(fs, f, 0, 1); }
            else if (ft == 2) { a.peak(fs, f, g, q); b.peak(fs, f, g, q); }
            else { a.peak(fs, f, g, q); b.peak(fs, f, 0, 1); }
        };
        mkE(sc.ftype, 0, sc.e1f, sc.e1g, sc.e1w); mkE(sc.ftype, 1, sc.e2f, sc.e2g, sc.e2w);
        scXo[0].set(fs, sc.split, 4); scXo[1].set(fs, sc.split, 4);
        scThr = -6 - (sc.sens / 100.0) * 54; lowVol = D2L(sc.lowVol); hiVol = D2L(sc.hiVol);
        static const double gridBeats[4] = {0.25, 0.5, 1, 2}; scGridBeats = gridBeats[clampv(sc.grid, 0, 3)];
        env[0] = {sc.lStr / 100.0, sc.lAtk * 0.001 * fs, sc.lHold * 0.001 * fs, sc.lRel * 0.001 * fs, sc.lAtkC, sc.lRelC};
        env[1] = {sc.hStr / 100.0, sc.hAtk * 0.001 * fs, sc.hHold * 0.001 * fs, sc.hRel * 0.001 * fs, sc.hAtkC, sc.hRelC};
    }
    // Process a stereo block in place. scL/scR: sidechain input (may be null). Returns nothing; latency is constant.
    void process(float* L, float* R, int n, const float* scL, const float* scR, const HostInfo& host) {
        if (!prepared) return;
        const int bypassDelay = xoLat + 2 * (LA - 1), mixDelay = xoLat + LA - 1;
        double bl[NB], br[NB]; double preP[2] = {0, 0}, postP[2] = {0, 0};
        const double bpm = host.bpm > 1 ? host.bpm : 120, spb = 60.0 / bpm;
        int mi = 0;
        for (int i = 0; i < n; i++) {
            double xl = L[i], xr = R[i];
            const double beat = host.playing ? host.ppq + i * bpm / 60.0 / fs : free + i * bpm / 60.0 / fs;
            const float dl = dryTot[0].proc((float)xl, bypassDelay), dr = dryTot[1].proc((float)xr, bypassDelay);
            double ol = dl, orr = dr;
            preP[0] = std::max(preP[0], std::fabs(xl)); preP[1] = std::max(preP[1], std::fabs(xr));
            if (p.on) {
                xl *= gIn * pIn[0]; xr *= gIn * pIn[1];
                if (nb == 1) { split_one(xl, xr, bl, br); }
                else if (linear) split.push(xl, xr, bl, br);
                else iirSplit(xl, xr, bl, br);
                double wl = 0, wr = 0;
                for (int b = 0; b < nb; b++) {
                    const Q& q = bp[b]; BandState& S = bs[b];
                    double l = bl[b] * q.inG, r = br[b] * q.inG;
                    const double dBl = bandDry[b][0].proc((float)l, LA - 1), dBr = bandDry[b][1].proc((float)r, LA - 1);
                    double al = l, ar = r, gdb = 0; const double lv = std::max(std::fabs(l), std::fabs(r));
                    if (q.on) {
                        if (q.cOn) {
                            double det;
                            if (p.rms) { S.pw += (lv * lv - S.pw) * krms; det = std::sqrt(S.pw); }
                            else { S.env = lv > S.env ? lv : lv + (S.env - lv) * (1 - kdet); det = S.env; }
                            const double ldb = 20 * std::log10(det + 1e-9); S.lvl = ldb;
                            const double tg = std::min(60.0, grCurve(ldb - q.thr, q.R, q.knee, 6));
                            double k; if (tg > S.cur) k = q.aA; else k = q.autoR ? 1 - std::exp(-1 / ((30 + 8 * std::max(S.cur, 0.0)) * 0.001 * fs)) : q.aR;
                            S.cur += (tg - S.cur) * k; gdb = S.cur;
                        } else { S.lvl = 20 * std::log10(lv + 1e-9); S.cur += (0 - S.cur) * 0.01; }
                        double g = std::exp(-gdb * 0.1151293) * (q.cOn ? q.boost : 1) * (p.autoGain && q.cOn ? q.agm : 1);
                        double dg = 1;
                        if (q.dOn) {
                            double cyc = q.dX2 ? 2 : 4, ph = std::fmod(std::fmod(beat / cyc, 1.0) + 1.0, 1.0);
                            double y = p.b[b].lut[(int)(ph * 512)]; if (q.dFlip) y = 1 - y;
                            dg = std::exp(-(1 - y) * 24 * q.dStr * 0.1151293);
                            S.dpcG += (dg - S.dpcG) * 0.02; dg = S.dpcG;
                        }
                        if (q.dOn && q.dPre) g *= dg;
                        al = l * g; ar = r * g;
                        LL& Lb = bandLL[b];
                        Lb.proc(al, ar, q.ceil, q.lAuto ? 1 - std::exp(-1 / ((40 + 12 * std::min(S.lgr, 20.0)) * 0.001 * fs)) : q.lRel, q.lOn);
                        al = Lb.ol; ar = Lb.orr; S.lgr = q.lOn ? Lb.gr : 0;
                        if (q.dOn && !q.dPre) { al *= dg; ar *= dg; }
                        S.gr = gdb;
                    } else {
                        LL& Lb = bandLL[b]; Lb.proc(al, ar, 1e9, 1, false); al = Lb.ol; ar = Lb.orr;
                        S.cur += (0 - S.cur) * 0.01; S.lgr = 0; S.gr = 0; S.lvl = 20 * std::log10(lv + 1e-9);
                    }
                    al = dBl * (1 - q.mix) + al * q.mix; ar = dBr * (1 - q.mix) + ar * q.mix;
                    al *= q.outG; ar *= q.outG;
                    const bool audible = !q.mute && (!anySolo || q.solo);
                    if (audible) { wl += al; wr += ar; }
                }
                const double tdl = mixDry[0].proc((float)xl, mixDelay), tdr = mixDry[1].proc((float)xr, mixDelay);   // true dry, delayed like the wet path
                ol = tdl * (1 - mMix) + wl * mMix; orr = tdr * (1 - mMix) + wr * mMix;
                ol *= gOut; orr *= gOut;
                mLL.proc(ol, orr, mCeil, mKr, p.limOn); ol = mLL.ol * gPost; orr = mLL.orr * gPost; mGr = p.limOn ? (float)mLL.gr : 0.f;
            } else { mixDry[0].proc((float)xl, mixDelay); mixDry[1].proc((float)xr, mixDelay); }
            // ---------- page 2: multiband sidechain ----------
            if (s.on) {
                const double sg = scInG;
                const double fl = scL ? scL[i] : 0, fr = scR ? scR[i] : 0;
                double fsrc = (s.feed == 0 && scL) ? (fl + fr) * 0.5 : (ol + orr) * 0.5; fsrc *= sg;
                double u = fsrc; u = scEq[0][0].proc(u); u = scEq[0][1].proc(u); u = scEq[1][0].proc(u); u = scEq[1][1].proc(u);
                const double au = std::fabs(u); bool trig = false;
                if (s.trig == 0) {
                    scEnvF = au > scEnvF ? au : au + (scEnvF - au) * 0.9995; const double ldb = 20 * std::log10(scEnvF + 1e-9);
                    if (!scArm && ldb > scThr && (sampleCount - scLast) > 0.03 * fs) { scArm = true; trig = true; } else if (scArm && ldb < scThr - 6) scArm = false;
                } else if (s.trig == 1) {
                    while (mi < host.nMidiOn && host.midiOn[mi] < i) mi++;
                    while (mi < host.nMidiOn && host.midiOn[mi] == i) { trig = true; mi++; }
                } else {
                    const double ph = beat / scGridBeats - s.offset; const double pc = std::floor(ph);
                    if (prevBeat < -0.5e9) prevBeat = pc;
                    if (pc != prevBeat) { prevBeat = pc; trig = true; }
                }
                if (trig) { scLast = (double)sampleCount; scT[0] = scT[1] = 0; }
                const double gl = envGain(env[0], scT[0]), gh = envGain(env[1], scT[1]); scT[0]++; scT[1]++;
                scGain[0] += (gl - scGain[0]) * 0.12; scGain[1] += (gh - scGain[1]) * 0.12;
                scXo[0].proc(ol * sg); scXo[1].proc(orr * sg);
                double a0 = scXo[0].lo * scGain[0] * lowVol, a1 = scXo[1].lo * scGain[0] * lowVol, b0 = scXo[0].hi * scGain[1] * hiVol, b1 = scXo[1].hi * scGain[1] * hiVol;
                if (s.listen == 1) b0 = b1 = 0; else if (s.listen == 2) a0 = a1 = 0;
                ol = (a0 + b0) * scOutG; orr = (a1 + b1) * scOutG;
                scLow = (float)scGain[0]; scHigh = (float)scGain[1];
            }
            if (bypass) { ol = dl; orr = dr; }
            if (!std::isfinite(ol) || !std::isfinite(orr)) { ol = orr = 0; reset(); }
            L[i] = (float)ol; R[i] = (float)orr; sampleCount++;
            postP[0] = std::max(postP[0], std::fabs(ol)); postP[1] = std::max(postP[1], std::fabs(orr));
            { const float k = 0.00015f; preMs[0] += ((float)(dl * dl) - preMs[0]) * k; preMs[1] += ((float)(dr * dr) - preMs[1]) * k; postMs[0] += ((float)(ol * ol) - postMs[0]) * k; postMs[1] += ((float)(orr * orr) - postMs[1]) * k; }
        }
        free += n * bpm / 60.0 / fs; (void)spb;
        const float dec = (float)std::pow(0.9, n / 512.0);
        for (int c = 0; c < 2; c++) { prePk[c] = std::max((float)preP[c], prePk[c] * dec); postPk[c] = std::max((float)postP[c], postPk[c] * dec); }
        for (int b = 0; b < NB; b++) { bandGr[b] = (float)bs[b].cur; bandLvl[b] = (float)bs[b].lvl; bandLimGr[b] = (float)bs[b].lgr; }
    }
    void reset() {
        for (auto& a : xo) for (auto& x : a) x.reset();
        for (auto& a : ap) for (auto& pr : a) for (auto& x : pr) x.reset();
        for (auto& st : bs) st.env = st.pw = st.cur = st.agr = 0;
    }
    MbcP p; ScP s;
private:
    struct Q { bool on, cOn, knee, lOn, lAuto, autoR, mute, solo, dOn, dX2, dFlip, dPre; double thr, R, aA, relMs, aR, boost, agm, lRel, ceil, inG, outG, mix, dStr; };
    Q bp[NB]; BandState bs[NB];
    XO xo[NB - 1][2], ap[NB][2][2]; FirSplit split; LL bandLL[NB], mLL; DL bandDry[NB][2], dryTot[2], mixDry[2];
    XO scXo[2]; Biq scEq[2][2]; Env env[2];
    int nb = 4, iirN = 8, LA = 1, maxLA = 1, xoLat = 0, maxLat = 0; bool linear = true, prepared = false, anySolo = false;
    double mMix = 0.5, mCeil = 1, mKr = 0, gIn = 1, gOut = 1, gPost = 1, pIn[2] = {1, 1}, pOut[2] = {1, 1}, kag = 0, kdet = 0, krms = 0;
    double scInG = 1, scOutG = 1, scThr = -40, lowVol = 1, hiVol = 1, scGridBeats = 1, scEnvF = 0, scLast = -1e9, scT[2] = {1e9, 1e9}, scGain[2] = {1, 1}, free = 0, prevBeat = -1e9 - 1;
    bool scArm = false; long long sampleCount = 0;
    void split_one(double xl, double xr, double* bl, double* br) {
        if (linear) { double a[NB], b[NB]; split.push(xl, xr, a, b); bl[0] = a[0]; br[0] = b[0]; } else { bl[0] = xl; br[0] = xr; }
    }
    void iirSplit(double xl, double xr, double* bl, double* br) {
        double sl = xl, sr = xr;
        for (int c = nb - 2; c >= 0; c--) {
            XO& a = xo[c][0]; XO& b = xo[c][1]; a.proc(sl); b.proc(sr); double hl = a.hi, hr = b.hi;
            for (int j = 0; j < c; j++) { XO& pa = ap[c + 1][j][0]; XO& pb = ap[c + 1][j][1]; pa.proc(hl); pb.proc(hr); hl = pa.lo + pa.hi; hr = pb.lo + pb.hi; }
            bl[c + 1] = hl; br[c + 1] = hr; sl = a.lo; sr = b.lo;
        }
        bl[0] = sl; br[0] = sr;
    }
};

} // namespace multipaw
