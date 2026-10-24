#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

// Graba hasta 5 WAV estereo (MAIN, HARMONIES, DOUBLER, TEXTURE, FX) sin bloquear el hilo de audio.
class StemRecorder
{
public:
    static constexpr int kNumStems = 5;

    StemRecorder() : thread ("MV CHOIR LAB stem writer") { thread.startThread(); }

    ~StemRecorder()
    {
        stop();
        thread.stopThread (2000);
    }

    static juce::File getFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("MV CHOIR LAB")
            .getChildFile ("Stems");
    }

    // mask: bit i = grabar el stem i
    bool start (double sampleRate, int mask)
    {
        stop();
        auto dir = getFolder();
        if (! dir.createDirectory().wasOk())
            return false;

        const juce::String stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
        static const char* names[kNumStems] = { "MAIN", "HARMONIES", "DOUBLER", "TEXTURE", "FX" };

        juce::WavAudioFormat wav;
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < kNumStems; ++i)
        {
            if ((mask & (1 << i)) == 0)
                continue;

            auto file = dir.getChildFile (stamp + "_" + names[i] + ".wav");
            std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
            if (stream == nullptr)
                continue;

            // 32 bits = WAV de punto flotante (sin recortes)
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.get(), sampleRate, 2, 32, juce::StringPairArray(), 0));
            if (writer == nullptr)
                continue;

            stream.release(); // el writer es dueno del stream
            writers[i].reset (new juce::AudioFormatWriter::ThreadedWriter (writer.release(), thread, 32768));
        }
        recording = true;
        return true;
    }

    void stop()
    {
        recording = false;
        const juce::ScopedLock sl (lock);
        for (auto& w : writers)
            w.reset(); // el destructor vacia el buffer a disco
    }

    bool isRecording() const { return recording.load(); }

    // Hilo de audio
    void write (int index, const float* left, const float* right, int numSamples)
    {
        if (! recording.load())
            return;
        const juce::ScopedTryLock sl (lock);
        if (sl.isLocked() && writers[index] != nullptr)
        {
            const float* chans[2] = { left, right };
            writers[index]->write (chans, numSamples);
        }
    }

private:
    juce::TimeSliceThread thread;
    juce::CriticalSection lock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writers[kNumStems];
    std::atomic<bool> recording { false };
};
