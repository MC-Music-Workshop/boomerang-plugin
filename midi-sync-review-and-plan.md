# MIDI Sync Implementation: Thorough Review & Revised Plan

**Issue:** #20 — Feature: MIDI Sync
**Reviewed:** 2026-02-11
**Reviewer:** Claude (Opus 4.6)
**Inputs:** Issue #20 discussion, `2026-02-02-midi-sync-plan.md`, codebase analysis, archived `looper.cxx`, JUCE AudioPlayHead docs, Mobius/SooperLooper sync design patterns

---

## Part 1: Review of the Existing Plan

### What the Plan Gets Right

1. **Phased approach** — Starting with quantized recording (MVP) before tackling transport sync and tempo tracking is the correct prioritization. Users' #1 request is loops that land on bar boundaries.

2. **Host sync, not MIDI clock** — The plan correctly uses JUCE's `AudioPlayHead` API rather than raw MIDI clock messages. As documented by Mobius: "MIDI Slave Sync is almost never used if you are running as a plugin — Host Sync is usually better." MIDI clock is less accurate and requires a MIDI Start message to establish beat alignment.

3. **Edge cases table** — Covering standalone mode, tempo=0, very short recordings, and mid-recording tempo changes shows good defensive thinking.

4. **Simple toggle UI** — Aligns with the Boomerang philosophy of simplicity. The hardware Boomerang III uses a single sync switch.

5. **Sprint breakdown** — Reasonable time estimates and logical sequencing.

---

### Critical Issues Found

#### Issue 1: The Title Is a Misnomer

The feature is **Host Tempo Sync** (reading BPM/time-signature from the DAW via `AudioPlayHead`), not "MIDI Sync" (receiving MIDI clock/start/stop messages). This distinction matters because:

- Users coming from hardware loopers may expect MIDI clock support (24 PPQN messages).
- The implementation uses zero MIDI messages — it's purely host transport info.
- **Recommendation:** Rename to "Tempo Sync" or "Host Sync" in the UI and docs. Keep "MIDI Sync" in the issue title since that's what users searched for, but clarify in documentation.

#### Issue 2: Missing Sample-Accurate Start/Stop Within processBlock

**This is the most significant gap.** The plan proposes quantizing the loop *length* after recording stops, but ignores *when within a processBlock buffer* the bar boundary actually falls.

The current approach: User presses stop → `stopRecording()` fires → loop length gets rounded to nearest bar in samples.

The problem: `processBlock()` processes 256-512 samples at a time. If the bar boundary falls at sample 137 of a 256-sample buffer, the plan would still record all 256 samples and then do the math afterward. This introduces up to one buffer's worth of timing error (~5-12ms at 44.1kHz).

**The archived `looper.cxx` already solves this correctly** (lines 216-301):

```cpp
int startRecordingSample = -1;  // sample offset within buffer
int stopRecordingSample = -1;

// Calculate exact sample within buffer where boundary falls
double expectedSamplePosition = quarterNotesToSamples(expectedPos, bpm);
int nextSample = int(floor(expectedSamplePosition + 0.5)) - transport.positionInSamples;

// Then in the per-sample loop:
if (int(i) == stopRecordingSample)
    stopRecording();
```

**Recommendation:** Port this sample-accurate approach. When the user signals "stop recording" with sync enabled, don't stop immediately — calculate the next bar boundary's exact sample offset within the current (or a future) processBlock, and stop precisely there.

#### Issue 3: PPQ-Based Quantization Is More Robust Than Sample-Count Math

The plan proposes:
```
samplesPerBar = (60.0 / bpm) * sampleRate * beatsPerBar
quantizedLength = round(rawLength / samplesPerBar) * samplesPerBar
```

This breaks if tempo changed during recording, because `samplesPerBar` at the end doesn't reflect the actual elapsed time.

**Better approach — use PPQ positions from the host:**

1. When recording starts, store `ppqPositionAtRecordStart` from `PositionInfo::getPpqPosition()`.
2. When user signals stop, read `ppqPositionAtRecordEnd`.
3. Calculate `ppqRecorded = ppqEnd - ppqStart`.
4. Quantize in PPQ space: `ppqQuantized = round(ppqRecorded / ppqPerBar) * ppqPerBar` (where `ppqPerBar = timeSigNumerator * 4.0 / timeSigDenominator`).
5. Calculate the target PPQ stop position: `ppqTargetStop = ppqStart + ppqQuantized`.
6. In subsequent processBlock calls, find the exact sample where PPQ reaches `ppqTargetStop` and stop there.

