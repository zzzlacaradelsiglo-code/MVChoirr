#pragma once
// MV CHOIR//LAB - motor de coro vocal (C++ puro, sin JUCE).
// Toma TU voz real, la analiza una sola vez por bloque (FFT) y genera hasta 10 voces:
// cada voz cambia el tono con un vocoder de fase con bloqueo de picos y conserva los formantes
// (la "garganta"), para que suene natural y no como una ardilla.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace mvc
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr int kVoices = 10;
constexpr int kFrame = 2048;
#ifndef MVC_OSAMP
#define MVC_OSAMP 4
#endif
constexpr int kOsamp = MVC_OSAMP;
constexpr int kHop = kFrame / kOsamp;
constexpr int kBins = kFrame / 2 + 1;
constexpr int kDelayLen = 16384;
constexpr int kDryLen = 4096;
constexpr int kEnvIter = 3;

struct Rng
{
    uint32_t s = 22222u;
    void seed (uint32_t v) { s = v ? v : 1u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
};

class Fft
{
public:
    void init()
    {
        for (int i = 0; i < kFrame; ++i)
        {
            int r = 0, x = i;
            for (int b = 1; b < kFrame; b <<= 1) { r = (r << 1) | (x & 1); x >>= 1; }
            rev[i] = r;
        }
        for (int i = 0; i < kFrame / 2; ++i)
        {
            cosT[i] = (float) std::cos (kTwoPi * i / kFrame);
            sinT[i] = (float) std::sin (kTwoPi * i / kFrame);
        }
    }

    // sin escalar en ningun sentido: la inversa hay que dividirla por kFrame
    void transform (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < kFrame; ++i)
        {
            const int j = rev[i];
            if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= kFrame; len <<= 1)
        {
            const int half = len >> 1, step = kFrame / len;
            for (int i = 0; i < kFrame; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[k * step];
                    const float wi = inverse ? sinT[k * step] : -sinT[k * step];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
    }

private:
    int rev[kFrame] = {};
    float cosT[kFrame / 2] = {};
    float sinT[kFrame / 2] = {};
};

struct VoiceParams
{
    bool active = false;
    float pitchSt = 0.f;   // semitonos
    float fineCents = 0.f; // cents
    float formantSt = 0.f; // 0 = formantes naturales (conservados); +/- los mueve
    float delayMs = 0.f;
    float humanize = 0.f;  // 0..1
    float level = 1.f;     // lineal
    float pan = 0.f;       // -1..1
    float bright = 0.f;    // -1..1
    float send = 0.f;      // envio a reverb 0..1
    int mutMode = 0;       // 0 off, 1 whisper, 2 blur, 3 morph
    float mutAmt = 0.f;    // 0..1
};

struct Macros
{
    float detune = 0.f, humanize = 0.f, movement = 0.f, width = 1.f;
};

class Engine
{
public:
    Engine() = default;

    int latencySamples() const { return kFrame; }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        fft.init();
        for (int i = 0; i < kFrame; ++i)
            win[i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / kFrame));
        double s = 0.0;
        for (int m = 0; m < kOsamp; ++m)
        {
            const double w = win[m * kHop + kHop / 2];
            s += w * w;
        }
        olaNorm = (float) (1.0 / s / kFrame);

        const int Q = std::max (8, (int) (sr / 700.0));
        for (int n = 0; n < kFrame; ++n)
        {
            const int d = std::min (n, kFrame - n);
            const double flat = 0.75 * Q;
            double w = 0.0;
            if (d <= flat) w = 1.0;
            else if (d < Q) w = 0.5 + 0.5 * std::cos (kPi * (d - flat) / (Q - flat));
            lift[n] = (float) (w / kFrame);
        }

        toneA = (float) (1.0 - std::exp (-kTwoPi * 1500.0 / sr));
        std::memset (ring, 0, sizeof (ring));
        std::memset (dryRingL, 0, sizeof (dryRingL));
        std::memset (dryRingR, 0, sizeof (dryRingR));
        std::memset (lastPhase, 0, sizeof (lastPhase));
        wpos = hopCount = outPos = dryPos = 0;

