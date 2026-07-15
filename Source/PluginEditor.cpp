#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "git_version.h"  // Generated at build time with current git hash

//==============================================================================
BoomerangAudioProcessorEditor::BoomerangAudioProcessorEditor (BoomerangAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), progressBar(progressValue)
{
    // Load background image from embedded binary data
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::turrama_jpg,
                                                      BinaryData::turrama_jpgSize);
    
    // Set default size and make resizable with aspect ratio constraint
    // Aspect ratio depends on footer visibility: 700x240 (with footer) or 700x200 (without)
    setResizable(true, true);
    if (showFooterBar)
    {
        // Footer visible: 700x240 aspect ratio, default at 1.5x scale (1050x360)
        setSize(1050, 360);
        setResizeLimits(350, 120, 1400, 480);  // 0.5x to 2x scale
        getConstrainer()->setFixedAspectRatio(700.0 / 240.0);
    }
    else
    {
        // Footer hidden: 700x200 aspect ratio (image only), default at 1.5x scale (1050x300)
        setSize(1050, 300);
        setResizeLimits(350, 100, 1400, 400);  // 0.5x to 2x scale
        getConstrainer()->setFixedAspectRatio(700.0 / 200.0);
    }

    // Setup buttons as transparent overlays
    // Position them over the foot switches in the image
    // Image dimensions: 700x200, buttons are roughly centered vertically at y~130
    
    setupButton(thruMuteButton, "", juce::Colours::transparentBlack, true);  // No text, transparent
    setupButton(recordButton, "", juce::Colours::transparentBlack, false);
    setupButton(playButton, "", juce::Colours::transparentBlack, false);
    setupButton(onceButton, "", juce::Colours::transparentBlack, false);
    setupButton(stackButton, "", juce::Colours::transparentBlack, false);  // Momentary, not toggle
    setupButton(reverseButton, "", juce::Colours::transparentBlack, true);
    
    // Make buttons semi-transparent for hover feedback
    thruMuteButton.setAlpha(0.0f);  // Fully transparent by default
    recordButton.setAlpha(0.0f);
    playButton.setAlpha(0.0f);
    onceButton.setAlpha(0.0f);
    stackButton.setAlpha(0.0f);
    reverseButton.setAlpha(0.0f);

    // Setup sliders
    // Volume slider overlays the OUTPUT LEVEL knob in the image
    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(1.0);
    volumeSlider.setAlpha(1.0f);  // Semi-transparent to show device knob
    addAndMakeVisible(volumeSlider);

    statusLabel.setText("Stopped", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(statusLabel);

    // Setup version label
    versionLabel.setText("v" BOOMERANG_VERSION " (" BOOMERANG_GIT_HASH ")", juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::bottomRight);
    versionLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    versionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    addAndMakeVisible(versionLabel);

    // Setup settings button (invisible, gear drawn in paint())
    settingsButton.setButtonText("");
    settingsButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    settingsButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    settingsButton.onClick = [this]() { showSettingsMenu(); };
    addAndMakeVisible(settingsButton);

    // Setup progress bar
    // addAndMakeVisible(progressBar);  // Hidden for now

    // Attach continuous controls to APVTS
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "volume", volumeSlider);
    
    // All buttons use onClick - manual control, visual state synced in timer
    thruMuteButton.onClick = [this]() {
        audioProcessor.getLooperEngine()->onThruMuteButtonPressed();
    };
    
    recordButton.onClick = [this]() {
        audioProcessor.getLooperEngine()->onRecordButtonPressed();
    };
    
    playButton.onClick = [this]() {
        audioProcessor.getLooperEngine()->onPlayButtonPressed();
    };
    
    onceButton.onClick = [this]() {
        audioProcessor.getLooperEngine()->onOnceButtonPressed();
    };
    
    reverseButton.onClick = [this]() {
        audioProcessor.getLooperEngine()->onReverseButtonPressed();
    };
    
    // Stack button - use onClick for stopped (speed toggle), onStateChange for playing (overdub)
    stackButton.onClick = [this]() {
        auto state = audioProcessor.getLooperEngine()->getState();
        // onClick only does something when stopped - toggles speed
        if (state == LooperEngine::LooperState::Stopped)
        {
            audioProcessor.getLooperEngine()->onStackButtonPressed();
        }
    };
    
    // Handle press/release for overdub while playing
    stackButton.onStateChange = [this]() {
        bool isDown = stackButton.isDown();
        bool wasDown = stackButtonWasDown.load();
        auto state = audioProcessor.getLooperEngine()->getState();
        
        // Only handle state changes when playing/overdubbing
        if (state == LooperEngine::LooperState::Playing || state == LooperEngine::LooperState::Overdubbing)
        {
            if (isDown && !wasDown)
            {
                audioProcessor.getLooperEngine()->onStackButtonPressed();
                stackButtonWasDown.store(true);
            }
            else if (!isDown && wasDown)
            {
                audioProcessor.getLooperEngine()->onStackButtonReleased();
                stackButtonWasDown.store(false);
            }
        }
        else
        {
            // Keep tracking synced when not playing
            stackButtonWasDown.store(isDown);
        }
    };

    // Start timer for UI updates and audio thread request processing (issue #38)
    startTimer(16); // 16ms (~60Hz) for responsive UI and Once mode auto-off
}

