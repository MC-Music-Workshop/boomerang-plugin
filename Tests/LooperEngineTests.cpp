#include "TestHelpers.h"
#include <atomic>
#include <thread>

using namespace test;
using State = LooperEngine::LooperState;

//==============================================================================
// Known-input / known-output tests of the looper engine on its own.
class LooperEngineTests : public BoomerangTest
{
public:
  LooperEngineTests() : BoomerangTest("LooperEngine", "Boomerang") {}

  void runTest() override
  {
    const Buffer loop_a = ramp(300);         // first take
    const Buffer loop_b = ramp(120, 1000);   // a second, shorter take
    const Buffer live   = ramp(900, 5000);   // what the player is playing right now

    //==========================================================================
    beginTest("Stopped: input passes through; THRU MUTE silences it");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      checkBuffers(d.run(live), live, "dry signal when stopped");

      d.engine.onThruMuteButtonPressed();
      checkBuffers(d.run(live), silence(900), "muted when stopped");
    });

    //==========================================================================
    beginTest("Recording: input still passes through; THRU MUTE silences it");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.onRecordButtonPressed();
      check(d.engine.getState() == State::Recording, "RECORD starts recording");
      checkBuffers(d.run(loop_a), loop_a, "dry signal while recording");

      EngineDriver muted(block_size);
      muted.engine.onThruMuteButtonPressed();
      muted.engine.onRecordButtonPressed();
      checkBuffers(muted.run(loop_a), silence(300), "muted while recording");
      muted.engine.onRecordButtonPressed();
      checkBuffers(muted.run(silence(300)), loop_a, "the muted take was still recorded");
    });

    //==========================================================================
    beginTest("Record then RECORD again: the take loops from its start");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      check(d.engine.getState() == State::Playing, "second RECORD starts playback");
      checkBuffers(d.run(silence(750)), cycle(loop_a, 750), "loop repeats exactly");
    });

    //==========================================================================
    beginTest("Loop wrap is flagged once per pass");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      check(! d.engine.checkAndClearLoopWrapped(), "no wrap before playback");
      d.run(silence(299));
      check(! d.engine.checkAndClearLoopWrapped(), "no wrap one sample before the end");
      d.run(silence(1));
      check(d.engine.checkAndClearLoopWrapped(), "wrap at the loop end");
      check(! d.engine.checkAndClearLoopWrapped(), "flag clears once read");
    });

    //==========================================================================
    beginTest("PLAY during recording keeps the take and stops; PLAY replays it");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.onRecordButtonPressed();
      d.run(loop_a);
      d.engine.onPlayButtonPressed();
      check(d.engine.getState() == State::Stopped, "PLAY ends recording into stopped");
      checkBuffers(d.run(live), live, "only dry signal while stopped");

      d.engine.onPlayButtonPressed();
      check(d.engine.getState() == State::Playing, "PLAY starts the loop");
      d.run(silence(100));
      d.engine.onPlayButtonPressed();
      check(d.engine.getState() == State::Stopped, "PLAY stops the loop");
      d.engine.onPlayButtonPressed();
      checkBuffers(d.run(silence(450)), cycle(loop_a, 450), "playback restarts from the loop start");
    });

    //==========================================================================
    beginTest("Playing: loop is mixed with the live input; THRU MUTE leaves the loop only");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      checkBuffers(d.run(live), mix(cycle(loop_a, 900), 1.0f, live, 1.0f), "loop + live");

      d.engine.onThruMuteButtonPressed();
      checkBuffers(d.run(live), cycle(loop_a, 900), "loop only when muted");
    });

    //==========================================================================
    beginTest("Volume scales the loop, never the live input");
    forEachBlockSize([&](int block_size)
    {
      for (float volume : { 0.0f, 0.5f, 1.0f })
      {
        EngineDriver d(block_size);
        d.engine.setVolume(volume);
        d.recordLoop(loop_a);
        checkBuffers(d.run(live), mix(cycle(loop_a, 900), volume, live, 1.0f), "volume " + juce::String(volume));
      }
    });

    //==========================================================================
    beginTest("RECORD while playing replaces the loop with the new take");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.run(silence(450));
      d.recordLoop(loop_b);
      check(d.engine.getState() == State::Playing, "playing after the second take");
      checkBuffers(d.run(silence(500)), cycle(loop_b, 500), "new take plays at its own length");
    });

    //==========================================================================
    beginTest("Reverse: a forward take plays backwards from its end");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.engine.onPlayButtonPressed();
      d.engine.onReverseButtonPressed();
      check(d.engine.getLoopMode() == LooperEngine::LoopMode::Reverse, "REVERSE engages");
      d.engine.onPlayButtonPressed();
      checkBuffers(d.run(silence(750)), cycle(reversed(loop_a), 750), "reversed loop repeats");
    });

    //==========================================================================
    beginTest("Reverse pressed mid-playback turns around at the current position");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.run(silence(100));   // playhead now at sample 100
      d.engine.onReverseButtonPressed();
      // sample 100 is at index 199 of the reversed loop
      checkBuffers(d.run(silence(450)), cycle(reversed(loop_a), 450, 199), "backwards from sample 100");

      d.engine.onReverseButtonPressed();
      // 450 steps back from 100 lands on 250
      checkBuffers(d.run(silence(450)), cycle(loop_a, 450, 250), "forwards again from where it was");
    });

    //==========================================================================
    // A click on the REVERSE button reaches the engine from the message thread,
    // usually while the audio thread is partway through a block. Where the presses
    // land can't be scripted, so this checks the shape of the output: every sample
    // is the loop sample next to the one before (each turnaround is clean), and
    // turnarounds happen inside blocks, not only where a block starts.
    beginTest("REVERSE pressed while a block is playing turns around inside that block");
    {
      const int loop_length = 1000;
      const int block_size  = 4096;
      const int num_blocks  = 500;

      EngineDriver d(block_size, 1);
      d.recordLoop(ramp(loop_length, 0, 1));

      Buffer output(1, block_size * num_blocks);
      std::atomic<bool> playing { true };
      std::thread audio_thread([&]
      {
        Buffer block(1, block_size);
        for (int b = 0; b < num_blocks; ++b)
        {
          block.clear();
          d.engine.processBlock(block);
          output.copyFrom(0, b * block_size, block, 0, 0, block_size);
        }
        playing = false;
      });

      // Busy-wait between presses: sleep granularity on some CI machines is longer than the whole run
      int presses = 0;
      while (playing)
      {
        d.engine.onReverseButtonPressed();
        ++presses;
        const auto until = juce::Time::getHighResolutionTicks() + juce::Time::secondsToHighResolutionTicks(0.0002);
        while (playing && juce::Time::getHighResolutionTicks() < until) {}
      }
      audio_thread.join();

      // Loop sample n holds (n + 1) / 1024, so each output value names the loop position it came from
      auto positionAt = [&](int i) { return juce::roundToInt(output.getSample(0, i) * 1024.0f) - 1; };

      int jumps = 0, turnarounds = 0, mid_block_turnarounds = 0, previous_step = 0;
      for (int i = 1; i < output.getNumSamples(); ++i)
      {
        int step = positionAt(i) - positionAt(i - 1);
        if (step == loop_length - 1) step = -1;   // backwards across the loop start
        if (step == 1 - loop_length) step = 1;    // forwards across the loop end
        if (step != 1 && step != -1)
        {
          ++jumps;
          continue;
        }

        if (previous_step != 0 && step != previous_step)
        {
          ++turnarounds;
          // A press between blocks shows as a turnaround at the block's second sample
          if (i % block_size != 1)
            ++mid_block_turnarounds;
        }
        previous_step = step;
      }

      logMessage(juce::String(presses) + " presses, " + juce::String(turnarounds) + " turnarounds, "
                 + juce::String(mid_block_turnarounds) + " inside a block");
      check(jumps == 0, "every sample follows on from the one before");
      check(turnarounds > 0 && turnarounds <= presses, "each turnaround comes from a press");
      check(mid_block_turnarounds > 0, "presses take effect inside the block they land in");
    }

    //==========================================================================
    beginTest("Half speed: playback takes twice as long, in-between samples are interpolated");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.engine.onPlayButtonPressed();
      d.engine.onSlowButtonPressed();
      check(d.engine.getSpeedMode() == LooperEngine::SpeedMode::Half, "SLOW engages half speed");
      d.engine.onPlayButtonPressed();

      Buffer expected(2, 600);
      for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 300; ++i)
        {
          const float here = loop_a.getSample(channel, i);
          const float next = loop_a.getSample(channel, (i + 1) % 300);
          expected.setSample(channel, 2 * i, here);
          expected.setSample(channel, 2 * i + 1, (here + next) * 0.5f);
        }

      checkBuffers(d.run(silence(1500)), cycle(expected, 1500), "half-speed loop");
    });

    //==========================================================================
    beginTest("Half-speed recording plays back twice as fast at full speed");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.onSlowButtonPressed();
      d.engine.onRecordButtonPressed();
      d.run(loop_a);
      d.engine.onPlayButtonPressed();   // stop
      d.engine.onSlowButtonPressed();   // back to full speed
      d.engine.onPlayButtonPressed();

      const Buffer out = d.run(silence(300));
      check(d.engine.checkAndClearLoopWrapped(), "loop is half as long as the take");

      // The take is decimated by two. Which sample of each pair survives is an
      // implementation detail, so accept either.
      bool decimated = true;
      for (int channel = 0; channel < 2; ++channel)
        for (int n = 0; n < 300; ++n)
        {
          const float got = out.getSample(channel, n);
          const int   i   = n % 150;
          decimated = decimated && (juce::exactlyEqual(got, loop_a.getSample(channel, 2 * i)) || juce::exactlyEqual(got, loop_a.getSample(channel, 2 * i + 1)));
        }
      check(decimated, "every loop sample comes from the matching pair of input samples");
    });

    //==========================================================================
    beginTest("Once from stopped: one pass, then stopped with Once cleared");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.engine.onPlayButtonPressed();
      d.engine.onOnceButtonPressed();
      check(d.engine.getState() == State::Playing, "ONCE starts playback");

      const Buffer expected = concat(mix(loop_a, 1.0f, slice(live, 0, 300), 1.0f), slice(live, 300, 600));
      checkBuffers(d.run(live), expected, "one pass of the loop, then dry signal only");
      check(d.engine.getState() == State::Stopped, "stopped after one pass");
      check(d.engine.getOnceMode() == LooperEngine::OnceMode::Off, "Once clears itself");
    });

    //==========================================================================
    beginTest("Once while playing: finishes the current pass, then stops");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.run(silence(100));
      d.engine.onOnceButtonPressed();
      check(d.engine.getState() == State::Playing, "keeps playing to the loop end");
      checkBuffers(d.run(silence(500)), concat(slice(loop_a, 100, 200), silence(300)), "rest of the pass, then nothing");
      check(d.engine.getState() == State::Stopped, "stopped at the loop end");
    });

    //==========================================================================
    beginTest("Once pressed again restarts the pass; ONCE OFF keeps the loop cycling");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.engine.onPlayButtonPressed();
      d.engine.onOnceButtonPressed();
      d.run(silence(50));
      d.engine.onOnceButtonPressed();
      checkBuffers(d.run(silence(400)), concat(loop_a, silence(100)), "restarts from the loop start, plays once");

      d.engine.onOnceButtonPressed();
      d.run(silence(50));
      d.engine.onOnceOffButtonPressed();
      check(d.engine.getOnceMode() == LooperEngine::OnceMode::Off, "ONCE OFF cancels Once");
      checkBuffers(d.run(silence(700)), cycle(loop_a, 700, 50), "loop keeps cycling");
    });

    //==========================================================================
    beginTest("Stack: live input is mixed into the loop, which drops 2.5 dB per pass");
    forEachBlockSize([&](int block_size)
    {
      for (float feedback : { 0.0f, 0.5f, 1.0f })
      {
        const Buffer overdub = slice(live, 0, 300);
        const Buffer stacked = mix(loop_a, stack_gain, overdub, feedback);

        EngineDriver d(block_size);
        d.engine.setFeedback(feedback);
        d.recordLoop(loop_a);
        d.engine.onStackButtonPressed();
        check(d.engine.getState() == State::Overdubbing, "STACK starts overdubbing");
        checkBuffers(d.run(overdub), mix(stacked, 1.0f, overdub, 1.0f), "stacked loop + live while stacking", gain_tolerance);

        d.engine.onStackButtonReleased();
        check(d.engine.getState() == State::Playing, "releasing STACK returns to playback");
        checkBuffers(d.run(silence(750)), cycle(stacked, 750), "stacked loop plays back", gain_tolerance);
      }
    });

    //==========================================================================
    // README (STACK): "stacking works during reversed playback so any part can be
    // recorded forward or reversed"
    beginTest("Stack in reverse: the new part lines up with reversed playback and plays backwards going forward");
    forEachBlockSize([&](int block_size)
    {
      const float feedback = 0.5f;
      const Buffer overdub = slice(live, 0, 300);
      const Buffer stacked_backwards = mix(reversed(loop_a), stack_gain, overdub, feedback);

      EngineDriver d(block_size);
      d.engine.setFeedback(feedback);
      d.recordLoop(loop_a);
      d.engine.onPlayButtonPressed();
      d.engine.onReverseButtonPressed();
      d.engine.onPlayButtonPressed();   // reversed playback from the loop's end
      d.engine.onStackButtonPressed();
      checkBuffers(d.run(overdub), mix(stacked_backwards, 1.0f, overdub, 1.0f), "stacked loop + live while stacking", gain_tolerance);

      d.engine.onStackButtonReleased();
      checkBuffers(d.run(silence(600)), cycle(stacked_backwards, 600), "the part plays as it was played, in reverse", gain_tolerance);

      d.engine.onPlayButtonPressed();
      d.engine.onReverseButtonPressed();
      d.engine.onPlayButtonPressed();   // forward playback from the start
      checkBuffers(d.run(silence(300)), mix(loop_a, stack_gain, reversed(overdub), feedback), "forward, the stacked part plays backwards", gain_tolerance);
    });

    //==========================================================================
    beginTest("Stack latch: first press starts, release is ignored, second press ends");
    {
      EngineDriver d(64);
      d.engine.setStackLatch(true);
      d.recordLoop(loop_a);
      d.engine.onStackButtonPressed();
      d.engine.onStackButtonReleased();
      check(d.engine.getState() == State::Overdubbing, "still stacking after release");
      d.engine.onStackButtonPressed();
      check(d.engine.getState() == State::Playing, "second press ends the stack");
    }

    //==========================================================================
    beginTest("RECORD and PLAY while stacking");
    {
      EngineDriver d(64);
      d.recordLoop(loop_a);
      d.engine.onStackButtonPressed();
      d.engine.onRecordButtonPressed();
      check(d.engine.getState() == State::Playing, "RECORD ends the stack, loop keeps playing");

      d.engine.onStackButtonPressed();
      d.engine.onPlayButtonPressed();
      check(d.engine.getState() == State::Stopped, "PLAY stops everything");
      check(d.engine.getStackMode() == LooperEngine::StackMode::Off, "stack is off after stopping");
    }

    //==========================================================================
    beginTest("STACK while stopped toggles half speed");
    {
      EngineDriver d(64);
      d.engine.onStackButtonPressed();
      check(d.engine.getSpeedMode() == LooperEngine::SpeedMode::Half, "half speed on");
      d.engine.onStackButtonReleased();
      d.engine.onStackButtonPressed();
      check(d.engine.getSpeedMode() == LooperEngine::SpeedMode::Normal, "half speed off");
    }

    //==========================================================================
    beginTest("Extended features off: SLOW and ONCE OFF do nothing");
    {
      EngineDriver d(64);
      d.engine.setExtendedFeatures(false);
      d.engine.onSlowButtonPressed();
      check(d.engine.getSpeedMode() == LooperEngine::SpeedMode::Normal, "SLOW ignored");

      d.recordLoop(loop_a);
      d.engine.onOnceButtonPressed();
      d.engine.onOnceOffButtonPressed();
      check(d.engine.getOnceMode() == LooperEngine::OnceMode::On, "ONCE OFF ignored");

      d.engine.onPlayButtonPressed();
      d.engine.onStackButtonPressed();
      check(d.engine.getSpeedMode() == LooperEngine::SpeedMode::Half, "STACK while stopped still changes speed");
    }

    //==========================================================================
    beginTest("Buffer full: recording stops by itself and keeps the full-length loop");
    forEachBlockSize([&](int block_size)
    {
      const int max_samples = 240 * 1000;   // 4 minutes at the driver's 1 kHz
      const Buffer long_take = ramp(max_samples + 100);

      EngineDriver d(block_size);
      d.engine.onRecordButtonPressed();
      checkBuffers(d.run(long_take), long_take, "dry signal throughout");
      check(d.engine.getState() == State::Stopped, "stopped when the buffer filled");

      d.engine.onPlayButtonPressed();
      checkBuffers(d.run(silence(max_samples / 2)), slice(long_take, 0, max_samples / 2), "loop holds the take");
      check(std::abs(d.engine.getLoopProgress() - 0.5f) < 1.0e-4f, "loop is exactly the maximum length");
    });

    //==========================================================================
    beginTest("reset() wipes the loop and returns every mode to its default");
    {
      EngineDriver d(64);
      d.engine.onSlowButtonPressed();
      d.engine.onReverseButtonPressed();
      d.engine.onThruMuteButtonPressed();
      d.recordLoop(loop_a);
      d.engine.reset();

      check(d.engine.getState() == State::Stopped, "stopped");
      check(d.engine.getSpeedMode() == LooperEngine::SpeedMode::Normal, "full speed");
      check(d.engine.getLoopMode() == LooperEngine::LoopMode::Normal, "forward");
      check(d.engine.getThruMuteState() == LooperEngine::ThruMuteState::Off, "thru on");
      d.engine.onPlayButtonPressed();
      check(d.engine.getState() == State::Stopped, "nothing to play");
    }

    //==========================================================================
    // Continuous reverse (README, DIRECTION; issue #14): REVERSE on before the
    // take begins, then RECORD twice to set the loop length
    beginTest("Continuous reverse: each pass plays the pass before it backwards");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.onThruMuteButtonPressed();   // only the reversed signal
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_a);
      check(d.engine.getState() == State::ContinuousReverse, "second RECORD enters continuous reverse");
      check(d.engine.isRecording() && d.engine.isPlaying(), "recording and playing at once");
      checkBuffers(d.run(live), reversedPasses(loop_a, live), "the take backwards, then each pass of live input backwards");
    });

    //==========================================================================
    beginTest("Continuous reverse with THRU on mixes in the live input; volume scales only the reversed signal");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.setVolume(0.5f);
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_a);
      checkBuffers(d.run(live), mix(reversedPasses(loop_a, live), 0.5f, live, 1.0f), "reversed passes at half level + live");
    });

    //==========================================================================
    beginTest("Continuous reverse: loop wrap is flagged at the end of each pass");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_a);
      d.run(silence(299));
      check(! d.engine.checkAndClearLoopWrapped(), "no wrap one sample before the end of the first pass");
      d.run(silence(1));
      check(d.engine.checkAndClearLoopWrapped(), "wrap after the first pass");
      d.run(silence(299));
      check(! d.engine.checkAndClearLoopWrapped(), "no wrap one sample before the end of the second pass");
      d.run(silence(1));
      check(d.engine.checkAndClearLoopWrapped(), "wrap after the second pass");
    });

    //==========================================================================
    beginTest("Re-recording with Reverse on enters continuous reverse with the new take");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.run(silence(300));
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_b);
      check(d.engine.getState() == State::ContinuousReverse, "continuous reverse");
      checkBuffers(d.run(silence(120)), reversed(loop_b), "the new take, backwards");
    });

    //==========================================================================
    beginTest("RECORD during continuous reverse starts a new take, which sets the new loop length");
    forEachBlockSize([&](int block_size)
    {
      const Buffer more = slice(live, 0, 240);

      EngineDriver d(block_size);
      d.engine.onThruMuteButtonPressed();
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_a);
      d.run(slice(live, 0, 450));
      d.engine.onRecordButtonPressed();
      check(d.engine.getState() == State::Recording, "RECORD starts a new take");
      d.run(loop_b);
      d.engine.onRecordButtonPressed();
      check(d.engine.getState() == State::ContinuousReverse, "second RECORD re-enters continuous reverse");
      checkBuffers(d.run(more), reversedPasses(loop_b, more), "passes are the new take's length");
    });

    //==========================================================================
    beginTest("PLAY ends continuous reverse");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_a);
      d.run(slice(live, 0, 100));
      d.engine.onPlayButtonPressed();
      check(d.engine.getState() == State::Stopped, "stopped");
      check(! d.engine.isRecording(), "no longer recording");
      checkBuffers(d.run(live), live, "only dry signal");
    });

    //==========================================================================
    beginTest("REVERSE during continuous reverse stops recording; the loop plays on forwards");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.onThruMuteButtonPressed();
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_a);
      d.run(silence(100));   // the first pass has written silence over samples 299 down to 200
      d.engine.onReverseButtonPressed();
      check(d.engine.getState() == State::Playing, "plain playback");
      check(d.engine.getLoopMode() == LooperEngine::LoopMode::Normal, "forwards");

      // Forwards from sample 199; the live input is no longer recorded
      const Buffer loop_now = concat(slice(loop_a, 0, 200), silence(100));
      checkBuffers(d.run(slice(live, 0, 600)), cycle(loop_now, 600, 199), "the loop as it was, forwards");
    });

    //==========================================================================
    beginTest("STACK and ONCE leave continuous reverse alone");
    {
      EngineDriver d(64);
      d.engine.onReverseButtonPressed();
      d.recordLoop(loop_a);
      d.engine.onStackButtonPressed();
      d.engine.onStackButtonReleased();
      d.engine.onOnceButtonPressed();
      check(d.engine.getState() == State::ContinuousReverse, "still in continuous reverse");
      check(d.engine.getOnceMode() == LooperEngine::OnceMode::Off, "Once not armed");
    }

    //==========================================================================
    beginTest("REVERSE turned off during the take: the take plays forwards");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      const Buffer take = slice(loop_a, 0, 150);
      d.engine.onReverseButtonPressed();
      d.engine.onRecordButtonPressed();
      d.run(slice(take, 0, 100));
      d.engine.onReverseButtonPressed();
      d.run(slice(take, 100, 50));
      d.engine.onRecordButtonPressed();
      check(d.engine.getState() == State::Playing, "plain playback");
      checkBuffers(d.run(silence(300)), cycle(take, 300), "the take, forwards");
    });

    //==========================================================================
    beginTest("REVERSE while recording: the take keeps its length and plays backwards");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      const Buffer take = slice(loop_a, 0, 150);
      d.engine.onRecordButtonPressed();
      d.run(slice(take, 0, 100));
      d.engine.onReverseButtonPressed();
      d.run(slice(take, 100, 50));
      d.engine.onRecordButtonPressed();
      check(d.engine.getState() == State::Playing, "second RECORD starts playback");
      checkBuffers(d.run(silence(150)), reversed(take), "the whole take, backwards");
      check(d.engine.checkAndClearLoopWrapped(), "loop is as long as what was recorded (150 samples)");
    });

    //==========================================================================
    beginTest("Reverse at half speed interpolates between adjacent samples");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.engine.onPlayButtonPressed();
      d.engine.onReverseButtonPressed();
      d.engine.onSlowButtonPressed();
      d.engine.onPlayButtonPressed();

      const Buffer backwards = reversed(loop_a);
      Buffer expected(2, 600);
      for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 300; ++i)
        {
          const float here = backwards.getSample(channel, i);
          const float next = backwards.getSample(channel, (i + 1) % 300);
          expected.setSample(channel, 2 * i, here);
          expected.setSample(channel, 2 * i + 1, (here + next) * 0.5f);
        }

      checkBuffers(d.run(silence(600)), expected, "reversed half-speed loop");
    });

    //==========================================================================
    beginTest("One stack pass at half speed applies the 2.5 dB drop once");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.engine.setFeedback(0.0f);
      d.recordLoop(loop_a);
      d.engine.onSlowButtonPressed();
      d.engine.onStackButtonPressed();
      d.run(silence(600));   // one full pass at half speed
      d.engine.onStackButtonReleased();
      d.engine.onSlowButtonPressed();
      checkBuffers(d.run(silence(300)), mix(loop_a, stack_gain, loop_a, 0.0f), "loop after one pass", gain_tolerance);
    });

    //==========================================================================
    beginTest("THRU MUTE stays silent after Once stops the loop");
    forEachBlockSize([&](int block_size)
    {
      EngineDriver d(block_size);
      d.recordLoop(loop_a);
      d.engine.onPlayButtonPressed();
      d.engine.onThruMuteButtonPressed();
      d.engine.onOnceButtonPressed();
      checkBuffers(d.run(live), concat(loop_a, silence(600)), "loop once, then silence");
    });

    //==========================================================================
    beginTest("THRU MUTE stays silent when the buffer fills");
    forEachBlockSize([&](int block_size)
    {
      const int max_samples = 240 * 1000;
      EngineDriver d(block_size);
      d.engine.onThruMuteButtonPressed();
      d.engine.onRecordButtonPressed();
      checkBuffers(d.run(ramp(max_samples + 100)), silence(max_samples + 100), "silent throughout");
    });

    //==========================================================================
    beginTest("ONCE with nothing recorded doesn't leave Once armed");
    {
      EngineDriver d(64);
      d.engine.onOnceButtonPressed();
      check(d.engine.getOnceMode() == LooperEngine::OnceMode::Off, "Once stays off");
      d.recordLoop(loop_a);
      d.run(silence(650));
      check(d.engine.getState() == State::Playing, "the new loop keeps cycling");
    }

    //==========================================================================
    // Needs a real sample rate: 240 s at 44.1 kHz is 10.58M samples, and a
    // float playhead can't step by 0.5 above 8,388,608
    beginTest("Half speed past 2^23 samples: playback still wraps, recording still fills the buffer");
    {
      const int loop_length = 8400000;
      EngineDriver d(4096, 1, 44100.0);
      Buffer block(1, 4096);

      auto process = [&](int num_samples)
      {
        for (int done = 0; done < num_samples; done += 4096)
        {
          block.setSize(1, juce::jmin(4096, num_samples - done), false, false, true);
          block.clear();
          d.engine.processBlock(block);
        }
      };

      d.engine.onRecordButtonPressed();
      process(loop_length);
      d.engine.onPlayButtonPressed();
      d.engine.onSlowButtonPressed();
      d.engine.onPlayButtonPressed();
      process(2 * loop_length);
      check(d.engine.checkAndClearLoopWrapped(), "loop wrapped after one half-speed pass");

      // Half-speed recording writes one loop sample per two input samples
      const int max_samples = 240 * 44100;
      d.engine.onPlayButtonPressed();
      d.engine.onRecordButtonPressed();
      process(2 * max_samples + 4096);
      check(d.engine.getState() == State::Stopped, "half-speed recording stops when the buffer fills");
    }
  }
};

static LooperEngineTests looperEngineTests;
