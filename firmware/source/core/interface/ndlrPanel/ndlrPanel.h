#ifndef INTERFACE_NDLRPANEL_H_
#define INTERFACE_NDLRPANEL_H_

// NDLR front-panel module for the Tracker Mini.
//
// Shows the Conductive Labs NDLR panel on the 800x480 screen. The dpad moves a
// highlight over the NDLR controls, Middle presses the highlighted control, and
// +1/-1/+10/-10 turn the highlighted encoder. A small chord/arpeggio engine plays
// the Pad, Drone and Motifs on the Tracker's internal synth channels 1–4.
//
// Entry point: Config > Info > NDLR Panel. Hold the Mini Menu button for the usual
// module popup to leave.

#ifdef TRACKER_MINI

#include "core/modulesBase.h"

class cNdlrPanel : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}
    void hideControlsUnderMiniMenu() override;

    cNdlrPanel() {}
    ~cNdlrPanel() {}

    uint32_t nowMs() const;
    void toggleRecording();
    void reserveVoices(int part, int wanted);
    bool hasSession() const { return sessionActive; }
    bool toggleBackgroundPlayback();
    void updateJam();
    void endSession();
    void synthChanging(int part);
    hControl panel = -1;

  private:
    void resetSession();
    bool sessionActive = false;
};

extern cNdlrPanel ndlrPanel;

#endif // TRACKER_MINI
#endif /* INTERFACE_NDLRPANEL_H_ */
