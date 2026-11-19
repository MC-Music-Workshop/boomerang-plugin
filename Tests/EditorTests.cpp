#include "TestHelpers.h"
#include "PluginEditor.h"

using namespace test;
using State = LooperEngine::LooperState;

namespace
{
// Components may only be touched on the message thread; the tests run on a worker thread
void onMessageThread(const std::function<void()>& fn)
{
  juce::WaitableEvent done;
  juce::MessageManager::callAsync([&] { fn(); done.signal(); });
  done.wait();
}

// The skins' footswitch and LED order
enum Control { record, play, once, reverse, stack };
using Leds = std::array<bool, 5>;

juce::String describe(const Leds& leds)
{
  const char* names[] = { "RECORD", "PLAY", "ONCE", "REVERSE", "STACK" };
  juce::StringArray lit;
  for (size_t i = 0; i < leds.size(); ++i)
    if (leds[i])
      lit.add(names[i]);
  return lit.isEmpty() ? "none lit" : lit.joinIntoString(" + ") + " lit";
}

//==============================================================================
// A desktop window that exists only in memory. JUCE treats it as a real window,
// so mouse events fed to it run through JUCE's whole mouse handling (hit testing,
// the mouse source's button state, Button's press and release logic) as a real
// click does. Nothing reaches the screen, the real pointer never moves, and no
// other app can be clicked by mistake. Nothing is drawn between events either;
// the footswitches are invisible, so that matches the real window for them.
class VirtualWindow : public juce::Component
{
public:
  explicit VirtualWindow(juce::Component& content)
  {
    addAndMakeVisible(content);
    // Far from anywhere the real pointer can be: JUCE takes hover from it
    setBounds(content.getBounds().withPosition(-10000, -10000));
    addToDesktop(0);
    setVisible(true);
  }

  // Sends a mouse event at `position` (window coordinates) with these buttons held
  void mouse(juce::Point<float> position, juce::ModifierKeys buttons)
  {
    juce::ModifierKeys::currentModifiers = buttons;  // as the native event code does
    getPeer()->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, position, buttons,
                                juce::MouseInputSource::defaultPressure,
                                juce::MouseInputSource::defaultOrientation,
                                juce::Time::currentTimeMillis());
  }

private:
  struct Peer : public juce::ComponentPeer
  {
    using ComponentPeer::ComponentPeer;
    using ComponentPeer::localToGlobal;
    using ComponentPeer::globalToLocal;

    void* getNativeHandle() const override                                { return nullptr; }
    void setVisible(bool) override                                        {}
    void setTitle(const juce::String&) override                           {}
    void setBounds(const juce::Rectangle<int>& new_bounds, bool) override { bounds = new_bounds; }
    juce::Rectangle<int> getBounds() const override                       { return bounds; }
    juce::Point<float> localToGlobal(juce::Point<float> p) override       { return p + bounds.getPosition().toFloat(); }
    juce::Point<float> globalToLocal(juce::Point<float> p) override       { return p - bounds.getPosition().toFloat(); }
    void setMinimised(bool) override                                      {}
    bool isMinimised() const override                                     { return false; }
    bool isShowing() const override                                       { return true; }
    void setFullScreen(bool) override                                     {}
    bool isFullScreen() const override                                    { return false; }
    void setIcon(const juce::Image&) override                             {}
    bool contains(juce::Point<int> p, bool) const override                { return bounds.withZeroOrigin().contains(p); }
    OptionalBorderSize getFrameSizeIfPresent() const override             { return {}; }
    juce::BorderSize<int> getFrameSize() const override                   { return {}; }
    bool setAlwaysOnTop(bool) override                                    { return true; }
    void toFront(bool) override                                           {}
    void toBehind(ComponentPeer*) override                                {}
    bool isFocused() const override                                       { return false; }
    void grabFocus() override                                             {}
    void repaint(const juce::Rectangle<int>&) override                    {}
    void performAnyPendingRepaintsNow() override                          {}
    void setAlpha(float) override                                         {}
    juce::StringArray getAvailableRenderingEngines() override             { return {}; }
    void textInputRequired(juce::Point<int>, juce::TextInputTarget&) override {}

    juce::Rectangle<int> bounds;
  };

  juce::ComponentPeer* createNewPeer(int style_flags, void*) override { return new Peer(*this, style_flags); }
};

