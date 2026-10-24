#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
const juce::Colour kBg (0xff070a0f);
const juce::Colour kPanel (0xff0c1118);
const juce::Colour kEdge (0xff1a2a3d);
const juce::Colour kText (0xffb9c8da);
const juce::Colour kDim (0xff5f7189);
const juce::Colour kAccent (0xff4aa8ff);

constexpr int kRowH = 42;

// parametros que se editan para la voz seleccionada
const int kEditKeys[10] = { vpPitch, vpFine, vpForm, vpDelay, vpHum, vpLevel, vpPan, vpSend, vpBright, vpMutAmt };
const char* const kEditLabels[10] = { "PITCH", "FINE", "FORMANT", "DELAY", "HUMANIZE", "LEVEL", "PAN", "REV SEND", "BRIGHT", "MUT AMT" };
const char* const kStemNames[5] = { "MAIN", "HARMONIES", "DOUBLER", "TEXTURE", "FX" };
const int kStemParams[5] = { gStemMain, gStemHarm, gStemDbl, gStemTex, gStemFx };
}

MVEditor::MVEditor (MVProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    art = juce::ImageCache::getFromMemory (BinaryData::art_jpg, BinaryData::art_jpgSize);

    const juce::Colour cMacro (0xff4aa8ff), cOut (0xff8fd0ff);

    // ---- perillas macro ----
    static const int macroIds[7] = { gVoices, gSpread, gDetune, gHumanize, gMovement, gMutation, gWidth };
    static const char* const macroLabels[7] = { "VOICES", "HARMONY", "DETUNE", "HUMANIZE", "MOVEMENT", "MUTATION", "STEREO WIDTH" };
    for (int i = 0; i < 7; ++i)
    {
        auto* k = makeKnob (macroKnobs, macroLabels[i], cMacro);
        k->att = std::make_unique<SliderAtt> (proc.apvts, kGlobalIds[macroIds[i]], k->slider);
    }

    // ---- perillas de salida ----
    static const int outIds[4] = { gMix, gOut, gRevSize, gRevLevel };
    static const char* const outLabels[4] = { "MIX", "OUTPUT", "REV SIZE", "REV LEVEL" };
    for (int i = 0; i < 4; ++i)
    {
        auto* k = makeKnob (outKnobs, outLabels[i], cOut);
        k->att = std::make_unique<SliderAtt> (proc.apvts, kGlobalIds[outIds[i]], k->slider);
    }

    // ---- editor de voz (se re-asocia al elegir otra voz) ----
    for (int i = 0; i < 10; ++i)
        makeKnob (editKnobs, kEditLabels[i], i < 5 ? juce::Colour (0xff4aa8ff) : juce::Colour (0xff3de8d0));

    mutBox.addItemList (juce::StringArray { "OFF", "WHISPER", "BLUR", "MORPH" }, 1);
    addAndMakeVisible (mutBox);

    // ---- lista de voces ----
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        addAndMakeVisible (eye[v]);
        eyeAtt[v] = std::make_unique<ButtonAtt> (proc.apvts, voiceId (v, vpOn), eye[v]);
        solo[v].setButtonText ("S");
        addAndMakeVisible (solo[v]);
        soloAtt[v] = std::make_unique<ButtonAtt> (proc.apvts, voiceId (v, vpSolo), solo[v]);
    }

    // ---- salida ----
    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, kGlobalIds[gBypass], bypassBtn);
    for (int i = 0; i < 5; ++i)
    {
        stemBtn[i].setButtonText (kStemNames[i]);
        addAndMakeVisible (stemBtn[i]);
        stemAtt[i] = std::make_unique<ButtonAtt> (proc.apvts, kGlobalIds[kStemParams[i]], stemBtn[i]);
    }

    recBtn.onClick = [this]
    {
        if (proc.isRecording())
            proc.stopRecording();
        else
            proc.startRecording();
    };
    folderBtn.onClick = []
    {
        auto dir = StemRecorder::getFolder();
        dir.createDirectory();
        dir.revealToUser();
    };
    addAndMakeVisible (recBtn);
    addAndMakeVisible (folderBtn);

    // ---- presets ----
    for (int i = 0; i < kNumPresets; ++i)
        presetBox.addItem (kPresetNames[i], i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);
    prevBtn.onClick = [this] { presetBox.setSelectedId (juce::jmax (1, presetBox.getSelectedId() - 1)); };
    nextBtn.onClick = [this] { presetBox.setSelectedId (juce::jmin (kNumPresets, presetBox.getSelectedId() + 1)); };
    addAndMakeVisible (prevBtn);
    addAndMakeVisible (nextBtn);

    initBtn.onClick = [this]
    {
        proc.setCurrentProgram (0);
        presetBox.setSelectedId (1, juce::dontSendNotification);
    };
    saveBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Guardar preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mvpreset");
        const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                           | juce::FileBrowserComponent::warnAboutOverwriting;
        chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File())
                return;
            if (auto xml = proc.apvts.copyState().createXml())
                xml->writeTo (f.withFileExtension ("mvpreset"));
        });
    };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Cargar preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mvpreset");
        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile())
                return;
            if (auto xml = juce::parseXML (f))
                if (xml->hasTagName (proc.apvts.state.getType()))
                    proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
        });
    };
    addAndMakeVisible (saveBtn);
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (initBtn);

    harmBox.addItemList (juce::StringArray { "CUSTOM", "MAJOR STACK", "MINOR STACK", "OPEN FIFTHS", "OCTAVES", "CLUSTER" }, 1);
    harmBox.setSelectedId (1, juce::dontSendNotification);
    harmBox.onChange = [this]
    {
        const int id = harmBox.getSelectedId();
        if (id > 1)
            proc.setHarmonyType (id - 2);
    };
    addAndMakeVisible (harmBox);

    selectVoice (1);
    setSize (1100, 700);
    startTimerHz (30);
}

