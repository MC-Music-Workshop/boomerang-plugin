#include "Skin.h"
#include "BinaryData.h"

//==============================================================================
// Classic skin: the Turrama faceplate photo with the original overlay layout.

static Skin makeClassicSkin()
{
  Skin s;
  s.name       = "Classic";
  s.background = juce::ImageFileFormat::loadFrom (BinaryData::turrama_jpg,
                                                  BinaryData::turrama_jpgSize);

  s.thruMuteRect = { 100, 20, 50, 30 };
  s.volumeRect   = { 70, 60, 120, 90 };

  for (int i = 0; i < 5; ++i)
    s.footswitchRects[i] = { 190 + 94 * i, 155, 30, 23 };

  s.ledPositions    = {{ { 208, 44 }, { 300, 44 }, { 393, 44 }, { 485, 44 }, { 579, 44 } }};
  s.slowLedPosition = { 579, 27 };
  s.ledSize         = 10;

  s.ledColours.fill (juce::Colours::green);
  s.slowLedColour     = juce::Colours::orange;
  s.volumeThumbColour = juce::Colours::cyan;
  return s;
}

//==============================================================================
// Modern "Turrama" skin (default): procedurally rendered dark faceplate.
// The art is drawn from the same layout table the editor uses for hit areas,
// so controls and artwork can never drift apart.

static void drawModernFootswitch (juce::Graphics& g, float cx, float cy)
{
  // recessed bezel
  g.setColour (juce::Colour (0xff0d0e11));
  g.fillEllipse (cx - 22, cy - 22, 44, 44);

  // brushed-steel cap
  g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a616b), cx, cy - 18,
                                           juce::Colour (0xff30353c), cx, cy + 18, false));
  g.fillEllipse (cx - 18, cy - 18, 36, 36);
  g.setColour (juce::Colour (0x30ffffff));
  g.drawEllipse (cx - 17, cy - 17, 34, 34, 1.0f);

  // inner cap
  g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d434b), cx, cy - 12,
                                           juce::Colour (0xff4a515a), cx, cy + 12, false));
  g.fillEllipse (cx - 12, cy - 12, 24, 24);
}

static void drawLedBezel (juce::Graphics& g, float cx, float cy)
{
  g.setColour (juce::Colour (0xff0a0b0d));
  g.fillEllipse (cx - 7, cy - 7, 14, 14);
  g.setColour (juce::Colour (0x28ffffff));
  g.drawEllipse (cx - 7, cy - 7, 14, 14, 1.0f);
}

