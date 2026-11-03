# Volume roller emulation: design (issue #82)

## Goal

On the Classic skin, replace the stock slider that sits over the photo's OUTPUT LEVEL roller with a drawn emulation of the hardware thumbwheel. Rolling it turns the drum: the knurl and the diagonal row of white dots move, and the point where the dot line leaves the bottom of the window walks along the 0–10 scale printed below it.

## Decisions

| Question | Decision |
|----------|----------|
| Which skins | Classic only. Modern keeps its drawn fader. |
| How the level reads | The dot line's exit point at the window's bottom edge, read against the 0–10 scale. Close is fine; it doesn't need to be exact. |
| Approach | Draw the drum in the editor's `paint()` over the existing (now chrome-less) `juce::Slider`. This is the same "invisible hit area, editor draws the hardware" pattern used by STACK LATCH, ONCE OFF and SLOW. |
| Rejected | Custom `LookAndFeel` (a pattern new to this codebase, and it would have to be swapped on every skin change). Retouched photo plus moving dots (the knurl wouldn't move). Filmstrip (needs an art pipeline, and goes soft when the window is enlarged). |
| Out of scope | The volume curve (#21) and any Modern skin changes. |

## What the user gets

- Classic shows a knurled drum in the roller window with five or so white dots, which dim and squash toward the top and bottom edges as they curve away.
- Dragging up rolls the drum up and raises the level; dragging down lowers it. The surface follows the mouse pixel for pixel.
- Clicking doesn't jump the level, because a thumbwheel only moves when rolled.
- Mouse wheel, host automation and MIDI all still work. The editor repaints at 60 Hz, so the drum follows automation.
- The cyan hover box goes away on Classic.

## Geometry (base 700×200 units, measured from `img/boomerang.jpg`)

- Drum opening: `{ 104, 71, 54, 40 }`. The 0 and 10 ticks are at x = 112 and x = 147.
- Photo dots: about (125,75), (127,81), (129,89), (133,98), (136,107). They're about 8.5 apart along the surface, each stepping about 0.35 right per unit down.
- Knurl: a diamond cross-hatch with a pitch of about 8.

### Model

The drum is a cylinder lying on its side, and the window shows its front ±70°. With a half-height of 20, that gives a radius of 20 / sin 70° ≈ 21.3.

Points on the surface are given as `(x, u)`, where `u` is the distance around the circumference. Rolling slides the surface by `rolled = proportion × travel`. A point appears at angle `a = (u − rolled) / radius`, which puts it at screen `y = centreY + radius × sin a`. It's foreshortened by `cos a`.

Dots sit at `u = i × 8.5` along the line `x = lineX0 + 0.35 × u`. For the bottom-edge crossing to sweep the scale exactly once:

- `travel = scaleWidth / 0.35`, which is ≈ 100 units, or about 270° of drum rotation (a believable pot throw).
- `lineX0 = scaleX0 − 0.35 × radius × 70°`, so the crossing sits on "0" at proportion 0 and on "10" at proportion 1.

Check: at proportion ≈ 0.75 the model puts the dots at y ≈ 74, 81, 89, 97, 105, which matches the photo.

Drag sensitivity uses the same `travel` (scaled to the window size), so a full drag of `travel` pixels covers the whole range and the surface tracks the pointer.

### Rendering order (clipped to the drum opening, drawn in base units through a scale transform)

1. Near-black drum body.
2. Knurl: two families of diagonal grooves, each sampled down the visible arc so it curves with the drum.
3. Dots: off-white ellipses, height and alpha scaled by `cos a`.
4. Cylinder shading: a vertical gradient that's dark at the top and bottom and clear just above centre, so light seems to come from above.

The photo's own window frame stays visible around the drawn drum.

## Code touch points

- `Source/Skin.h`: `hasVolumeRoller`, `rollerWindow`, `rollerScaleX`.
- `Source/Skins.cpp`: Classic sets them, and drops its now-unused `volumeThumbColour`.
- `Source/PluginEditor.h/.cpp`:
  - A `Roller` constants namespace.
  - `drawVolumeRoller(g, scale, proportion)`.
  - `setSkin()` hides the slider chrome and turns off snap-to-mouse for roller skins.
  - `resized()` sets the drag sensitivity.
  - `paint()` draws the roller instead of the thumb.

## Testing

There's no automated suite. Verify with a build, pluginval (`make validate`), and a standalone run under an isolated `HOME`:
- Screenshot the drum at volumes 0, about 0.75 and 1, and compare with the photo.
- Drag, scroll the mouse wheel, and resize the window by hand. Scripted calls don't reproduce real mouse behaviour (see AGENTS.md).