This is immune to tempo changes because PPQ is the host's canonical timeline.

#### Issue 4: "Nearest Bar" Rounding Needs a Musical Policy

The plan asks: "What happens if user records 1.5 bars?" and recommends "round to nearest."

Consider: rounding 1.5 bars UP to 2 bars means 0.5 bars (~1 second at 120 BPM) of silence appended. This sounds bad. Rounding DOWN to 1 bar means truncating half the performance. This also sounds bad.

**Recommendation — use a threshold policy:**
- If within 25% of the next bar boundary → round UP (extend with captured audio or silence + crossfade)
- If beyond 75% past a bar boundary → round UP
- Otherwise → round DOWN (truncate with crossfade)
- Always ensure minimum 1 bar
- Make the threshold configurable in a future phase

Actually, for the MVP the simplest correct behavior is: **always round to the nearest bar**. Users can re-record if it sounds wrong. The key is that the crossfade makes it seamless.

#### Issue 5: Crossfade Specification Is Incomplete

The plan mentions "crossfade for truncation" but doesn't specify:

- **Length:** 5-10ms is standard for click-free splicing. The archived `looper.cxx` uses 1ms (`fadeTime = int(.001 * sampleRate)`), which works but is aggressive.
- **Shape:** Linear is fine for short crossfades. The archived code uses linear.
- **What happens when extending:** Zero-fill is mentioned, but a better approach is to crossfade the loop boundary so the end blends into the beginning — this creates a smoother loop point.

**Recommendation:** Use 5ms linear crossfade for both truncation and extension seams.

#### Issue 6: Half-Speed Mode Interaction Not Addressed

When `SpeedMode::Half` is active, recording advances at 0.5x speed (the `speed` multiplier in `processRecording`). This means:
- Raw recording length in samples is the same wall-clock time
- But the *effective* recorded content spans half the time musically
- Quantization to bars should account for this: the loop plays back at half speed, so a "bar" of half-speed content is 2x as many samples

**Recommendation:** For MVP, disable sync when half-speed mode is active (simplest). Document this limitation. Address in Phase 2.

#### Issue 7: Missing `ppqPositionOfLastBarStart` Usage

JUCE's `PositionInfo` provides `getPpqPositionOfLastBarStart()`, which gives you the PPQ of the most recent downbeat. This is extremely useful for calculating bar boundaries without doing the math yourself. The plan doesn't mention it.

**Caveat:** Not all hosts populate this field (notably Pro Tools may not). Need a fallback that calculates bar boundaries from `ppqPosition` + `timeSignature`.

#### Issue 8: Thread Safety Could Be Cleaner

The plan uses individual atomics for each transport field (`hostBPM`, `beatsPerBar`, `beatUnit`, `hostIsPlaying`). This can lead to torn reads if tempo and time signature change simultaneously.

**Better pattern:** Create a small struct and use a single atomic or a `juce::SpinLock`:

```cpp
struct HostTransportInfo {
    double bpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double ppqPosition = 0.0;
    double ppqOfLastBarStart = 0.0;
    bool isPlaying = false;
    bool isValid = false;
};
```

Read all fields at the top of `processBlock`, store into a local copy, and pass that around. This is what the archived `looper.cxx` does with `data.transport`.

#### Issue 9: Standalone Detection

The plan says sync should be "hidden or disabled" in standalone but doesn't say how to detect it. In JUCE, `getPlayHead()` returns `nullptr` in standalone mode, and `getPosition()` returns nullopt values. The simplest approach is to check `if (auto* playhead = getPlayHead())` — if null, sync is unavailable. Show sync controls but display "No host tempo" or keep the control grayed out.

#### Issue 10: The "Pending Stop" State Is Missing

When the user presses stop-recording with sync ON, the recording shouldn't stop immediately — it needs to continue until the next bar boundary. This creates a **new state** that the current `LooperState` enum doesn't have. The user pressed stop, but the engine is still recording, waiting for the bar boundary.

Options:
- Add `RecordingPendingStop` state (cleanest, but touches the state machine)
- Use a flag `pendingQuantizedStop` alongside the `Recording` state (simpler, less invasive)