BoomerangAudioProcessorEditor::~BoomerangAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void BoomerangAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    // Right-click no longer used - settings are in the gear menu
    juce::AudioProcessorEditor::mouseDown(event);
}

void BoomerangAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Draw background image scaled to window size
    if (backgroundImage.isValid())
    {
        // When footer is hidden, image fills entire window
        // When footer is shown, image fills top portion (200/240 of height)
        int destHeight = showFooterBar 
            ? static_cast<int>(getHeight() * (200.0 / 240.0))
            : getHeight();
        
        // drawImage(image, destX, destY, destW, destH, srcX, srcY, srcW, srcH)
        g.drawImage(backgroundImage, 
                   0, 0, getWidth(), destHeight,
                   0, 0, backgroundImage.getWidth(), backgroundImage.getHeight(),
                   false);
    }
    else
    {
        // Fallback: solid color
        g.fillAll(juce::Colour(0xff404040));
        g.setColour(juce::Colours::white);
        g.drawText("Background image not found", getLocalBounds(), juce::Justification::centred);
    }
    
    // Draw inset shadow effect when buttons are pressed (always active)
    // This creates a "pushed in" look by drawing shadows at top/left edges
    auto drawPressedInsetShadow = [&](juce::TextButton& button) {
        if (button.isDown())
        {
            auto bounds = button.getBounds();
            float shadowDepth = 4.0f;  // How "deep" the button appears pressed
            
            // Dark shadow on top edge (light coming from above)
            g.setGradientFill(juce::ColourGradient(
                juce::Colours::black.withAlpha(0.6f), 
                bounds.getX(), bounds.getY(),
                juce::Colours::transparentBlack,
                bounds.getX(), bounds.getY() + shadowDepth * 2,
                false));
            g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), (int)(shadowDepth * 2));
            
            // Dark shadow on left edge
            g.setGradientFill(juce::ColourGradient(
                juce::Colours::black.withAlpha(0.5f),
                bounds.getX(), bounds.getY(),
                juce::Colours::transparentBlack,
                bounds.getX() + shadowDepth * 2, bounds.getY(),
                false));
            g.fillRect(bounds.getX(), bounds.getY(), (int)(shadowDepth * 2), bounds.getHeight());
            
            // Subtle highlight on bottom edge (recessed surface catching light)
            g.setGradientFill(juce::ColourGradient(
                juce::Colours::transparentWhite,
                bounds.getX(), bounds.getBottom() - shadowDepth,
                juce::Colours::white.withAlpha(0.15f),
                bounds.getX(), bounds.getBottom(),
                false));
            g.fillRect(bounds.getX(), bounds.getBottom() - (int)shadowDepth, bounds.getWidth(), (int)shadowDepth);
            
            // Overall darkening to simulate being in shadow
            g.setColour(juce::Colours::black.withAlpha(0.2f));
            g.fillRect(bounds);
        }
    };
    
    // Apply inset shadow to all buttons when pressed
    drawPressedInsetShadow(thruMuteButton);
    drawPressedInsetShadow(recordButton);
    drawPressedInsetShadow(playButton);
    drawPressedInsetShadow(onceButton);
    drawPressedInsetShadow(stackButton);
    drawPressedInsetShadow(reverseButton);
    
    // Additional overlay effects - only when overlays enabled
    if (showButtonOverlays)
    {
        auto drawButtonPressEffect = [&](juce::TextButton& button, juce::Colour colour, int flashCounter) {
            auto bounds = button.getBounds();
            
            // Release flash - bright pulse that fades
            if (!button.isDown() && flashCounter > 0)
            {
                float alpha = flashCounter / 5.0f * 0.5f;  // Fade from 0.5 to 0
                g.setColour(colour.withAlpha(alpha));
                g.fillRect(bounds);
            }
        };
        
        // Draw release flash effects for all buttons
        drawButtonPressEffect(thruMuteButton, juce::Colours::yellow, thruMuteFlashCounter);
        drawButtonPressEffect(playButton, juce::Colours::green, playFlashCounter);
        drawButtonPressEffect(onceButton, juce::Colours::blue, onceFlashCounter);
        drawButtonPressEffect(stackButton, juce::Colours::orange, stackFlashCounter);
        drawButtonPressEffect(reverseButton, juce::Colours::purple, reverseFlashCounter);
        
        // Record button: combine press effect with loop wrap flash
        if (recordButton.isDown())
        {
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRect(recordButton.getBounds());
        }
        else if (recordFlashCounter > 0)  // Loop wrap flash
        {
            g.setColour(juce::Colours::red.withAlpha(0.7f));
            g.fillRect(recordButton.getBounds());
        }
        else if (recordBtnFlashCounter > 0)  // Release flash
        {
            float alpha = recordBtnFlashCounter / 5.0f * 0.5f;
            g.setColour(juce::Colours::red.withAlpha(alpha));
            g.fillRect(recordButton.getBounds());
        }
        
        // Hover/toggle overlays
        auto drawHoverOverlay = [&](juce::TextButton& button, juce::Colour colour) {
            if (button.isMouseOver() && !button.isDown())
            {
                g.setColour(colour.withAlpha(0.2f));
                g.fillRect(button.getBounds());
            }
            if (button.getToggleState())
            {
                g.setColour(colour.withAlpha(0.4f));
                g.fillRect(button.getBounds());
            }
        };
        
        drawHoverOverlay(thruMuteButton, juce::Colours::yellow);
        drawHoverOverlay(recordButton, juce::Colours::red);
        drawHoverOverlay(playButton, juce::Colours::green);
        drawHoverOverlay(onceButton, juce::Colours::blue);
        drawHoverOverlay(stackButton, juce::Colours::orange);
        drawHoverOverlay(reverseButton, juce::Colours::purple);
    }
    
    // Volume slider overlay when interacting - small box that follows the slider position
    if (volumeSlider.isMouseOver() || volumeSlider.isMouseButtonDown())
    {
        auto sliderBounds = volumeSlider.getBounds();
        double proportion = (volumeSlider.getValue() - volumeSlider.getMinimum()) / 
                           (volumeSlider.getMaximum() - volumeSlider.getMinimum());
        
        // Calculate thumb position (inverted for vertical slider - top is max)
        int thumbHeight = sliderBounds.getHeight() / 8;  // Small box
        int thumbWidth = sliderBounds.getWidth() / 2;     // Half width
        int thumbX = sliderBounds.getX() + (sliderBounds.getWidth() - thumbWidth) / 2;  // Center horizontally
        int thumbY = sliderBounds.getY() + static_cast<int>((1.0 - proportion) * (sliderBounds.getHeight() - thumbHeight));
        
        juce::Rectangle<int> thumbRect(thumbX, thumbY, thumbWidth, thumbHeight);
        
        g.setColour(juce::Colours::cyan.withAlpha(volumeSlider.isMouseButtonDown() ? 0.5f : 0.3f));
        g.fillRect(thumbRect);
    }
    
    // Draw LEDs at top of device (scaled coordinates)
    float scale = getWidth() / 700.0f;
    int ledSize = static_cast<int>(10 * scale);
    int ledY = static_cast<int>(44 * scale);  // Near top of device
    
    // LED positions aligned with buttons below (approximate x positions)
    // Record LED: flash when loop wraps (like record button overlay), otherwise show normal state
    bool recordLEDFlashing = (recordFlashCounter > 0);
    drawLED(g, static_cast<int>(208 * scale), ledY, ledSize, juce::Colours::green, recordLEDFlashing || recordLED);
    drawLED(g, static_cast<int>(300 * scale), ledY, ledSize, juce::Colours::green, playLED);
    drawLED(g, static_cast<int>(393 * scale), ledY, ledSize, juce::Colours::green, onceLED);
    drawLED(g, static_cast<int>(485 * scale), ledY, ledSize, juce::Colours::green, reverseLED);
    drawLED(g, static_cast<int>(579 * scale), ledY, ledSize, juce::Colours::green, stackLED);
    drawLED(g, static_cast<int>(579 * scale), static_cast<int>(27 * scale), ledSize, juce::Colours::orange, slowLED);  // SLOW LED below stack LED
    
    // Draw gear icon for settings button
    {
        auto gearBounds = settingsButton.getBounds().toFloat();
        float gearRadius = gearBounds.getWidth() * 0.35f;
        float centerX = gearBounds.getCentreX();
        float centerY = gearBounds.getCentreY();
        
        // Gear color - brighter on hover
        float alpha = settingsButton.isMouseOver() ? 0.95f : 0.75f;
        g.setColour(juce::Colours::white.withAlpha(alpha));
        
        // Draw gear teeth using a path
        juce::Path gearPath;
        int numTeeth = 8;
        float toothDepth = gearRadius * 0.3f;
        float innerRadius = gearRadius - toothDepth;
        
        for (int i = 0; i < numTeeth * 2; ++i)
        {
            float angle = (float)i * juce::MathConstants<float>::pi / numTeeth;
            float r = (i % 2 == 0) ? gearRadius : innerRadius;
            float x = centerX + std::cos(angle) * r;
            float y = centerY + std::sin(angle) * r;
            
            if (i == 0)
                gearPath.startNewSubPath(x, y);
            else
                gearPath.lineTo(x, y);
        }
        gearPath.closeSubPath();
        
        // Cut out center hole
        float holeRadius = innerRadius * 0.45f;
        gearPath.addEllipse(centerX - holeRadius, centerY - holeRadius, 
                           holeRadius * 2, holeRadius * 2);
        gearPath.setUsingNonZeroWinding(false);
        
        g.fillPath(gearPath);
    }
}

void BoomerangAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    
    // Calculate scale factor based on current width (base width is 700)
    float scale = getWidth() / 700.0f;
    
    // Helper lambda to scale positions
    auto scaleRect = [scale](int x, int y, int w, int h) {
        return juce::Rectangle<int>(
            static_cast<int>(x * scale),
            static_cast<int>(y * scale),
            static_cast<int>(w * scale),
            static_cast<int>(h * scale)
        );
    };
    
    // Position transparent buttons over foot switches in the background image
    // Base positions for 700x240 window:
    
    // Thru/Mute button (left side, above OUTPUT LEVEL)
    thruMuteButton.setBounds(scaleRect(100, 20, 50, 30));
    
    // Volume slider overlays OUTPUT LEVEL knob on left side
    // Wide clickable area to emulate rolling the volume knob
    volumeSlider.setBounds(scaleRect(70, 60, 120, 90));
    
    // Main foot switches (centered horizontally)
    int baseStartX = 190;
    int baseSpacing = 94;
    int baseButtonY = 155;
    int baseButtonWidth = 30;
    int baseButtonHeight = 23;
    
    recordButton.setBounds(scaleRect(baseStartX, baseButtonY, baseButtonWidth, baseButtonHeight));
    playButton.setBounds(scaleRect(baseStartX + baseSpacing, baseButtonY, baseButtonWidth, baseButtonHeight));
    onceButton.setBounds(scaleRect(baseStartX + baseSpacing * 2, baseButtonY, baseButtonWidth, baseButtonHeight));
    reverseButton.setBounds(scaleRect(baseStartX + baseSpacing * 3, baseButtonY, baseButtonWidth, baseButtonHeight));
    stackButton.setBounds(scaleRect(baseStartX + baseSpacing * 4, baseButtonY, baseButtonWidth, baseButtonHeight));
    
    // Settings button (gear icon) - top right corner of the device image
    int gearSize = static_cast<int>(28 * scale);
    settingsButton.setBounds(
        getWidth() - gearSize - static_cast<int>(6 * scale),
        static_cast<int>(6 * scale),
        gearSize,
        gearSize
    );
    
    // Controls area below the image (base: skip 210px for image, then 30px for controls)
    auto controlsArea = bounds;
    controlsArea.removeFromTop(static_cast<int>(210 * scale));
    controlsArea = controlsArea.removeFromTop(static_cast<int>(30 * scale)).reduced(
        static_cast<int>(20 * scale),
        static_cast<int>(5 * scale)
    );
    
    // Status label - visible only when footer bar is shown
    statusLabel.setBounds(controlsArea);
    statusLabel.setFont(juce::Font(juce::FontOptions(12.0f * scale)));
    statusLabel.setVisible(showFooterBar);
    
    // Version label (bottom right, scaled) - visible only when footer bar is shown
    versionLabel.setBounds(
        getWidth() - static_cast<int>(150 * scale),
        getHeight() - static_cast<int>(20 * scale),
        static_cast<int>(140 * scale),
        static_cast<int>(15 * scale)
    );
    versionLabel.setFont(juce::Font(juce::FontOptions(10.0f * scale)));
    versionLabel.setVisible(showFooterBar);
}

