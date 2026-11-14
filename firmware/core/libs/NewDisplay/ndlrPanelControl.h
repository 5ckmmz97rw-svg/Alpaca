#ifndef LIBS_NEWDISPLAY_NDLRPANELCONTROL_H_
#define LIBS_NEWDISPLAY_NDLRPANELCONTROL_H_

// NDLR front-panel view for the Tracker Mini (800x480 FT812 screen).
//
// The control only draws. All state lives in strNdlrPanelView (filled in by the
// ndlrPanel module) so this file knows nothing about MIDI or navigation.
//
// Drawing budget: every control's display list must fit in one 8192-byte RAM_G
// slot and the sum of all visible controls must stay under 8172 bytes per frame
// (see cDisplay::step3). The panel therefore only draws the labels that matter
// for the current mode, and the info text is capped (see kNdlr* limits below).

#ifdef TRACKER_MINI

#include "displayControls.h"

#include <stdint.h>

//--------------------------------------------------------------------
// Physical NDLR controls, in the order the module navigates them.
enum enNdlrControl : uint8_t
{
    ndlrEnc1,   // Pad: position / strum
    ndlrEnc2,   // Pad: range / spread
    ndlrEnc3,   // Drone: position / notes / trigger
    ndlrEnc4,   // Tempo / key / mode
    ndlrEnc5,   // Motif: position / patt-len
    ndlrEnc6,   // Motif: variation / pattern
    ndlrEnc7,   // Motif: clk div / rhy-len / gate
    ndlrEnc8,   // Motif: accent / rhythm / offset
    ndlrBtnAll, // ALL play/pause (shift: MOD)
    ndlrBtnMotifSel, // 1/2 motif select (shift: PANIC)
    ndlrChord1, // chord degrees I..VII (shift: chord type)
    ndlrChord2,
    ndlrChord3,
    ndlrChord4,
    ndlrChord5,
    ndlrChord6,
    ndlrChord7,
    ndlrScreenItem, // the NDLR display itself
    ndlrBtnMenu,
    ndlrBtnShift,
    ndlrBtnPad,
    ndlrBtnDrone,
    ndlrBtnMotif1,
    ndlrBtnMotif2,
    ndlrControlCount
};

enum enNdlrKind : uint8_t
{
    ndlrKindEnc,
    ndlrKindBtn,
    ndlrKindChord,
    ndlrKindScreen,
    ndlrKindShift,
    ndlrKindPart,
};

struct strNdlrGeom
{
    int16_t x; // position on the NDLR panel photo (800 x 520 px reference)
    int16_t y;
    uint8_t r; // radius in the same reference
    uint8_t kind;
};

// Positions measured from the front-panel photo in the NDLR Rev2 manual (p.11).
// Screen position = 6 + ref * 3 / 4 (see ndlrPanelControl.cpp).
inline constexpr strNdlrGeom kNdlrGeom[ndlrControlCount] = {
    { 143, 128, 22, ndlrKindEnc },   { 143, 225, 22, ndlrKindEnc },
    { 143, 325, 22, ndlrKindEnc },   { 143, 425, 22, ndlrKindEnc },
    { 662, 128, 22, ndlrKindEnc },   { 662, 222, 22, ndlrKindEnc },
    { 662, 323, 22, ndlrKindEnc },   { 662, 425, 22, ndlrKindEnc },
    { 268, 90, 10, ndlrKindBtn },    { 535, 90, 10, ndlrKindBtn },
    { 403, 128, 15, ndlrKindChord }, { 493, 163, 15, ndlrKindChord },
    { 535, 257, 15, ndlrKindChord }, { 497, 350, 15, ndlrKindChord },
    { 307, 350, 15, ndlrKindChord }, { 268, 252, 15, ndlrKindChord },
    { 305, 160, 15, ndlrKindChord }, { 403, 243, 0, ndlrKindScreen },
    { 403, 322, 10, ndlrKindBtn },   { 403, 385, 16, ndlrKindShift },
    { 258, 433, 10, ndlrKindPart },  { 313, 433, 10, ndlrKindPart },
    { 492, 433, 10, ndlrKindPart },  { 547, 433, 10, ndlrKindPart },
};

//--------------------------------------------------------------------
// Everything the control needs to draw one frame.
constexpr uint8_t kNdlrInfoLines = 18;
constexpr uint8_t kNdlrInfoLineLen = 30;
constexpr uint8_t kNdlrDescLines = 3;
constexpr uint8_t kNdlrDescLineLen = 112;

enum enNdlrInfoLineKind : uint8_t
{
    ndlrLineTitle,  // control name
    ndlrLineHeader, // row heading, e.g. "PRESS / TURN"
    ndlrLineText,   // row text, inactive mode
    ndlrLineActive, // row text, active mode
    ndlrLineNow,    // live state readout
};

struct strNdlrPanelView
{
    uint8_t selected;  // enNdlrControl
    uint8_t shift;     // shift held or latched
    uint8_t menuMode;  // 0 normal, 1 menu, 2 editor
    uint8_t partOn[4]; // pad, drone, motif 1, motif 2
    uint8_t modRing[4];
    uint8_t motif;     // 1 or 2: motif selected by the 1/2 button
    uint8_t activeEnc; // 0..7, encoder shown on the NDLR screen
    uint8_t encLabel[8];
    uint8_t chordDegree; // 0..6, drawn highlighted
    uint8_t typePending;
    char keyMode[20];  // "C - MAJOR"
    char chordText[12]; // "IV F"
    char typeText[8];   // "triad"
    char bpmText[12];   // "90 BPM"
    char toast[32];
    uint8_t infoCount;
    uint8_t infoKind[kNdlrInfoLines];
    char info[kNdlrInfoLines][kNdlrInfoLineLen];
    uint8_t descCount;
    char desc[kNdlrDescLines][kNdlrDescLineLen];
};

//--------------------------------------------------------------------
class cNdlrPanelControl : public cDisplayControl
{
  public:
    cNdlrPanelControl(strControlProperties* properties = nullptr);
    virtual ~cNdlrPanelControl();

    virtual void update();  // stage 0: panel body, ring, labels
    virtual void update2(); // stage 1: controls, selection, NDLR screen
    virtual void update3(); // stage 2: info column, description, toast

    virtual void setStyle(uint32_t style);
    virtual void setText(const char* text);
    virtual void setValue(int value);
    virtual void setColors(const uint32_t* colors);
    virtual void setData(void* data);

  private:
    const strNdlrPanelView* view = nullptr;
};

#endif // TRACKER_MINI
#endif /* LIBS_NEWDISPLAY_NDLRPANELCONTROL_H_ */
