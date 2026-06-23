#include "LooperEngine.h"
#include "PluginProcessor.h"  // For ParameterIDs

//==============================================================================
LooperEngine::LooperEngine()
{
}

LooperEngine::~LooperEngine()
{
}

//==============================================================================
void LooperEngine::prepare(double newSampleRate, int newSamplesPerBlock, int newNumChannels)
{
    sampleRate = newSampleRate;
    samplesPerBlock = newSamplesPerBlock;
    numChannels = newNumChannels;
    maxLoopSamples = static_cast<int>(sampleRate * maxLoopLengthSeconds);

    // Initialize all loop slots
    for (auto& slot : loopSlots)
    {
        slot.buffer.setSize(numChannels, maxLoopSamples);
        slot.buffer.clear();
        slot.length.store(0);
        slot.hasContent.store(false);
        slot.isRecording.store(false);
        slot.isPlaying.store(false);
        slot.playPosition.store(0.0f);
        slot.recordPosition.store(0.0f);
    }

    reset();
}

void LooperEngine::reset()
{
    currentState = LooperState::Stopped;
    loopMode = LoopMode::Normal;
    currentDirection = DirectionMode::Forward;
    stackMode = StackMode::Off;
    onceMode = OnceMode::Off;
    thruMute = ThruMuteState::Off;
    speedMode = SpeedMode::Normal;
    activeLoopSlot = 0;

    for (auto& slot : loopSlots)
    {
        slot.buffer.clear();
        slot.length.store(0);
        slot.hasContent.store(false);
        slot.isRecording.store(false);
        slot.isPlaying.store(false);
        slot.playPosition.store(0.0f);
        slot.recordPosition.store(0.0f);
    }
}

//==============================================================================
void LooperEngine::processBlock(juce::AudioBuffer<float>& buffer)
{
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];

    // Thread-safe state access (issue #38)
    auto state = currentState.load();
    
    switch (state)
    {
        case LooperState::Stopped:
            // When thru mute is on, mute the input passthrough
            if (thruMute.load() == ThruMuteState::On)
                buffer.clear();
            // Otherwise pass-through audio when stopped
            break;

        case LooperState::Recording:
            processRecording(buffer, activeSlot);
            break;

        case LooperState::Playing:
            processPlayback(buffer, activeSlot);
            break;

        case LooperState::Overdubbing:
            processOverdubbing(buffer, activeSlot);
            break;

        case LooperState::ContinuousReverse:
        case LooperState::BufferFilled:
            // No processing defined yet
            break;
    }
    // Note: Volume is applied to loop signal only in processPlayback/processOverdubbing (issue #44)
}

//==============================================================================
void LooperEngine::onThruMuteButtonPressed()
{
    // Thread safety: Only allow one button press at a time
    bool expected = false;
    if (!stateTransitionInProgress.compare_exchange_strong(expected, true))
        return;  // Another thread is already processing a button press
    
    // Toggle thru/mute state
    // In ThruMute mode, input is recorded but not passed through. Only the recorded sound is played back.
    toggleThruMute();
    
    stateTransitionInProgress.store(false);
}
//==============================================================================
void LooperEngine::onRecordButtonPressed()
{
    // Thread safety: Only allow one button press at a time
    bool expected = false;
    if (!stateTransitionInProgress.compare_exchange_strong(expected, true))
        return;  // Another thread is already processing a button press
    
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    auto state = currentState.load();
    
    switch (state)
    {
        case LooperState::Stopped:
            // Start recording first loop
            startRecording();
            break;

        case LooperState::Recording:
            // Check if we've recorded anything yet
            if (activeSlot.recordPosition.load() > 0) 
            {
                // Stop recording, start playback
                stopRecording();
                startPlayback();
            }
            break;

        case LooperState::Playing:
            // Stop playing and start new recording
            stopPlayback();
            startRecording();
            break;

        case LooperState::Overdubbing:
            // Stop overdubbing, continue playing
            stopOverdubbing();
            break;

        case LooperState::ContinuousReverse:
        case LooperState::BufferFilled:
            break;
    }
    
    stateTransitionInProgress.store(false);
}

