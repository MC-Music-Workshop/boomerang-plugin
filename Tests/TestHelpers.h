#pragma once

#include "PluginProcessor.h"
#include <functional>

//==============================================================================
// Shared helpers for the known-input / known-output audio tests.
//
// Test signals are ramps of n/1024: every value, and every sum, half or average
// of two values, is exactly representable as a float. That lets the copy and
// passthrough paths be compared bit-for-bit (tolerance 0).
namespace test
{
using Buffer = juce::AudioBuffer<float>;

// Fixed -2.5 dB applied to the existing loop on each stack pass (LooperEngine.cpp)
constexpr float stack_gain = 0.74989420933f;

// Wherever the stack gain is involved, arm64 may fuse the multiply-add and
// differ from x86_64 in the last bit
constexpr float gain_tolerance = 1.0e-5f;

inline bool envFlag(const char* name)
{
  return juce::SystemStats::getEnvironmentVariable(name, "") == "1";
}

//==============================================================================
// Signal builders

// Sample n is (first + n + 1) / 1024 on even channels and the negative on odd
// channels, so a swapped or duplicated channel shows up as a mismatch
inline Buffer ramp(int num_samples, int first = 0, int num_channels = 2)
{
  Buffer buffer(num_channels, num_samples);
  for (int channel = 0; channel < num_channels; ++channel)
    for (int n = 0; n < num_samples; ++n)
    {
      const float value = static_cast<float>(first + n + 1) / 1024.0f;
      buffer.setSample(channel, n, (channel % 2 == 0) ? value : -value);
    }
  return buffer;
}

inline Buffer silence(int num_samples, int num_channels = 2)
{
  Buffer buffer(num_channels, num_samples);
  buffer.clear();
  return buffer;
}

inline Buffer concat(const Buffer& a, const Buffer& b)
{
  Buffer out(a.getNumChannels(), a.getNumSamples() + b.getNumSamples());
  for (int channel = 0; channel < out.getNumChannels(); ++channel)
  {
    out.copyFrom(channel, 0, a, channel, 0, a.getNumSamples());
    out.copyFrom(channel, a.getNumSamples(), b, channel, 0, b.getNumSamples());
  }
  return out;
}

inline Buffer slice(const Buffer& source, int start, int num_samples)
{
  Buffer out(source.getNumChannels(), num_samples);
  for (int channel = 0; channel < out.getNumChannels(); ++channel)
    out.copyFrom(channel, 0, source, channel, start, num_samples);
  return out;
}

inline Buffer reversed(const Buffer& source)
{
  Buffer out;
  out.makeCopyOf(source);
  out.reverse(0, out.getNumSamples());
  return out;
}

// num_samples of `loop` played round and round, starting at loop sample `start`
inline Buffer cycle(const Buffer& loop, int num_samples, int start = 0)
{
  const int length = loop.getNumSamples();
  Buffer out(loop.getNumChannels(), num_samples);
  for (int channel = 0; channel < out.getNumChannels(); ++channel)
    for (int n = 0; n < num_samples; ++n)
      out.setSample(channel, n, loop.getSample(channel, (start + n) % length));
  return out;
}

// What continuous reverse puts out while `input` is played over a loop recorded
// from `take`: the take backwards, then each loop-length stretch of the input
// backwards, one pass late
inline Buffer reversedPasses(const Buffer& take, const Buffer& input)
{
  const int length = take.getNumSamples();
  const Buffer recorded = concat(take, input);
  Buffer out(input.getNumChannels(), input.getNumSamples());
  for (int channel = 0; channel < out.getNumChannels(); ++channel)
    for (int n = 0; n < out.getNumSamples(); ++n)
    {
      const int pass = n / length;
      out.setSample(channel, n, recorded.getSample(channel, pass * length + (length - 1 - n % length)));
    }
  return out;
}

// a * gain_a + b * gain_b, sample by sample
inline Buffer mix(const Buffer& a, float gain_a, const Buffer& b, float gain_b)
{
  Buffer out(a.getNumChannels(), a.getNumSamples());
  for (int channel = 0; channel < out.getNumChannels(); ++channel)
    for (int n = 0; n < out.getNumSamples(); ++n)
      out.setSample(channel, n, a.getSample(channel, n) * gain_a + b.getSample(channel, n) * gain_b);
  return out;
}

// Empty when the buffers match, otherwise a description of the first difference
inline juce::String firstMismatch(const Buffer& actual, const Buffer& expected, float tolerance = 0.0f)
{
  if (actual.getNumChannels() != expected.getNumChannels() || actual.getNumSamples() != expected.getNumSamples())
    return "size differs: got " + juce::String(actual.getNumChannels()) + "x" + juce::String(actual.getNumSamples())
         + ", expected " + juce::String(expected.getNumChannels()) + "x" + juce::String(expected.getNumSamples());

  for (int channel = 0; channel < actual.getNumChannels(); ++channel)
    for (int n = 0; n < actual.getNumSamples(); ++n)
    {
      const float got  = actual.getSample(channel, n);
      const float want = expected.getSample(channel, n);
      if (! (std::abs(got - want) <= tolerance))
        return "channel " + juce::String(channel) + ", sample " + juce::String(n)
             + ": got " + juce::String(got, 6) + ", expected " + juce::String(want, 6);
    }

  return {};
}

//==============================================================================
// Runs every message already queued on the message thread (the engine's state
// echoes to the host parameters are delivered that way), then returns.
// Tests run on a worker thread, so this never blocks the queue it waits for.
inline void flushMessageQueue()
{
  juce::WaitableEvent done;
  if (juce::MessageManager::callAsync([&done] { done.signal(); }))
    done.wait(5000);
}

//==============================================================================
// Drives a LooperEngine the way the processor does: audio in blocks, button
// presses only between blocks, processAudioThreadRequests() after each block.
// 1 kHz keeps the 4-minute loop buffers small (240,000 samples).
struct EngineDriver
{
  explicit EngineDriver(int block_size_to_use, int num_channels = 2, double sample_rate = 1000.0)
    : block_size(block_size_to_use)
  {
    engine.prepare(sample_rate, block_size, num_channels);
  }

