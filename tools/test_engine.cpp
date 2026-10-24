// Pruebas objetivas del motor: g++ -O2 -std=c++17 tools/test_engine.cpp -o test_engine && ./test_engine
#include "../Source/VocalEngine.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using namespace mvc;

static void writeWav (const char* path, const std::vector<float>& x, int sr)
{
    FILE* f = fopen (path, "wb");
    const uint32_t n = (uint32_t) x.size(), bytes = n * 2, riff = 36 + bytes, rate = (uint32_t) sr, br = rate * 2;
    const uint16_t fmt = 1, ch = 1, al = 2, bits = 16; const uint32_t fl = 16;
    fwrite ("RIFF", 1, 4, f); fwrite (&riff, 4, 1, f); fwrite ("WAVEfmt ", 1, 8, f); fwrite (&fl, 4, 1, f);
    fwrite (&fmt, 2, 1, f); fwrite (&ch, 2, 1, f); fwrite (&rate, 4, 1, f); fwrite (&br, 4, 1, f);
    fwrite (&al, 2, 1, f); fwrite (&bits, 2, 1, f); fwrite ("data", 1, 4, f); fwrite (&bytes, 4, 1, f);
    for (float v : x) { int16_t s = (int16_t) std::clamp (v * 32767.f, -32767.f, 32767.f); fwrite (&s, 2, 1, f); }
    fclose (f);
}

// Voz sintetica de prueba: tren de pulsos con vibrato -> 3 resonadores (formantes de una "a")
struct Reson { double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0;
    void set (double f, double bw, double sr) { double r = std::exp (-kPi * bw / sr); a1 = 2 * r * std::cos (kTwoPi * f / sr); a2 = -r * r; g = 1 - r; }
    double run (double x) { double y = g * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; } };

static std::vector<float> makeVoice (double sr, double secs, double f0Start, double f0End, double F1, double F2, double F3)
{
    std::vector<float> o; Reson r1, r2, r3; r1.set (F1, 90, sr); r2.set (F2, 110, sr); r3.set (F3, 160, sr);
    double ph = 0, vph = 0; const int n = (int) (secs * sr);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr, f0 = (f0Start + (f0End - f0Start) * t / secs) * std::pow (2.0, 0.25 * std::sin (kTwoPi * 5.5 * t) / 12.0);
        ph += f0 / sr; double pulse = 0; if (ph >= 1.0) { ph -= 1.0; pulse = 1.0; }
        (void) vph;
        const double e = pulse * 40.0;
        const double y = r1.run (e) + 0.7 * r2.run (e) + 0.4 * r3.run (e);
        const double env = std::min (1.0, t * 5.0) * std::min (1.0, (secs - t) * 5.0);
        o.push_back ((float) (y * env * 0.5));
    }
    return o;
}