MVEditor::~MVEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

MVEditor::Knob* MVEditor::makeKnob (std::vector<std::unique_ptr<Knob>>& store, const juce::String& label, juce::Colour colour)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    auto& s = k->slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, colour);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (s);
    Knob* raw = k.get();
    store.push_back (std::move (k));
    return raw;
}

void MVEditor::selectVoice (int v)
{
    selected = juce::jlimit (0, mvc::kVoices - 1, v);
    for (int i = 0; i < 10; ++i)
    {
        editKnobs[(size_t) i]->att.reset();
        editKnobs[(size_t) i]->att = std::make_unique<SliderAtt> (proc.apvts, voiceId (selected, kEditKeys[i]), editKnobs[(size_t) i]->slider);
    }
    mutAtt.reset();
    mutAtt = std::make_unique<ComboAtt> (proc.apvts, voiceId (selected, vpMut), mutBox);
    repaint();
}

juce::Colour MVEditor::groupColour (int voice) const
{
    switch (kVoiceGroup[voice])
    {
        case 0: return juce::Colour (0xffe8f1ff);
        case 1: return juce::Colour (0xff4aa8ff);
        case 2: return juce::Colour (0xff9b7bff);
        case 3: return juce::Colour (0xff3de8d0);
        default: return juce::Colour (0xff8896a8);
    }
}

juce::String MVEditor::voiceInfo (int v) const
{
    const float pitch = proc.vval (v, vpPitch);
    const float fine = proc.vval (v, vpFine);
    if (v == 0 && pitch == 0.0f && fine == 0.0f)
        return "DRY";
    if (pitch != 0.0f)
        return (pitch > 0.0f ? "+" : "") + juce::String (pitch, 1) + " st";
    if (fine != 0.0f)
        return (fine > 0.0f ? "+" : "") + juce::String (juce::roundToInt (fine)) + " c";
    return "-";
}