**Recommendation:** Use a flag approach for MVP. The engine stays in `Recording` state but has `pendingQuantizedStop = true` and `ppqTargetStop` set. Each processBlock checks if the PPQ has reached the target.

---

### Minor Issues

- **No mention of `timeInSamples`** from `PositionInfo` — this is useful for debugging and could be logged.
- **The parameter version number should increment** — adding a new `sync` parameter means bumping the APVTS parameter version from 1 to 2 for proper state migration.
- **Missing: What if host tempo is unreasonably extreme?** (e.g., 10 BPM = 24 sec/bar, or 999 BPM = 0.27 sec/bar). Should clamp to a reasonable range (30-300 BPM).

---

## Part 2: Revised Implementation Plan

### Architecture Overview

```
processBlock()
  ├─ Read PositionInfo from AudioPlayHead
  ├─ Store transport snapshot in HostTransportInfo
  ├─ Pass to LooperEngine::processBlock(buffer, transportInfo)
  │   ├─ If Recording && pendingQuantizedStop:
  │   │   ├─ Check each sample: has PPQ reached ppqTargetStop?
  │   │   ├─ If yes: stop recording at that exact sample
  │   │   └─ Apply crossfade at the splice point
  │   ├─ If Recording && just received stop signal:
  │   │   ├─ Calculate ppqTargetStop (next bar boundary)
  │   │   ├─ If boundary is within this buffer: stop at exact sample
  │   │   └─ Else: set pendingQuantizedStop, continue recording
  │   └─ Normal processing for other states
  └─ Update sync UI state
```

### Phase 1 Implementation (MVP): Quantized Record-Stop

#### Step 1: Transport Info Infrastructure

**Files:** `LooperEngine.h`, `PluginProcessor.cpp`

Add a transport info struct and plumbing:

```cpp
// In LooperEngine.h
struct HostTransportInfo {
    double bpm = 0.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double ppqPosition = 0.0;
    double ppqOfLastBarStart = 0.0;
    int64_t timeInSamples = 0;
    bool isPlaying = false;
    bool isValid = false;  // false if no host or no data
};
```

Change `processBlock` signature:
```cpp
void processBlock(juce::AudioBuffer<float>& buffer, const HostTransportInfo& transport);
```

In `PluginProcessor::processBlock()`, read the playhead:
```cpp
LooperEngine::HostTransportInfo transport;
if (auto* playhead = getPlayHead()) {
    if (auto pos = playhead->getPosition()) {
        if (auto bpm = pos->getBpm())
            transport.bpm = *bpm;
        if (auto ts = pos->getTimeSignature()) {
            transport.timeSigNumerator = ts->numerator;
            transport.timeSigDenominator = ts->denominator;
        }
        if (auto ppq = pos->getPpqPosition())
            transport.ppqPosition = *ppq;
        if (auto barStart = pos->getPpqPositionOfLastBarStart())
            transport.ppqOfLastBarStart = *barStart;
        if (auto playing = pos->getIsPlaying())
            transport.isPlaying = *playing;
        if (auto samples = pos->getTimeInSamples())
            transport.timeInSamples = *samples;
        transport.isValid = (transport.bpm > 0.0);
    }
}
looperEngine->processBlock(buffer, transport);
```

#### Step 2: Sync Mode & Quantization State

**File:** `LooperEngine.h`

```cpp
enum class SyncMode { Off, Bars };  // Keep it simple for MVP

// Sync state
std::atomic<SyncMode> syncMode { SyncMode::Off };
std::atomic<bool> pendingQuantizedStop { false };

// Stored at recording start for quantization
double ppqAtRecordStart = 0.0;

// Target PPQ position where recording should actually stop
double ppqTargetStop = 0.0;

// Crossfade length for quantization splices
static constexpr int quantizeCrossfadeSamples = 220; // ~5ms at 44.1kHz
```

Public methods:
```cpp
void setSyncMode(SyncMode mode);
SyncMode getSyncMode() const { return syncMode.load(); }
bool isSyncAvailable() const;  // Returns true if valid transport info exists
```

#### Step 3: Quantized Recording Logic

**File:** `LooperEngine.cpp`