        for (int v = 0; v < kVoices; ++v)
        {
            VoiceState& S = voice[v];
            std::memset (static_cast<void*> (&S), 0, sizeof (S)); // evita un temporal gigante en la pila
            S.ratio = 1.0;
            S.first = true;
            S.lfoRate = 0.3f;
            S.rng = Rng{};
            S.rng.seed (1234u + 7919u * (uint32_t) (v + 1));
            S.detuneConst = S.rng.bi();
            S.lfoRate = 0.15f + 0.3f * S.rng.uni();
            S.lfoPh = S.rng.uni() * (float) kTwoPi;
        }
    }

    void setVoice (int v, const VoiceParams& p)
    {
        vp[v] = p;
        toneHi[v] = std::pow (10.0f, p.bright * 6.0f / 20.0f);
        toneLo[v] = std::pow (10.0f, -p.bright * 6.0f / 20.0f);
    }

    void setMacros (const Macros& m) { macros = m; }

    float takeVoicePeak (int v)
    {
        const float p = voice[v].peak;
        voice[v].peak = 0.f;
        return p;
    }

    // Entrada estereo -> salidas estereo por voz (con delay, tono, nivel y paneo) + envio a reverb + dry alineado
    void process (const float* inL, const float* inR, int n,
                  float* const* voiceL, float* const* voiceR,
                  float* sendL, float* sendR, float* dryL, float* dryR)
    {
        // objetivos de paneo/nivel por bloque
        float tgtL[kVoices], tgtR[kVoices], tgtDelay[kVoices];
        for (int v = 0; v < kVoices; ++v)
        {
            const VoiceParams& P = vp[v];
            const float pan = std::clamp ((P.pan + voice[v].panMove) * macros.width, -1.0f, 1.0f);
            if (useDry (v))
            {
                tgtL[v] = P.level * (pan > 0.f ? 1.0f - pan : 1.0f);
                tgtR[v] = P.level * (pan < 0.f ? 1.0f + pan : 1.0f);
            }
            else
            {
                const float ang = (pan + 1.0f) * 0.25f * (float) kPi;
                tgtL[v] = P.level * std::cos (ang);
                tgtR[v] = P.level * std::sin (ang);
            }
            tgtDelay[v] = std::clamp ((P.delayMs + voice[v].jitterMs) * (float) sr * 0.001f, 0.0f, (float) (kDelayLen - 4));
        }

        for (int i = 0; i < n; ++i)
        {
            const float l = inL[i], r = inR[i];
            dryRingL[dryPos] = l;
            dryRingR[dryPos] = r;
            const int rd = (dryPos - kFrame) & (kDryLen - 1);
            const float dl = dryRingL[rd], dr = dryRingR[rd];
            dryPos = (dryPos + 1) & (kDryLen - 1);
            dryL[i] = dl;
            dryR[i] = dr;

            ring[wpos] = 0.5f * (l + r);
            wpos = (wpos + 1) & (kFrame - 1);
            if (++hopCount >= kHop)
            {
                hopCount = 0;
                processFrame();
                outPos = 0;
            }

            float sumL = 0.f, sumR = 0.f;
            for (int v = 0; v < kVoices; ++v)
            {
                VoiceState& S = voice[v];
                const VoiceParams& P = vp[v];
                if (! P.active)
                {
                    voiceL[v][i] = 0.f;
                    voiceR[v][i] = 0.f;
                    continue;
                }
                float sl, sr2;
                if (useDry (v)) { sl = dl; sr2 = dr; }
                else { sl = sr2 = S.outFifo[outPos]; }

                // delay fraccionario
                S.dly[0][S.dpos] = sl;
                S.dly[1][S.dpos] = sr2;
                S.dSmooth += (tgtDelay[v] - S.dSmooth) * 0.002f;
                const int di = (int) S.dSmooth;
                const float fr = S.dSmooth - (float) di;
                const int i0 = (S.dpos - di) & (kDelayLen - 1);
                const int i1 = (i0 - 1) & (kDelayLen - 1);
                float xl = S.dly[0][i0] * (1.0f - fr) + S.dly[0][i1] * fr;
                float xr = S.dly[1][i0] * (1.0f - fr) + S.dly[1][i1] * fr;
                S.dpos = (S.dpos + 1) & (kDelayLen - 1);

                // tono (inclinacion +/-6 dB alrededor de 1.5 kHz)
                S.lp[0] += toneA * (xl - S.lp[0]);
                S.lp[1] += toneA * (xr - S.lp[1]);
                xl = S.lp[0] * toneLo[v] + (xl - S.lp[0]) * toneHi[v];
                xr = S.lp[1] * toneLo[v] + (xr - S.lp[1]) * toneHi[v];

                S.gL += (tgtL[v] - S.gL) * 0.003f;
                S.gR += (tgtR[v] - S.gR) * 0.003f;
                const float oL = xl * S.gL, oR = xr * S.gR;
                voiceL[v][i] = oL;
                voiceR[v][i] = oR;
                sumL += P.send * xl; // envio pre-fader: una voz "solo efecto" puede tener nivel 0 y seguir alimentando la reverb
                sumR += P.send * xr;
                S.peak = std::max (S.peak, std::max (std::fabs (oL), std::fabs (oR)));
            }
            sendL[i] = sumL;
            sendR[i] = sumR;
            ++outPos;
            if (outPos >= kHop) outPos = kHop - 1; // seguridad (no deberia pasar)
        }
    }

private:
    struct VoiceState
    {
        float acc[kFrame] = {};
        float outFifo[kHop] = {};
        float magS[kBins] = {};
        int prevQ[kBins] = {};
        double prevTheta[kBins] = {};
        int curQ[kBins] = {};
        double curTheta[kBins] = {};
        int nPrev = 0;
        float dly[2][kDelayLen] = {};
        int dpos = 0;
        float lp[2] = { 0.f, 0.f };
        float gL = 0.f, gR = 0.f;
        float dSmooth = 0.f;
        float peak = 0.f;
        double ratio = 1.0;
        bool first = true;
        float drift = 0.f, drift2 = 0.f;
        float detuneConst = 0.f, lfoPh = 0.f, lfoRate = 0.3f;
        float panMove = 0.f, jitterMs = 0.f;
        Rng rng;
    };