void LooperEngine::onPlayButtonPressed()
{
    // Thread safety: Only allow one button press at a time
    bool expected = false;
    if (!stateTransitionInProgress.compare_exchange_strong(expected, true))
        return;  // Another thread is already processing a button press
    
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    auto state = currentState.load();

    switch (state)
    {
        case LooperState::Stopped:
            if (activeSlot.hasContent.load())
            {
                startPlayback();
            }
            break;

        case LooperState::Recording:
            // Stop recording, go idle
            stopRecording();
            break;

        case LooperState::Playing:
        case LooperState::Overdubbing:
            // Stop playback
            stopPlayback();
            break;

        case LooperState::ContinuousReverse:
            // go idle
            stopPlayback();
            break;

        case LooperState::BufferFilled:
            break;
    }
    
    stateTransitionInProgress.store(false);
}

void LooperEngine::onOnceButtonPressed()
{
    // Thread safety: Only allow one button press at a time
    bool expected = false;
    if (!stateTransitionInProgress.compare_exchange_strong(expected, true))
        return;  // Another thread is already processing a button press
    
    auto state = currentState.load();
    auto once = onceMode.load();
    
    if (state == LooperState::Playing)
    {
        if (once == OnceMode::Off)
        {
            // The first time Once is pressed while playing, enable Once mode.
            // Playback continues until the end of the loop.
            // Another Once press will restart the loop.
            toggleOnceMode();
        }
        else
        {
            // Once when playing in once mode restarts at the beginning of the loop
            stopPlayback();
            startPlayback();
            // onceMode is unchanged here, so re-assert onceState so the ONCE
            // widget re-syncs after its toggle press (issue #66)
            setOnceMode(OnceMode::On);
        }
    }
    else if (state == LooperState::Stopped || state == LooperState::Recording)
    {
        if (state == LooperState::Recording)
        {
            stopRecording();
        }

        // If stopped or recording, play once
        setOnceMode(OnceMode::On);
        startPlayback();
    }
    
    stateTransitionInProgress.store(false);
}

// Momentary behavior - engaged while pressed
void LooperEngine::onStackButtonPressed()
{
    // Thread safety: Only allow one button press at a time
    bool expected = false;
    if (!stateTransitionInProgress.compare_exchange_strong(expected, true))
        return;  // Another thread is already processing a button press
    
    auto state = currentState.load();

    if (state == LooperState::Playing)
    {
        startOverdubbing();
    }
    else if (state == LooperState::Stopped)
    {
        toggleSpeedMode();
    }
    
    stateTransitionInProgress.store(false);
}

void LooperEngine::onStackButtonReleased()
{
    // Thread safety: Only allow one button press at a time
    bool expected = false;
    if (!stateTransitionInProgress.compare_exchange_strong(expected, true))
        return;  // Another thread is already processing a button press
    
    auto state = currentState.load();
    
    if (state == LooperState::Overdubbing)
    {
        stopOverdubbing();
    }
    
    stateTransitionInProgress.store(false);
}

void LooperEngine::onReverseButtonPressed()
{
    // Thread safety: Only allow one button press at a time
    bool expected = false;
    if (!stateTransitionInProgress.compare_exchange_strong(expected, true))
        return;  // Another thread is already processing a button press
    
    toggleDirection();
    
    stateTransitionInProgress.store(false);
}

//==============================================================================
void LooperEngine::startRecording()
{
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    
    activeSlot.isRecording.store(true);
    // Start at end if reverse, beginning if forward
    activeSlot.recordPosition.store((loopMode.load() == LoopMode::Reverse) 
        ? static_cast<float>(maxLoopSamples - 1) 
        : 0.0f);
    currentState.store(LooperState::Recording);
    
    // Notify host that record button is on
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::record, 1.0f);
}

void LooperEngine::stopRecording()
{
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    
    activeSlot.isRecording.store(false);
    
    // Calculate recorded length based on direction
    int finalLength = static_cast<int>(activeSlot.recordPosition.load());
    if (loopMode.load() == LoopMode::Reverse)
        finalLength = (maxLoopSamples - 1) - finalLength;
    
    activeSlot.length.store(finalLength);
    activeSlot.hasContent.store(finalLength > 0);
    currentState.store(LooperState::Stopped);
    
    // Notify host that record button is off
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::record, 0.0f);
}

