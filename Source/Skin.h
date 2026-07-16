#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>

//==============================================================================
/**
    A skin bundles everything the editor needs for one look:
    a background image, the control/LED layout, and a palette.

    All layout values are in base 700x200 coordinates; the editor scales
    them to the current window size.
*/
struct Skin
{
  juce::String name;
  juce::Image  background;

  // Layout (base 700x200 coords). Footswitch/LED order: record, play, once, reverse, stack.
  juce::Rectangle<int>                thruMuteRect;
  juce::Rectangle<int>                volumeRect;
  std::array<juce::Rectangle<int>, 5> footswitchRects;
  std::array<juce::Point<int>, 5>     ledPositions;
  juce::Point<int>                    slowLedPosition;
  int                                 ledSize = 10;

  // Thru Mute engaged indicator. The classic hardware had a hard switch with
  // no LED, so the classic skin deliberately leaves this off.
  bool             hasThruMuteLed = false;
  juce::Point<int> thruMuteLedPosition;

  // Palette
  std::array<juce::Colour, 5> ledColours;
  juce::Colour                slowLedColour;
  juce::Colour                thruMuteLedColour;
  juce::Colour                volumeThumbColour;

  bool roundFootswitches     = false;  // pressed overlay: ellipse instead of rect inset shadow
  bool alwaysShowVolumeThumb = false;  // skins that draw their own fader need a visible thumb
};

namespace Skins
{
  int         count();
  const Skin& get (int index);  // index 0 is the default skin
}
