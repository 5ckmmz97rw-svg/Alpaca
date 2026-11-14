#ifdef TRACKER_MINI

#include "ndlrPanel.h"

#include <stdio.h>
#include <string.h>

#include "core/interfaceMenuPopup.h"
#include "core/songTimer.h"

#include "Platform.h"
#include "Tracker.h"
#include "cr_section_macros.h"
#include "interruptsControl.h"
#include "mtMidi.h"
#include "ndlrPanelControl.h"

cNdlrPanel ndlrPanel;
static cNdlrPanel* P = &ndlrPanel;

//==============================================================================================
// Tuning knobs for the sound side. Synth parts 1-3 listen on virtual MIDI channels 1-3.
//==============================================================================================
static constexpr uint8_t kChPad = 1;
static constexpr uint8_t kChDrone = 2;
static constexpr uint8_t kChMotif1 = 3;
static constexpr uint8_t kChMotif2 = 3; // only three synth parts exist; both motifs share part 3
static constexpr uint8_t kVelPad = 80;
static constexpr uint8_t kVelDrone = 90;
static constexpr uint8_t kVelMotif = 100;

//==============================================================================================
// Music tables
//==============================================================================================
struct strMode
{
    const char* name;
    uint8_t n; // notes per octave
    uint8_t iv[7];
};

