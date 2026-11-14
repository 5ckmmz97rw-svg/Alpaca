# Alpaca
NDLR-style chord machine firmware for the Polyend Tracker Mini, built on Tracker CE (Community Edition). Eight encoders, seven chord buttons and four parts on the Mini's screen, played with the dpad, driving the internal synths.  Fan-made and unofficial. Not made, sold or endorsed by Conductive Labs or Polyend.

- `firmware/` : source files to copy over the root of a `tracker-ce` checkout. See `firmware/INSTALL.md`.
- `docs/` : the project website (serve with GitHub Pages, branch `main`, folder `/docs`).

Build: `make mini TOOLCHAIN=arm-none-eabi- BUILD_TAG=alpaca`, copy the `.ptf` to `/Firmware` on the SD card, then Config > Firmware update.

Licence: same as Tracker CE, PolyForm Noncommercial. Add the licence text from https://polyformproject.org/licenses/noncommercial/1.0.0 as `LICENSE`.

Tip jar: https://ko-fi.com/alpacamaraca

