# Volume Roller Emulation Implementation Plan (issue #82)

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** On the Classic skin, replace the stock slider over the photo's OUTPUT LEVEL roller with a drawn thumbwheel. The knurl and the diagonal white dots roll with the volume, and the dot line's exit at the window's bottom edge reads against the 0–10 scale.

**Architecture:** The existing `volumeSlider` stays. It keeps the APVTS attachment, automation and mouse wheel, but loses its visible chrome and switches to relative drag on roller skins. The editor's `paint()` draws a projected cylinder (drum, knurl, dots, shading) over the photo's drum opening. This is the same invisible-control, editor-draws-hardware pattern as STACK LATCH, ONCE OFF and SLOW. Layout values live in `Skin`. Modern is untouched.

**Tech Stack:** C++17, JUCE (`juce::Graphics`, `juce::Slider`), CMake.

**Design:** `docs/plans/2026-11-03-volume-roller-design.md` (read it first; it has the geometry derivation).

**House rules (AGENTS.md):** use absolute paths in commands, align `=` across similar lines, and match each file's indentation (`PluginEditor.*` uses 4 spaces, `Skin.h` / `Skins.cpp` use 2). Keep the change minimal: no refactors of neighbouring code. There's no unit test suite, so each task's "test" is a compile, a screenshot, or a hands-on check.

`$WT` below means this worktree: `/Users/maximiliancascone/github/boomerang-plugin/.claude/worktrees/volume-roller-plan`.

---

### Task 0: Make the worktree buildable