void LooperEngine::startPlayback()
{
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    
    if (activeSlot.hasContent.load())
    {
        activeSlot.isPlaying.store(true);
        activeSlot.playPosition.store((loopMode.load() == LoopMode::Reverse)
            ? static_cast<float>(activeSlot.length.load() - 1)
            : 0.0f);
        currentState.store(LooperState::Playing);

        // Notify host that play button is on
        if (parameterNotifyCallback)
        {
            parameterNotifyCallback(ParameterIDs::play, 1.0f);
            // Dedicated read-only output for PLAY/STOP widget (issue #66)
            parameterNotifyCallback(ParameterIDs::playState, 1.0f);
        }
    }
}

void LooperEngine::stopPlayback()
{
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    
    activeSlot.isPlaying.store(false);
    currentState.store(LooperState::Stopped);

    // Notify host that play button is off
    if (parameterNotifyCallback)
    {
        parameterNotifyCallback(ParameterIDs::play, 0.0f);
        // Dedicated read-only output for PLAY/STOP widget (issue #66)
        parameterNotifyCallback(ParameterIDs::playState, 0.0f);
    }
}

void LooperEngine::startOverdubbing()
{
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    
    if (activeSlot.hasContent.load())
    {
        activeSlot.isRecording.store(true);
        activeSlot.isPlaying.store(true);
        currentState.store(LooperState::Overdubbing);
        stackMode.store(StackMode::On);
        
        // Notify host of stack mode on
        if (parameterNotifyCallback)
            parameterNotifyCallback(ParameterIDs::stack, 1.0f);
    }
}

void LooperEngine::stopOverdubbing()
{
    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    
    activeSlot.isRecording.store(false);
    currentState.store(LooperState::Playing);
    stackMode.store(StackMode::Off);
    
    // Notify host of stack mode off
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::stack, 0.0f);
}

void LooperEngine::toggleThruMute()
// do we even need enable/disable functions separately?
{
    auto current = thruMute.load();
    auto newState = (current == ThruMuteState::Off) ? ThruMuteState::On : ThruMuteState::Off;
    thruMute.store(newState);
    
    // Notify host of state change
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::thruMute, (newState == ThruMuteState::On) ? 1.0f : 0.0f);
}

void LooperEngine::toggleDirection()
{
    auto currentDir = currentDirection.load();
    auto currentLoop = loopMode.load();
    
    auto newDir = (currentDir == DirectionMode::Forward) ? DirectionMode::Reverse : DirectionMode::Forward;
    auto newLoop = (currentLoop == LoopMode::Normal) ? LoopMode::Reverse : LoopMode::Normal;
    
    currentDirection.store(newDir);
    loopMode.store(newLoop);

    auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot.load())];
    // Ensure playhead stays within bounds after direction change
    float currentPlayPos = activeSlot.playPosition.load();
    int currentLength = activeSlot.length.load();
    if (currentPlayPos < 0.0f && currentLength > 0)
        activeSlot.playPosition.store(currentPlayPos + static_cast<float>(currentLength));
    else if (currentPlayPos >= static_cast<float>(currentLength) && currentLength > 0)
        activeSlot.playPosition.store(std::fmod(currentPlayPos, static_cast<float>(currentLength)));
    
    // Notify host of state change
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::reverse, (newLoop == LoopMode::Reverse) ? 1.0f : 0.0f);
}

void LooperEngine::toggleOnceMode()
{
    auto current = onceMode.load();
    auto newMode = (current == OnceMode::Off) ? OnceMode::On : OnceMode::Off;
    onceMode.store(newMode);
    
    // Notify host of state change (use onceState output param, not once input)
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::onceState, (newMode == OnceMode::On) ? 1.0f : 0.0f);
}

void LooperEngine::setOnceMode(OnceMode mode)
{
    onceMode.store(mode);
    
    // Notify host of state change (use onceState output param, not once input)
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::onceState, (mode == OnceMode::On) ? 1.0f : 0.0f);
}

void LooperEngine::toggleStackMode()
{
    auto current = stackMode.load();
    stackMode.store((current == StackMode::Off) ? StackMode::On : StackMode::Off);
}

void LooperEngine::setStackMode(StackMode mode)
{
    stackMode.store(mode);
}