Modify `startRecording()`:
```cpp
void LooperEngine::startRecording()
{
    auto& activeSlot = loopSlots[...];
    // ... existing setup ...

    // Store PPQ at record start for later quantization
    ppqAtRecordStart = lastTransport.ppqPosition;
    pendingQuantizedStop.store(false);

    currentState.store(LooperState::Recording);
}
```

When the user signals stop and sync is on, don't stop immediately — calculate the target:
```cpp
void LooperEngine::requestQuantizedStop(const HostTransportInfo& transport)
{
    if (!transport.isValid || syncMode.load() == SyncMode::Off) {
        stopRecording();  // Fallback: immediate stop
        return;
    }

    double ppqPerBar = transport.timeSigNumerator * 4.0 / transport.timeSigDenominator;
    double ppqRecorded = transport.ppqPosition - ppqAtRecordStart;
    double barsRecorded = ppqRecorded / ppqPerBar;

    // Round to nearest bar (minimum 1)
    int quantizedBars = std::max(1, static_cast<int>(std::round(barsRecorded)));
    double ppqQuantized = quantizedBars * ppqPerBar;

    ppqTargetStop = ppqAtRecordStart + ppqQuantized;

    // If we've already passed the target, stop now
    if (transport.ppqPosition >= ppqTargetStop) {
        finalizeQuantizedStop(transport);
        return;
    }

    pendingQuantizedStop.store(true);
    // Recording continues — processBlock will check each buffer
}
```

In `processBlock`, when recording with a pending stop:
```cpp
if (currentState.load() == LooperState::Recording && pendingQuantizedStop.load())
{
    // Check if the target PPQ falls within this buffer
    double ppqPerSample = transport.bpm / (60.0 * sampleRate);
    double ppqAtBufferEnd = transport.ppqPosition + (buffer.getNumSamples() * ppqPerSample);

    if (ppqTargetStop <= ppqAtBufferEnd)
    {
        // Calculate the exact sample offset
        int stopSample = static_cast<int>(
            (ppqTargetStop - transport.ppqPosition) / ppqPerSample);
        stopSample = juce::jlimit(0, buffer.getNumSamples() - 1, stopSample);

        // Process recording up to that sample, then stop
        processRecordingPartial(buffer, activeSlot, 0, stopSample);
        finalizeQuantizedStop(transport);
        // Process remaining samples as playback (or silence)
        processPlaybackPartial(buffer, activeSlot, stopSample, buffer.getNumSamples());
        return;
    }
    // else: target is in a future buffer, keep recording normally
}
```

`finalizeQuantizedStop()` handles the crossfade and length finalization.

#### Step 4: APVTS Parameter

**File:** `PluginProcessor.cpp`

Add a choice parameter:
```cpp
layout.add(std::make_unique<juce::AudioParameterChoice>(
    juce::ParameterID("sync", 2),  // Version 2!
    "Sync",
    juce::StringArray { "Off", "Bars" },
    0));  // Default: Off
```

Wire to engine in `parameterChanged()`:
```cpp
if (parameterID == "sync") {
    auto mode = static_cast<LooperEngine::SyncMode>(static_cast<int>(newValue));
    looperEngine->setSyncMode(mode);
}
```

#### Step 5: UI Changes

**File:** `PluginEditor.h/cpp`

Add a sync indicator to the settings menu or as a new LED:

Option A (simplest): Add "Sync: Off/Bars" to the gear settings menu, with a SYNC LED near the existing LEDs that lights when sync is active and valid transport is available.

Option B: Add a dedicated sync toggle button. Given the Boomerang hardware aesthetic with fixed button positions, Option A is cleaner for MVP.

Also add a small tempo display (e.g., "120 BPM" text) near the sync indicator — this is invaluable for confirming the host is sending tempo data.

#### Step 6: Edge Cases

| Scenario | Behavior |
|----------|----------|
| No host / no tempo data | `isSyncAvailable()` returns false; sync control grayed out; recording is free-form |
| Host tempo = 0 or negative | Treated as "no tempo"; fallback to free-form |
| BPM outside 30-300 range | Clamp to 30-300; log warning |
| Tempo changes during recording | PPQ-based quantization handles this automatically |
| Very short recording (<0.5 bars) | Quantize to 1 bar; extend with recorded audio looped + crossfade |
| Very long recording (>64 bars) | Quantize normally; no special handling needed |
| pendingQuantizedStop but host stops | Stop recording immediately; quantize to nearest bar using last known tempo |
| Half-speed mode + sync | Disable sync (show warning); address in Phase 2 |
| User presses Record again while pendingQuantizedStop | Cancel the pending stop; stop recording immediately and start new recording |
| Standalone mode (no host) | `getPlayHead()` returns nullptr; sync unavailable |
| Host doesn't provide ppqOfLastBarStart | Calculate from ppqPosition + timeSignature |
| Time signature change during recording | PPQ-based math handles this; ppqPerBar changes but the target was set in PPQ space |