    bool useDry (int v) const
    {
        const VoiceParams& P = vp[v];
        return v == 0 && P.pitchSt == 0.f && P.fineCents == 0.f && P.formantSt == 0.f && P.mutMode == 0;
    }

    static double wrapPi (double x) { return x - kTwoPi * std::floor ((x + kPi) / kTwoPi); }

    float envLogAt (float bin) const
    {
        if (bin <= 0.f) return aEnvLog[0];
        if (bin >= (float) (kBins - 1)) return aEnvLog[kBins - 1];
        const int k = (int) bin;
        const float f = bin - (float) k;
        return aEnvLog[k] * (1.0f - f) + aEnvLog[k + 1] * f;
    }

    void computeEnvelope()
    {
        for (int k = 0; k < kBins; ++k)
        {
            logm[k] = std::log (aMag[k] + 1e-6f);
            target[k] = logm[k];
        }
        for (int it = 0; it < kEnvIter; ++it)
        {
            for (int k = 0; k < kBins; ++k) re[k] = target[k];
            for (int k = 1; k < kBins - 1; ++k) re[kFrame - k] = target[k];
            std::memset (im, 0, sizeof (im));
            fft.transform (re, im, false);
            for (int n = 0; n < kFrame; ++n) { re[n] *= lift[n]; im[n] = 0.f; }
            fft.transform (re, im, false);
            for (int k = 0; k < kBins; ++k)
            {
                aEnvLog[k] = re[k];
                target[k] = std::max (logm[k], re[k]);
            }
        }
    }