void MVEditor::timerCallback()
{
    for (int c = 0; c < 2; ++c)
    {
        const float db = juce::Decibels::gainToDecibels (proc.outPeak[c].load(), -60.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        outSmooth[c] = lvl > outSmooth[c] ? lvl : outSmooth[c] * 0.9f;
    }
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        const float db = juce::Decibels::gainToDecibels (proc.voiceMeter[v].load(), -60.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        voiceSmooth[v] = lvl > voiceSmooth[v] ? lvl : voiceSmooth[v] * 0.9f;
    }
    recBtn.setButtonText (proc.isRecording() ? "STOP REC" : "REC STEMS");
    recBtn.setColour (juce::TextButton::buttonColourId, proc.isRecording() ? juce::Colour (0xffb3261e) : juce::Colour (0xff111b28));
    repaint();
}

void MVEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    const int top = leftPanel.getY() + 44;
    if (leftPanel.contains (pos) && pos.y >= top && pos.y < top + kRowH * mvc::kVoices)
        selectVoice ((pos.y - top) / kRowH);
}

void MVEditor::resized()
{
    leftPanel = { 12, 76, 244, 614 };
    stagePanel = { 266, 76, 560, 254 };
    macroPanel = { 266, 338, 560, 102 };
    editPanel = { 266, 448, 560, 242 };
    outPanel = { 836, 76, 252, 614 };

    // cabecera
    prevBtn.setBounds (404, 22, 26, 28);
    presetBox.setBounds (434, 22, 230, 28);
    nextBtn.setBounds (668, 22, 26, 28);
    saveBtn.setBounds (716, 18, 72, 36);
    loadBtn.setBounds (794, 18, 72, 36);
    initBtn.setBounds (872, 18, 72, 36);

    // voces
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        const int y = leftPanel.getY() + 44 + v * kRowH;
        eye[v].setBounds (leftPanel.getX() + 10, y + 9, 38, 24);
        solo[v].setBounds (leftPanel.getRight() - 40, y + 9, 28, 24);
    }
    harmBox.setBounds (leftPanel.getX() + 14, leftPanel.getY() + 44 + kRowH * 10 + 34, leftPanel.getWidth() - 28, 28);

    // macros
    for (size_t i = 0; i < macroKnobs.size(); ++i)
        macroKnobs[i]->slider.setBounds (macroPanel.getX() + (int) i * 80, macroPanel.getY() + 16, 80, 84);

    // editor de voz: 2 filas
    const int cw = 93;
    for (size_t i = 0; i < editKnobs.size(); ++i)
    {
        const int row = i < 6 ? 0 : 1;
        const int col = i < 6 ? (int) i : (int) i - 6;
        editKnobs[i]->slider.setBounds (editPanel.getX() + col * cw, editPanel.getY() + 42 + row * 100, cw, 84);
    }
    mutBox.setBounds (editPanel.getX() + 4 * cw + 2, editPanel.getY() + 142 + 38, 88, 26);

    // salida
    for (size_t i = 0; i < outKnobs.size(); ++i)
    {
        const int col = (int) (i % 2), row = (int) (i / 2);
        outKnobs[i]->slider.setBounds (outPanel.getX() + 4 + col * 122, outPanel.getY() + 150 + row * 100, 122, 84);
    }
    bypassBtn.setBounds (outPanel.getX() + 14, outPanel.getY() + 358, 120, 26);
    for (int i = 0; i < 5; ++i)
        stemBtn[i].setBounds (outPanel.getX() + 14, outPanel.getY() + 422 + i * 28, 170, 24);
    recBtn.setBounds (outPanel.getX() + 12, outPanel.getY() + 574, 112, 30);
    folderBtn.setBounds (outPanel.getX() + 130, outPanel.getY() + 574, 110, 30);
}

void MVEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (kAccent);
        g.setFont (12.0f);
        g.drawText (title, r.getX() + 14, r.getY() + 8, r.getWidth() - 28, 16, juce::Justification::left);
    }
}

void MVEditor::paintOutputMeters (juce::Graphics& g)
{
    const int y0 = outPanel.getY() + 44, h = 90, w = 38;
    for (int c = 0; c < 2; ++c)
    {
        const int x0 = outPanel.getX() + 70 + c * 80;
        g.setColour (juce::Colour (0xff0f1722));
        g.fillRoundedRectangle ((float) x0, (float) y0, (float) w, (float) h, 3.0f);
        const int segs = 20;
        const float sh = (float) h / segs;
        const int lit = (int) (outSmooth[c] * segs);
        for (int s = 0; s < segs; ++s)
        {
            const bool on = s < lit;
            const float yy = (float) y0 + (float) h - (float) (s + 1) * sh;
            g.setColour (on ? (s > 17 ? juce::Colour (0xffff5a5a) : juce::Colour (0xff4aa8ff)) : juce::Colour (0xff17212f));
            g.fillRect ((float) x0 + 3.0f, yy + 1.0f, (float) w - 6.0f, sh - 2.0f);
        }
        g.setColour (kDim);
        g.setFont (11.0f);
        g.drawText (c == 0 ? "L" : "R", x0, y0 + h + 2, w, 14, juce::Justification::centred);
    }
}