---

### Task Breakdown

#### Sprint 1: Transport Infrastructure (2-3 hrs)

- [ ] Create `HostTransportInfo` struct in `LooperEngine.h`
- [ ] Update `LooperEngine::processBlock()` signature to accept transport info
- [ ] Read `AudioPlayHead::PositionInfo` in `PluginProcessor::processBlock()`
- [ ] Pass transport info from Processor → Engine
- [ ] Add `SyncMode` enum, `syncMode` atomic, `setSyncMode()`, `getSyncMode()`
- [ ] Add `isSyncAvailable()` method
- [ ] Add `sync` parameter to APVTS (version 2)
- [ ] Wire parameter to engine
- [ ] Verify plugin loads without regression (all existing tests pass)

#### Sprint 2: Quantization Logic (3-4 hrs)

- [ ] Add `ppqAtRecordStart`, `ppqTargetStop`, `pendingQuantizedStop` members
- [ ] Modify `startRecording()` to store PPQ start position
- [ ] Implement `requestQuantizedStop()` — calculates target bar boundary in PPQ space
- [ ] Implement `finalizeQuantizedStop()` — applies crossfade, sets loop length
- [ ] Modify `processBlock` to check pending stop each buffer (sample-accurate boundary)
- [ ] Implement `processRecordingPartial()` and `processPlaybackPartial()` for split-buffer handling
- [ ] Apply 5ms linear crossfade at the quantized splice point
- [ ] Handle extension case (quantized > raw): continue recording silence/zero-fill with crossfade
- [ ] Handle truncation case (quantized < raw): trim with crossfade
- [ ] Modify `onRecordButtonPressed()` to call `requestQuantizedStop()` when sync is on
- [ ] Unit test: quantization math at various tempos (60, 120, 180 BPM)
- [ ] Unit test: various time signatures (4/4, 3/4, 6/8, 7/8)
- [ ] Unit test: edge cases (0.5 bars, 1.0 bars exactly, 1.5 bars, 4.0 bars)

#### Sprint 3: UI & Integration (2-3 hrs)

- [ ] Add sync toggle to settings menu (gear icon)
- [ ] Add SYNC LED to the UI (near existing LEDs)
- [ ] Add optional BPM display (small text, toggle in settings)
- [ ] Show "Waiting..." or visual indicator when pendingQuantizedStop is active
- [ ] Gray out sync option when no host tempo is available
- [ ] Persist sync preference via APVTS state save/restore
- [ ] Disable sync when half-speed mode is active (show notification)

#### Sprint 4: Testing & Polish (2-3 hrs)

- [ ] Test in Gig Performer (primary user host)
- [ ] Test in Logic Pro
- [ ] Test in Reaper
- [ ] Test in Renoise (Linux)
- [ ] Test standalone mode (sync should be unavailable/grayed)
- [ ] Test: record 1 bar, 2 bars, 4 bars at 120 BPM — loop length should be exact
- [ ] Test: record ~1.5 bars — verify quantization to 2 bars
- [ ] Test: record ~0.7 bars — verify quantization to 1 bar
- [ ] Test: change tempo mid-recording — verify correct quantization
- [ ] Test: very slow tempo (60 BPM) — long bars still work
- [ ] Test: very fast tempo (200 BPM) — short bars still work
- [ ] Test: 3/4 and 6/8 time signatures
- [ ] Test: toggle sync on/off mid-session
- [ ] Test: sync + reverse playback
- [ ] Test: sync + overdub (overdub should use existing quantized length)
- [ ] Verify pluginval passes
- [ ] Run ThreadSanitizer build

---

### Phase 2 Roadmap (Future)