static juce::Image renderModernBackground (const Skin& s)
{
  const int ss = 2;  // supersample so the art stays crisp at 2x window scale
  juce::Image img (juce::Image::ARGB, 700 * ss, 200 * ss, true);
  juce::Graphics g (img);
  g.addTransform (juce::AffineTransform::scale ((float) ss));

  const juce::Colour gold      (0xffd9a441);
  const juce::Colour labelText (0xccd8dce2);
  const juce::Colour dimText   (0x8896a0ac);

  // Faceplate
  g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23262c), 0.0f, 0.0f,
                                           juce::Colour (0xff121418), 0.0f, 200.0f, false));
  g.fillRect (0, 0, 700, 200);
  g.setColour (juce::Colour (0x18ffffff));
  g.fillRect (0, 0, 700, 2);    // top edge catch-light
  g.setColour (juce::Colour (0x66000000));
  g.fillRect (0, 198, 700, 2);  // bottom edge shadow

  // Wordmark
  const auto sans      = juce::Font::getDefaultSansSerifFontName();
  const auto labelFont = juce::Font (juce::FontOptions (sans, 9.0f, juce::Font::bold));
  const auto tinyFont  = juce::Font (juce::FontOptions (sans, 7.5f, juce::Font::plain));

  g.setColour (gold);
  g.setFont (juce::Font (juce::FontOptions (sans, 27.0f, juce::Font::bold)));
  g.drawText ("TURRAMA", 24, 6, 230, 28, juce::Justification::centredLeft);
  g.setColour (dimText);
  g.setFont (juce::Font (juce::FontOptions (sans, 9.0f, juce::Font::plain)));
  g.drawText ("P H R A S E   S A M P L E R", 26, 34, 230, 10, juce::Justification::centredLeft);
  g.setColour (gold.withAlpha (0.35f));
  g.fillRect (24, 48, 132, 1);

  // Divider between the level section and the switch deck
  g.setColour (juce::Colour (0x16ffffff));
  g.fillRect (160, 58, 1, 128);

  // Thru Mute pad, with an engaged-state LED on its left
  {
    auto r = s.thruMuteRect.toFloat();
    g.setColour (juce::Colour (0xff2c3037));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colour (0x2effffff));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    drawLedBezel (g, (float) s.thruMuteLedPosition.x, (float) s.thruMuteLedPosition.y);
    g.setColour (labelText);
    g.setFont (labelFont);
    g.drawText ("THRU MUTE", s.thruMuteRect.withTrimmedLeft (22), juce::Justification::centred);
  }

  // Output level fader (track endpoints match the editor's thumb travel)
  {
    const float cx     = (float) s.volumeRect.getCentreX();
    const float top    = (float) s.volumeRect.getY() + 5.0f;
    const float bottom = (float) s.volumeRect.getBottom() - 5.0f;

    g.setColour (juce::Colour (0xff0b0c0f));
    g.fillRoundedRectangle (cx - 3.5f, top, 7.0f, bottom - top, 3.5f);
    g.setColour (juce::Colour (0x22ffffff));
    g.drawRoundedRectangle (cx - 3.5f, top, 7.0f, bottom - top, 3.5f, 1.0f);

    g.setColour (dimText);
    g.setFont (tinyFont);
    for (int i = 0; i <= 4; ++i)
      g.fillRect (cx - 16.0f, top + (bottom - top) * (float) i / 4.0f, 8.0f, 1.0f);

    g.drawText ("10", (int) cx - 34, (int) top - 4,    16, 8, juce::Justification::centredRight);
    g.drawText ("0",  (int) cx - 34, (int) bottom - 4, 16, 8, juce::Justification::centredRight);
    g.drawText ("OUTPUT LEVEL",
                s.volumeRect.getX() - 15, s.volumeRect.getBottom() - 2,
                s.volumeRect.getWidth() + 30, 9, juce::Justification::centred);
  }

  // Footswitches, labels, and LED bezels
  const char* labels[5] = { "RECORD", "PLAY (STOP)", "ONCE", "DIRECTION", "STACK (SPEED)" };
  for (int i = 0; i < 5; ++i)
  {
    auto r = s.footswitchRects[i];
    drawModernFootswitch (g, (float) r.getCentreX(), (float) r.getCentreY());
    g.setColour (labelText);
    g.setFont (labelFont);
    g.drawText (labels[i], r.getCentreX() - 45, r.getBottom() + 9, 90, 10, juce::Justification::centred);
    drawLedBezel (g, (float) s.ledPositions[i].x, (float) s.ledPositions[i].y);
  }

  drawLedBezel (g, (float) s.slowLedPosition.x, (float) s.slowLedPosition.y);
  g.setColour (dimText);
  g.setFont (tinyFont);
  g.drawText ("SLOW", s.slowLedPosition.x - 20, s.slowLedPosition.y + 10, 40, 8, juce::Justification::centred);

  return img;
}

static Skin makeModernSkin()
{
  Skin s;
  s.name = "Turrama";

  s.thruMuteRect = { 30, 55, 95, 26 };
  s.volumeRect   = { 40, 90, 75, 95 };

  for (int i = 0; i < 5; ++i)
  {
    s.footswitchRects[i] = { 183 + 105 * i, 113, 44, 44 };
    s.ledPositions[i]    = { 205 + 105 * i, 98 };
  }
  s.slowLedPosition = { 668, 98 };
  s.ledSize         = 10;

  s.hasThruMuteLed      = true;
  s.thruMuteLedPosition = { 44, 68 };  // inside the pad's left edge

  s.ledColours = {{ juce::Colour (0xffff4545),    // record: red
                    juce::Colour (0xff3ddc55),    // play: green
                    juce::Colour (0xff4aa8ff),    // once: blue
                    juce::Colour (0xffc06aff),    // direction: purple
                    juce::Colour (0xffffa63d) }}; // stack: orange
  s.slowLedColour         = juce::Colour (0xffffc94a);
  s.thruMuteLedColour     = juce::Colour (0xffffd24a);  // yellow: direct signal is muted
  s.volumeThumbColour     = juce::Colour (0xffd9a441);
  s.roundFootswitches     = true;
  s.alwaysShowVolumeThumb = true;

  s.background = renderModernBackground (s);
  return s;
}

//==============================================================================
namespace Skins
{
  static const std::array<Skin, 2>& registry()
  {
    static const std::array<Skin, 2> skins { makeModernSkin(), makeClassicSkin() };
    return skins;
  }

  int count() { return (int) registry().size(); }

  const Skin& get (int index)
  {
    return registry()[(size_t) juce::jlimit (0, count() - 1, index)];
  }
}