int main()
{
    const double sr = 44100.0;
    std::unique_ptr<Engine> eng (new Engine());
    eng->prepare (sr);
    const int block = 256;

    auto run = [&] (const std::vector<float>& in, std::vector<std::vector<float>>& out, std::vector<float>& dry)
    {
        out.assign (kVoices, {}); dry.clear();
        std::vector<float> vl[kVoices], vr[kVoices], sl (block), sr2 (block), dl (block), dr (block), inl (block), inr (block);
        float* pl[kVoices]; float* pr[kVoices];
        for (int v = 0; v < kVoices; ++v) { vl[v].assign (block, 0.f); vr[v].assign (block, 0.f); pl[v] = vl[v].data(); pr[v] = vr[v].data(); }
        for (size_t pos = 0; pos < in.size(); pos += block)
        {
            const int n = (int) std::min<size_t> (block, in.size() - pos);
            for (int i = 0; i < n; ++i) inl[i] = inr[i] = in[pos + i];
            eng->process (inl.data(), inr.data(), n, pl, pr, sl.data(), sr2.data(), dl.data(), dr.data());
            for (int v = 0; v < kVoices; ++v) for (int i = 0; i < n; ++i) out[v].push_back (vl[v][i] + vr[v][i]);
            for (int i = 0; i < n; ++i) dry.push_back (dl[i]);
        }
    };

    // --- Prueba 1: seno 440 Hz, +7 st ---
    {
        std::vector<float> in; for (int i = 0; i < (int) (sr * 2); ++i) in.push_back (0.5f * (float) std::sin (kTwoPi * 440.0 * i / sr));
        VoiceParams p; p.active = true; p.pitchSt = 7.f; p.level = 1.f; p.pan = -1.f; // pan -1: todo a la izquierda (cos(0)=1)
        for (int v = 0; v < kVoices; ++v) { VoiceParams q; eng->setVoice (v, q); }
        eng->setVoice (1, p);
        std::vector<std::vector<float>> out; std::vector<float> dry; run (in, out, dry);
        // frecuencia por cruces por cero en el ultimo segundo
        const auto& y = out[1]; int zc = 0; size_t a = y.size() / 2;
        for (size_t i = a + 1; i < y.size(); ++i) if (y[i - 1] < 0 && y[i] >= 0) ++zc;
        const double f = zc / ((y.size() - a) / sr); double pk = 0; for (size_t i = a; i < y.size(); ++i) pk = std::max<double> (pk, std::fabs (y[i]));
        printf ("[1] seno 440 +7st: esperado %.2f Hz, medido %.2f Hz, pico %.3f (entrada 0.5)\n", 440 * std::pow (2.0, 7.0 / 12), f, pk);
    }

    // --- Prueba 2: latencia ratio=1 (voz PV 1 con pitch 0 contra dry) ---
    {
        auto in = makeVoice (sr, 2.0, 140, 140, 700, 1200, 2600);
        for (int v = 0; v < kVoices; ++v) { VoiceParams q; eng->setVoice (v, q); }
        VoiceParams p; p.active = true; p.level = 1.f; p.pan = -1.f; eng->setVoice (1, p);
        std::vector<std::vector<float>> out; std::vector<float> dry; run (in, out, dry);
        int bestLag = 0; double best = -1e30;
        for (int lag = -64; lag <= 64; ++lag)
        {
            double s = 0; for (size_t i = 8000; i < 60000; ++i) s += (double) out[1][i + lag] * dry[i];
            if (s > best) { best = s; bestLag = lag; }
        }
        double num = 0, den = 0; for (size_t i = 8000; i < 60000; ++i) { num += (double) (out[1][i + bestLag] - dry[i]) * (out[1][i + bestLag] - dry[i]); den += (double) dry[i] * dry[i]; }
        printf ("[2] PV con pitch 0 vs dry: desfase %d muestras (debe ser 0), error relativo %.1f dB\n", bestLag, 10 * std::log10 (num / den + 1e-12));
    }

    // --- Prueba 3: voz sintetica, +4 st y -5 st; F0 y formantes ---
    {
        auto in = makeVoice (sr, 4.0, 120, 150, 700, 1200, 2600);
        for (int v = 0; v < kVoices; ++v) { VoiceParams q; eng->setVoice (v, q); }
        VoiceParams a; a.active = true; a.pitchSt = 4.f; a.level = 1.f; a.pan = -1.f; eng->setVoice (1, a);
        VoiceParams b; b.active = true; b.pitchSt = -5.f; b.level = 1.f; b.pan = -1.f; eng->setVoice (2, b);
        VoiceParams c; c.active = true; c.pitchSt = 4.f; c.formantSt = 4.f; c.level = 1.f; c.pan = -1.f; eng->setVoice (3, c); // formantes movidos con el tono
        std::vector<std::vector<float>> out; std::vector<float> dry; run (in, out, dry);
        writeWav ("t3_in.wav", dry, (int) sr); writeWav ("t3_p4.wav", out[1], (int) sr); writeWav ("t3_m5.wav", out[2], (int) sr); writeWav ("t3_p4_fshift.wav", out[3], (int) sr);
        printf ("[3] WAV de voz sintetica escritos (t3_*.wav) para analisis en python\n");
    }

    // --- Prueba 4: mutaciones, doblador, ruido y silencio: sin NaN / explosiones ---
    {
        for (int v = 0; v < kVoices; ++v) { VoiceParams q; eng->setVoice (v, q); }
        for (int v = 0; v < kVoices; ++v)
        {
            VoiceParams q; q.active = true; q.pitchSt = (float) (v - 4) * 1.7f; q.fineCents = 12.f; q.humanize = 0.7f; q.delayMs = 10.f + 3.f * v; q.level = 0.7f; q.pan = (v - 4.5f) / 5.f; q.send = 0.3f;
            q.mutMode = v % 4; q.mutAmt = 0.8f; eng->setVoice (v, q);
        }
        Macros m; m.detune = 0.6f; m.humanize = 0.5f; m.movement = 0.7f; m.width = 1.2f; eng->setMacros (m);
        std::vector<float> in = makeVoice (sr, 2.0, 200, 260, 800, 1150, 2900);
        std::vector<float> nz (44100); Rng r; for (auto& x : nz) x = 0.3f * r.bi();
        in.insert (in.end(), nz.begin(), nz.end()); in.insert (in.end(), 44100, 0.f);
        in.insert (in.end(), 4410, 1.0f); in.insert (in.end(), 44100, 0.f);
        std::vector<std::vector<float>> out; std::vector<float> dry; run (in, out, dry);
        bool bad = false; double pk = 0;
        for (auto& o : out) for (float x : o) { if (! std::isfinite (x)) bad = true; pk = std::max<double> (pk, std::fabs (x)); }
        printf ("[4] 10 voces con mutaciones + ruido + silencio + escalon: NaN/Inf=%s, pico maximo=%.2f\n", bad ? "SI" : "no", pk);
    }

    // --- Prueba 5: CPU con 10 voces activas ---
    {
        std::vector<float> in = makeVoice (sr, 20.0, 130, 180, 700, 1200, 2600);
        std::vector<std::vector<float>> out; std::vector<float> dry;
        const auto t0 = std::chrono::steady_clock::now(); run (in, out, dry); const auto t1 = std::chrono::steady_clock::now();
        const double sec = std::chrono::duration<double> (t1 - t0).count();
        printf ("[5] 20 s de audio con 10 voces: %.2f s de CPU -> %.1f %% de un nucleo\n", sec, 100.0 * sec / 20.0);
    }
    return 0;
}