| Feature | Description | Complexity |
|---------|-------------|------------|
| Quantized Record Start | Wait for bar boundary before recording begins (pre-roll) | Medium |
| Beat-level Quantization | Snap to beats, not just bars (`SyncMode::Beats`) | Low |
| Transport Sync | Start/stop loop with host play/stop | Medium |
| Half-Speed + Sync | Properly handle the 2x sample relationship | Medium |
| Tempo Tracking | Time-stretch loop when host tempo changes | High |
| Visual Countdown | Show beats-until-bar during pre-roll | Low |
| Configurable Rounding | User chooses round-up vs round-down vs nearest | Low |

---

### Files to Modify (Phase 1)

| File | Changes |
|------|---------|
| `Source/LooperEngine.h` | Add `HostTransportInfo` struct, `SyncMode` enum, quantization state members, new method signatures |
| `Source/LooperEngine.cpp` | Implement quantized stop logic, split-buffer processing, crossfade at splice |
| `Source/PluginProcessor.h` | (minimal — possibly add cached transport for debugging) |
| `Source/PluginProcessor.cpp` | Read `AudioPlayHead`, construct `HostTransportInfo`, pass to engine; add `sync` APVTS parameter |
| `Source/PluginEditor.h` | Add sync LED member, BPM display label |
| `Source/PluginEditor.cpp` | Settings menu sync toggle, SYNC LED rendering, BPM display, pending-stop visual indicator |
| `CMakeLists.txt` | No changes expected |

---

### Verification Notes (Post-Review)

#### All callers of `stopRecording()` must be sync-aware

There are 4 call sites for `stopRecording()` in the current codebase:

| Call Site | Context | Sync Behavior |
|-----------|---------|---------------|
| `onRecordButtonPressed()` (line 134) | Recording → stop, start playback | **Quantize** — this is the primary use case |
| `onPlayButtonPressed()` (line 179) | Recording → stop (abort) | **Immediate** — Play=Stop is an intentional abort |
| `onOnceButtonPressed()` (line 230) | Recording → stop, play once | **Immediate** — Once is a mode switch, not a "finish recording" |
| `processRecording()` (line 493) | Buffer overflow safety stop | **Immediate** — emergency/safety |

Only the Record button path should use quantized stop. The others should bypass sync and stop immediately, even when sync is enabled.

#### Thread Safety: `requestQuantizedStop()` cannot access transport directly

`onRecordButtonPressed()` is called from the UI thread (or parameter listener thread), but transport info is only valid inside `processBlock()` on the audio thread. Therefore:

1. `onRecordButtonPressed()` should **only set a flag**: `pendingQuantizedStop.store(true)`
2. The *next* `processBlock()` call reads transport info, computes `ppqTargetStop`, and begins checking for the boundary each buffer.

This is the same pattern the codebase already uses for `shouldDisableOnce` — a flag set by one thread, processed by another. It fits naturally.

#### `processAudioThreadRequests()` interaction

The current `processAudioThreadRequests()` (called from processBlock) handles the `shouldDisableOnce` flag. The pending quantized stop is a separate concern but uses the same pattern. No conflict — but if Once mode triggers during a pending quantized stop (e.g., user enables Once while waiting for bar boundary), the Once logic should wait until after the quantized stop completes. The state machine handles this naturally: pendingQuantizedStop only applies during Recording state, and Once mode auto-stop only applies during Playing state.

#### `stateTransitionInProgress` guard

The pending stop flag doesn't conflict with the `stateTransitionInProgress` atomic guard. The flag is set inside the existing guarded `onRecordButtonPressed()` method. The actual stop happens inside `processBlock()`, which doesn't use the guard (it reads state atomically). No deadlock or race condition.

---

### Open Questions for Discussion

1. **Naming:** Should the UI say "Sync", "Tempo Sync", or "Host Sync"? Recommendation: "Sync" for simplicity, matching the hardware Boomerang's labeling.

2. **Rounding policy for MVP:** Always round to nearest bar? Or always round up (never truncate user's recording)? Round-up is safer for the user but may add more silence.

3. **Visual feedback during pending stop:** Flash the Record LED differently? Show a brief "Syncing..." text? Or keep it invisible and just let the loop snap?

4. **Should sync be on by default?** Recommendation: Off by default, since it changes existing behavior.

5. **Parameter version migration:** Bumping from version 1 to 2 — should we handle migration of existing presets, or is it acceptable for the sync parameter to default to Off on existing sessions?