void MVEditor::paintStage (juce::Graphics& g)
{
    auto r = stagePanel.toFloat();
    juce::Path clip;
    clip.addRoundedRectangle (r, 8.0f);
    g.saveState();
    g.reduceClipRegion (clip);
    g.setColour (juce::Colours::black);
    g.fillRect (r);
    if (art.isValid())
    {
        g.setOpacity (0.85f);
        g.drawImage (art, r, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    }
    juce::ColourGradient shade (juce::Colours::transparentBlack, r.getX(), r.getY() + r.getHeight() * 0.45f,
                                juce::Colour (0xe6070a0f), r.getX(), r.getBottom(), false);
    g.setGradientFill (shade);
    g.fillRect (r);

    // voces sobre el escenario: paneo horizontal, nivel vertical
    const float width = proc.gval (gWidth);
    const int voicesCount = juce::roundToInt (proc.gval (gVoices));
    for (int v = mvc::kVoices - 1; v >= 0; --v)
    {
        const bool on = proc.vval (v, vpOn) > 0.5f && v < voicesCount;
        const float pan = juce::jlimit (-1.0f, 1.0f, proc.vval (v, vpPan) * width);
        const float db = juce::jlimit (-40.0f, 6.0f, proc.vval (v, vpLevel));
        const float cx = r.getX() + 36.0f + (pan * 0.5f + 0.5f) * (r.getWidth() - 72.0f);
        const float cy = r.getBottom() - 34.0f - ((db + 40.0f) / 46.0f) * (r.getHeight() - 90.0f);
        const float rad = 6.0f + voiceSmooth[v] * 9.0f;
        auto col = groupColour (v);
        if (! on)
            col = col.withAlpha (0.25f);
        juce::ColourGradient glow (col.withAlpha (0.55f), cx, cy, col.withAlpha (0.0f), cx + rad * 2.6f, cy, true);
        g.setGradientFill (glow);
        g.fillEllipse (cx - rad * 2.6f, cy - rad * 2.6f, rad * 5.2f, rad * 5.2f);
        g.setColour (col);
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
        if (v == selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (cx - rad - 3.0f, cy - rad - 3.0f, rad * 2.0f + 6.0f, rad * 2.0f + 6.0f, 1.2f);
        }
    }
    g.setColour (kAccent);
    g.setFont (11.0f);
    g.drawText ("SPATIAL VIEW   /   PAN  >  LEVEL  ^", stagePanel.getX() + 14, stagePanel.getY() + 8, 300, 14, juce::Justification::left);
    g.restoreState();
    g.setColour (kEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
}

void MVEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // ---- cabecera ----
    if (logo.isValid())
        g.drawImageWithin (logo, 14, 8, 62, 54, juce::RectanglePlacement::centred);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (30.0f).withStyle ("Bold")));
    g.drawText ("CHOIR//LAB", 86, 6, 280, 36, juce::Justification::left);
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawText (juce::String::fromUTF8 ("MA\xC3\x91" "ANA VEMOS"), 88, 40, 280, 14, juce::Justification::left);
    g.drawText ("VOCAL CHORUS ENGINE", 88, 53, 280, 14, juce::Justification::left);
    g.setColour (kAccent);
    g.drawText ("PRESET", 436, 6, 100, 14, juce::Justification::left);
    g.setColour (kDim);
    g.drawText (juce::String::fromUTF8 ("MA\xC3\x91" "ANA VEMOS  //  2026"), getWidth() - 330, 24, 316, 14, juce::Justification::right);
    g.drawText ("V2.0", getWidth() - 330, 40, 316, 14, juce::Justification::right);

    // ---- voces ----
    drawPanel (g, leftPanel, "VOICES");
    const int top = leftPanel.getY() + 44;
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        const auto row = juce::Rectangle<int> (leftPanel.getX() + 6, top + v * kRowH, leftPanel.getWidth() - 12, kRowH - 4);
        if (v == selected)
        {
            g.setColour (juce::Colour (0xff12263d));
            g.fillRoundedRectangle (row.toFloat(), 6.0f);
            g.setColour (kAccent.withAlpha (0.7f));
            g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 6.0f, 1.0f);
        }
        const bool on = proc.vval (v, vpOn) > 0.5f;
        g.setColour (on ? kText : kDim.withAlpha (0.6f));
        g.setFont (12.0f);
        g.drawText (kVoiceNames[v], row.getX() + 50, row.getY() + 4, 100, 18, juce::Justification::left);
        g.setColour (kDim);
        g.drawText (voiceInfo (v), row.getX() + 120, row.getY() + 4, 56, 18, juce::Justification::right);
        // actividad
        g.setColour (juce::Colour (0xff17212f));
        g.fillRect ((float) row.getX() + 50.0f, (float) row.getBottom() - 8.0f, 126.0f, 3.0f);
        g.setColour (groupColour (v));
        g.fillRect ((float) row.getX() + 50.0f, (float) row.getBottom() - 8.0f, 126.0f * voiceSmooth[v], 3.0f);
    }
    g.setColour (kAccent);
    g.setFont (11.0f);
    g.drawText ("HARMONY ARCHITECT", leftPanel.getX() + 14, harmBox.getY() - 18, 200, 14, juce::Justification::left);
    g.setColour (kDim);
    const double lat = proc.getSampleRate() > 0 ? 1000.0 * proc.getLatencySamples() / proc.getSampleRate() : 0.0;
    g.drawText ("LATENCY " + juce::String (juce::roundToInt ((float) lat)) + " ms  (the host compensates it)",
                leftPanel.getX() + 14, harmBox.getBottom() + 8, leftPanel.getWidth() - 28, 14, juce::Justification::left);
    g.drawText ("Use it on a mixer insert with a real vocal.", leftPanel.getX() + 14, harmBox.getBottom() + 24, leftPanel.getWidth() - 28, 14, juce::Justification::left);

    // ---- escenario ----
    paintStage (g);

    // ---- macros ----
    drawPanel (g, macroPanel, {});
    g.setFont (11.0f);
    g.setColour (kDim);
    for (size_t i = 0; i < macroKnobs.size(); ++i)
        g.drawText (macroKnobs[i]->label, macroPanel.getX() + (int) i * 80, macroPanel.getY() + 4, 80, 14, juce::Justification::centred);

    // ---- editor de voz ----
    drawPanel (g, editPanel, juce::String ("EDITING  /  ") + kVoiceNames[selected] + "   (" + voiceInfo (selected) + ")");
    g.setColour (kDim);
    g.setFont (11.0f);
    for (size_t i = 0; i < editKnobs.size(); ++i)
    {
        const auto b = editKnobs[i]->slider.getBounds();
        g.drawText (editKnobs[i]->label, b.getX(), b.getY() - 14, b.getWidth(), 14, juce::Justification::centred);
    }
    g.drawText ("MUTATION", mutBox.getX(), mutBox.getY() - 16, mutBox.getWidth(), 14, juce::Justification::centred);

    // ---- salida ----
    drawPanel (g, outPanel, "OUTPUT");
    paintOutputMeters (g);
    g.setColour (kDim);
    g.setFont (11.0f);
    for (size_t i = 0; i < outKnobs.size(); ++i)
    {
        const auto b = outKnobs[i]->slider.getBounds();
        g.drawText (outKnobs[i]->label, b.getX(), b.getY() - 2, b.getWidth(), 14, juce::Justification::centred);
    }
    g.setColour (kAccent);
    g.drawText ("EXPORT STEMS", outPanel.getX() + 14, outPanel.getY() + 402, 200, 14, juce::Justification::left);
}
