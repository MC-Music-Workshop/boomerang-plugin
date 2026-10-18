#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "LooperEngine.h"

//==============================================================================
// Parameter IDs
namespace ParameterIDs
{
    const juce::String thruMute    = "thruMute";
    const juce::String record      = "record";
    const juce::String play        = "play";
    const juce::String once        = "once";
    const juce::String stack       = "stack";
    const juce::String reverse     = "reverse";
    const juce::String volume      = "volume";
    const juce::String feedback    = "feedback";
    const juce::String loopCycle   = "loopCycle";  // Pulses when loop wraps (for REC blink)
    const juce::String slowMode    = "slowMode";   // On when speed is half (SLOW LED)
    const juce::String onceState   = "onceState"; // On when Once mode is active (ONCE LED)
    const juce::String playState   = "playState"; // On when playing (for PLAY/STOP widget)
    const juce::String inputSource = "inputSource"; // 2x2 input routing (issue #69)
    const juce::String onceOff     = "onceOff";     // Cancels Once mode, loop keeps playing (issue #62)
}

// Plugin state properties that aren't host parameters (saved with the session)
namespace StateIDs
{
    const juce::Identifier extendedFeatures { "extendedFeatures" };  // Non-hardware features on/off
}

// Choices for the inputSource parameter, in parameter index order
namespace InputSource
{
    enum { stereo = 0, left, right, mono };
    const juce::StringArray names { "Stereo", "Left only", "Right only", "Mono (L + R)" };
}

//==============================================================================
/**
    Boomerang+ Looper Plugin Processor
    
    Professional looper with momentary button controls, multiple loop slots,
    overdubbing, reverse, and stack modes.
    
    Implements APVTS::Listener to respond to parameter changes from MIDI CC
    or DAW automation, in addition to UI button clicks.
*/
class BoomerangAudioProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    BoomerangAudioProcessor();
    ~BoomerangAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
   #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // APVTS access for UI and DAW automation
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    
    // Looper engine access for UI
    LooperEngine* getLooperEngine() const { return looperEngine.get(); }

    // Features the original hardware doesn't have; off = faithful emulation
    void setExtendedFeatures(bool enabled);
    bool getExtendedFeatures() const { return looperEngine->getExtendedFeatures(); }

private:
    //==============================================================================
    // AudioProcessorValueTreeState::Listener implementation
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    // Core looper engine
    std::unique_ptr<LooperEngine> looperEngine;

    // AudioProcessorValueTreeState for parameter management
    juce::AudioProcessorValueTreeState apvts;
    
    // Track previous button states to detect edges (press/release)
    std::atomic<bool> prevStackValue { false };
    
    // Flag to prevent circular notifications when internal state changes update parameters
    std::atomic<bool> updatingFromInternalState { false };
    
    // Loop cycle pulse tracking (issue #51)
    // Tracks loop wrap state to pulse loopCycle parameter from processBlock
    std::atomic<bool> loopCyclePulseActive { false };
    std::atomic<int> loopCyclePulseCounter { 0 };
    static constexpr int loopCyclePulseDurationFrames = 5;  // ~80ms at 60Hz callback rate
    
    // Applies the inputSource routing to the first two channels of the buffer
    static void applyInputSource(juce::AudioBuffer<float>& buffer, int source);

    // Helper function to create parameter layout
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomerangAudioProcessor)
};