    void processFrame()
    {
        for (int i = 0; i < kFrame; ++i)
        {
            re[(i + kFrame / 2) & (kFrame - 1)] = ring[(wpos + i) & (kFrame - 1)] * win[i];
            im[i] = 0.f;
        }
        fft.transform (re, im, false);

        float maxMag = 0.f;
        const double expct = kTwoPi * kHop / kFrame;
        for (int k = 0; k < kBins; ++k)
        {
            aRe[k] = re[k];
            aIm[k] = im[k];
            const float m = std::sqrt (re[k] * re[k] + im[k] * im[k]);
            aMag[k] = m;
            maxMag = std::max (maxMag, m);
            const double ph = std::atan2 ((double) im[k], (double) re[k]);
            double d = ph - lastPhase[k];
            lastPhase[k] = ph;
            d = wrapPi (d - expct * k);
            aOmega[k] = (float) (kTwoPi * k / kFrame + d / kHop);
        }

        nPeaks = 0;
        if (maxMag > 1e-5f)
        {
            computeEnvelope();
            const float thr = std::max (maxMag * 0.001f, 1e-7f);
            for (int k = 2; k < kBins - 2; ++k)
            {
                const float m = aMag[k];
                if (m > thr && m >= aMag[k - 1] && m > aMag[k + 1] && m >= aMag[k - 2] && m > aMag[k + 2])
                {
                    float localMax = m;
                    const int a = std::max (0, k - 6), b = std::min (kBins - 1, k + 6);
                    for (int i = a; i <= b; ++i) localMax = std::max (localMax, aMag[i]);
                    if (m >= 0.1f * localMax) // descarta lobulos laterales de la ventana
                        peakIdx[nPeaks++] = k;
                }
            }
            for (int j = 0; j < nPeaks; ++j)
            {
                regLo[j] = (j == 0) ? 0 : regHi[j - 1] + 1;
                if (j == nPeaks - 1)
                    regHi[j] = kBins - 1;
                else
                {
                    int best = peakIdx[j];
                    float bm = aMag[best];
                    for (int k = peakIdx[j]; k <= peakIdx[j + 1]; ++k)
                        if (aMag[k] < bm) { bm = aMag[k]; best = k; }
                    regHi[j] = best;
                }
            }
        }

        for (int v = 0; v < kVoices; ++v)
        {
            VoiceState& S = voice[v];
            if (! vp[v].active || useDry (v) || nPeaks == 0)
            {
                std::memset (S.outFifo, 0, sizeof (S.outFifo));
                if (! vp[v].active) std::memset (S.acc, 0, sizeof (S.acc));
                S.nPrev = 0;
                if (nPeaks == 0 && vp[v].active && ! useDry (v))
                {
                    // silencio: dejar vaciar el acumulador
                    for (int j = 0; j < kHop; ++j) S.outFifo[j] = S.acc[j];
                    std::memmove (S.acc, S.acc + kHop, (kFrame - kHop) * sizeof (float));
                    std::memset (S.acc + kFrame - kHop, 0, kHop * sizeof (float));
                }
                continue;
            }
            synthVoice (v);
        }
    }