// The NDLR also has Tonic 2nds/3rds/4ths/6ths; their note sets are not in the manual,
// so they are left out of this build.
static const strMode kModes[] = {
    { "MAJOR", 7, { 0, 2, 4, 5, 7, 9, 11 } },      { "DORIAN", 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "PHRYGIAN", 7, { 0, 1, 3, 5, 7, 8, 10 } },   { "LYDIAN", 7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "MIXOLYDIAN", 7, { 0, 2, 4, 5, 7, 9, 10 } }, { "MINOR", 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "LOCRIAN", 7, { 0, 1, 3, 5, 6, 8, 10 } },    { "GYPSY MIN", 7, { 0, 2, 3, 6, 7, 8, 11 } },
    { "HARM MIN", 7, { 0, 2, 3, 5, 7, 8, 11 } },   { "MIN PENTA", 5, { 0, 3, 5, 7, 10, 0, 0 } },
    { "WHOLE TONE", 6, { 0, 2, 4, 6, 8, 10, 0 } },
};
static constexpr int kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// Circle of fifths, as the NDLR steps through keys
static const char* const kKeyName[12] = { "C", "G", "D", "A", "E", "B", "F#", "Db", "Ab", "Eb", "Bb", "F" };
static const uint8_t kKeySemi[12] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };
static const char* const kNoteName[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char* const kRomanUpper[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char* const kRomanLower[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };

// Chord types in chord-button order I..VII (as printed on the NDLR with Shift)
enum enChordType : uint8_t
{
    ctTriad,
    ct7th,
    ctSus2,
    ctAlt2,
    ctAlt1,
    ctSus4,
    ct6th,
    ctCount
};
static const char* const kTypeName[ctCount] = { "triad", "7th", "sus2", "alt2", "alt1", "sus4", "6th" };

// alt1 / alt2 resolve to a fixed type per chord degree (NDLR manual, p.13)
static const uint8_t kAlt1[7] = { ctTriad, ct7th, ctTriad, ctTriad, ct7th, ct7th, ct6th };
static const uint8_t kAlt2[7] = { ct7th, ct7th, ctTriad, ct7th, ct7th, ct6th, ct7th };

// Scale-step offsets stacked on the chord root
struct strShape
{
    uint8_t n;
    uint8_t step[4];
};
static const strShape kShape[ctCount] = {
    { 3, { 0, 2, 4, 0 } }, // triad
    { 4, { 0, 2, 4, 6 } }, // 7th
    { 3, { 0, 1, 4, 0 } }, // sus2
    { 4, { 0, 2, 4, 6 } }, // alt2 (replaced by its resolved type)
    { 3, { 0, 2, 4, 0 } }, // alt1 (replaced by its resolved type)
    { 3, { 0, 3, 4, 0 } }, // sus4
    { 4, { 0, 2, 4, 5 } }, // 6th
};

//==============================================================================================
// State
//==============================================================================================
enum enInfoMode : uint8_t
{
    imNormal,
    imShift,
    imMenu,
    imEditor,
    imView,
    imBoot,
};

struct strInfoRow
{
    uint8_t mode;
    const char* text;
};
struct strInfo
{
    const char* name;
    uint8_t rowCount;
    strInfoRow rows[4];
    const char* desc;
};

struct strState
{
    uint8_t sel;
    uint8_t shiftLatched;
    uint8_t shiftHeld;
    uint8_t menuMode;
    uint8_t partOn[4];
    uint8_t modRing[4];
    uint8_t motif; // 1 or 2
    uint8_t activeEnc;
    uint8_t encLabel[8];
    uint8_t degree;
    uint8_t type;        // chord type 0..6
    int8_t pendingType;  // -1 none
    uint16_t tempo;
    uint8_t key;         // index into circle of fifths
    uint8_t mode;
    uint8_t padOct;      // 1..6
    uint8_t droneOct;    // 1..4
    uint8_t motifOct[2]; // 2..6
    uint8_t dirty;
    uint32_t lastRefresh;
    uint32_t toastUntil;
    uint32_t nextTick;
    uint8_t step;
    // sounding notes (-1 = none)
    int16_t padNotes[4];
    uint8_t padCount;
    int16_t droneNote;
    int16_t motifNote[2];
};

static strState s;
__NOINIT(EXTERNAL_RAM) static strNdlrPanelView s_view;

static uint32_t nowMs()
{
    return Tracker::getInstance()->getPlatform()->getTimer()->millis();
}

static bool shiftOn()
{
    return s.shiftLatched || s.shiftHeld;
}

static void toast(const char* msg)
{
    snprintf(s_view.toast, sizeof(s_view.toast), "%s", msg);
    s.toastUntil = nowMs() + 1500;
    s.dirty = 1;
}

//==============================================================================================
// Sound
//==============================================================================================
static void synthNote(bool on, int note, uint8_t velocity, uint8_t channel)
{
    if (note < 0 || note > 127)
    {
        return;
    }
    DISABLE_AUDIO_INTERRUPTS();
    if (on)
    {
        sendMidiNoteOn((uint8_t)note, velocity, channel, true);
    }
    else
    {
        sendMidiNoteOff((uint8_t)note, 0, channel, true);
    }
    ENABLE_AUDIO_INTERRUPTS();
}

static uint8_t resolvedType()
{
    if (s.type == ctAlt1)
        return kAlt1[s.degree];
    if (s.type == ctAlt2)
        return kAlt2[s.degree];
    return s.type;
}

// MIDI note for scale step `stepIdx` (may exceed one octave) in the current key/mode
static int noteForStep(int stepIdx, int baseOctave)
{
    const strMode& m = kModes[s.mode];
    const int oct = stepIdx / m.n;
    const int idx = stepIdx % m.n;
    return 12 * (baseOctave + 1) + kKeySemi[s.key] + m.iv[idx] + 12 * oct;
}

static int buildChord(int baseOctave, int16_t* out, int maxNotes)
{
    const strShape& sh = kShape[resolvedType()];
    int n = 0;
    for (int i = 0; i < sh.n && n < maxNotes; i++)
    {
        out[n++] = (int16_t)noteForStep(s.degree + sh.step[i], baseOctave);
    }
    return n;
}

static void releasePad()
{
    for (int i = 0; i < s.padCount; i++)
    {
        synthNote(false, s.padNotes[i], 0, kChPad);
    }
    s.padCount = 0;
}

static void releaseDrone()
{
    if (s.droneNote >= 0)
    {
        synthNote(false, s.droneNote, 0, kChDrone);
        s.droneNote = -1;
    }
}

static void releaseMotif(int m)
{
    if (s.motifNote[m] >= 0)
    {
        synthNote(false, s.motifNote[m], 0, m == 0 ? kChMotif1 : kChMotif2);
        s.motifNote[m] = -1;
    }
}

static void allNotesOff()
{
    releasePad();
    releaseDrone();
    releaseMotif(0);
    releaseMotif(1);
}

// Re-voice the sustained parts after a chord / key / mode / octave change
static void revoice()
{
    releasePad();
    releaseDrone();
    if (s.partOn[0])
    {
        int16_t notes[4];
        const int n = buildChord(s.padOct, notes, 4);
        for (int i = 0; i < n; i++)
        {
            s.padNotes[i] = notes[i];
            synthNote(true, notes[i], kVelPad, kChPad);
        }
        s.padCount = (uint8_t)n;
    }
    if (s.partOn[1])
    {
        s.droneNote = (int16_t)noteForStep(s.degree, s.droneOct);
        synthNote(true, s.droneNote, kVelDrone, kChDrone);
    }
}

static void setPart(int p, bool on)
{
    s.partOn[p] = on;
    if (p == 0 || p == 1)
    {
        revoice();
    }
    else if (!on)
    {
        releaseMotif(p - 2);
    }
}

// 8th-note clock: motif 1 arpeggiates up every tick, motif 2 down every second tick
static void musicTick(uint32_t now)
{
    if (!s.partOn[2] && !s.partOn[3])
    {
        s.nextTick = now;
        return;
    }
    if ((int32_t)(now - s.nextTick) < 0)
    {
        return;
    }
    const uint32_t tickMs = 30000u / (s.tempo > 0 ? s.tempo : 90);
    s.nextTick = ((int32_t)(now - s.nextTick) > (int32_t)(tickMs * 4)) ? now + tickMs : s.nextTick + tickMs;

    for (int m = 0; m < 2; m++)
    {
        if (!s.partOn[2 + m])
        {
            continue;
        }
        if (m == 1 && (s.step & 1))
        {
            continue;
        }
        int16_t chord[4];
        const int n = buildChord(s.motifOct[m], chord, 4);
        const int span = n * 2; // two octaves of chord tones
        const int idx = (m == 0) ? (s.step % span) : (span - 1 - ((s.step / 2) % span));
        const int note = chord[idx % n] + 12 * (idx / n);

        releaseMotif(m);
        s.motifNote[m] = (int16_t)note;
        synthNote(true, note, kVelMotif, m == 0 ? kChMotif1 : kChMotif2);
    }
    s.step++;
}

//==============================================================================================
// Info text (from the NDLR Rev2 manual, FW 3.3.x)
//==============================================================================================
static const char* const kModeHeader[] = { "PRESS / TURN", "+ SHIFT", "IN MENU MODE",
                                           "IN EDITOR MODE", "ON SCREEN", "AT BOOT" };

static const strInfo kInfo[ndlrControlCount] = {
    /* E1 */ { "Encoder 1 - PAD", 3,
              { { imNormal, "Turn: Pad POSITION / STRUM. Press: switch between the two." },
                { imMenu, "Mod Matrix menu 1&2." },
                { imEditor, "Chord Sequencer." }, { 0, "" } },
              "Position is the centre note of the Pad range (octaves 0-7); moving it inverts the chord. "
              "Strum has 7 settings from none to 1/8 note. In this build only Position (Pad octave) works." },
    /* E2 */ { "Encoder 2 - PAD", 2,
              { { imNormal, "Turn: Pad RANGE / SPREAD. Press: switch between the two." },
                { imMenu, "Mod Matrix menu 3&4." }, { 0, "" }, { 0, "" } },
              "Range is how many Pad notes sound (1-22). Spread sets the voicing: stacked thirds, "
              "open voicings, root-only bass, root+5ths." },
    /* E3 */ { "Encoder 3 - DRONE", 2,
              { { imNormal, "Turn: Drone POSITION / NOTES / TRIGGER. Press: cycle the three." },
                { imMenu, "Mod Matrix menu 5&6." }, { 0, "" }, { 0, "" } },
              "Position is one of 5 octaves. Notes: root, root+octave, root+5th, or all three. Trigger: "
              "Root or Chord mode with cadence rhythms. In this build only Position (Drone octave) works." },
    /* E4 */ { "Enc 4 - TEMPO KEY MODE", 4,
              { { imNormal, "Turn: Tempo, Key or Mode. Press: cycle the three." },
                { imShift, "Hold Shift while turning: applies when Shift is released (not in this build)." },
                { imMenu, "Mod Matrix menu 7&8." },
                { imEditor, "MIDI Controller editor." } },
              "Tempo 10-300 BPM. Key steps round the circle of fifths. Mode: Major, Dorian, Phrygian, "
              "Lydian, Mixolydian, Minor, Locrian, Gypsy Min, Harmonic Min, Min Penta, Whole Tone." },
    /* E5 */ { "Encoder 5 - MOTIF", 3,
              { { imNormal, "Turn: Motif POSITION / PATT-LEN for the selected motif." },
                { imMenu, "LFO Config." }, { imEditor, "Pattern Editor." }, { 0, "" } },
              "Position is the first note of the motif; Patt-Len is how many of the 16 pattern notes "
              "play. In this build only Position (motif octave) works." },
    /* E6 */ { "Encoder 6 - MOTIF", 3,
              { { imNormal, "Turn: Motif VARIATION / PATTERN." },
                { imMenu, "Settings 1/3 (mostly MIDI)." }, { imEditor, "Rhythm Editor." }, { 0, "" } },
              "Variation: forward, backward, ping-pong, odd-then-even, random. Pattern: 1-20 presets, "
              "21-40 user patterns." },
    /* E7 */ { "Encoder 7 - MOTIF", 3,
              { { imNormal, "Turn: CLK DIV / RHY-LEN. Centre arrow (GATE) is not printed on the panel." },
                { imMenu, "Settings 2/3 (mostly velocities)." },
                { imEditor, "Perlin Noise generator." }, { 0, "" } },
              "Clk Div: 1/1, 1/2, 1/4, 1/8, triplets 1/3 and 1/6. Rhy-Len: 4-32 beats. Gate sets how "
              "soon each note turns off." },
    /* E8 */ { "Encoder 8 - MOTIF", 2,
              { { imNormal, "Turn: ACCENT / RHYTHM / OFFSET." },
                { imMenu, "Settings 3/3 (general)." }, { 0, "" }, { 0, "" } },
              "Accent picks the velocity source. Rhythm: 1-20 presets, 21-40 user. Offset delays the "
              "two motifs against each other by 0 to 3/16 of a beat." },
    /* ALL */ { "ALL  ||/>", 2,
               { { imNormal, "Play/pause all four parts. Restarts both motifs in sync." },
                 { imShift, "MOD: toggle ALL modulations (red rings; visual only here)." }, { 0, "" }, { 0, "" } },
               "Restarts both motifs on their first pattern note and first rhythm beat so they lock together." },
    /* 1/2 */ { "1/2  (Shift: PANIC)", 2,
               { { imNormal, "Right-hand encoders edit Motif 1 or Motif 2." },
                 { imShift, "PANIC: all notes off." }, { 0, "" }, { 0, "" } },
               "The selected motif is highlighted on the NDLR screen. Panic clears stuck notes." },
    /* chords: filled in dynamically */
    { nullptr, 0, { { 0, "" }, { 0, "" }, { 0, "" }, { 0, "" } }, nullptr },
    { nullptr, 0, { { 0, "" }, { 0, "" }, { 0, "" }, { 0, "" } }, nullptr },
    { nullptr, 0, { { 0, "" }, { 0, "" }, { 0, "" }, { 0, "" } }, nullptr },
    { nullptr, 0, { { 0, "" }, { 0, "" }, { 0, "" }, { 0, "" } }, nullptr },
    { nullptr, 0, { { 0, "" }, { 0, "" }, { 0, "" }, { 0, "" } }, nullptr },
    { nullptr, 0, { { 0, "" }, { 0, "" }, { 0, "" }, { 0, "" } }, nullptr },
    { nullptr, 0, { { 0, "" }, { 0, "" }, { 0, "" }, { 0, "" } }, nullptr },
    /* SCREEN */ { "NDLR SCREEN", 2,
                  { { imView, "Top stripes: Motif 1, Pad, Motif 2." },
                    { imView, "Side markers: selected encoder." }, { 0, "" }, { 0, "" } },
                  "Shows key and mode, BPM, chord degree + name + type, and the four play indicators. "
                  "The centre box changes to Pad / Drone / T-K-M / Motif detail." },
    /* MENU */ { "MENU (Shift: Editors)", 3,
                { { imNormal, "Toggle Menu mode: the eight encoder buttons then open menus." },
                  { imShift, "Toggle Editor mode." },
                  { imBoot, "At power-on, Shift+Menu opens the Boot Menu." }, { 0, "" } },
                "Left four encoders open the Mod Matrix, top-right opens LFO Config, the others open "
                "Settings 1-3. Editors: Chord Seq, MIDI Controller, Pattern, Rhythm, Perlin." },
    /* SHIFT */ { "SHIFT", 2,
                 { { imNormal, "Hold it with another control for the italic label. Middle latches it." },
                   { imBoot, "At power-on: freeze the splash screen." }, { 0, "" }, { 0, "" } },
                 "Shifted functions: chord type on the 7 chord buttons, MOD on part buttons and ALL, "
                 "PANIC on 1/2, Editors on MENU. The Mini Shift button works as NDLR Shift too." },
    /* PAD */ { "PAD  ||/>", 2,
               { { imNormal, "Play / pause the Pad." },
                 { imShift, "MOD: toggle modulations that target the Pad (red ring)." }, { 0, "" }, { 0, "" } },
               "Pad plays every note of the current chord. Plays on Tracker synth channel 1." },
    /* DRONE */ { "DRONE  ||/>", 2,
                 { { imNormal, "Play / pause the Drone." },
                   { imShift, "MOD: toggle modulations that target the Drone (red ring)." }, { 0, "" }, { 0, "" } },
                 "Drone holds the chord root. Plays on Tracker synth channel 2." },
    /* M1 */ { "MOTIF 1  ||/>", 2,
               { { imNormal, "Play / pause Motif 1 (up arpeggio)." },
                 { imShift, "MOD: toggle modulations that target Motif 1 (red ring)." }, { 0, "" }, { 0, "" } },
               "Plays an 8th-note arpeggio of the chord on Tracker synth channel 3." },
    /* M2 */ { "MOTIF 2  ||/>", 2,
               { { imNormal, "Play / pause Motif 2 (down arpeggio)." },
                 { imShift, "MOD: toggle modulations that target Motif 2 (red ring)." }, { 0, "" }, { 0, "" } },
               "Plays a quarter-note down arpeggio on channel 3, sharing the synth with Motif 1." },
};

// word-wrap `src` into fixed-stride lines; returns the number of lines written
static int wrap(const char* src, int maxChars, char* dst, int stride, int maxLines)
{
    int lines = 0;
    const char* p = src;
    while (*p && lines < maxLines)
    {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const int remaining = (int)strlen(p);
        int take = remaining <= maxChars ? remaining : maxChars;
        if (remaining > maxChars)
        {
            int cut = maxChars;
            while (cut > 0 && p[cut] != ' ')
                cut--;
            if (cut > 0)
                take = cut;
        }
        memcpy(dst + lines * stride, p, take);
        dst[lines * stride + take] = 0;
        p += take;
        lines++;
    }
    return lines;
}

static void addInfoLines(const char* text, uint8_t kind)
{
    if (s_view.infoCount >= kNdlrInfoLines)
        return;
    const int first = s_view.infoCount;
    const int n = wrap(text, 27, &s_view.info[first][0], kNdlrInfoLineLen, kNdlrInfoLines - first);
    for (int i = 0; i < n; i++)
        s_view.infoKind[first + i] = kind;
    s_view.infoCount = (uint8_t)(first + n);
}

static void chordLabels(char* roman, size_t romanSize, char* name, size_t nameSize)
{
    const strMode& m = kModes[s.mode];
    const int d = s.degree;
    const int rootSemi = (kKeySemi[s.key] + m.iv[d % m.n]) % 12;
    snprintf(name, nameSize, "%s", kNoteName[rootSemi]);
    const char* rom = kRomanUpper[d];
    if (m.n == 7)
    {
        const int third = (m.iv[(d + 2) % 7] - m.iv[d] + 12) % 12;
        const int fifth = (m.iv[(d + 4) % 7] - m.iv[d] + 12) % 12;
        if (third == 3)
        {
            snprintf(roman, romanSize, "%s%s", kRomanLower[d], fifth == 6 ? "o" : "");
            return;
        }
    }
    snprintf(roman, romanSize, "%s", rom);
}

static void rebuildView()
{
    strNdlrPanelView& v = s_view;
    v.selected = s.sel;
    v.shift = shiftOn();
    v.menuMode = s.menuMode;
    for (int i = 0; i < 4; i++)
    {
        v.partOn[i] = s.partOn[i];
        v.modRing[i] = s.modRing[i];
    }
    v.motif = s.motif;
    v.activeEnc = s.activeEnc;
    for (int i = 0; i < 8; i++)
        v.encLabel[i] = s.encLabel[i];
    v.chordDegree = s.degree;
    v.typePending = s.pendingType >= 0;

    char roman[8], name[4];
    chordLabels(roman, sizeof(roman), name, sizeof(name));
    snprintf(v.keyMode, sizeof(v.keyMode), "%s - %s", kKeyName[s.key], kModes[s.mode].name);
    snprintf(v.chordText, sizeof(v.chordText), "%s %s", roman, name);
    snprintf(v.typeText, sizeof(v.typeText), "%s",
             s.pendingType >= 0 ? kTypeName[s.pendingType] : kTypeName[resolvedType()]);
    snprintf(v.bpmText, sizeof(v.bpmText), "%u BPM", (unsigned)s.tempo);

    if (nowMs() >= s.toastUntil)
        v.toast[0] = 0;

    // info column + description for the highlighted control
    v.infoCount = 0;
    v.descCount = 0;
    const strNdlrGeom& g = kNdlrGeom[s.sel];

    strInfo chordInfo{};
    const strInfo* inf = &kInfo[s.sel];
    char chordName[24], chordNormal[64], chordShift[64], chordDesc[160];
    if (g.kind == ndlrKindChord)
    {
        const int d = s.sel - ndlrChord1;
        snprintf(chordName, sizeof(chordName), "Chord %s", kRomanUpper[d]);
        snprintf(chordNormal, sizeof(chordNormal), "Play chord degree %s. All parts follow, in key.",
                 kRomanUpper[d]);
        snprintf(chordShift, sizeof(chordShift), "Set chord type %s. Applies on the next chord press.",
                 kTypeName[d]);
        snprintf(chordDesc, sizeof(chordDesc),
                 "Buttons I-VII are the scale degrees. With Shift they pick the type: I triad, II 7th, "
                 "III sus2, IV alt2, V alt1, VI sus4, VII 6th.");
        chordInfo.name = chordName;
        chordInfo.rowCount = 2;
        chordInfo.rows[0] = { imNormal, chordNormal };
        chordInfo.rows[1] = { imShift, chordShift };
        chordInfo.desc = chordDesc;
        inf = &chordInfo;
    }

    uint8_t active = imNormal;
    {
        const uint8_t want = s.menuMode == 2 ? imEditor : s.menuMode == 1 ? imMenu : shiftOn() ? imShift : imNormal;
        bool hasWant = false, hasView = false;
        for (int i = 0; i < inf->rowCount; i++)
        {
            if (inf->rows[i].mode == want)
                hasWant = true;
            if (inf->rows[i].mode == imView)
                hasView = true;
        }
        active = hasView ? imView : (hasWant ? want : imNormal);
    }

    snprintf(v.info[0], kNdlrInfoLineLen, "%s", inf->name);
    v.infoKind[0] = ndlrLineTitle;
    v.infoCount = 1;
    for (int i = 0; i < inf->rowCount; i++)
    {
        const bool act = inf->rows[i].mode == active;
        if (v.infoCount >= kNdlrInfoLines)
            break;
        snprintf(v.info[v.infoCount], kNdlrInfoLineLen, "%s", kModeHeader[inf->rows[i].mode]);
        v.infoKind[v.infoCount++] = ndlrLineHeader;
        addInfoLines(inf->rows[i].text, act ? ndlrLineActive : ndlrLineText);
    }

    // live readout
    char now[40] = "";
    if (g.kind == ndlrKindPart)
    {
        snprintf(now, sizeof(now), "Now: %s", s.partOn[s.sel - ndlrBtnPad] ? "PLAYING" : "paused");
    }
    else if (s.sel == ndlrEnc4)
    {
        snprintf(now, sizeof(now), "%u BPM %s %s", (unsigned)s.tempo, kKeyName[s.key], kModes[s.mode].name);
    }
    else if (g.kind == ndlrKindChord || g.kind == ndlrKindScreen)
    {
        snprintf(now, sizeof(now), "Now: %s %s %s", roman, name, kTypeName[resolvedType()]);
    }
    else if (s.sel == ndlrBtnMotifSel)
    {
        snprintf(now, sizeof(now), "Now: Motif %u", (unsigned)s.motif);
    }
    if (now[0] && v.infoCount < kNdlrInfoLines)
    {
        snprintf(v.info[v.infoCount], kNdlrInfoLineLen, "%s", now);
        v.infoKind[v.infoCount++] = ndlrLineNow;
    }

    v.descCount = (uint8_t)wrap(inf->desc ? inf->desc : "", 100, &v.desc[0][0], kNdlrDescLineLen,
                                kNdlrDescLines);
}

//==============================================================================================
// Refresh
//==============================================================================================
static void refreshPanel()
{
    rebuildView();
    display.refreshControl(P->panel);
    display.synchronizeRefresh();
    s.lastRefresh = nowMs();
    s.dirty = 0;
}

//==============================================================================================
// Navigation (dpad)
//==============================================================================================
enum enDir : uint8_t
{
    dirLeft,
    dirRight,
    dirUp,
    dirDown
};

struct strOverride
{
    uint8_t from;
    uint8_t dir;
    uint8_t to;
};
static const strOverride kOverrides[] = {
    { ndlrBtnShift, dirLeft, ndlrBtnDrone },   { ndlrBtnShift, dirRight, ndlrBtnMotif1 },
    { ndlrBtnMenu, dirLeft, ndlrChord5 },      { ndlrBtnMenu, dirRight, ndlrChord4 },
    { ndlrChord3, dirUp, ndlrChord2 },         { ndlrChord6, dirUp, ndlrChord7 },
    { ndlrBtnMotifSel, dirLeft, ndlrChord1 },  { ndlrBtnAll, dirRight, ndlrChord1 },
};

static uint8_t neighbour(uint8_t from, uint8_t dir)
{
    for (const strOverride& o : kOverrides)
    {
        if (o.from == from && o.dir == dir)
            return o.to;
    }
    const int dx = dir == dirLeft ? -1 : dir == dirRight ? 1 : 0;
    const int dy = dir == dirUp ? -1 : dir == dirDown ? 1 : 0;
    int best = -1;
    long bestScore = 0x7fffffff;
    for (int i = 0; i < ndlrControlCount; i++)
    {
        if (i == from)
            continue;
        const int vx = kNdlrGeom[i].x - kNdlrGeom[from].x;
        const int vy = kNdlrGeom[i].y - kNdlrGeom[from].y;
        const int along = vx * dx + vy * dy;
        if (along <= 8)
            continue;
        const int perp = (vx * dy < 0 ? -vx * dy : vx * dy) + (vy * dx < 0 ? -vy * dx : vy * dx);
        const long score = (long)along * 10 + (long)perp * 22;
        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    return best >= 0 ? (uint8_t)best : from;
}

static void move(uint8_t dir)
{
    s.sel = neighbour(s.sel, dir);
    if (kNdlrGeom[s.sel].kind == ndlrKindEnc)
    {
        s.activeEnc = s.sel - ndlrEnc1;
    }
    s.dirty = 1;
}

//==============================================================================================
// Press / turn
//==============================================================================================
static void toggleAll()
{
    bool any = false;
    for (int i = 0; i < 4; i++)
        any = any || s.partOn[i];
    s.partOn[0] = s.partOn[1] = s.partOn[2] = s.partOn[3] = !any;
    s.step = 0;
    allNotesOff();
    if (!any)
        revoice();
    toast(any ? "All parts paused" : "All parts playing");
}

static void toggleAllMods()
{
    const bool on = !s.modRing[0];
    for (int i = 0; i < 4; i++)
        s.modRing[i] = on;
    toast("MOD rings: visual only");
}

static void pressSelected()
{
    const uint8_t c = s.sel;
    const strNdlrGeom& g = kNdlrGeom[c];
    const bool sh = shiftOn();

    switch (g.kind)
    {
        case ndlrKindEnc:
        {
            const int e = c - ndlrEnc1;
            s.activeEnc = e;
            if (s.menuMode == 1)
            {
                toast("Menu screens: not in this build");
            }
            else if (s.menuMode == 2)
            {
                toast("Editors: not in this build");
            }
            else
            {
                s.encLabel[e] = (uint8_t)((s.encLabel[e] + 1) % (e == 0 || e == 1 || e == 4 || e == 5 ? 2 : (e == 7 ? 3 : 3)));
            }
            break;
        }
        case ndlrKindChord:
        {
            const int d = c - ndlrChord1;
            if (sh)
            {
                s.pendingType = (int8_t)d;
                toast("Chord type set for next chord");
            }
            else
            {
                s.degree = (uint8_t)d;
                if (s.pendingType >= 0)
                {
                    s.type = (uint8_t)s.pendingType;
                    s.pendingType = -1;
                }
                revoice();
            }
            break;
        }
        case ndlrKindPart:
        {
            const int p = c - ndlrBtnPad;
            if (sh)
                s.modRing[p] = !s.modRing[p];
            else
                setPart(p, !s.partOn[p]);
            break;
        }
        case ndlrKindShift: s.shiftLatched = !s.shiftLatched; break;
        case ndlrKindBtn:
        {
            if (c == ndlrBtnAll)
            {
                sh ? toggleAllMods() : toggleAll();
            }
            else if (c == ndlrBtnMotifSel)
            {
                if (sh)
                {
                    allNotesOff();
                    toast("PANIC");
                }
                else
                {
                    s.motif = s.motif == 1 ? 2 : 1;
                }
            }
            else if (c == ndlrBtnMenu)
            {
                if (sh)
                    s.menuMode = s.menuMode == 2 ? 0 : 2;
                else
                    s.menuMode = s.menuMode == 1 ? 0 : 1;
            }
            break;
        }
        default: break;
    }
    s.dirty = 1;
}

static void turn(int n)
{
    const strNdlrGeom& g = kNdlrGeom[s.sel];
    if (g.kind != ndlrKindEnc)
    {
        toast("Select an encoder to turn it");
        return;
    }
    const int e = s.sel - ndlrEnc1;
    const int label = s.encLabel[e];
    const int dir = n > 0 ? 1 : -1;
    s.activeEnc = e;

    switch (e)
    {
        case 3: // tempo / key / mode
        {
            if (label == 0)
            {
                int t = (int)s.tempo + n;
                s.tempo = (uint16_t)(t < 10 ? 10 : t > 300 ? 300 : t);
            }
            else if (label == 1)
            {
                s.key = (uint8_t)((s.key + dir + 12) % 12);
                revoice();
            }
            else
            {
                s.mode = (uint8_t)((s.mode + dir + kModeCount) % kModeCount);
                revoice();
            }
            break;
        }
        case 0: // pad position -> pad octave
        {
            if (label == 0)
            {
                int o = (int)s.padOct + dir;
                s.padOct = (uint8_t)(o < 1 ? 1 : o > 6 ? 6 : o);
                revoice();
            }
            else
                toast("Strum: not in this build");
            break;
        }
        case 2: // drone position -> drone octave
        {
            if (label == 0)
            {
                int o = (int)s.droneOct + dir;
                s.droneOct = (uint8_t)(o < 1 ? 1 : o > 4 ? 4 : o);
                revoice();
            }
            else
                toast("Not in this build");
            break;
        }
        case 4: // motif position -> octave of the selected motif
        {
            if (label == 0)
            {
                uint8_t& oct = s.motifOct[s.motif - 1];
                int o = (int)oct + dir;
                oct = (uint8_t)(o < 2 ? 2 : o > 6 ? 6 : o);
            }
            else
                toast("Pattern length: not in this build");
            break;
        }
        default: toast("Not in this build"); break;
    }
    s.dirty = 1;
}

//==============================================================================================
// Button callbacks
//==============================================================================================
static void functUp() { move(dirUp); }
static void functDown() { move(dirDown); }
static void functLeft() { move(dirLeft); }
static void functRight() { move(dirRight); }
static void functMiddle() { pressSelected(); }
static void functPlay()
{
    toggleAll();
}

static void functPlus1(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        turn(1);
}
static void functMinus1(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        turn(-1);
}
static void functPlus10(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        turn(10);
}
static void functMinus10(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        turn(-10);
}

static void functSwitchModule(uint8_t button)
{
    P->eventFunct(eventSwitchModule, P, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(P);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(P);
    }
}

//==============================================================================================
// Module
//==============================================================================================
void cNdlrPanel::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    songTimer.hide();
    titleBar->hide();

    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functMiddle);
    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlay);

    ip->setButtonObj(interfaceButtonMiniPlus1, functPlus1);
    ip->setButtonObj(interfaceButtonMiniMinus1, functMinus1);
    ip->setButtonObj(interfaceButtonMiniPlus10, functPlus10);
    ip->setButtonObj(interfaceButtonMiniMinus10, functMinus10);

    // fresh state
    memset(&s, 0, sizeof(s));
    memset(&s_view, 0, sizeof(s_view));
    s.sel = ndlrEnc1;
    s.motif = 1;
    s.degree = 0;
    s.type = ctTriad;
    s.pendingType = -1;
    s.tempo = 90;
    s.padOct = 3;
    s.droneOct = 2;
    s.motifOct[0] = 4;
    s.motifOct[1] = 5;
    s.droneNote = -1;
    s.motifNote[0] = s.motifNote[1] = -1;
    s.nextTick = nowMs();

    display.setControlData(panel, &s_view);
    display.setControlShow(panel);
    refreshPanel();
}

void cNdlrPanel::stop()
{
    allNotesOff();
    moduleRefresh = 0;
    display.setControlHide(panel);
    songTimer.show();
}

void cNdlrPanel::update()
{
    const uint32_t now = nowMs();

    const uint8_t held = buttons->isButtonPressed(interfaceButtonShift) ? 1 : 0;
    if (held != s.shiftHeld)
    {
        s.shiftHeld = held;
        s.dirty = 1;
    }

    if (s_view.toast[0] != 0 && now >= s.toastUntil)
    {
        s_view.toast[0] = 0;
        s.dirty = 1;
    }

    musicTick(now);

    if (s.dirty && (now - s.lastRefresh) >= 30)
    {
        refreshPanel();
    }
}

void cNdlrPanel::initDisplayControls()
{
    strControlProperties prop;
    prop.colors = nullptr;
    prop.text = nullptr;
    prop.data = nullptr;
    prop.style = controlStyleShow;
    prop.value = 0;
    prop.x = 0;
    prop.y = 0;
    prop.w = 800;
    prop.h = 480;
    panel = display.createControl<cNdlrPanelControl>(&prop);
}

#endif // TRACKER_MINI
