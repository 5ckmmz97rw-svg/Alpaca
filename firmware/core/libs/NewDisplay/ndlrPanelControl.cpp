#ifdef TRACKER_MINI

#include "ndlrPanelControl.h"

#include "FT812.h"
#include "display.h"

#include <math.h>
#include <string.h>

// Font slots (see display.cpp): 0=14 Regular, 1=17 Regular, 2=14 Bold, 3=17 Bold, 4=12 Regular
static constexpr int kFont12 = 4;
static constexpr int kFont14 = 0;
static constexpr int kFont17B = 3;

// Palette (matches the web prototype)
static constexpr uint32_t kColBody = DISP_RGB(0x15, 0x19, 0x1f);
static constexpr uint32_t kColBodyEdge = DISP_RGB(0x2f, 0x37, 0x47);
static constexpr uint32_t kColRingEdge = DISP_RGB(0x3b, 0x44, 0x56);
static constexpr uint32_t kColKnob = DISP_RGB(0x11, 0x14, 0x19);
static constexpr uint32_t kColKnobEdge = DISP_RGB(0x3b, 0x44, 0x58);
static constexpr uint32_t kColNotch = DISP_RGB(0xae, 0xb7, 0xc9);
static constexpr uint32_t kColText = DISP_RGB(0xd5, 0xdc, 0xea);
static constexpr uint32_t kColTextDim = DISP_RGB(0x8d, 0x98, 0xad);
static constexpr uint32_t kColTextFaint = DISP_RGB(0x56, 0x60, 0x74);
static constexpr uint32_t kColHi = DISP_RGB(0xff, 0xd2, 0x3f);
static constexpr uint32_t kColOn = DISP_RGB(0x3d, 0xdc, 0x84);
static constexpr uint32_t kColBlue = DISP_RGB(0x2f, 0x6b, 0xff);
static constexpr uint32_t kColBlueLight = DISP_RGB(0x6f, 0xa0, 0xff);
static constexpr uint32_t kColRed = DISP_RGB(0xff, 0x4d, 0x4d);
static constexpr uint32_t kColCream = DISP_RGB(0xe9, 0xe2, 0xcf);
static constexpr uint32_t kColBlack = DISP_RGB(0, 0, 0);
static constexpr uint32_t kColInfoBg = DISP_RGB(0x15, 0x1a, 0x22);

// NDLR panel reference (photo) coordinates -> Mini screen coordinates
static inline int16_t sx(int v)
{
    return (int16_t)(6 + (v * 3) / 4);
}
static inline int16_t sy(int v)
{
    return (int16_t)(6 + (v * 3) / 4);
}
static inline int16_t sr(int v)
{
    return (int16_t)((v * 3) / 4);
}

static constexpr int kRingCx = 403, kRingCy = 255, kRingR = 130; // reference coordinates

