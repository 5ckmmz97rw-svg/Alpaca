# NDLR Panel for Tracker CE (Mini)

Unzip over the root of your tracker-ce-main checkout (files keep their paths; 3 new files in
core/interface/ndlrPanel/, 2 in core/libs/NewDisplay/, 6 existing files modified).
Everything is wrapped in `#ifdef TRACKER_MINI`.

Build (MSYS2 as in the repo docs): `make mini TOOLCHAIN=arm-none-eabi- BUILD_TAG=ndlr`
Copy the .ptf to /Firmware on the SD card, then Config > Firmware update.
Open it via Config > Info > NDLR Panel. Hold Mini Menu for the module popup to leave.

Controls: dpad = move highlight, Middle = press, +1/-1/+10/-10 = turn highlighted encoder,
Play = ALL, Shift = NDLR Shift (hold, or highlight SHIFT and press Middle to latch).

Works: chord buttons I-VII (+ Shift types), Pad/Drone/Motif 1/2 play, ALL, PANIC, Menu/Editor mode
display, tempo/key/mode (E4), Pad/Drone/Motif octave (E1/E3/E5), live NDLR screen.
Sound: Pad->synth ch1, Drone->ch2, Motif 1 (up arp) and Motif 2 (down arp)->ch3.
Not implemented: strum/spread/range, pattern/rhythm/variation, mod matrix, LFOs, editors,
the Tonic 2nd/3rd/4th/6th modes (set those synths' MIDI channel mapping/instruments yourself).

UNTESTED: written without an ARM toolchain or hardware. Syntax-checked on host only (no errors in
the new files). Possible first-build issues: display-list budget in ndlrPanelControl.cpp (8 KB/slot
- if the screen is blank or truncated, trim drawing), and exact API names in fNdlrPanel.cpp.
