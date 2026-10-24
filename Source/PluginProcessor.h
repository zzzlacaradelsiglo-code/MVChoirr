#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <memory>
#include "VocalEngine.h"
#include "StemRecorder.h"

// ---- parametros por voz ----
enum VoiceParam
{
    vpOn, vpSolo, vpPitch, vpFine, vpForm, vpDelay, vpHum, vpLevel, vpPan, vpSend, vpBright, vpMut, vpMutAmt,
    vpCount
};

inline const char* const kVoiceKeys[vpCount] = {
    "on", "solo", "pitch", "fine", "form", "delay", "hum", "level", "pan", "send", "bright", "mut", "mutamt"
};

// ---- parametros globales ----
enum GlobalParam
{
    gVoices, gSpread, gDetune, gHumanize, gMovement, gMutation, gWidth,
    gMix, gBypass, gOut, gRevSize, gRevLevel,
    gStemMain, gStemHarm, gStemDbl, gStemTex, gStemFx,
    gCount
};

inline const char* const kGlobalIds[gCount] = {
    "voices", "spread", "detune", "humanize", "movement", "mutation", "width",
    "mix", "bypass", "out", "revsize", "revlevel",
    "stem_main", "stem_harm", "stem_dbl", "stem_tex", "stem_fx"
};

inline const char* const kVoiceNames[mvc::kVoices] = {
    "MAIN", "HARMONY 1", "HARMONY 2", "HARMONY 3", "HARMONY 4", "DOUBLER 1", "DOUBLER 2", "TEXTURE", "GHOST", "FX LAYER"
};

// grupo de stem de cada voz: 0 MAIN, 1 HARMONIES, 2 DOUBLER, 3 TEXTURE, 4 FX
inline const int kVoiceGroup[mvc::kVoices] = { 0, 1, 1, 1, 1, 2, 2, 3, 3, 4 };

inline const char* const kPresetNames[] = {
    "Init", "Minor Stack", "Dark Cathedral", "Open Fifths", "Wide Doubles", "Ghost Choir", "Cluster", "Octaves"
};
constexpr int kNumPresets = 8;

inline juce::String voiceId (int v, int key)
{
    return juce::String ("v") + juce::String (v) + "_" + kVoiceKeys[key];
}

class MVProcessor : public juce::AudioProcessor
{
public:
    MVProcessor();
    ~MVProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MV CHOIR LAB"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return kNumPresets; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override
    {
        return (index >= 0 && index < kNumPresets) ? juce::String (kPresetNames[index]) : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // usado por el editor
    void applyPreset (int index);
    void setHarmonyType (int type);
    bool startRecording();
    void stopRecording() { recorder.stop(); }
    bool isRecording() const { return recorder.isRecording(); }
    void setPlain (const juce::String& id, float plainValue);

    float vval (int v, int key) const { return vraw[(size_t) (v * vpCount + key)]->load(); }
    float gval (int idx) const { return graw[(size_t) idx]->load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> outPeak[2];
    std::atomic<float> voiceMeter[mvc::kVoices];

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void resetAllToDefaults();

    static constexpr int chIn = 0, chVoice = 2, chSend = 22, chDry = 24, chStem = 26, chRev = 36, kScratchCh = 38;

    std::unique_ptr<mvc::Engine> engine; // grande: va en el heap
    StemRecorder recorder;
    juce::Reverb reverb;
    juce::AudioBuffer<float> scratch;
    juce::SmoothedValue<float> mixSm, outSm, bypSm;
    std::array<std::atomic<float>*, (size_t) (mvc::kVoices * vpCount)> vraw {};
    std::array<std::atomic<float>*, (size_t) gCount> graw {};
    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MVProcessor)
};