static const char* const kChordNumeral[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char* const kChordType[7] = { "triad", "7th", "sus2", "alt2", "alt1", "sus4", "6th" };
static const char* const kPartLabel[4] = { "PAD", "DRONE", "1", "2" };

// Short in-panel labels (the info column shows the full names)
static const char* const kEncLabel[8][3] = {
    { "POS", "STRUM", "" },      { "RANGE", "SPREAD", "" },    { "POS", "NOTES", "TRIG" },
    { "TEMPO", "KEY", "MODE" },  { "POS", "P-LEN", "" },       { "VAR", "PATT", "" },
    { "DIV", "R-LEN", "GATE" },  { "ACCNT", "RHY", "OFFS" },
};
static const uint8_t kEncLabelCount[8] = { 2, 2, 3, 3, 2, 2, 3, 3 };
static const char* const kEncMenuName[8] = { "MOD 1&2", "MOD 3&4", "MOD 5&6", "MOD 7&8",
                                             "LFO CFG", "SET 1",   "SET 2",   "SET 3" };
static const char* const kEncEditorName[8] = { "CHORD SEQ", "",       "",       "MIDI CTL",
                                               "PATT ED",   "RHY ED", "PERLIN", "" };

//--------------------------------------------------------------------
// drawing helpers (all coordinates in screen pixels)
static void disc(int x, int y, int r, uint32_t color)
{
    API_COLOR(color);
    API_POINT_SIZE(16 * r);
    API_BEGIN(FTPOINTS);
    API_VERTEX2F(x, y);
    API_END();
}

static void fillRect(int x0, int y0, int x1, int y1, uint32_t color)
{
    API_COLOR(color);
    API_LINE_WIDTH(16);
    API_BEGIN(RECTS);
    API_VERTEX2F(x0, y0);
    API_VERTEX2F(x1, y1);
    API_END();
}

static void frameRect(int x0, int y0, int x1, int y1, uint32_t color)
{
    API_COLOR(color);
    API_LINE_WIDTH(16);
    API_BEGIN(LINE_STRIP);
    API_VERTEX2F(x0, y0);
    API_VERTEX2F(x1, y0);
    API_VERTEX2F(x1, y1);
    API_VERTEX2F(x0, y1);
    API_VERTEX2F(x0, y0);
    API_END();
}

static void line(int x0, int y0, int x1, int y1, uint32_t color, int width16)
{
    API_COLOR(color);
    API_LINE_WIDTH(width16);
    API_BEGIN(LINES);
    API_VERTEX2F(x0, y0);
    API_VERTEX2F(x1, y1);
    API_END();
}

// text anchored with OPT_CENTERY: y is the vertical centre of the line
static void drawText(int x, int y, int font, uint16_t opt, uint32_t color, const char* s)
{
    if (s == nullptr || s[0] == 0)
    {
        return;
    }
    API_COLOR(color);
    API_CMD_TEXT(x, y, fonts[font].handle, opt | OPT_CENTERY, s);
}

//--------------------------------------------------------------------
cNdlrPanelControl::cNdlrPanelControl(strControlProperties* properties)
  : cDisplayControl(controlIdNdlrPanel, 3)
{
    colorsCount = 0;
    colors = nullptr;
    posX = 0;
    posY = 0;
    width = 800;
    height = 480;
    style = 0;

    if (properties != nullptr)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        setStyle(properties->style);
    }
}

cNdlrPanelControl::~cNdlrPanelControl()
{
}

void cNdlrPanelControl::setStyle(uint32_t style)
{
    this->style = style;
}

void cNdlrPanelControl::setText(const char* text)
{
    (void)text;
}

void cNdlrPanelControl::setValue(int value)
{
    this->value = value;
}

void cNdlrPanelControl::setColors(const uint32_t* colors)
{
    (void)colors;
}

void cNdlrPanelControl::setData(void* data)
{
    view = static_cast<const strNdlrPanelView*>(data);
}

//--------------------------------------------------------------------
// Stage 0: panel body, ring, encoder labels, outer (menu/editor) labels
void cNdlrPanelControl::update()
{
    if (view == nullptr)
    {
        return;
    }

    fillRect(6, 6, 606, 396, kColBody);
    frameRect(6, 6, 606, 396, kColBodyEdge);

    // chord ring
    const int rcx = sx(kRingCx), rcy = sy(kRingCy);
    disc(rcx, rcy, sr(kRingR) + 4, kColRingEdge);
    disc(rcx, rcy, sr(kRingR) - 3, kColBody);

    for (int i = 0; i < 8; i++)
    {
        const strNdlrGeom& g = kNdlrGeom[ndlrEnc1 + i];
        const int ex = sx(g.x), ey = sy(g.y), er = sr(g.r);
        const bool left = i < 4;

        // labels between the encoder and the chord ring
        const int n = kEncLabelCount[i];
        const int lx = left ? ex + er + 8 : ex - er - 8;
        const uint16_t opt = left ? 0 : OPT_RIGHTX;
        for (int j = 0; j < n; j++)
        {
            const bool active = view->menuMode == 0 && view->activeEnc == i && view->encLabel[i] == j;
            drawText(lx, ey - (n - 1) * 6 + j * 12, kFont12, opt, active ? kColHi : kColTextDim,
                 kEncLabel[i][j]);
        }

        // outer labels: only the set that matches the current mode
        if (view->menuMode != 0)
        {
            const char* outer = view->menuMode == 1 ? kEncMenuName[i] : kEncEditorName[i];
            const int ox = left ? ex - er - 10 : ex + er + 10;
            drawText(ox, ey, kFont12, left ? OPT_RIGHTX : 0, outer[0] ? kColHi : kColTextFaint,
                 outer[0] ? outer : "-");
        }
    }
}

