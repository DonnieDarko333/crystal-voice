#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace cvui {

// Schriften, Logo und Hammerschlag-Struktur, einmal pro Prozess erzeugt.
struct Assets {
    Assets();
    juce::Typeface::Ptr semi, bold;
    juce::Image logo;
    juce::Image hammer; // Struktur-Overlay (hell/dunkel mit Alpha), doppelte Auflösung
    juce::Font font(float h, bool b = true) const;
};

// Drehschalter mit Bakelit-Zeigerknopf und rastenden Stellungen.
class RotarySwitch : public juce::Component {
public:
    RotarySwitch(juce::StringArray labels, juce::Array<float> anglesDeg);
    void setIndex(int i);
    int getIndex() const { return index; }
    std::function<void(int)> onChange;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void select(int i);
    juce::Point<float> centre() const;
    juce::SharedResourcePointer<Assets> assets;
    juce::StringArray labels;
    juce::Array<float> angles;
    int index = 0;
};

struct Look : juce::LookAndFeel_V4 {
    Look();
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 13; }
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::SharedResourcePointer<Assets> assets;
};

} // namespace cvui

class CrystalVoiceEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit CrystalVoiceEditor(CrystalVoiceProcessor&);
    ~CrystalVoiceEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile(bool reference);

    cvui::Look look;
    juce::SharedResourcePointer<cvui::Assets> assets;
    CrystalVoiceProcessor& proc;

    juce::Slider vocal, instr, sync;
    cvui::RotarySwitch micSwitch { { "HEIL\nPR 37", "SHURE\nKSM9", "NEUMANN\nKMS 105" }, { -52.0f, 0.0f, 52.0f } };
    cvui::RotarySwitch presSwitch { { "OFF", "ON" }, { -45.0f, 45.0f } };
    juce::TextButton loadRef { "LOAD" }, loadIr { "LOAD" }, measure { "MEASURE" };
    juce::Label status;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aVocal, aInstr, aSync;
    std::unique_ptr<juce::ParameterAttachment> aMic, aPres;
    std::unique_ptr<juce::FileChooser> chooser;

    // Zeigerstellungen 0..1 (mit Trägheit) und Lampen
    float needle[3] = { 0.0f, 0.0f, 0.0f };
    bool firstTick = true;
    bool lampLink = false, lampRoom = false, lampFault = true;
    int waitTicks = 0, blinkTicks = 0;
    juce::String refusedNote;
    int refusedTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrystalVoiceEditor)
};
