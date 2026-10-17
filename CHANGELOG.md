# Changelog

All notable changes to fldigi-feldhell-core. Versions follow semantic versioning; the
contract tag `feldhell-c1` changes only with an incompatible API change.

## 0.1.0 - 2026-10-17

First release (wave-owl change `add-fldigi-hellschreiber-core`).

- Receiver and transmitter of all seven Hellschreiber submodes of fldigi 4.2.13 (Feld Hell,
  Slow Hell, Feld Hell X5/X9, FSK Hell-245/105, Hell 80), ported line by line from
  `src/feld/feld.cxx`, with the 15 raster fonts of `src/feld/Feld*.cxx`.
- The raster recogniser (new code, not an fldigi port): received raster -> characters with
  a confidence, "no match" below the floor.
- Goldens from fldigi 4.2.13 itself: 37 TX overs (identical int16 output) and 32 receive
  rasters (identical columns) on the reference platform.
- Built on fldigi-dsp-core 0.5.0 (`fdsp_movavgh` for the long AGC average).