void LooperEngine::toggleSpeedMode()
{
    auto current = speedMode.load();
    auto newMode = (current == SpeedMode::Normal) ? SpeedMode::Half : SpeedMode::Normal;
    speedMode.store(newMode);
    
    // Notify host of speed mode change
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::slowMode, (newMode == SpeedMode::Half) ? 1.0f : 0.0f);
}

void LooperEngine::setSpeedMode(SpeedMode mode)
{
    speedMode.store(mode);
    
    // Notify host of speed mode change
    if (parameterNotifyCallback)
        parameterNotifyCallback(ParameterIDs::slowMode, (mode == SpeedMode::Half) ? 1.0f : 0.0f);
}

//==============================================================================
void LooperEngine::processRecording(juce::AudioBuffer<float>& buffer, LoopSlot& slot)
{
    int numSamples = buffer.getNumSamples();
    const int inputChannels = buffer.getNumChannels();
    float speed = (speedMode.load() == SpeedMode::Half) ? 0.5f : 1.0f;
    
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float currentRecordPos = slot.recordPosition.load();
        int writePos = static_cast<int>(currentRecordPos);
        
        // Check bounds BEFORE writing
        if (writePos < 0 || writePos >= maxLoopSamples)
        {
            // Buffer filled or invalid position - stop recording
            stopRecording();
            return;  // Exit immediately, don't process remaining samples
        }

        for (int channel = 0; channel < numChannels; ++channel)
        {
            // If we only have a mono input, mirror it across all loop channels
            const int sourceChannel = (inputChannels > 0) ? juce::jmin(channel, inputChannels - 1) : 0;
            const float inputSample = (inputChannels > 0) ? buffer.getSample(sourceChannel, sample) : 0.0f;
            slot.buffer.setSample(channel, writePos, inputSample);
        }
        
        // Advance record position respecting direction
        if (loopMode.load() == LoopMode::Reverse)
            slot.recordPosition.store(currentRecordPos - speed);
        else
            slot.recordPosition.store(currentRecordPos + speed);
    }
    
    // When thru mute is on, mute the input passthrough while recording
    if (thruMute.load() == ThruMuteState::On)
        buffer.clear();
}

void LooperEngine::processPlayback(juce::AudioBuffer<float>& buffer, LoopSlot& slot)
{
    if (!slot.hasContent.load() || slot.length.load() == 0)
    {
        buffer.clear();
        return;
    }

    // If stack mode is active, handle the whole buffer in the overdub path to
    // avoid re-processing the buffer per channel/sample.
    if (stackMode.load() == StackMode::On)
    {
        processOverdubbing(buffer, slot);
        return;
    }

    int numSamples = buffer.getNumSamples();
    float speed = (speedMode.load() == SpeedMode::Half) ? 0.5f : 1.0f;
    auto loopDirection = loopMode.load();
    auto thruMuteState = thruMute.load();
    auto once = onceMode.load();
    int slotLength = slot.length.load();
    float volume = outputVolume.load();  // Apply volume to loop signal only (issue #44)
    
    for (int sampleNum = 0; sampleNum < numSamples; ++sampleNum)
    {
        float currentPlayPos = slot.playPosition.load();
        for (int channel = 0; channel < numChannels; ++channel)
        {
            int pos = static_cast<int>(currentPlayPos);
            float frac = currentPlayPos - pos;
            
            float loopSample;
            
            if (loopDirection == LoopMode::Reverse)
            {
                // Read directly at the decreasing playhead position
                int prevPos = (pos > 0) ? pos - 1 : (slotLength - 1);
                float sample1 = slot.buffer.getSample(channel, pos);
                float sample2 = slot.buffer.getSample(channel, prevPos);
                loopSample = sample1 + frac * (sample2 - sample1);
            }
            else
            {
                int nextPos = (pos + 1) % slotLength;
                float sample1 = slot.buffer.getSample(channel, pos);
                float sample2 = slot.buffer.getSample(channel, nextPos);
                loopSample = sample1 + frac * (sample2 - sample1);
            }
            
            // Apply volume to loop signal only, not input (issue #44)
            float scaledLoopSample = loopSample * volume;
            
            // Thru mute handling: when ON, only play loop; when OFF, mix input with loop
            if (thruMuteState == ThruMuteState::On)
            {
                buffer.setSample(channel, sampleNum, scaledLoopSample);
            }
            else
            {
                float inputSample = buffer.getSample(channel, sampleNum);
                buffer.setSample(channel, sampleNum, scaledLoopSample + inputSample);
            }
        }
        
        bool wrapped = advancePosition(slot.playPosition, slotLength, speed);
        
        if (wrapped)
            loopWrapped.store(true);
        
        // Once mode: request stop via flag instead of direct state write (issue #38)
        if (once == OnceMode::On && wrapped)
        {
            stopPlayback();
            shouldDisableOnce.store(true);  // UI timer will call toggleOnceMode()
            break;
        }
    }
}