void BoomerangAudioProcessorEditor::timerCallback()
{
    // Note: processAudioThreadRequests() is now called from processBlock (issue #51)
    // to ensure Once mode updates even when UI is closed
    
    updateStatusDisplay();
    
    // Update progress bar
    progressValue = audioProcessor.getLooperEngine()->getLoopProgress();
    
    // Update button toggle states to match engine state
    auto thruState = audioProcessor.getLooperEngine()->getThruMuteState();
    thruMuteButton.setToggleState(thruState == LooperEngine::ThruMuteState::On, juce::dontSendNotification);
    
    auto looperState = audioProcessor.getLooperEngine()->getState();
    bool isRecording = (looperState == LooperEngine::LooperState::Recording || 
                        looperState == LooperEngine::LooperState::Overdubbing);
    recordButton.setToggleState(isRecording, juce::dontSendNotification);
    
    bool isPlaying = (looperState == LooperEngine::LooperState::Playing || 
                      looperState == LooperEngine::LooperState::Overdubbing);
    playButton.setToggleState(isPlaying, juce::dontSendNotification);
    
    auto loopMode = audioProcessor.getLooperEngine()->getLoopMode();
    bool isReverse = (loopMode == LooperEngine::LoopMode::Reverse);
    reverseButton.setToggleState(isReverse, juce::dontSendNotification);
    
    auto onceMode = audioProcessor.getLooperEngine()->getOnceMode();
    bool isOnce = (onceMode == LooperEngine::OnceMode::On);
    onceButton.setToggleState(isOnce, juce::dontSendNotification);
    
    auto thruMuteState = audioProcessor.getLooperEngine()->getThruMuteState();
    bool isThruMuted = (thruMuteState == LooperEngine::ThruMuteState::On);
    thruMuteButton.setToggleState(isThruMuted, juce::dontSendNotification);
    
    // Flash record button when loopCycle parameter is pulsed (issue #51)
    // The Processor now handles detecting loop wraps and pulsing loopCycle
    // We just read the parameter value to sync our visual flash
    if (auto* loopCycleParam = audioProcessor.getAPVTS().getRawParameterValue(ParameterIDs::loopCycle))
    {
        bool loopCycleActive = loopCycleParam->load() >= 0.5f;
        if (loopCycleActive && recordFlashCounter == 0)
        {
            recordFlashCounter = 5;  // Start flash for ~80ms (5 frames at 16ms)
        }
        else if (!loopCycleActive && recordFlashCounter > 0)
        {
            recordFlashCounter--;
        }
    }
    
    // Update LED states
    recordLED = isRecording;
    playLED = isPlaying;
    onceLED = isOnce;
    reverseLED = isReverse;
    
    // Stack LED: on when overdubbing (holding stack while playing)
    stackLED = (looperState == LooperEngine::LooperState::Overdubbing);
    
    // SLOW LED: on when speed mode is slow (half speed)
    auto speedMode = audioProcessor.getLooperEngine()->getSpeedMode();
    slowLED = (speedMode == LooperEngine::SpeedMode::Half);
    
    // Button release flash animation - track state changes
    auto updateButtonFlash = [](juce::TextButton& button, bool& prevDown, int& flashCounter) {
        bool currentlyDown = button.isDown();
        
        if (prevDown && !currentlyDown)  // Just released
            flashCounter = 5;  // Start flash (~80ms)
        else if (flashCounter > 0)
            flashCounter--;
        
        prevDown = currentlyDown;
    };
    
    updateButtonFlash(thruMuteButton, prevThruMuteDown, thruMuteFlashCounter);
    updateButtonFlash(recordButton, prevRecordDown, recordBtnFlashCounter);
    updateButtonFlash(playButton, prevPlayDown, playFlashCounter);
    updateButtonFlash(onceButton, prevOnceDown, onceFlashCounter);
    updateButtonFlash(stackButton, prevStackDown, stackFlashCounter);
    updateButtonFlash(reverseButton, prevReverseDown, reverseFlashCounter);
    
    repaint(); // Refresh overlays and flash indicators
}

