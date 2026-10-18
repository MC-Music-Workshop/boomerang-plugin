#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    Boomerang+ Plugin Editor with perfect parameter synchronization
    
    Features ButtonParameterAttachment for momentary button behavior
    that automatically syncs with the processor state.
*/
class BoomerangAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    BoomerangAudioProcessorEditor (BoomerangAudioProcessor&);
    ~BoomerangAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    //==============================================================================
    BoomerangAudioProcessor& audioProcessor;

    // UI Components
    juce::TextButton thruMuteButton;
    juce::TextButton recordButton;
    juce::TextButton playButton;
    juce::TextButton onceButton;
    juce::TextButton stackButton;
    juce::TextButton reverseButton;
    juce::TextButton stackLatchButton;  // Hit area for the MOM | LATCH slide switch (issue #71)

    juce::Slider volumeSlider;
    
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label versionLabel;
    
    juce::TextButton settingsButton;  // Gear icon for settings menu

    // Progress indicator
    juce::ProgressBar progressBar;
    double progressValue = 0.0;
    
    // Background image
    juce::Image backgroundImage;
    
    // LED states
    bool recordLED  = false;
    bool playLED    = false;
    bool onceLED    = false;
    bool reverseLED = false;
    bool stackLED   = false;
    bool slowLED    = false;

    // Parameter Attachments - only for continuous controls
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stackLatchAttachment;

    // Visual state / settings
    bool showButtonOverlays = false;  // Toggle for button hover/state overlays
    bool showFooterBar      = false;   // Toggle for footer bar with status messages
    
    juce::Colour recordColour = juce::Colours::red;
    juce::Colour playColour = juce::Colours::green;
    juce::Colour onceColour = juce::Colours::blue;
    juce::Colour stackColour = juce::Colours::orange;
    juce::Colour reverseColour = juce::Colours::purple;
    juce::Colour thruMuteColour = juce::Colours::grey;
    juce::Colour thruMuteColour_OFF = juce::Colours::lightgrey;
    
    int recordFlashCounter = 0;  // For loop wrap flash indicator
    float stackLatchThumbPos = 0.0f;  // Slide switch thumb: 0 = MOM, 1 = LATCH (eased in timer)
    std::atomic<bool> stackButtonWasDown { false };  // Track STACK button state for proper press/release
    
    // Button press animation state
    int thruMuteFlashCounter  = 0;
    int recordBtnFlashCounter = 0;
    int playFlashCounter      = 0;
    int onceFlashCounter      = 0;
    int stackFlashCounter     = 0;
    int reverseFlashCounter   = 0;
    bool prevThruMuteDown  = false;
    bool prevRecordDown    = false;
    bool prevPlayDown      = false;
    bool prevOnceDown      = false;
    bool prevStackDown     = false;
    bool prevReverseDown   = false;

    void setupButton(juce::TextButton& button, const juce::String& text, juce::Colour colour, bool isToggle = false);
    void updateStatusDisplay();
    void drawLED(juce::Graphics& g, int x, int y, int size, juce::Colour colour, bool isLit);
    void drawStackLatchSwitch(juce::Graphics& g, float scale);
    void showSettingsMenu();
    static constexpr int inputSourceMenuBase = 100;  // Menu item IDs for the Input Source submenu

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoomerangAudioProcessorEditor)
};