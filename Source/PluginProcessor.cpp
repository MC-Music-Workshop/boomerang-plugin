#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BoomerangAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Button parameters - use bool parameters that can be MIDI mapped
    // Note: For momentary buttons, we'll handle press/release in parameter change callback
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::thruMute, 1),
        "Thru/Mute",
        false));  // toggle button

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::record, 1),
        "Record",
        false));  // momentary button

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::play, 1),
        "Play/Stop",
        false));  // momentary button

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::once, 1),
        "Once",
        false));  // momentary button

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::stack, 1),
        "Stack/Speed",
        false));  // momentary button (tracks press/release)

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::reverse, 1),
        "Reverse",
        false));  // toggle button

    // Loop cycle indicator - pulses briefly when loop wraps (for external REC blink)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::loopCycle, 1),
        "Loop Cycle",
        false));  // read-only pulse indicator

    // Slow mode indicator - on when speed is half (for external SLOW LED)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::slowMode, 1),
        "Slow Mode",
        false));  // read-only state indicator

    // Once state indicator - on when Once mode is active (for external ONCE LED)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::onceState, 1),
        "Once State",
        false));  // read-only state indicator

    // Play state indicator - on when playing (for external PLAY/STOP widget)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParameterIDs::playState, 1),
        "Play State",
        false));  // read-only state indicator

    // Continuous parameters
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParameterIDs::volume, 1),
        "Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParameterIDs::feedback, 1),
        "Feedback",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f));

    return layout;
}

//==============================================================================
BoomerangAudioProcessor::BoomerangAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    // Initialize looper engine
    looperEngine = std::make_unique<LooperEngine>();
    
    // Set up callback for looper engine to notify host of internal state changes
    looperEngine->setParameterNotifyCallback([this](const juce::String& paramID, float value)
    {
        // Capture parameter ID and value to pass to message thread
        // Don't capture raw parameter pointer - look it up in the async call
        juce::MessageManager::callAsync([this, paramID, value]()
        {
            if (auto* param = apvts.getParameter(paramID))
            {
                // Normalize value to 0-1 range
                float normalizedValue = param->convertTo0to1(value);
                
                // Set flag to prevent parameterChanged from being called
                updatingFromInternalState.store(true);
                
                // Use gesture for proper host notification
                param->beginChangeGesture();
                param->setValueNotifyingHost(normalizedValue);
                param->endChangeGesture();
                
                // Clear flag
                updatingFromInternalState.store(false);
            }
        });
    });
    
    // Add parameter listeners for MIDI/automation support
    apvts.addParameterListener(ParameterIDs::thruMute, this);
    apvts.addParameterListener(ParameterIDs::record, this);
    apvts.addParameterListener(ParameterIDs::play, this);
    apvts.addParameterListener(ParameterIDs::once, this);
    apvts.addParameterListener(ParameterIDs::stack, this);
    apvts.addParameterListener(ParameterIDs::reverse, this);
}

BoomerangAudioProcessor::~BoomerangAudioProcessor()
{
    // Remove parameter listeners
    apvts.removeParameterListener(ParameterIDs::thruMute, this);
    apvts.removeParameterListener(ParameterIDs::record, this);
    apvts.removeParameterListener(ParameterIDs::play, this);
    apvts.removeParameterListener(ParameterIDs::once, this);
    apvts.removeParameterListener(ParameterIDs::stack, this);
    apvts.removeParameterListener(ParameterIDs::reverse, this);
}

//==============================================================================
// Parameter change listener - handles MIDI CC and DAW automation
void BoomerangAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // Ignore parameter changes that originated from internal state changes
    if (updatingFromInternalState.load())
        return;
    
    // Safety check - ensure looperEngine exists
    if (looperEngine == nullptr)
        return;
    
    const bool buttonPressed = (newValue >= 0.5f);  // Treat bool params as pressed when >= 0.5
    
    // Pure toggle buttons - call engine on EVERY change (both on and off)
    // These buttons toggle their internal state each time they're pressed
    if (parameterID == ParameterIDs::thruMute)
    {
        looperEngine->onThruMuteButtonPressed();
    }
    else if (parameterID == ParameterIDs::reverse)
    {
        looperEngine->onReverseButtonPressed();
    }
    // Action buttons - trigger on every toggle edge since GP uses toggle widgets
    // The updatingFromInternalState flag prevents circular calls
    else if (parameterID == ParameterIDs::record)
    {
        looperEngine->onRecordButtonPressed();
    }
    else if (parameterID == ParameterIDs::play)
    {
        // Act on the press edge only (momentary widget). Acting on both edges
        // double-fires: e.g. PLAY/STOP during record would stop recording on
        // press, then start playback on release (issue #66).
        if (buttonPressed)
            looperEngine->onPlayButtonPressed();
    }
    else if (parameterID == ParameterIDs::once)
    {
        if (buttonPressed)
            looperEngine->onOnceButtonPressed();
    }
    // Stack button - needs both press and release events
    else if (parameterID == ParameterIDs::stack)
    {
        bool prevValue = prevStackValue.load();
        prevStackValue.store(buttonPressed);
        
        // Detect edges: 0→1 = press, 1→0 = release
        if (buttonPressed && !prevValue)
            looperEngine->onStackButtonPressed();
        else if (!buttonPressed && prevValue)
            looperEngine->onStackButtonReleased();
    }
}