//--------------------------------------------------------------------
// Stage 1: selection highlight, all controls, the little NDLR screen
void cNdlrPanelControl::update2()
{
    if (view == nullptr)
    {
        return;
    }

    const int rcx = sx(kRingCx), rcy = sy(kRingCy);
    const bool shiftOn = view->shift != 0;

    // 1) selection highlight (drawn first so the controls sit on top of it)
    {
        const strNdlrGeom& g = kNdlrGeom[view->selected];
        const int x = sx(g.x), y = sy(g.y);
        if (g.kind == ndlrKindScreen)
        {
            fillRect(x - 39, y - 49, x + 39, y + 49, kColHi);
        }
        else
        {
            const int r = sr(g.r) + (g.kind == ndlrKindEnc ? 8 : 6);
            disc(x, y, r, kColHi);
            disc(x, y, r - 3, kColBody);
        }
    }

    // 2) controls
    for (int c = 0; c < ndlrControlCount; c++)
    {
        const strNdlrGeom& g = kNdlrGeom[c];
        const int x = sx(g.x), y = sy(g.y), r = sr(g.r);

        switch (g.kind)
        {
            case ndlrKindEnc:
            {
                disc(x, y, r + 1, kColKnobEdge);
                disc(x, y, r - 1, kColKnob);
                line(x, y - r + 4, x, y - 3, kColNotch, 36);
                break;
            }
            case ndlrKindChord:
            {
                const int deg = c - ndlrChord1;
                const bool active = view->chordDegree == deg;
                disc(x, y, r, active ? kColOn : DISP_RGB(0x8a, 0x93, 0xa3));
                disc(x, y, r - 2, kColKnob);
                drawText(x, y, kFont12, OPT_CENTERX, active ? kColOn : kColText, kChordNumeral[deg]);

                // chord type label on the inside of the ring, shown while Shift is active
                if (shiftOn)
                {
                    const float vx = (float)(rcx - x), vy = (float)(rcy - y);
                    const float len = sqrtf(vx * vx + vy * vy);
                    const int tx = x + (int)(vx / len * (float)(r + 18));
                    const int ty = y + (int)(vy / len * (float)(r + 18));
                    drawText(tx, ty, kFont12, OPT_CENTERX, kColHi, kChordType[deg]);
                }
                break;
            }
            case ndlrKindShift:
            {
                if (shiftOn)
                {
                    disc(x, y, r + 4, kColBlueLight);
                    disc(x, y, r + 2, kColBody);
                }
                disc(x, y, r, kColBlue);
                drawText(x, y + r + 12, kFont12, OPT_CENTERX, kColTextDim, "SHIFT");
                break;
            }
            case ndlrKindPart:
            {
                const int p = c - ndlrBtnPad;
                if (view->modRing[p])
                {
                    disc(x, y, r + 3, kColRed);
                    disc(x, y, r + 1, kColBody);
                }
                disc(x, y, r, view->partOn[p] ? kColOn : kColCream);
                drawText(x, y - r - 8, kFont12, OPT_CENTERX, kColTextDim, kPartLabel[p]);
                break;
            }
            case ndlrKindBtn:
            {
                bool lit = false;
                if (c == ndlrBtnMenu)
                {
                    lit = view->menuMode != 0;
                }
                else if (c == ndlrBtnAll)
                {
                    lit = view->partOn[0] && view->partOn[1] && view->partOn[2] && view->partOn[3];
                }
                disc(x, y, r, lit ? kColHi : kColCream);
                if (c == ndlrBtnAll)
                {
                    drawText(x - r - 4, y, kFont12, OPT_RIGHTX, kColTextDim, "ALL");
                    drawText(x + r + 4, y, kFont12, 0, shiftOn ? kColHi : kColTextFaint, "MOD");
                }
                else if (c == ndlrBtnMotifSel)
                {
                    drawText(x - r - 4, y, kFont12, OPT_RIGHTX, shiftOn ? kColHi : kColTextFaint, "PANIC");
                    drawText(x + r + 4, y, kFont12, 0, kColHi, view->motif == 2 ? "1/[2]" : "[1]/2");
                }
                else
                {
                    drawText(x + r + 5, y, kFont12, 0, kColTextDim, "MENU");
                    drawText(x + r + 5, y + 12, kFont12, 0, shiftOn ? kColHi : kColTextFaint, "+EDIT");
                }
                break;
            }
            case ndlrKindScreen:
            {
                // the NDLR's own display, drawn small
                const int x0 = x - 34, y0 = y - 45, x1 = x + 34, y1 = y + 45;
                fillRect(x0, y0, x1, y1, kColBlack);
                frameRect(x0, y0, x1, y1, kColBodyEdge);

                drawText(x, y0 + 8, kFont12, OPT_CENTERX, kColText, view->keyMode);

                // encoder-select markers: left column = encoders 1-4, right column = 5-8
                for (int i = 0; i < 8; i++)
                {
                    const int mx = i < 4 ? x0 + 4 : x1 - 4;
                    const int my = y0 + 24 + (i % 4) * 9;
                    disc(mx, my, 3, kColText);
                    if (view->activeEnc != i)
                    {
                        disc(mx, my, 2, kColBlack);
                    }
                }
                frameRect(x0 + 10, y0 + 18, x1 - 10, y0 + 60, kColText);
                const uint8_t a = view->activeEnc;
                const char* sub = a < 2 ? "PAD" : a == 2 ? "DRONE" : a == 3 ? "T-K-M"
                                                         : view->motif == 2 ? "MOTIF 2" : "MOTIF 1";
                drawText(x, y0 + 39, kFont12, OPT_CENTERX, kColTextDim, sub);

                drawText(x0 + 4, y0 + 67, kFont12, 0, kColText, view->bpmText);
                drawText(x0 + 3, y0 + 79, kFont14, 0, kColText, view->chordText);
                drawText(x1 - 3, y0 + 79, kFont12, OPT_RIGHTX, view->typePending ? kColBlueLight : kColText,
                     view->typeText);

                for (int p = 0; p < 4; p++)
                {
                    const int dx = x0 + 9 + p * 16;
                    const bool motifSel = (p == 2 && view->motif == 1) || (p == 3 && view->motif == 2);
                    disc(dx, y1 - 4, 2, motifSel ? kColHi : kColText);
                    if (!view->partOn[p])
                    {
                        disc(dx, y1 - 4, 1, kColBlack);
                    }
                }
                break;
            }
            default: break;
        }
    }
}