**Step 1: Check out the JUCE submodule (it's empty in a fresh worktree)**

Run: `git -C $WT submodule update --init JUCE`
Expected: `JUCE/modules` exists. This should be quick, because objects are shared with the main checkout.

**Step 2: Configure a single-arch build (faster than `./build.sh`, which builds universal)**

Run: `cmake -S $WT -B $WT/build -DCMAKE_OSX_ARCHITECTURES=arm64`
Expected: configures without errors and prints the pluginval path if it's installed.

**Step 3: Build the standalone once to get a baseline**

Run: `cmake --build $WT/build -j8 --target Boomerang_Standalone`
Expected: `$WT/build/Boomerang_artefacts/Standalone/Boomerang+.app` exists.

Note: `COPY_PLUGIN_AFTER_BUILD` installs VST3/AU builds over the user's installed plugins. Build only the Standalone target until Task 5.

---

### Task 1: Skin layout fields

**Files:**
- Modify: `Source/Skin.h` (after `volumeRect`, around line 22; the palette block is around line 38)
- Modify: `Source/Skins.cpp` (`makeClassicSkin()`, lines 7–34)

**Step 1: Add the fields to `Skin`** (after the `latchSlot` / legend block, before `// Palette`):

```cpp
  // OUTPUT LEVEL thumbwheel drawn over the panel (issue #82). Skins without one
  // show the slider + volume thumb instead.
  bool                   hasVolumeRoller = false;
  juce::Rectangle<float> rollerWindow;  // drum opening in the faceplate
  juce::Range<float>     rollerScaleX;  // x of the 0 and 10 ticks printed under it
```

**Step 2: Set them in `makeClassicSkin()`** (after `s.italicLegends`), and delete the Classic line `s.volumeThumbColour = juce::Colours::cyan;`, because Classic no longer draws the thumb:

```cpp
  s.hasVolumeRoller = true;
  s.rollerWindow    = { 104.0f, 71.0f, 54.0f, 40.0f };  // Drum opening in the photo
  s.rollerScaleX    = { 112.0f, 147.0f };               // 0 and 10 ticks below it
```

**Step 3: Compile**

Run: `cmake --build $WT/build -j8 --target Boomerang_Standalone`
Expected: builds. There's no visible change yet.

**Step 4: Commit**

```bash
git -C $WT add Source/Skin.h Source/Skins.cpp
git -C $WT commit -m "Add volume roller layout to Skin; Classic gets its drum window and 0-10 scale (#82)"
```

---

### Task 2: Roller-style slider behaviour

**Files:**
- Modify: `Source/PluginEditor.cpp`:
  - top of file, after the includes: the constants
  - `setSkin()`, around lines 637–658
  - `resized()`, the volume bounds line around line 460

**Step 1: Add the geometry constants** near the top of `PluginEditor.cpp`, after the includes:

```cpp
// OUTPUT LEVEL roller geometry (issue #82), in base 700x200 units, measured off the faceplate photo
namespace Roller
{
    constexpr float visibleHalfAngle = 1.22f;  // the window shows the drum's front ±70°
    constexpr float dotSpacing       = 8.5f;   // between dots, along the drum surface
    constexpr float dotSlope         = 0.35f;  // each dot sits this much right per unit further round
    constexpr float dotSize          = 4.5f;
    constexpr float knurlPitch       = 8.0f;

    // Surface rolled past the window across the full level range: just enough for the
    // dot line's exit point to sweep the printed 0-10 scale once
    inline float travel(const Skin& s) { return s.rollerScaleX.getLength() / dotSlope; }
}
```

**Step 2: In `setSkin()`, hide the chrome for roller skins too and make the drag relative.** Change the `if (skin->alwaysShowVolumeThumb)` condition and its comment, and add the snap line after the if/else:

```cpp
    // Procedural skins draw their own fader and roller skins their own drum, so hide the stock slider chrome there
    if (skin->alwaysShowVolumeThumb || skin->hasVolumeRoller)
    {
        ... unchanged ...
    }
    else
    {
        ... unchanged ...
    }

    // A thumbwheel moves with your finger: clicking it shouldn't jump the level (issue #82)
    volumeSlider.setSliderSnapsToMousePosition(! skin->hasVolumeRoller);
```

**Step 3: In `resized()`, right after `volumeSlider.setBounds(scaleRect(skin->volumeRect));`**, make one drag pixel roll one pixel of drum:

```cpp
    // Rolling the full range takes as many pixels as the drum surface travels, so it tracks the pointer
    if (skin->hasVolumeRoller)
        volumeSlider.setMouseDragSensitivity(juce::jmax(1, juce::roundToInt(Roller::travel(*skin) * scale)));
```

Why this works: in JUCE's `Slider::Pimpl::handleAbsoluteDrag` (`JUCE/modules/juce_gui_basics/widgets/juce_Slider.cpp` around line 772), a `LinearVertical` slider with snap-to-mouse off moves by `mouseDiff / pixelsForFullDragExtent` from the value at mouse-down. Upward drags increase it. Holding Cmd/Alt/Ctrl gives JUCE's velocity (fine-adjust) mode for free.

**Step 4: Build, then check by hand**

Run: `cmake --build $WT/build -j8 --target Boomerang_Standalone`, then launch an isolated instance:

```bash
mkdir -p $WT/build/test-home
HOME=$WT/build/test-home "$WT/build/Boomerang_artefacts/Standalone/Boomerang+.app/Contents/MacOS/Boomerang+" &
echo $!   # note the PID for killing later
```

Switch to Classic (gear, then Skin, then Classic). Expected:
- No slider track or thumb is drawn over the photo. The cyan box still shows on hover until Task 3.
- Clicking doesn't change the level.
- Dragging up and down changes it relatively.

Then switch back to Modern. Expected: clicking the fader jumps to that position, as before.

Kill the instance with `kill <PID>`.

**Step 5: Commit**

```bash
git -C $WT add Source/PluginEditor.cpp
git -C $WT commit -m "Classic volume control rolls like a thumbwheel: relative drag, no click-to-jump, hidden slider chrome (#82)"
```

---

### Task 3: Draw the roller

**Files:**
- Modify: `Source/PluginEditor.h` (private helpers, next to `drawStackLatchSwitch` around line 106)
- Modify: `Source/PluginEditor.cpp`:
  - `paint()`: the thumb block around lines 339–361, and the extension draws around line 386
  - new function after `drawMiniButton`

**Step 1: Declare the helper** in `PluginEditor.h`, next to the other draw helpers:

```cpp
    void drawVolumeRoller(juce::Graphics& g, float scale, float proportion);
```

**Step 2: Implement it** in `PluginEditor.cpp` after `drawMiniButton(...)`:

```cpp
// OUTPUT LEVEL thumbwheel seen through the faceplate window (issue #82).
// The drum is a cylinder on its side; the window shows its front face. Surface points are
// (x, u) with u running round the circumference: a point shows at angle a = (u - rolled) / radius,
// i.e. at y = centre + radius * sin(a), foreshortened by cos(a). The dots sit on a diagonal
// line, so as the drum rolls, the point where that line leaves the window's bottom edge
// walks along the 0-10 scale printed below it.
void BoomerangAudioProcessorEditor::drawVolumeRoller(juce::Graphics& g, float scale, float proportion)
{
    const auto  win    = skin->rollerWindow;
    const float radius = win.getHeight() * 0.5f / std::sin(Roller::visibleHalfAngle);
    const float arc    = radius * Roller::visibleHalfAngle;  // surface visible either side of centre
    const float rolled = proportion * Roller::travel(*skin);  // surface at the window's centre line
    const float lineX0 = skin->rollerScaleX.getStart() - Roller::dotSlope * arc;  // dot line's x at u = 0, so it exits at "0" when rolled = 0

    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::scale(scale));  // draw in base 700x200 units
    g.reduceClipRegion(win.toNearestInt());

    // Drum body
    g.setColour(juce::Colour(0xff060606));
    g.fillRect(win);

    // Knurl: two families of diagonal grooves, sampled down the visible arc so they bend with the drum
    constexpr int samples = 16;
    const int firstGroove = static_cast<int>(std::floor((rolled - arc - win.getWidth()) / Roller::knurlPitch));
    const int lastGroove  = static_cast<int>(std::ceil ((rolled + arc + win.getWidth()) / Roller::knurlPitch));

    g.setColour(juce::Colour(0xff5c5c5c));
    for (int n = firstGroove; n <= lastGroove; ++n)
    {
        for (float dir : { 1.0f, -1.0f })
        {
            juce::Path groove;
            for (int i = 0; i <= samples; ++i)
            {
                const float a = Roller::visibleHalfAngle * (2.0f * static_cast<float>(i) / samples - 1.0f);
                const float u = rolled + radius * a;
                const juce::Point<float> p(win.getX() + dir * (u - static_cast<float>(n) * Roller::knurlPitch),
                                           win.getCentreY() + radius * std::sin(a));
                if (i == 0) groove.startNewSubPath(p);
                else        groove.lineTo(p);
            }
            g.strokePath(groove, juce::PathStrokeType(0.7f));
        }
    }

    // Dots, squashed and dimmed as they turn away
    const int firstDot = static_cast<int>(std::ceil ((rolled - arc) / Roller::dotSpacing));
    const int lastDot  = static_cast<int>(std::floor((rolled + arc) / Roller::dotSpacing));
    for (int i = firstDot; i <= lastDot; ++i)
    {
        const float u      = static_cast<float>(i) * Roller::dotSpacing;
        const float a      = (u - rolled) / radius;
        const float facing = std::cos(a);
        const juce::Point<float> centre(lineX0 + Roller::dotSlope * u, win.getCentreY() + radius * std::sin(a));

        g.setColour(juce::Colour(0xfff2f0e6).withAlpha(0.4f + 0.6f * facing));
        g.fillEllipse(juce::Rectangle<float>(Roller::dotSize, Roller::dotSize * facing).withCentre(centre));
    }

    // Cylinder shading: top and bottom turn away into shadow; lit from just above centre
    juce::ColourGradient shade(juce::Colours::black.withAlpha(0.85f), 0.0f, win.getY(),
                               juce::Colours::black.withAlpha(0.85f), 0.0f, win.getBottom(), false);
    shade.addColour(0.45, juce::Colours::transparentBlack);
    g.setGradientFill(shade);
    g.fillRect(win);
}
```

**Step 3: Wire it into `paint()`**
- Stop drawing the thumb on roller skins. Change the thumb block's condition to:
  ```cpp
  if (! skin->hasVolumeRoller
      && (skin->alwaysShowVolumeThumb || volumeSlider.isMouseOver() || volumeSlider.isMouseButtonDown()))
  ```
- Draw the roller right before `if (stackLatchButton.isVisible())`, where `scale` is already in scope:
  ```cpp
  if (skin->hasVolumeRoller)
      drawVolumeRoller(g, scale, static_cast<float>(volumeSlider.valueToProportionOfLength(volumeSlider.getValue())));
  ```

  `valueToProportionOfLength` keeps the drum on the slider's position if #21 later adds a skew.

**Step 4: Build and screenshot**

Build as in Task 2, launch the isolated instance, and switch to Classic. To screenshot just the plugin window:

```bash
osascript -e 'tell application "System Events" to get {position, size} of window 1 of (first process whose unix id is <PID>)'
screencapture -x -R<x>,<y>,<w>,<h> $WT/build/roller.png
```

Expected:
- A dark knurled drum fills the photo's roller opening, and the photo's grey frame still shows around it.
- Five or six white dots run diagonally from upper-left to lower-right, smaller and dimmer at the top and bottom.
- There's no cyan box.

**Step 5: Commit**

```bash
git -C $WT add Source/PluginEditor.h Source/PluginEditor.cpp
git -C $WT commit -m "Classic skin draws the OUTPUT LEVEL roller: knurled drum and white dots roll with the volume (#82)"
```

---

### Task 4: Tune by eye against the photo

Compare against `img/boomerang.jpg`, zoomed. For example, `magick $WT/img/boomerang.jpg -crop 60x60+95+62 -scale 600% $WT/build/photo-roller.png` puts the photo's roller in the same frame as a 6× crop of your screenshot.

**Checks (close is fine; user-approved tolerance):**
1. **Volume about 0.75:** the dots sit near the photo's (125,75), (127,81), (129,89), (133,98), (136,107) in base units. The model predicts y ≈ 74, 81, 89, 97, 105.
2. **Volume 0:** the dot line leaves the window's bottom edge above the "0" tick (x ≈ 112). The top half of the window may show no dots; that's expected, since the line runs off the left.
3. **Volume 1:** the line leaves the bottom edge above the "10" tick (x ≈ 147).
4. **Rolling:** dragging up moves dots up and the bottom-edge exit point to the right.
5. **Window sizes:** resize the window. The drum stays inside the opening at small and large sizes, and the knurl doesn't turn into mush when small.

**What to tweak:**
- Only the `Roller` constants, the knurl/dot colours, and `rollerWindow` / `rollerScaleX`.
- If the knurl reads too busy, lower the groove alpha (`0xff5c5c5c`, then about `0xff484848`) before changing the pitch.

**Commit** any tuning: `git -C $WT commit -am "Tune volume roller to match the faceplate photo (#82)"`

---

### Task 5: Full check

**Step 1: Hands-on with a real mouse** (scripted calls miss real mouse behaviour; see AGENTS.md). On Classic:
- Dragging rolls relatively, and clicking alone doesn't change the level.
- The mouse wheel rolls it.
- The level persists after closing and reopening the editor.

On Modern, check the fader works exactly as before.

**Step 2: Automation follows.** In a host such as Gig Performer, automate or MIDI-map `volume` and watch the drum roll.

**Step 3: pluginval**

Run: `cmake --build $WT/build -j8 --target validate`
Expected: pluginval passes at strictness 5. This builds and installs the VST3.

**Step 4: Push and open a PR** referencing `Closes #82`. Include before and after screenshots of the Classic roller, and note that Modern is unchanged.