//==============================================================================
// The plugin window in a VirtualWindow, clicked with the mouse. The LEDs are
// read from a picture of the window.
struct EditorDriver
{
  EditorDriver(int block_size, int skin_index)
    : driver(block_size), skin(Skins::get(skin_index))
  {
    onMessageThread([&]
    {
      driver.processor.getAPVTS().state.setProperty(StateIDs::skin, skin_index, nullptr);
      editor.reset(dynamic_cast<BoomerangAudioProcessorEditor*>(driver.processor.createEditorAndMakeActive()));
      window = std::make_unique<VirtualWindow>(*editor);
    });
  }

  ~EditorDriver()
  {
    // Close the window as a host does, before the processor goes
    onMessageThread([&]
    {
      window.reset();
      driver.processor.editorBeingDeleted(editor.get());
      editor.reset();
    });
  }

  // Presses and releases the mouse in the middle of a footswitch
  void click(Control control)
  {
    juce::Point<float> centre;
    onMessageThread([&]
    {
      centre = (skin.footswitchRects[control].toFloat() * scale()).getCentre();
      window->mouse(centre, {});                                      // the pointer arrives
      window->mouse(centre, juce::ModifierKeys::leftButtonModifier);  // press
    });
    onMessageThread([&] { window->mouse(centre, {}); });              // release, as a separate event
    flushMessageQueue();
  }

  // Which LEDs are drawn lit: a lit LED's centre is drawn in its skin colour
  Leds litLeds()
  {
    Leds lit {};
    onMessageThread([&]
    {
      editor->timerCallback();  // sync the LEDs with the engine now, not on the next tick
      const auto picture = editor->createComponentSnapshot(editor->getLocalBounds());
      for (size_t i = 0; i < lit.size(); ++i)
      {
        const auto centre = (skin.ledPositions[i].toFloat() * scale()).toInt();
        const auto pixel  = picture.getPixelAt(centre.x, centre.y);
        const auto colour = skin.ledColours[i];
        lit[i] = std::abs(pixel.getRed()   - colour.getRed())   <= 2
              && std::abs(pixel.getGreen() - colour.getGreen()) <= 2
              && std::abs(pixel.getBlue()  - colour.getBlue())  <= 2;
      }
    });
    return lit;
  }

  ProcessorDriver driver;
  const Skin& skin;
  std::unique_ptr<BoomerangAudioProcessorEditor> editor;
  std::unique_ptr<VirtualWindow> window;

private:
  // The editor lays the skin out in 700-wide base coordinates
  float scale() const { return static_cast<float>(editor->getWidth()) / 700.0f; }
};
}

//==============================================================================
// The plugin window, driven by mouse clicks. Covers what the audio tests can't:
// that a click on a footswitch reaches the looper, and what the LEDs show.
class EditorTests : public BoomerangTest
{
public:
  EditorTests() : BoomerangTest("Editor", "Boomerang") {}

  void runTest() override
  {
    const Buffer take = ramp(300);

    //==========================================================================
    beginTest("Continuous reverse: REVERSE, RECORD, RECORD lights RECORD, PLAY and REVERSE");
    for (int skin_index = 0; skin_index < Skins::count(); ++skin_index)
    {
      EditorDriver e(64, skin_index);
      const juce::String on_skin = " (" + Skins::get(skin_index).name + " skin)";

      e.click(reverse);
      e.click(record);
      e.driver.run(take);
      checkLeds(e.litLeds(), { true, false, false, true, false }, "recording" + on_skin);

      e.click(record);
      check(e.driver.engine().getState() == State::ContinuousReverse, "in continuous reverse" + on_skin);
      checkLeds(e.litLeds(), { true, true, false, true, false }, "continuous reverse" + on_skin);

      e.click(play);
      checkLeds(e.litLeds(), { false, false, false, true, false }, "stopped" + on_skin);
    }

    //==========================================================================
    // After a release JUCE flashes a never-painted button down again for 100 ms;
    // the footswitches are invisible, so every click used to press STACK twice
    beginTest("Stack latch: one click latches the stack, the release flash doesn't unlatch it (issue #71)");
    {
      EditorDriver e(64, 0);
      e.driver.set(ParameterIDs::stackLatch, 1.0f);
      e.click(record);
      e.driver.run(take);
      e.click(record);

      e.click(stack);
      juce::Thread::sleep(300);  // past the flash
      flushMessageQueue();
      check(e.driver.engine().getState() == State::Overdubbing, "still stacking after the flash");

      e.click(stack);
      check(e.driver.engine().getState() == State::Playing, "the next click ends the stack");
    }
  }

private:
  void checkLeds(const Leds& actual, const Leds& expected, const juce::String& what)
  {
    expect(actual == expected, what + ": expected " + describe(expected) + ", got " + describe(actual));
  }
};

static EditorTests editorTests;