  // Feeds `input` through the engine and returns everything it put out
  Buffer run(const Buffer& input)
  {
    Buffer output(input.getNumChannels(), input.getNumSamples());

    for (int start = 0; start < input.getNumSamples(); start += block_size)
    {
      const int num = juce::jmin(block_size, input.getNumSamples() - start);
      Buffer block = slice(input, start, num);

      engine.processBlock(block);
      engine.processAudioThreadRequests();

      for (int channel = 0; channel < output.getNumChannels(); ++channel)
        output.copyFrom(channel, start, block, channel, 0, num);
    }

    return output;
  }

  // RECORD, feed `loop`, RECORD: leaves the engine playing `loop` from its start
  Buffer recordLoop(const Buffer& loop)
  {
    engine.onRecordButtonPressed();
    Buffer output = run(loop);
    engine.onRecordButtonPressed();
    return output;
  }

  LooperEngine engine;
  int block_size;
};

//==============================================================================
// Drives the whole plugin processor through its host parameters, as a host or
// a MIDI pedal mapped by the host would.
struct ProcessorDriver
{
  explicit ProcessorDriver(int block_size_to_use, bool mono_input = false)
    : block_size(block_size_to_use)
  {
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(mono_input ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    processor.setBusesLayout(layout);
    processor.setRateAndBufferSizeDetails(1000.0, block_size);
    processor.prepareToPlay(1000.0, block_size);
  }

  ~ProcessorDriver()
  {
    // Queued echoes hold a pointer to the processor: deliver them before it goes
    flushMessageQueue();
    processor.releaseResources();
    flushMessageQueue();
  }

  // Sets a host parameter to a real-world value (0/1 for the buttons), then
  // lets any resulting state echoes land
  void set(const juce::String& parameter_id, float value)
  {
    auto* parameter = processor.getAPVTS().getParameter(parameter_id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    flushMessageQueue();
  }

  // A momentary pedal: 1 on press, 0 on release
  void tap(const juce::String& parameter_id)
  {
    set(parameter_id, 1.0f);
    set(parameter_id, 0.0f);
  }

  float get(const juce::String& parameter_id)
  {
    return processor.getAPVTS().getRawParameterValue(parameter_id)->load();
  }

  // `input` always has two channels, like the host buffer of a stereo-out plugin
  Buffer run(const Buffer& input)
  {
    Buffer output(input.getNumChannels(), input.getNumSamples());
    juce::MidiBuffer midi;

    for (int start = 0; start < input.getNumSamples(); start += block_size)
    {
      const int num = juce::jmin(block_size, input.getNumSamples() - start);
      Buffer block = slice(input, start, num);

      processor.processBlock(block, midi);

      for (int channel = 0; channel < output.getNumChannels(); ++channel)
        output.copyFrom(channel, start, block, channel, 0, num);
    }

    flushMessageQueue();
    return output;
  }

  LooperEngine& engine() { return *processor.getLooperEngine(); }

  BoomerangAudioProcessor processor;
  int block_size;
};

//==============================================================================
class BoomerangTest : public juce::UnitTest
{
public:
  using juce::UnitTest::UnitTest;

protected:
  // Runs a scenario at several host buffer sizes: the audio must not depend on it
  void forEachBlockSize(const std::function<void(int)>& scenario)
  {
    for (int size : { 1, 64, 511 })
    {
      current_block_size = size;
      scenario(size);
    }
    current_block_size = 0;
  }

  void check(bool condition, const juce::String& what)
  {
    expect(condition, what + blockSizeNote());
  }

  void checkBuffers(const Buffer& actual, const Buffer& expected, const juce::String& what, float tolerance = 0.0f)
  {
    const auto mismatch = firstMismatch(actual, expected, tolerance);
    expect(mismatch.isEmpty(), what + blockSizeNote() + " - " + mismatch);
  }

  // A test for behaviour that is believed broken. juce::UnitTest has no "skip",
  // so by default this only logs. Set BOOMERANG_RUN_KNOWN_BUGS=1 to run the
  // assertions and see which of them really fail.
  void knownBug(const juce::String& description, const std::function<void()>& body)
  {
    if (envFlag("BOOMERANG_RUN_KNOWN_BUGS"))
    {
      beginTest("KNOWN BUG: " + description);
      body();
    }
    else
    {
      logMessage("SKIPPED (known bug): " + description);
    }
  }

private:
  juce::String blockSizeNote() const
  {
    return current_block_size > 0 ? " [block size " + juce::String(current_block_size) + "]" : juce::String();
  }

  int current_block_size = 0;
};
} // namespace test
