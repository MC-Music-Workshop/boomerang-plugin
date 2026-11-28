#include "TestHelpers.h"

using namespace test;
using State = LooperEngine::LooperState;

namespace
{
// Both channels carry channel `source_channel` of `source`
Buffer bothChannelsFrom(const Buffer& source, int source_channel)
{
  Buffer out(2, source.getNumSamples());
  out.copyFrom(0, 0, source, source_channel, 0, source.getNumSamples());
  out.copyFrom(1, 0, source, source_channel, 0, source.getNumSamples());
  return out;
}

// Left and right are unrelated ramps, so each input routing gives a distinct result
Buffer stereoInput(int num_samples)
{
  Buffer out(2, num_samples);
  out.copyFrom(0, 0, ramp(num_samples, 0), 0, 0, num_samples);
  out.copyFrom(1, 0, ramp(num_samples, 4096), 0, 0, num_samples);
  return out;
}
}

//==============================================================================
// The plugin processor driven through its host parameters: input routing,
// channel layouts, and the transport as a host or mapped MIDI pedal sees it.
class ProcessorTests : public BoomerangTest
{
public:
  ProcessorTests() : BoomerangTest("Processor", "Boomerang") {}

  void runTest() override
  {
    const Buffer loop_a = ramp(300);
    const Buffer live   = ramp(600, 5000);

    //==========================================================================
    beginTest("Input source routes the stereo input pair");
    forEachBlockSize([&](int block_size)
    {
      const Buffer input = stereoInput(300);
      Buffer mono_sum    = mix(bothChannelsFrom(input, 0), 0.5f, bothChannelsFrom(input, 1), 0.5f);

      ProcessorDriver d(block_size);
      checkBuffers(d.run(input), input, "Stereo passes left and right through");

      d.set(ParameterIDs::inputSource, InputSource::left);
      checkBuffers(d.run(input), bothChannelsFrom(input, 0), "Left only puts left on both sides");

      d.set(ParameterIDs::inputSource, InputSource::right);
      checkBuffers(d.run(input), bothChannelsFrom(input, 1), "Right only puts right on both sides");

      d.set(ParameterIDs::inputSource, InputSource::mono);
      checkBuffers(d.run(input), mono_sum, "Mono puts the average on both sides");
    });

    //==========================================================================
    beginTest("Input source applies to what gets recorded");
    forEachBlockSize([&](int block_size)
    {
      const Buffer input = stereoInput(300);

      ProcessorDriver d(block_size);
      d.set(ParameterIDs::inputSource, InputSource::right);
      d.tap(ParameterIDs::record);
      d.run(input);
      d.tap(ParameterIDs::record);
      checkBuffers(d.run(silence(450)), cycle(bothChannelsFrom(input, 1), 450), "loop holds the right input on both sides");
    });

    //==========================================================================
    beginTest("Mono input, stereo output: both sides carry the input and the loop");
    forEachBlockSize([&](int block_size)
    {
      // Channel 1 of the host buffer is not an input here; whatever it holds must be ignored
      ProcessorDriver d(block_size, true);
      checkBuffers(d.run(loop_a), bothChannelsFrom(loop_a, 0), "dry signal on both sides");

      d.tap(ParameterIDs::record);
      d.run(loop_a);
      d.tap(ParameterIDs::record);
      checkBuffers(d.run(silence(450)), cycle(bothChannelsFrom(loop_a, 0), 450), "loop on both sides");
    });

    //==========================================================================
    beginTest("Transport parameters drive the looper");
    forEachBlockSize([&](int block_size)
    {
      ProcessorDriver d(block_size);

      d.tap(ParameterIDs::record);
      check(d.engine().getState() == State::Recording, "a RECORD tap starts recording");
      d.run(loop_a);
      d.tap(ParameterIDs::record);
      check(d.engine().getState() == State::Playing, "a second RECORD tap starts playback");
      checkBuffers(d.run(silence(600)), cycle(loop_a, 600), "recorded loop plays");

      d.set(ParameterIDs::stack, 1.0f);
      check(d.engine().getState() == State::Overdubbing, "stack on starts stacking");
      d.set(ParameterIDs::stack, 0.0f);
      check(d.engine().getState() == State::Playing, "stack off ends stacking");

      d.set(ParameterIDs::thruMute, 1.0f);
      checkBuffers(d.run(live), cycle(loop_a, 600), "thruMute leaves the loop only");
      d.set(ParameterIDs::thruMute, 0.0f);

      d.set(ParameterIDs::reverse, 1.0f);
      check(d.engine().getLoopMode() == LooperEngine::LoopMode::Reverse, "reverse on");
      d.set(ParameterIDs::reverse, 0.0f);
      check(d.engine().getLoopMode() == LooperEngine::LoopMode::Normal, "reverse off");

      d.tap(ParameterIDs::slow);
      check(d.engine().getSpeedMode() == LooperEngine::SpeedMode::Half, "slow toggles half speed");
      d.tap(ParameterIDs::slow);
      check(d.engine().getSpeedMode() == LooperEngine::SpeedMode::Normal, "slow toggles back");

      d.tap(ParameterIDs::once);
      check(d.engine().getOnceMode() == LooperEngine::OnceMode::On, "once arms Once");
      d.tap(ParameterIDs::onceOff);
      check(d.engine().getOnceMode() == LooperEngine::OnceMode::Off, "onceOff cancels it");
    });

    //==========================================================================
    beginTest("Volume, feedback and stack latch parameters reach the engine");
    forEachBlockSize([&](int block_size)
    {
      const Buffer overdub = slice(live, 0, 300);

      ProcessorDriver d(block_size);
      d.set(ParameterIDs::volume, 0.5f);
      d.set(ParameterIDs::feedback, 1.0f);
      d.set(ParameterIDs::stackLatch, 1.0f);
      d.tap(ParameterIDs::record);
      d.run(loop_a);
      d.tap(ParameterIDs::record);
      checkBuffers(d.run(silence(300)), mix(loop_a, 0.5f, loop_a, 0.0f), "volume halves the loop");

      d.tap(ParameterIDs::stack);
      check(d.engine().getState() == State::Overdubbing, "latched stack survives the release");
      d.run(overdub);
      d.tap(ParameterIDs::stack);
      check(d.engine().getState() == State::Playing, "second tap ends the stack");

      d.set(ParameterIDs::volume, 1.0f);
      checkBuffers(d.run(silence(300)), mix(loop_a, stack_gain, overdub, 1.0f), "feedback 1 stacks the input at full level", gain_tolerance);
    });

    //==========================================================================
    beginTest("Saved state restores settings but not transient modes");
    {
      juce::MemoryBlock saved;
      {
        ProcessorDriver d(64);
        d.set(ParameterIDs::volume, 0.25f);
        d.set(ParameterIDs::inputSource, InputSource::mono);
        d.tap(ParameterIDs::slow);
        d.processor.getStateInformation(saved);
      }

      ProcessorDriver restored(64);
      restored.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
      flushMessageQueue();

      check(std::abs(restored.get(ParameterIDs::volume) - 0.25f) < 1.0e-6f, "volume restored");
      check(static_cast<int>(restored.get(ParameterIDs::inputSource)) == InputSource::mono, "input source restored");
      check(restored.engine().getSpeedMode() == LooperEngine::SpeedMode::Normal, "half speed is not restored");
      check(restored.engine().getState() == State::Stopped, "transport is stopped");
    }
  }
};

