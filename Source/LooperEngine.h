#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <functional>

//==============================================================================
/**
    Core looper engine implementing the Boomerang+ functionality
    
    Features:
    - Circular buffer recording/playback
    - Multiple loop slots
    - Overdubbing with crossfading
    - Reverse playback
    - Stack mode
    - Once mode
    - Proper momentary button handling
*/
class LooperEngine
{
public:
    //==============================================================================
    enum class LooperState
    {
        Stopped,
        Recording,
        Playing,
        Overdubbing,
        ContinuousReverse,
        BufferFilled
    };

    enum class LoopMode
    {
        Normal,
        Reverse,
    };

    enum class DirectionMode
    {
        Forward,
        Reverse
    };

    enum class StackMode
    {
        Off,
        On
    };

    enum class OnceMode
    {
        Off,
        On
    };

    enum class ThruMuteState
    {
        Off,
        On
    };

    enum class SpeedMode
    {
        Normal,
        Half
    };

    //==============================================================================
    LooperEngine();
    ~LooperEngine();

    //==============================================================================
    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();

    //==============================================================================
    void processBlock(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    // Button event handlers
    void onThruMuteButtonPressed();
    void onRecordButtonPressed();
    void onPlayButtonPressed();
    void onOnceButtonPressed();
    void onOnceOffButtonPressed();    // Cancels Once mode, loop keeps playing (issue #62)
    void onStackButtonPressed();      // Momentary: called when pressed (latch: toggles the stack)
    void onStackButtonReleased();     // Momentary: called when released (latch: ignored)
    void onReverseButtonPressed();

    //==============================================================================
    // Parameter setters
    void setVolume(float volume) { outputVolume.store(volume); }
    void setExtendedFeatures(bool enabled) { extendedFeatures.store(enabled); }
    bool getExtendedFeatures() const       { return extendedFeatures.load(); }
    void setFeedback(float feedback) { feedbackAmount.store(feedback); }
    void setStackLatch(bool latch) { stackLatch.store(latch); }

    //==============================================================================
    // State queries for UI updates (thread-safe via atomic loads)
    LooperState getState() const { return currentState.load(); }
    LoopMode getLoopMode() const { return loopMode.load(); }
    DirectionMode getDirection() const { return currentDirection.load(); }
    StackMode getStackMode() const { return stackMode.load(); }
    OnceMode getOnceMode() const { return onceMode.load(); }
    ThruMuteState getThruMuteState() const { return thruMute.load(); }
    SpeedMode getSpeedMode() const { return speedMode.load(); }

    bool isRecording() const { auto state = currentState.load(); return state == LooperState::Recording || state == LooperState::Overdubbing; }
    bool isPlaying() const { auto state = currentState.load(); return state == LooperState::Playing || state == LooperState::Overdubbing; }
    float getLoopProgress() const;
    int getCurrentLoopSlot() const { return activeLoopSlot.load(); }
    
    // Check and clear loop wrap flag for UI flash indicator
    bool checkAndClearLoopWrapped() 
    { 
        return loopWrapped.exchange(false); 
    }
    
    // Callback for notifying host of parameter state changes
    // parameterID: the ID of the parameter that changed
    // newValue: the new value (0.0 or 1.0 for button parameters)
    using ParameterNotifyCallback = std::function<void(const juce::String& parameterID, float newValue)>;
    void setParameterNotifyCallback(ParameterNotifyCallback callback) { parameterNotifyCallback = callback; }
    
    // Process audio thread requests (called from UI timer - issue #38)
    void processAudioThreadRequests()
    {
        // Check if audio thread requested Once mode disable
        // Set rather than toggle: a ONCE OFF between the stop and this call
        // must not flip Once back on (issue #62)
        if (shouldDisableOnce.exchange(false))
        {
            setOnceMode(OnceMode::Off);
        }
    }
    
    // Reset transient state to defaults (called on state restoration)
    // This resets modes that shouldn't persist across sessions
    void resetTransientState()
    {
        speedMode.store(SpeedMode::Normal);
        onceMode.store(OnceMode::Off);
    }

private:
    //==============================================================================
    struct LoopSlot
    {
        juce::AudioBuffer<float> buffer;
        std::atomic<int> length { 0 };
        std::atomic<bool> hasContent { false };
        std::atomic<bool> isRecording { false };
        std::atomic<bool> isPlaying { false };
        std::atomic<float> playPosition { 0.0f };
        std::atomic<float> recordPosition { 0.0f };
        float fadeInGain = 1.0f;
        float fadeOutGain = 1.0f;
    };

    //==============================================================================
    static constexpr int maxLoopSlots = 4;
    static constexpr int maxLoopLengthSeconds = 240; // 4 minutes

    std::array<LoopSlot, maxLoopSlots> loopSlots;
    std::atomic<int> activeLoopSlot{0};

    // Thread-safe state variables (issue #38)
    // These are accessed from both UI and audio threads
    std::atomic<LooperState> currentState{LooperState::Stopped};
    std::atomic<LoopMode> loopMode{LoopMode::Normal};
    std::atomic<DirectionMode> currentDirection{DirectionMode::Forward};
    std::atomic<StackMode> stackMode{StackMode::Off};
    std::atomic<OnceMode> onceMode{OnceMode::Off};
    std::atomic<bool> extendedFeatures{true};  // Non-hardware features (ONCE OFF); off = faithful emulation
    std::atomic<ThruMuteState> thruMute{ThruMuteState::Off};
    std::atomic<SpeedMode> speedMode{SpeedMode::Normal};

    double sampleRate = 44100.0;
    int samplesPerBlock = 512;
    int numChannels = 2;
    int maxLoopSamples = 0;

    // Audio processing parameters (thread-safe)
    std::atomic<float> outputVolume { 1.0f };
    std::atomic<float> feedbackAmount { 0.5f };
    std::atomic<bool> stackLatch { false };  // STACK presses toggle overdub instead of holding it (issue #71)

    // Timing and synchronization
    std::atomic<bool> loopWrapped{false};  // Set when loop cycles to position 0
    
    // Request flags for audio→UI thread communication (issue #38)
    // Audio thread sets these, UI timer processes them
    std::atomic<bool> shouldDisableOnce{false};
    
    // Thread safety: Prevent concurrent state transitions (issue #39)
    std::atomic<bool> stateTransitionInProgress{false};
    
    // Callback for parameter state notifications to host
    ParameterNotifyCallback parameterNotifyCallback;

    //==============================================================================
    void startRecording();
    void stopRecording();
    void startPlayback();
    void stopPlayback();
    void startOverdubbing();
    void stopOverdubbing();
    void toggleThruMute();
    void toggleDirection();
    void toggleOnceMode();
    void setOnceMode(OnceMode mode);
    void toggleStackMode();
    void setStackMode(StackMode mode);
    void toggleSpeedMode();
    void setSpeedMode(SpeedMode mode);

    void processRecording(juce::AudioBuffer<float>& buffer, LoopSlot& slot);
    void processPlayback(juce::AudioBuffer<float>& buffer, LoopSlot& slot);
    void processOverdubbing(juce::AudioBuffer<float>& buffer, LoopSlot& slot);

    bool advancePosition(std::atomic<float>& position, int length, float speed);
    void switchToNextLoopSlot();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperEngine)
};