//==============================================================================
const juce::String BoomerangAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BoomerangAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BoomerangAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BoomerangAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BoomerangAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BoomerangAudioProcessor::getNumPrograms()
{
    return 1;
}

int BoomerangAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BoomerangAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String BoomerangAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void BoomerangAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void BoomerangAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use whichever is larger so we can still play back in stereo even if the host only
    // provides a mono input (common with a single mic).
    const int maxChannels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());
    looperEngine->prepare(sampleRate, samplesPerBlock, maxChannels);
}

void BoomerangAudioProcessor::releaseResources()
{
    looperEngine->reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BoomerangAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    const auto& outSet = layouts.getMainOutputChannelSet();
    const auto& inSet  = layouts.getMainInputChannelSet();

    // Allow mono or stereo output only
    if (outSet != juce::AudioChannelSet::mono() && outSet != juce::AudioChannelSet::stereo())
        return false;

    #if ! JucePlugin_IsSynth
    // Permit mono-in→stereo-out or matching mono/mono, stereo/stereo.
    const bool inputIsMonoOrStereo = (inSet == juce::AudioChannelSet::mono() || inSet == juce::AudioChannelSet::stereo());
    if (! inputIsMonoOrStereo)
        return false;

    // NOTE: We don't currently support stereo-in → mono-out because the engine assumes
    // output channels >= input channels. If we add downmixing, we can relax this.
    const bool matches = (inSet == outSet) || (inSet == juce::AudioChannelSet::mono() && outSet == juce::AudioChannelSet::stereo());
    if (! matches)
        return false;
    #endif

    return true;
  #endif
}
#endif

void BoomerangAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Safety check
    if (looperEngine == nullptr)
    {
        buffer.clear();
        return;
    }

    // Update continuous parameters from APVTS (thread-safe)
    if (auto* volumeParam = apvts.getRawParameterValue(ParameterIDs::volume))
    {
        float volumeValue = volumeParam->load();
        if (!std::isnan(volumeValue) && !std::isinf(volumeValue))
            looperEngine->setVolume(volumeValue);
    }
    
    if (auto* feedbackParam = apvts.getRawParameterValue(ParameterIDs::feedback))
    {
        float feedbackValue = feedbackParam->load();
        if (!std::isnan(feedbackValue) && !std::isinf(feedbackValue))
            looperEngine->setFeedback(feedbackValue);
    }

    // Process audio through looper engine
    looperEngine->processBlock(buffer);

    // Upmix mono input to stereo output so users with a single mic still hear both channels
    if (totalNumInputChannels == 1 && totalNumOutputChannels >= 2)
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    
    // Process audio thread requests (issue #51 - ensures Once mode updates even when UI closed)
    // This handles shouldDisableOnce flag set by audio thread when loop wraps in Once mode
    looperEngine->processAudioThreadRequests();
    
    // Pulse loopCycle parameter when loop wraps (issue #51)
    // This runs in processBlock so it works even when UI is closed
    // Use peek (load) to check without clearing - the UI may also want to flash
    if (looperEngine->checkAndClearLoopWrapped())
    {
        // Start the pulse
        if (!loopCyclePulseActive.load())
        {
            loopCyclePulseActive.store(true);
            loopCyclePulseCounter.store(loopCyclePulseDurationFrames);
            
            // Set parameter to 1.0 (on) using async call to avoid audio thread blocking
            juce::MessageManager::callAsync([this]()
            {
                if (auto* param = apvts.getParameter(ParameterIDs::loopCycle))
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost(1.0f);
                    param->endChangeGesture();
                }
            });
        }
    }
    else if (loopCyclePulseActive.load())
    {
        // Decrement pulse counter
        int count = loopCyclePulseCounter.load();
        if (count > 0)
        {
            loopCyclePulseCounter.store(count - 1);
        }
        else
        {
            // Pulse complete - reset to 0
            loopCyclePulseActive.store(false);
            
            juce::MessageManager::callAsync([this]()
            {
                if (auto* param = apvts.getParameter(ParameterIDs::loopCycle))
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost(0.0f);
                    param->endChangeGesture();
                }
            });
        }
    }
}

//==============================================================================
bool BoomerangAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* BoomerangAudioProcessor::createEditor()
{
    return new BoomerangAudioProcessorEditor (*this);
}

//==============================================================================
void BoomerangAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Save APVTS state - this automatically handles all parameters
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void BoomerangAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore APVTS state - this automatically handles all parameters
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    
    // Reset read-only indicator parameters AND engine state to defaults
    // These are state indicators, not persistent settings
    if (auto* slowParam = apvts.getParameter(ParameterIDs::slowMode))
        slowParam->setValueNotifyingHost(0.0f);  // Normal speed
    if (auto* onceParam = apvts.getParameter(ParameterIDs::onceState))
        onceParam->setValueNotifyingHost(0.0f);  // Once off
    if (auto* cycleParam = apvts.getParameter(ParameterIDs::loopCycle))
        cycleParam->setValueNotifyingHost(0.0f);  // No pulse
    
    // Reset engine to default state (Normal speed, Once off, etc.)
    looperEngine->resetTransientState();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BoomerangAudioProcessor();
}
