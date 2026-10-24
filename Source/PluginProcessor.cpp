#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct VoiceDefault
{
    float pitch, fine, form, delay, hum, levelDb, pan, send, bright;
    int mut;
    float mutAmt;
};

// Valores iniciales de cada voz (MAIN, H1-H4, D1-D2, TEXTURE, GHOST, FX LAYER)
const VoiceDefault kDef[mvc::kVoices] = {
    { 0.f, 0.f, 0.f, 0.f, 0.0f, 0.f, 0.00f, 0.15f, 0.0f, 0, 0.0f },     // MAIN (voz original, intacta)
    { 4.f, 0.f, 0.f, 8.f, 0.3f, -6.f, -0.35f, 0.25f, 0.0f, 0, 0.0f },    // HARMONY 1  (+3ra mayor)
    { 7.f, 0.f, 0.f, 12.f, 0.3f, -6.f, 0.35f, 0.25f, 0.0f, 0, 0.0f },    // HARMONY 2  (+5ta)
    { -5.f, 0.f, 0.f, 16.f, 0.3f, -8.f, -0.70f, 0.30f, 0.0f, 0, 0.0f },  // HARMONY 3  (5ta abajo)
    { -8.f, 0.f, 0.f, 20.f, 0.3f, -8.f, 0.70f, 0.30f, 0.0f, 0, 0.0f },   // HARMONY 4  (3ra mayor abajo)
    { 0.f, 12.f, 0.f, 22.f, 0.3f, -9.f, -0.55f, 0.15f, 0.0f, 0, 0.0f },  // DOUBLER 1  (+12 cents)
    { 0.f, -12.f, 0.f, 30.f, 0.3f, -9.f, 0.55f, 0.15f, 0.0f, 0, 0.0f },  // DOUBLER 2  (-12 cents)
    { 0.f, 0.f, 0.f, 0.f, 0.2f, -14.f, 0.00f, 0.50f, 0.6f, 1, 0.5f },    // TEXTURE    (aire / susurro)
    { 12.f, 0.f, 0.f, 40.f, 0.3f, -12.f, 0.00f, 0.70f, -0.3f, 2, 0.6f }, // GHOST      (octava arriba, difuminada)
    { 0.f, 0.f, 0.f, 0.f, 0.0f, -60.f, 0.00f, 0.80f, 0.0f, 3, 0.4f }     // FX LAYER   (solo alimenta la reverb)
};