    void synthVoice (int v)
    {
        VoiceState& S = voice[v];
        const VoiceParams& P = vp[v];

        // ---- modulaciones por voz (el MAIN no las usa) ----
        double cents = (double) P.pitchSt * 100.0 + (double) P.fineCents;
        float fSt = P.formantSt;
        S.lfoPh += (float) (kTwoPi * S.lfoRate * kHop / sr);
        if (S.lfoPh > (float) kTwoPi) S.lfoPh -= (float) kTwoPi;
        S.drift = S.drift * 0.985f + S.rng.bi() * 0.12f;
        S.drift2 = S.drift2 * 0.985f + S.rng.bi() * 0.12f;
        if (v > 0)
        {
            const float hum = std::min (1.0f, P.humanize + macros.humanize);
            cents += hum * 18.0f * S.drift;
            S.jitterMs = hum * 5.0f * S.drift2;
            cents += macros.movement * 12.0f * std::sin (S.lfoPh);
            S.panMove = macros.movement * 0.3f * std::sin (S.lfoPh * 0.73f + 1.3f);
            cents += macros.detune * 30.0f * S.detuneConst;
        }
        if (P.mutMode == 3) fSt += P.mutAmt * (5.0f * std::sin (S.lfoPh * 1.9f) + 3.0f * S.drift2);

        const double rt = std::exp2 (cents / 1200.0);
        if (S.first) { S.ratio = rt; S.first = false; }
        else S.ratio += (rt - S.ratio) * 0.35;
        const double r = S.ratio;
        const double fr = std::exp2 ((double) fSt / 12.0);

        // ---- mutaciones ----
        const bool whisper = (P.mutMode == 1 && P.mutAmt > 0.001f);
        const bool blur = (P.mutMode == 2 && P.mutAmt > 0.001f);
        const float blurA = 1.0f - 0.97f * P.mutAmt;
        if (blur)
            for (int k = 0; k < kBins; ++k) S.magS[k] += blurA * (aMag[k] - S.magS[k]);

        std::memset (yre, 0, sizeof (yre));
        std::memset (yim, 0, sizeof (yim));
        std::memset (ymag, 0, sizeof (ymag));

        int newN = 0;
        int pp = 0;
        for (int j = 0; j < nPeaks; ++j)
        {
            const int p = peakIdx[j];
            const double ws = (double) aOmega[p] * r;
            const double fb = (double) aOmega[p] * kFrame / kTwoPi;     // posicion real del pico, en bins
            const int delta = (int) std::lround ((r - 1.0) * fb);        // desplazamiento entero del lobulo
            const int q = p + delta;
            if (q < 1 || q >= kBins - 1) continue;
            const float magP = aMag[p];
            if (magP < 1e-12f) continue;

            double theta;
            while (pp + 1 < S.nPrev && std::abs (S.prevQ[pp + 1] - q) <= std::abs (S.prevQ[pp] - q)) ++pp;
            if (S.nPrev > 0 && std::abs (S.prevQ[pp] - q) <= 2)
                theta = S.prevTheta[pp] + (double) kHop * ws;
            else
                theta = std::atan2 ((double) aIm[p], (double) aRe[p]);
            theta = wrapPi (theta);
            S.curQ[newN] = q;
            S.curTheta[newN] = theta;
            ++newN;

            // ganancia de formantes: envolvente original en la frecuencia de salida / en la de entrada
            float g = std::exp (envLogAt ((float) ((double) q / fr)) - aEnvLog[p]);
            g = std::clamp (g, 0.1f, 10.0f);

            const float ct = (float) std::cos (theta), st = (float) std::sin (theta);
            const float cr = aRe[p] / magP, ci = -aIm[p] / magP;
            const float zr = g * (ct * cr - st * ci);
            const float zi = g * (ct * ci + st * cr);
            for (int k = regLo[j]; k <= regHi[j]; ++k)
            {
                const int kk = k + delta;
                if (kk < 0 || kk >= kBins) continue;
                float xr = aRe[k], xi = aIm[k];
                if (blur)
                {
                    const float sc = std::min (20.0f, S.magS[k] / (aMag[k] + 1e-9f));
                    xr *= sc;
                    xi *= sc;
                }
                float yr = xr * zr - xi * zi;
                float yi = xr * zi + xi * zr;
                if (whisper)
                {
                    const float rot = P.mutAmt * (float) kPi * S.rng.bi();
                    const float c2 = std::cos (rot), s2 = std::sin (rot);
                    const float tr = yr * c2 - yi * s2;
                    yi = yr * s2 + yi * c2;
                    yr = tr;
                }
                const float mg = yr * yr + yi * yi;
                if (mg > ymag[kk])
                {
                    ymag[kk] = mg;
                    yre[kk] = yr;
                    yim[kk] = yi;
                }
            }
        }
        std::memcpy (S.prevQ, S.curQ, newN * sizeof (int));
        std::memcpy (S.prevTheta, S.curTheta, newN * sizeof (double));
        S.nPrev = newN;

        // ---- sintesis ----
        for (int k = 0; k < kBins; ++k) { re[k] = yre[k]; im[k] = yim[k]; }
        for (int k = 1; k < kFrame / 2; ++k) { re[kFrame - k] = yre[k]; im[kFrame - k] = -yim[k]; }
        im[0] = 0.f;
        im[kFrame / 2] = 0.f;
        fft.transform (re, im, true);
        for (int i = 0; i < kFrame; ++i)
            S.acc[i] += re[(i + kFrame / 2) & (kFrame - 1)] * win[i] * olaNorm;
        for (int j = 0; j < kHop; ++j) S.outFifo[j] = S.acc[j];
        std::memmove (S.acc, S.acc + kHop, (kFrame - kHop) * sizeof (float));
        std::memset (S.acc + kFrame - kHop, 0, kHop * sizeof (float));
    }

    double sr = 44100.0;
    Fft fft;
    Macros macros;
    VoiceParams vp[kVoices];
    float toneHi[kVoices] = {}, toneLo[kVoices] = {};
    float toneA = 0.2f;
    float olaNorm = 1.f;
    float win[kFrame] = {};
    float lift[kFrame] = {};
    float ring[kFrame] = {};
    float dryRingL[kDryLen] = {}, dryRingR[kDryLen] = {};
    int wpos = 0, hopCount = 0, outPos = 0, dryPos = 0;

    // analisis compartido
    float re[kFrame] = {}, im[kFrame] = {};
    float aRe[kBins] = {}, aIm[kBins] = {}, aMag[kBins] = {}, aOmega[kBins] = {};
    float aEnvLog[kBins] = {}, logm[kBins] = {}, target[kBins] = {};
    double lastPhase[kBins] = {};
    int peakIdx[kBins] = {}, regLo[kBins] = {}, regHi[kBins] = {};
    int nPeaks = 0;
    float yre[kBins] = {}, yim[kBins] = {}, ymag[kBins] = {};

    VoiceState voice[kVoices];
};

} // namespace mvc