//==============================================================================
void BoomerangAudioProcessorEditor::setupButton(juce::TextButton& button, 
                                               const juce::String& text, 
                                               juce::Colour colour,
                                               bool isToggle)
{
    button.setButtonText(text);
    button.setColour(juce::TextButton::buttonColourId, colour.darker(0.8f));
    button.setColour(juce::TextButton::buttonOnColourId, colour);
    button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    button.setClickingTogglesState(isToggle);
    addAndMakeVisible(button);
}

void BoomerangAudioProcessorEditor::drawLED(juce::Graphics& g, int x, int y, int size, juce::Colour colour, bool isLit)
{
    // Draw LED as a circle
    if (isLit)
    {
        // Glowing effect when lit
        g.setColour(colour.withAlpha(0.3f));
        g.fillEllipse(x - size, y - size, size * 2, size * 2);  // Outer glow
        
        g.setColour(colour);
        g.fillEllipse(x - size/2, y - size/2, size, size);  // LED center
    }
    else
    {
        // Dim when off
        g.setColour(colour.withAlpha(0.1f));
        g.fillEllipse(x - size/2, y - size/2, size, size);
    }
}

void BoomerangAudioProcessorEditor::updateStatusDisplay()
{
    juce::String statusText;
    auto state = audioProcessor.getLooperEngine()->getState();
    auto loopMode = audioProcessor.getLooperEngine()->getLoopMode();
    auto once = audioProcessor.getLooperEngine()->getOnceMode();
    auto stack = audioProcessor.getLooperEngine()->getStackMode();
    auto speed = audioProcessor.getLooperEngine()->getSpeedMode();
    auto thru = audioProcessor.getLooperEngine()->getThruMuteState();
    
    switch (state)
    {
        case LooperEngine::LooperState::Stopped:
            statusText = "Stopped";
            break;
        case LooperEngine::LooperState::Recording:
            statusText = "Recording";
            break;
        case LooperEngine::LooperState::Playing:
            statusText = "Playing";
            break;
        case LooperEngine::LooperState::Overdubbing:
            statusText = "Overdubbing";
            break;
        case LooperEngine::LooperState::ContinuousReverse:
            statusText = "Continuous Reverse";
            break;
        case LooperEngine::LooperState::BufferFilled:
            statusText = "Buffer Filled";
            break;
        default:
            break;
    }
    
    // Add mode info
    if (loopMode == LooperEngine::LoopMode::Reverse)
        statusText += " [Reverse]";

    if (once == LooperEngine::OnceMode::On)
        statusText += " [Once]";
    
    if (stack == LooperEngine::StackMode::On)
        statusText += " [Stack]";

    if (speed == LooperEngine::SpeedMode::Half)
        statusText += " [1/2 Speed]";

    if (thru == LooperEngine::ThruMuteState::On)
        statusText += " [Thru Mute]";
    
    statusLabel.setText(statusText, juce::dontSendNotification);
}
void BoomerangAudioProcessorEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    
    menu.addItem(1, "Show Button Overlays", true, showButtonOverlays);
    menu.addItem(2, "Show Footer Bar",      true, showFooterBar);
    
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton),
        [this](int result) {
            switch (result)
            {
                case 1:
                    showButtonOverlays = !showButtonOverlays;
                    repaint();
                    break;
                case 2:
                {
                    showFooterBar = !showFooterBar;
                    
                    // Update aspect ratio constraint and resize window
                    int currentWidth = getWidth();
                    if (showFooterBar)
                    {
                        // Footer visible: 700x240 aspect ratio
                        getConstrainer()->setFixedAspectRatio(700.0 / 240.0);
                        setResizeLimits(350, 120, 1400, 480);
                        int newHeight = static_cast<int>(currentWidth * (240.0 / 700.0));
                        setSize(currentWidth, newHeight);
                    }
                    else
                    {
                        // Footer hidden: 700x200 aspect ratio (image only)
                        getConstrainer()->setFixedAspectRatio(700.0 / 200.0);
                        setResizeLimits(350, 100, 1400, 400);
                        int newHeight = static_cast<int>(currentWidth * (200.0 / 700.0));
                        setSize(currentWidth, newHeight);
                    }
                    break;
                }
                default:
                    break;
            }
        });
}