juce::String fmtPct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String fmtSt (float v, int) { return juce::String (v, 1) + " st"; }
juce::String fmtCents (float v, int) { return juce::String (juce::roundToInt (v)) + " c"; }
juce::String fmtMs (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }
juce::String fmtDb (float v, int) { return v <= -59.5f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }
juce::String fmtPan (float v, int)
{
    const int p = juce::roundToInt (std::abs (v) * 100.0f);
    if (p == 0) return "C";
    return (v < 0.0f ? "L " : "R ") + juce::String (p);
}
juce::String fmtBright (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MVProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;

    auto addF = [&] (const juce::String& id, const juce::String& name, float lo, float hi, float def, float interval,
                     std::function<juce::String (float, int)> fmt)
    {
        v.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, interval), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
    };

    for (int i = 0; i < mvc::kVoices; ++i)
    {
        const auto& d = kDef[i];
        const juce::String nm = juce::String (kVoiceNames[i]) + " ";
        v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { voiceId (i, vpOn), 1 }, nm + "On", true));
        v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { voiceId (i, vpSolo), 1 }, nm + "Solo", false));
        addF (voiceId (i, vpPitch), nm + "Pitch", -24.0f, 24.0f, d.pitch, 0.01f, fmtSt);
        addF (voiceId (i, vpFine), nm + "Fine", -100.0f, 100.0f, d.fine, 0.1f, fmtCents);
        addF (voiceId (i, vpForm), nm + "Formant", -12.0f, 12.0f, d.form, 0.01f, fmtSt);
        addF (voiceId (i, vpDelay), nm + "Delay", 0.0f, 120.0f, d.delay, 0.1f, fmtMs);
        addF (voiceId (i, vpHum), nm + "Humanize", 0.0f, 1.0f, d.hum, 0.001f, fmtPct);
        addF (voiceId (i, vpLevel), nm + "Level", -60.0f, 12.0f, d.levelDb, 0.1f, fmtDb);
        addF (voiceId (i, vpPan), nm + "Pan", -1.0f, 1.0f, d.pan, 0.001f, fmtPan);
        addF (voiceId (i, vpSend), nm + "Reverb Send", 0.0f, 1.0f, d.send, 0.001f, fmtPct);
        addF (voiceId (i, vpBright), nm + "Brightness", -1.0f, 1.0f, d.bright, 0.001f, fmtBright);
        v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { voiceId (i, vpMut), 1 }, nm + "Mutation",
                                                                   juce::StringArray { "OFF", "WHISPER", "BLUR", "MORPH" }, d.mut));
        addF (voiceId (i, vpMutAmt), nm + "Mutation Amount", 0.0f, 1.0f, d.mutAmt, 0.001f, fmtPct);
    }

    v.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { kGlobalIds[gVoices], 1 }, "Voices", 1, 10, 10));
    addF (kGlobalIds[gSpread], "Harmony Spread", 0.0f, 1.5f, 1.0f, 0.001f, fmtPct);
    addF (kGlobalIds[gDetune], "Detune", 0.0f, 1.0f, 0.12f, 0.001f, fmtPct);
    addF (kGlobalIds[gHumanize], "Humanize", 0.0f, 1.0f, 0.30f, 0.001f, fmtPct);
    addF (kGlobalIds[gMovement], "Movement", 0.0f, 1.0f, 0.25f, 0.001f, fmtPct);
    addF (kGlobalIds[gMutation], "Mutation", 0.0f, 1.0f, 1.0f, 0.001f, fmtPct);
    addF (kGlobalIds[gWidth], "Stereo Width", 0.0f, 1.5f, 1.0f, 0.001f, fmtPct);
    addF (kGlobalIds[gMix], "Mix", 0.0f, 1.0f, 1.0f, 0.001f, fmtPct);
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGlobalIds[gBypass], 1 }, "Bypass", false));
    addF (kGlobalIds[gOut], "Output", -24.0f, 12.0f, 0.0f, 0.1f, fmtDb);
    addF (kGlobalIds[gRevSize], "Reverb Size", 0.0f, 1.0f, 0.6f, 0.001f, fmtPct);
    addF (kGlobalIds[gRevLevel], "Reverb Level", 0.0f, 1.0f, 0.3f, 0.001f, fmtPct);
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGlobalIds[gStemMain], 1 }, "Export Main", true));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGlobalIds[gStemHarm], 1 }, "Export Harmonies", true));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGlobalIds[gStemDbl], 1 }, "Export Doubler", true));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGlobalIds[gStemTex], 1 }, "Export Texture", true));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGlobalIds[gStemFx], 1 }, "Export FX", true));

    return { v.begin(), v.end() };
}

MVProcessor::MVProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MVCHOIRLAB", createLayout())
{
    engine = std::make_unique<mvc::Engine>();
    for (int v = 0; v < mvc::kVoices; ++v)
        for (int k = 0; k < vpCount; ++k)
            vraw[(size_t) (v * vpCount + k)] = apvts.getRawParameterValue (voiceId (v, k));
    for (int i = 0; i < gCount; ++i)
        graw[(size_t) i] = apvts.getRawParameterValue (kGlobalIds[i]);
    outPeak[0].store (0.0f);
    outPeak[1].store (0.0f);
    for (auto& m : voiceMeter)
        m.store (0.0f);
}

bool MVProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MVProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine->prepare (sampleRate);
    setLatencySamples (engine->latencySamples());
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    scratch.setSize (kScratchCh, juce::jmax (1, samplesPerBlock));
    scratch.clear();
    mixSm.reset (sampleRate, 0.02);
    outSm.reset (sampleRate, 0.02);
    bypSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (gval (gMix));
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gval (gOut)));
    bypSm.setCurrentAndTargetValue (gval (gBypass) > 0.5f ? 1.0f : 0.0f);
}

void MVProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh < 1)
        return;
    if (scratch.getNumSamples() < n)
        scratch.setSize (kScratchCh, n, false, false, true); // solo si el host manda bloques mas grandes de lo anunciado

    float* const* sc = scratch.getArrayOfWritePointers();
    std::memcpy (sc[chIn], buffer.getReadPointer (0), (size_t) n * sizeof (float));
    std::memcpy (sc[chIn + 1], buffer.getReadPointer (numCh > 1 ? 1 : 0), (size_t) n * sizeof (float));

    // ---- parametros -> motor ----
    const int voicesCount = juce::roundToInt (gval (gVoices));
    bool anySolo = false;
    for (int v = 0; v < mvc::kVoices; ++v)
        if (vval (v, vpOn) > 0.5f && vval (v, vpSolo) > 0.5f)
            anySolo = true;

    const float spread = gval (gSpread);
    const float mutMacro = gval (gMutation);
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        mvc::VoiceParams p;
        const bool on = vval (v, vpOn) > 0.5f && v < voicesCount;
        const bool solo = vval (v, vpSolo) > 0.5f;
        p.active = on && (! anySolo || solo);
        float pitch = vval (v, vpPitch);
        if (v >= 1 && v <= 4)
            pitch *= spread; // macro HARMONY: abre o cierra los intervalos de las 4 armonias
        p.pitchSt = pitch;
        p.fineCents = vval (v, vpFine);
        p.formantSt = vval (v, vpForm);
        p.delayMs = vval (v, vpDelay);
        p.humanize = vval (v, vpHum);
        const float db = vval (v, vpLevel);
        p.level = db <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db);
        p.pan = vval (v, vpPan);
        p.bright = vval (v, vpBright);
        p.send = vval (v, vpSend);
        p.mutMode = juce::jlimit (0, 3, juce::roundToInt (vval (v, vpMut)));
        p.mutAmt = juce::jlimit (0.0f, 1.0f, vval (v, vpMutAmt) * mutMacro);
        engine->setVoice (v, p);
    }
    mvc::Macros m;
    m.detune = gval (gDetune);
    m.humanize = gval (gHumanize);
    m.movement = gval (gMovement);
    m.width = gval (gWidth);
    engine->setMacros (m);

    float* vL[mvc::kVoices];
    float* vR[mvc::kVoices];
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        vL[v] = sc[chVoice + 2 * v];
        vR[v] = sc[chVoice + 2 * v + 1];
    }
    engine->process (sc[chIn], sc[chIn + 1], n, vL, vR, sc[chSend], sc[chSend + 1], sc[chDry], sc[chDry + 1]);

    for (int v = 0; v < mvc::kVoices; ++v)
        voiceMeter[v].store (engine->takeVoicePeak (v));

    // ---- grupos (stems) ----
    for (int g = 0; g < StemRecorder::kNumStems; ++g)
    {
        std::memset (sc[chStem + 2 * g], 0, (size_t) n * sizeof (float));
        std::memset (sc[chStem + 2 * g + 1], 0, (size_t) n * sizeof (float));
    }
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        float* gl = sc[chStem + 2 * kVoiceGroup[v]];
        float* gr = sc[chStem + 2 * kVoiceGroup[v] + 1];
        for (int i = 0; i < n; ++i)
        {
            gl[i] += vL[v][i];
            gr[i] += vR[v][i];
        }
    }

    // ---- reverb (compartida) ----
    std::memcpy (sc[chRev], sc[chSend], (size_t) n * sizeof (float));
    std::memcpy (sc[chRev + 1], sc[chSend + 1], (size_t) n * sizeof (float));
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.3f + 0.69f * gval (gRevSize);
    rp.damping = 0.5f;
    rp.wetLevel = 0.33f;
    rp.dryLevel = 0.0f;
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    reverb.processStereo (sc[chRev], sc[chRev + 1], n);
    const float revLevel = gval (gRevLevel);
    for (int i = 0; i < n; ++i)
    {
        sc[chStem + 8][i] += sc[chRev][i] * revLevel; // la cola de reverb viaja en el stem FX
        sc[chStem + 9][i] += sc[chRev + 1][i] * revLevel;
    }

    // ---- grabacion de stems ----
    if (recorder.isRecording())
        for (int g = 0; g < StemRecorder::kNumStems; ++g)
            recorder.write (g, sc[chStem + 2 * g], sc[chStem + 2 * g + 1], n);

    // ---- mezcla final ----
    mixSm.setTargetValue (gval (gMix));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (gval (gOut)));
    bypSm.setTargetValue (gval (gBypass) > 0.5f ? 1.0f : 0.0f);

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (numCh > 1 ? 1 : 0);
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float mix = mixSm.getNextValue();
        const float og = outSm.getNextValue();
        const float by = bypSm.getNextValue();
        const float chL = sc[chStem + 2][i] + sc[chStem + 4][i] + sc[chStem + 6][i] + sc[chStem + 8][i];
        const float chR = sc[chStem + 3][i] + sc[chStem + 5][i] + sc[chStem + 7][i] + sc[chStem + 9][i];
        const float pl = (sc[chStem][i] + mix * chL) * og;
        const float pr = (sc[chStem + 1][i] + mix * chR) * og;
        const float l = by * sc[chDry][i] + (1.0f - by) * pl;
        const float r = by * sc[chDry + 1][i] + (1.0f - by) * pr;
        outL[i] = l;
        if (numCh > 1)
            outR[i] = r;
        peakL = std::max (peakL, std::fabs (l));
        peakR = std::max (peakR, std::fabs (r));
    }
    outPeak[0].store (peakL);
    outPeak[1].store (peakR);
}