//--------------------------------------------------------------------
// Stage 2: info column, description strip, toast
void cNdlrPanelControl::update3()
{
    if (view == nullptr)
    {
        return;
    }

    // info column
    fillRect(614, 6, 794, 396, kColInfoBg);
    frameRect(614, 6, 794, 396, kColBodyEdge);

    int y = 20;
    for (int i = 0; i < view->infoCount && i < kNdlrInfoLines; i++)
    {
        switch (view->infoKind[i])
        {
            case ndlrLineTitle:
                drawText(621, y, kFont17B, 0, kColHi, view->info[i]);
                y += 22;
                break;
            case ndlrLineHeader:
                y += 3;
                drawText(621, y, kFont12, 0, kColTextFaint, view->info[i]);
                y += 13;
                break;
            case ndlrLineActive:
                drawText(621, y, kFont12, 0, kColText, view->info[i]);
                y += 14;
                break;
            case ndlrLineNow:
                y += 4;
                drawText(621, y, kFont12, 0, kColOn, view->info[i]);
                y += 14;
                break;
            default:
                drawText(621, y, kFont12, 0, kColTextDim, view->info[i]);
                y += 14;
                break;
        }
    }

    // description strip
    fillRect(6, 402, 794, 474, kColInfoBg);
    frameRect(6, 402, 794, 474, kColBodyEdge);
    for (int i = 0; i < view->descCount && i < kNdlrDescLines; i++)
    {
        drawText(14, 417 + i * 19, kFont14, 0, kColText, view->desc[i]);
    }

    if (view->toast[0] != 0)
    {
        fillRect(156, 14, 456, 36, kColBlack);
        frameRect(156, 14, 456, 36, kColHi);
        drawText(306, 25, kFont12, OPT_CENTERX, kColHi, view->toast);
    }
}

#endif // TRACKER_MINI