static ProcessorTests processorTests;

//==============================================================================
// RECORD and PLAY through their host parameters, the way a momentary MIDI pedal
// mapped by the host drives them: 1 on press, 0 on release.
//
// Input parameters (`record`, `play`, ...) only carry presses; the plugin never
// echoes to them. State goes out on the read-only *State outputs (`recordState`,
// `playState`, ...), which a pedal LED follows through its own CC. An echo to an
// input left it at 1, so the pedal's next press changed nothing and was lost.
class RerecordTests : public BoomerangTest
{
public:
  RerecordTests() : BoomerangTest("Re-record through the host", "Boomerang") {}

  void runTest() override
  {
    const Buffer loop_a = ramp(300);
    const Buffer loop_b = ramp(120, 1000);

    //==========================================================================
    beginTest("Re-recording over a playing loop plays the new take");
    forEachBlockSize([&](int block_size)
    {
      ProcessorDriver d(block_size);
      d.tap(ParameterIDs::record);
      d.run(loop_a);
      d.tap(ParameterIDs::record);
      d.run(silence(450));

      d.tap(ParameterIDs::record);
      check(d.engine().getState() == State::Recording, "third tap starts the new take");
      d.run(loop_b);
      d.tap(ParameterIDs::record);
      check(d.engine().getState() == State::Playing, "fourth tap plays it");
      checkBuffers(d.run(silence(500)), cycle(loop_b, 500), "new take plays");
    });

    //==========================================================================
    beginTest("Re-recording after PLAY ended the first take");
    forEachBlockSize([&](int block_size)
    {
      ProcessorDriver d(block_size);
      d.tap(ParameterIDs::record);
      d.run(loop_a);
      d.tap(ParameterIDs::play);
      check(d.engine().getState() == State::Stopped, "PLAY ends the take");

      d.tap(ParameterIDs::record);
      check(d.engine().getState() == State::Recording, "next RECORD tap starts a new take");
      d.run(loop_b);
      d.tap(ParameterIDs::record);
      checkBuffers(d.run(silence(500)), cycle(loop_b, 500), "new take plays");
    });

    //==========================================================================
    beginTest("One PLAY tap stops a loop that RECORD started playing");
    forEachBlockSize([&](int block_size)
    {
      ProcessorDriver d(block_size);
      d.tap(ParameterIDs::record);
      d.run(loop_a);
      d.tap(ParameterIDs::record);
      check(d.engine().getState() == State::Playing, "loop is playing");

      d.tap(ParameterIDs::play);
      check(d.engine().getState() == State::Stopped, "one PLAY tap stops it");
    });

    //==========================================================================
    beginTest("RECORD acts on the press: releasing the pedal keeps recording");
    forEachBlockSize([&](int block_size)
    {
      ProcessorDriver d(block_size);
      d.set(ParameterIDs::record, 1.0f);
      d.run(slice(loop_a, 0, 32));   // the foot comes off a moment into the take
      d.set(ParameterIDs::record, 0.0f);
      check(d.engine().getState() == State::Recording, "still recording after the pedal is released");
    });

    //==========================================================================
    beginTest("State goes out on the *State outputs; the inputs are never echoed");
    forEachBlockSize([&](int block_size)
    {
      ProcessorDriver d(block_size);
      auto inputsAtRest = [&] { return d.get(ParameterIDs::record) < 0.5f && d.get(ParameterIDs::play) < 0.5f; };

      d.tap(ParameterIDs::record);
      check(d.get(ParameterIDs::recordState) > 0.5f, "recordState on while recording");
      check(d.get(ParameterIDs::playState) < 0.5f, "playState off while recording");
      d.run(loop_a);

      d.tap(ParameterIDs::record);
      check(d.get(ParameterIDs::recordState) < 0.5f, "recordState off once the take ends");
      check(d.get(ParameterIDs::playState) > 0.5f, "playState on while playing");
      check(inputsAtRest(), "record and play inputs stay at rest while playing");

      d.tap(ParameterIDs::play);
      check(d.get(ParameterIDs::playState) < 0.5f, "playState off when stopped");
      check(inputsAtRest(), "record and play inputs stay at rest when stopped");
    });

    //==========================================================================
    beginTest("A press that lands while the plugin echoes state to the host is not lost");
    {
      // Holds the message thread inside the echo of `recordState` to the
      // host, while a pedal press arrives from another thread
      struct EchoGate : juce::AudioProcessorParameter::Listener
      {
        void parameterValueChanged(int, float) override
        {
          if (armed.exchange(false))
          {
            inside.signal();
            release.wait(5000);
          }
        }
        void parameterGestureChanged(int, bool) override {}

        std::atomic<bool> armed { false };
        juce::WaitableEvent inside, release;
      };

      ProcessorDriver d(64);
      EchoGate gate;
      auto* record_state = d.processor.getAPVTS().getParameter(ParameterIDs::recordState);
      auto* play         = d.processor.getAPVTS().getParameter(ParameterIDs::play);
      auto* slow         = d.processor.getAPVTS().getParameter(ParameterIDs::slow);
      record_state->addListener(&gate);

      d.tap(ParameterIDs::record);
      d.run(loop_a);

      // PLAY ends the take; the engine queues an echo of recordState = off
      gate.armed = true;
      play->setValueNotifyingHost(1.0f);
      const bool echo_reached = gate.inside.wait(5000);
      slow->setValueNotifyingHost(1.0f);   // a pedal press during the echo
      gate.release.signal();
      flushMessageQueue();
      record_state->removeListener(&gate);

      check(echo_reached, "test setup: the echo was intercepted");
      check(d.engine().getSpeedMode() == LooperEngine::SpeedMode::Half, "the press during the echo took effect");
    }
  }
};

static RerecordTests rerecordTests;