bool MVProcessor::startRecording()
{
    int mask = 0;
    if (gval (gStemMain) > 0.5f) mask |= 1;
    if (gval (gStemHarm) > 0.5f) mask |= 2;
    if (gval (gStemDbl) > 0.5f) mask |= 4;
    if (gval (gStemTex) > 0.5f) mask |= 8;
    if (gval (gStemFx) > 0.5f) mask |= 16;
    if (mask == 0)
        return false;
    return recorder.start (currentSampleRate, mask);
}

void MVProcessor::setPlain (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void MVProcessor::resetAllToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
}

void MVProcessor::setHarmonyType (int type)
{
    static const float t[5][4] = {
        { 4.f, 7.f, -5.f, -8.f },   // MAJOR STACK
        { 3.f, 7.f, -5.f, -9.f },   // MINOR STACK
        { 7.f, -5.f, 12.f, -12.f }, // OPEN FIFTHS
        { 12.f, -12.f, 24.f, -24.f }, // OCTAVES
        { 1.f, 2.f, -1.f, -2.f }    // CLUSTER
    };
    if (type < 0 || type > 4)
        return;
    for (int i = 0; i < 4; ++i)
        setPlain (voiceId (i + 1, vpPitch), t[type][i]);
}

void MVProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumPresets)
        return;
    currentProgram = index;
    applyPreset (index);
}

void MVProcessor::applyPreset (int idx)
{
    resetAllToDefaults();
    auto V = [this] (int v, int key, float val) { setPlain (voiceId (v, key), val); };
    switch (idx)
    {
        case 1: // Minor Stack
            setHarmonyType (1);
            break;
        case 2: // Dark Cathedral
            setHarmonyType (1);
            setPlain (kGlobalIds[gRevSize], 0.92f);
            setPlain (kGlobalIds[gRevLevel], 0.55f);
            for (int v = 1; v <= 4; ++v) { V (v, vpSend, 0.5f); V (v, vpBright, -0.3f); }
            V (8, vpLevel, -8.f);
            V (7, vpLevel, -12.f);
            V (0, vpLevel, -2.f);
            break;
        case 3: // Open Fifths
            setHarmonyType (2);
            for (int v = 1; v <= 4; ++v) V (v, vpLevel, -7.f);
            break;
        case 4: // Wide Doubles
            for (int v = 1; v <= 4; ++v) V (v, vpLevel, -60.f);
            V (5, vpLevel, -4.f); V (6, vpLevel, -4.f);
            V (5, vpPan, -0.9f); V (6, vpPan, 0.9f);
            V (5, vpDelay, 18.f); V (6, vpDelay, 26.f);
            V (5, vpHum, 0.5f); V (6, vpHum, 0.5f);
            break;
        case 5: // Ghost Choir
            for (int v = 1; v <= 4; ++v) V (v, vpLevel, -12.f);
            V (8, vpLevel, -4.f);
            V (7, vpLevel, -8.f);
            setPlain (kGlobalIds[gRevSize], 0.9f);
            setPlain (kGlobalIds[gRevLevel], 0.6f);
            break;
        case 6: // Cluster
            setHarmonyType (4);
            setPlain (kGlobalIds[gDetune], 0.3f);
            break;
        case 7: // Octaves
            setHarmonyType (3);
            V (3, vpFine, 8.f);
            V (4, vpFine, -8.f);
            for (int v = 1; v <= 4; ++v) V (v, vpLevel, -9.f);
            break;
        default:
            break;
    }
}

juce::AudioProcessorEditor* MVProcessor::createEditor()
{
    return new MVEditor (*this);
}

void MVProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MVProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MVProcessor();
}
