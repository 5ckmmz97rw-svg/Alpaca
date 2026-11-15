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
    ndlrBtnRecord,
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
    ndlrKindRecord,
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
    { 768, 495, 0, ndlrKindRecord },
};

//--------------------------------------------------------------------
// Everything the control needs to draw one frame.
inline constexpr uint8_t kNdlrEncLabelCount[] = { 2, 2, 3, 5, 2, 2, 3, 3 };
constexpr int kNdlrFontSmall = 4;
constexpr int kNdlrFontNormal = 0;
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

//--------------------------------------------------------------------
// The sub screen at the top of the right-hand column. It follows the dial in use
// (Pad, Drone, Tempo-Key-Mode or Motif) and, in Editor mode, shows the open editor.
enum enNdlrSubKind : uint8_t
{
    ndlrSubPad,
    ndlrSubDrone,
    ndlrSubTkm,
    ndlrSubMotif,
    ndlrSubPattern, // Pattern Editor
    ndlrSubRhythm,  // Rhythm Editor
    ndlrSubChordSeq // Chord Sequencer
};

enum enNdlrBarState : uint8_t
{
    ndlrBarDim,      // outside the pattern or rhythm length
    ndlrBarNormal,
    ndlrBarPlaying,
    ndlrBarSelected, // the step or beat an editor is on
    ndlrBarRest,
    ndlrBarTie,
};

constexpr uint8_t kNdlrSubTexts = 12;
constexpr uint8_t kNdlrSubTextLen = 24;
constexpr int kNdlrSubHeight = 112;    // sub screen, in pixels
constexpr int kNdlrEditorHeight = 222; // editor screen, in pixels

// Text slots by kind (unused slots are left empty):
//   Pad      0 title, 1 synth, 2 lowest note, 3 middle, 4 highest, 5 note count, 6 spread, 7 strum
//   Drone    0 title, 1 synth, 2 "KEY"/"CHORD", 3 note names
//   T-K-M    0 title, 1 mode, 2 key, 3 tempo, 4 note names, 5 quantise, 6 clock
//   Motif    0 title, 1 synth, 2 pattern no., 3 clock divide, 4 offset, 5 rhythm no., 6 accent,
//            7 notes:beats, 8 octave, 9 variation, 10 gate
//   Pattern/Rhythm Editor  0 title, 1 motif, 2-9 the eight dial values, 10 note type, 11 notes:beats
//   Chord Sequencer        0 title, 2 load no., 3 duration, 4 key, 5 mode, 6 chord, 7 type,
//            8 save no., 9 armed, 10 playing
struct strNdlrSubView
{
    uint8_t kind; // enNdlrSubKind
    char text[kNdlrSubTexts][kNdlrSubTextLen];
    uint16_t hiMask; // text slots drawn highlighted; bits 12-14 highlight the Drone's three rows
    uint16_t onMask; // text slots drawn green
    uint8_t selCell; // editor: dial whose cell is framed, 0xff for none

    uint8_t barA[16]; // pattern bars: height in pixels. Pad: note position, 0-154
    uint8_t barAState[16];
    uint8_t countA;
    uint8_t barB[32]; // rhythm beats: velocity 0-127
    uint8_t barBState[32];
    uint8_t countB; // rhythm length

    uint8_t a; // Drone: octave 1-4
    uint8_t b; // Drone: bit 0 root, bit 1 fifth, bit 2 octave
    uint8_t c; // Drone: the four beats it retriggers on, bit 0 first
    uint8_t marks[12]; // T-K-M, per note from C: 0 not in key, 1 in key, 2 in the chord, 3 key root

    uint8_t steps[18];     // Chord Sequencer: 0 empty, 1 used, 2 selected, 3 playing
    char slotText[8][8];   // "Ax2"
    uint8_t slotState[8];  // 0 empty, 1 used, 2 playing
    uint8_t field;         // selected song field: slot * 2, +1 for its repeats
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
    int16_t encAngle[8]; // clockwise from vertical, in tenths of a degree
    uint8_t chordDegree; // 0..6, drawn highlighted
    uint8_t queuedChord; // 1..7, zero means none
    uint8_t pendingPartToggles;
    uint8_t typePending;
    bool recording;
    char keyMode[20];  // "C MAJOR"
    char detailText[16];
    char chordText[12]; // "IV F"
    char typeText[8];   // "triad"
    char bpmText[12];   // "90 BPM"
    char toast[32];
    uint8_t infoCount;
    uint8_t infoKind[kNdlrInfoLines];
    char info[kNdlrInfoLines][kNdlrInfoLineLen];
    uint8_t descCount;
    char desc[kNdlrDescLines][kNdlrDescLineLen];
    uint8_t editor; // in Editor mode: the dial (0-7) whose editor is open
    strNdlrSubView sub;
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