void LooperEngine::processOverdubbing(juce::AudioBuffer<float>& buffer, LoopSlot& slot)
{
    if (!slot.hasContent.load() || slot.length.load() == 0)
    {
        processRecording(buffer, slot);
        return;
    }

    int numSamples = buffer.getNumSamples();
    const int inputChannels = buffer.getNumChannels();
    float speed = (speedMode.load() == SpeedMode::Half) ? 0.5f : 1.0f;
    auto thruMuteState = thruMute.load();
    auto once = onceMode.load();
    int slotLength = slot.length.load();
    float volume = outputVolume.load();  // Apply volume to loop output only (issue #44)
    
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float currentPlayPos = slot.playPosition.load();
        int pos = static_cast<int>(currentPlayPos);
        
        for (int channel = 0; channel < numChannels; ++channel)
        {
            // Mirror mono input across all loop channels when needed
            const int sourceChannel = (inputChannels > 0) ? juce::jmin(channel, inputChannels - 1) : 0;
            float inputSample = (inputChannels > 0) ? buffer.getSample(sourceChannel, sample) : 0.0f;
            float loopSample = slot.buffer.getSample(channel, pos);
            
            // Overdub: mix input with existing content
            // Attenuate existing loop by 2.5dB to prevent overloading when stacking
            constexpr float stackAttenuation = 0.74989420933f; // -2.5dB
            float attenuatedLoop = loopSample * stackAttenuation;
            float overdubSample = attenuatedLoop + (inputSample * feedbackAmount.load());
            slot.buffer.setSample(channel, pos, overdubSample);
            
            // Apply volume to loop output only, not input (issue #44)
            float scaledLoopOutput = overdubSample * volume;
            
            // Thru mute handling: when ON, only output loop; when OFF, mix with input
            if (thruMuteState == ThruMuteState::On)
            {
                buffer.setSample(channel, sample, scaledLoopOutput);
            }
            else
            {
                buffer.setSample(channel, sample, scaledLoopOutput + inputSample);
            }
        }
        
        bool wrapped = advancePosition(slot.playPosition, slotLength, speed);

        if (wrapped)
            loopWrapped.store(true);

        // Once mode: request stop via flag instead of direct state write (issue #38)
        if (once == OnceMode::On && wrapped)
        {
            stopPlayback();
            shouldDisableOnce.store(true);  // UI timer will call toggleOnceMode()
            break;
        }
    }
}

bool LooperEngine::advancePosition(std::atomic<float>& position, int length, float speed)
{
    bool wrapped = false;
    auto loopDirection = loopMode.load();
    float currentPos = position.load();

    if (loopDirection == LoopMode::Reverse)
    {
        currentPos -= speed;
        // Wrap to end when going below 0
        if (currentPos < 0)
        {
            currentPos += length;
            wrapped = true;
        }
    }
    else
    {
        currentPos += speed;
        // Wrap to beginning when going past end
        if (currentPos >= length)
        {
            currentPos = 0.0f;
            wrapped = true;
        }
    }

    position.store(currentPos);
    return wrapped;
}

void LooperEngine::switchToNextLoopSlot()
{
    activeLoopSlot = (activeLoopSlot + 1) % maxLoopSlots;
}

float LooperEngine::getLoopProgress() const
{
    const auto& activeSlot = loopSlots[static_cast<size_t>(activeLoopSlot)];
    
    if (!activeSlot.hasContent.load() || activeSlot.length.load() == 0)
        return 0.0f;
    
    return activeSlot.playPosition.load() / static_cast<float>(activeSlot.length.load());
}
