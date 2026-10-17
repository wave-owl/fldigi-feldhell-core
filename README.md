# fldigi-feldhell-core

[![CI](https://github.com/wave-owl/fldigi-feldhell-core/actions/workflows/ci.yml/badge.svg)](https://github.com/wave-owl/fldigi-feldhell-core/actions/workflows/ci.yml)

A C99 library that receives and transmits **Hellschreiber** exactly the way fldigi 4.2.13
does, for all seven fldigi submodes (Feld Hell, Slow Hell, Feld Hell X5, Feld Hell X9, FSK
Hell-245, FSK Hell-105, Hell 80): audio in, the raster fldigi draws out; characters in,
audio out, with all 15 fldigi raster fonts. It is a line-by-line port of `src/feld/feld.cxx`,
`src/include/feld.h`, `src/include/fontdef.h` and the fonts `src/feld/Feld*.cxx`, built on
the shared [fldigi-dsp-core](https://github.com/wave-owl/fldigi-dsp-core) (Hilbert FIR,
fftfilt, moving averages). On the reference build the transmitter reproduces fldigi's TX
audio sample for sample and the receiver reproduces every raster column fldigi hands to its
display byte for byte (see [Verification](#verification)).

**One part is new and not an fldigi port:** fldigi shows the received raster to the
operator and never turns it back into text. This library adds a **recogniser** that matches
the raster against the glyphs of the ported fonts and reports characters with a confidence;
below a confidence floor it reports "no match" instead of guessing
([The recogniser](#the-recogniser-new-code)). It is the only Hellschreiber implementation of
[wave-owl](https://github.com/wave-owl/wave-owl).

| | |
|---|---|
| **Is** | A single-channel **receiver** for every Hellschreiber submode of fldigi 4.2.13: the raster columns of fldigi's display (height 14..42 pixels, fldigi's AGC, peak hold, blackboard, reverse, squelch and receive filter), fldigi's metric, and recognised text. |
| **Is** | The matching **transmitter** (capability tag `tx=1`): preamble, keyboard characters in any of the 15 fonts, the four key shapes, TX widths 1-3, idle and postamble, as fldigi's `tx_process`. |
| **Is not** | A skimmer: each instance receives one submode at one carrier. No RSID, no CW ID, no rate conversion (Hellschreiber runs at 8000 Hz). The recogniser knows the glyphs of the ported fonts only; a hand-drawn or foreign font gives "no match". |
| | An I/O layer, threaded or allocating at run time: no sound card or file access, no threads, all memory allocated in `*_create()` (only the stand-alone `ffeldhell_recognise` allocates a temporary instance), no global mutable state. |

## Contents

- [Hellschreiber in one paragraph](#hellschreiber-in-one-paragraph)
- [Submodes](#submodes)
- [Signal chain](#signal-chain)
- [fldigi source map](#fldigi-source-map)
- [fldigi behaviour kept on purpose](#fldigi-behaviour-kept-on-purpose)
- [The recogniser (new code)](#the-recogniser-new-code)
- [API reference](#api-reference)
- [Usage example](#usage-example)
- [Contract and versioning](#contract-and-versioning)
- [Verification](#verification)
- [Performance and memory](#performance-and-memory)
- [Building](#building)
- [Maintenance and fldigi updates](#maintenance-and-fldigi-updates)
- [License and credits](#license-and-credits)

## Hellschreiber in one paragraph

Hellschreiber (Rudolf Hell, 1929) is a facsimile teleprinter: every character is a small
bitmap sent column by column, each column as 14 on/off pixels from bottom to top. The
receiver paints the pixels in time order onto a strip, so the text appears as an image and
the reader's eye does the decoding, also through noise and with any font. Feld Hell keys a
carrier on and off (AM); the FSK variants shift between two tones instead. Because the
receiver's column clock is never exactly the sender's, fldigi paints every column twice,
one above the other, so that a slanted line of text stays readable.

## Submodes

All at 8000 Hz, 14 pixels per transmitted column, receive height `HellRcvHeight` pixels per
column (fldigi 20); `feld::restart` (`feld.cxx:146-259`), mode ids of fldigi 4.2.13
(`globals.cxx:93-99`), receive bandwidth defaults `HELL_BW_*` (`configuration.h:736-742`):

| Submode | fldigi id | Family | Columns/s | Pixels/s (TX) | Bandwidth | restart filter | Default receive bandwidth |
|---|---|---|---|---|---|---|---|
| FELDHELL (Feld Hell) | 31 | AM | 17.5 | 245 | 245 Hz | 295 Hz | 245 Hz |
| SLOWHELL (Slow Hell) | 32 | AM | 2.1875 | 30.625 | 30.625 Hz | 35 Hz | 30 Hz |
| HELLX5 (Feld Hell X5) | 33 | AM, hard keyed | 87.5 | 1225 | 1225 Hz | 1470 Hz | 1225 Hz |
| HELLX9 (Feld Hell X9) | 34 | AM, hard keyed | 157.5 | 2205 | 2205 Hz | 2645 Hz | 2205 Hz |
| FSKH245 (FSK Hell-245) | 35 | FSK, shift 122.5 Hz | 17.5 | 245 | 122.5 Hz | 490 Hz | 250 Hz |
| FSKH105 (FSK Hell-105) | 36 | FSK, shift 55 Hz | 17.5 | 245 | 55 Hz | 220 Hz | 120 Hz |
| HELL80 (Hell 80) | 37 | FSK, shift 300 Hz | 35 | 490 | 300 Hz | 1200 Hz | 450 Hz |

"Bandwidth" is fldigi's `bandwidth` (the carrier clamp of `modem::set_freq` uses it and the
FSK shift is `+-bandwidth/2`). The two filter columns matter because of quirk Q10.

## Signal chain

Receiver (`feld::rx_process`): real audio -> `(x, x)` -> Hilbert FIR (37 taps) -> mixer at
the carrier -> `fftfilt` low pass (1024) -> per sample: AM `rx` (magnitude, peak per pixel,
peak hold, AGC average) or FM `FSKH_rx` (phase step discriminator, 8-sample moving average,
AGC average); a fractional counter at `height x columns/s` ends each pixel; a full column
of `height` pixels goes to the raster (`put_rx_data`) unless squelched, and on to the
recogniser.

Transmitter (`feld::tx_process`): three '.' as preamble, every character as a blank column,
its glyph columns (bit 0 = bottom row first) and a blank column, a space as five blank
columns, two blank columns per idle step, three '.' and a space as postamble. Each pixel is
a run of samples of an NCO until the pixel counter wraps: Feld and Slow Hell shape the key
edges with raised-cosine `OnShape`/`OffShape`, X5/X9 key hard, the FSK modes shift the tone
by `-+bandwidth/2`. Peak 1.0 before fldigi's TX level.

## fldigi source map

fldigi 4.2.13 (mirror `wave-owl/fldigi`, tag v4.2.13, commit e7c3ab37):

| fldigi | Lines | Here |
|---|---|---|
| `feld::tx_init` | `feld.cxx:59-67` | `ffeldhell_tx_begin` (`feldhell_tx.c`) |
| `feld::rx_init` | `feld.cxx:69-118` | `rx_init` (`feldhell_rx.c`) |
| `feld::restart` (submode table, filters) | `feld.cxx:146-259` | `feld_params_init` (`feldhell_common.c`), `ffeldhell_rx_create` |
| constructor | `feld.cxx:268-292` | `ffeldhell_rx_create`, `ffeldhell_tx_create` |
| `feld::mixer` | `feld.cxx:296-310` | `mixer` |
| `feld::FSKH_rx` | `feld.cxx:312-376` | `fskh_rx` |
| `feld::rx` | `feld.cxx:378-445` | `am_rx` |
| `feld::rx_process` | `feld.cxx:447-510` | `ffeldhell_rx_process`, `clear_state` |
| `feld::get_font_data` | `feld.cxx:521-561` | `feld_font_data` (`feldhell_common.c`) |
| `feld::nco` | `feld.cxx:563-572` | `nco` (`feldhell_tx.c`) |
| `feld::send_symbol` | `feld.cxx:574-625` | `send_symbol` |
| `feld::send_null_column` | `feld.cxx:627-631` | `send_null_column` |
| `feld::tx_char` | `feld.cxx:633-659` | `tx_char` |
| `feld::tx_process` | `feld.cxx:661-715` | `ffeldhell_tx_begin/put/idle/end` |
| `feld::initKeyWaveform` | `feld.cxx:717-741` | `init_key_waveform` |
| class `feld`, constants | `feld.h:37-110` | `feldhell_internal.h` |
| `struct fntchr` | `fontdef.h:33` | `feldhell_font` |
| the 15 fonts | `src/feld/Feld*.cxx`, `feldfonts.cxx:41-56` | `feldhell_fonts.c` (generated by `tools/gen_fonts.py`) |
| `C_FIR_filter`, `fftfilt`, `Cmovavg` | `filters.cxx`, `fftfilt.cxx` | fldigi-dsp-core 0.5.0 (`dsp/`) |

## fldigi behaviour kept on purpose

Reproduced bit for bit; a later change to any of them is a visible decision:

| # | Behaviour | Source |
|---|---|---|
| Q1 | AM peak hold decays by `1 - 0.02/height` per pixel when not refreshed. | `feld.cxx:397-400` |
| Q2 | AGC decay per `hellagc`: 1 slow (`0.01/height`), 2 medium (`0.075/height`, fldigi's default), 3 fast (`0.2/height`). | `feld.cxx:331-344, 403-416` |
| Q3 | The squelch compares `metric = clamp(1000 agc, 0, 100)` (the averaged magnitude, not an SNR) with the squelch level; squelched columns are not displayed. | `feld.cxx:346-359, 418-427` |
| Q4 | Blackboard inverts the pixels (AM `255 - ix` is skipped, FM `1 - vid`). | `feld.cxx:351-354, 421-422` |
| Q5 | `reverse` mirrors the FSK shift in TX and the video in RX (FM only; AM ignores it). | `feld.cxx:351-352, 586-587` |
| Q6 | The restart filter is `5 round(1.2 bw / 5)` for AM and `5 round(4.0 bw / 5)` for FM. | `feld.cxx:160-226` |
| Q7 | The AGC average spans `500 x 8000 / (height x columns/s)` samples (11428 for Feld Hell at height 20, 91428 for Slow Hell). | `feld.cxx:236` |
| Q8 | A blank column before and after every character; a space is three more blank columns. | `feld.cxx:633-659` |
| Q9 | Each glyph column is sent `HellXmtWidth` times (1-3). | `feld.cxx:645-653` |
| Q10 | `rx_process` replaces the restart low pass (`filter_bandwidth / 8000`) by `0.5 HELL_BW / 8000` on the first block whenever the two differ. `HELL_BW` is one global setting ("Receive filter bandwidth", fldigi's default 245 Hz for every submode): at that default fldigi passes only +-122.5 Hz, which is right for Feld Hell and far too narrow for X5 and X9. `rx_bw_hz` is `HELL_BW`; 0 selects fldigi's per-mode default `HELL_BW_*` (the table above). | `feld.cxx:453-480` |
| Q11 | A silent start divides 0 by a peak hold of 0; fldigi's reference build (arm64) turns that NaN into pixel 0 (white). | `feld.cxx:401` |
| Q12 | Key shapes: slow 4 ms (32 samples, `pi i / 33`), medium 2 ms (16), fast 1 ms (8), hard; the key-up shape only covers the first 32 samples of a pixel, so at Slow Hell (261 samples per pixel) a pixel before a blank one ends after 32 samples. | `feld.cxx:597-603, 717-741` |
| Q13 | The NCO phase runs on across characters and overs; the pixel counter restarts with each over. | `feld.cxx:59-67, 563-572` |
| Q14 | fldigi's default TX font is number 4, "hell" (`feldhell_12`), not "7x7"; in it lower case is drawn as capitals. | `configuration.h:1043-1045` |
| Q15 | Characters outside `' '..'~'` send only their two blank columns; CR and LF are sent as space. | `feld.cxx:529, 709-710` |

## The recogniser (new code)

`src/feldhell_decode.c` is not a port; fldigi has no equivalent. It works on the raster the
ported receiver builds:

1. The displayed columns are joined into one pixel stream: one column of `height` pixels
   spans exactly one transmitted column of 14 pixels, so the stream is the received keying
   in time order and the vertical phase of the raster drops out. Pixel values become ink in
   `[0, 1]` (dark is ink, or bright with blackboard).
2. Every glyph of the selected fonts (default: "7x7" and fldigi's default TX font "hell")
   is rendered at the receive resolution with a blank column before and after it.
3. At the first ink pixel after the previous character, every template is tried at every
   offset within +-1 column; the score is the Pearson correlation of template and stream,
   clipped to `[0, 1]`.
4. The best fit is reported as a character only if its score reaches the floor (default
   0.55), the glyph stands out from its surroundings (ink contrast >= 0.4) and the score is
   significant for the glyph's size (`>= min(0.9, 3.5 / sqrt(ink pixels))`: a thin glyph
   such as `!` correlates with noise by chance far more often than a letter). Otherwise the
   block is reported as "no match" (`c = 0`) with its score -- never a guessed character.
5. Small glyphs (`.` and the apostrophe) fit at several heights. The column phase is the
   same for a whole transmission, so it is learned from confident matches of tall glyphs and
   then restricts the offsets to +-`height/10` pixels around it.
6. Five or more blank columns after a character are a word gap (a space is seven blank
   columns between two glyphs, other characters two).

The thresholds were set on synthetic AWGN tests (`tests/test_feldhell.c`, wave-owl's
conformance kit): no character from 4 s of noise at any level, and the sent text recovered
from fldigi's own rasters. Glyphs that are identical in a font (for example `O`/`0` in some
fonts, or the capitals fldigi's default font draws for lower case) are reported as the first
one in ASCII order.

## API reference

All functions are declared in [`include/fldigi_feldhell.h`](include/fldigi_feldhell.h).

| Function | Purpose |
|---|---|
| `ffeldhell_version()`, `ffeldhell_abi()` | Version string with contract and ABI tags, ABI number. |
| `ffeldhell_submode_count/at/find` | The seven submodes and their parameters. |
| `ffeldhell_font_count/name/find/columns` | The 15 fonts; a character's transmitted columns. |
| `ffeldhell_rx_config_default`, `ffeldhell_rx_create/destroy/reset` | Receiver instance with fldigi's defaults. |
| `ffeldhell_rx_process(rx, x, n, out, max)` | Feed 8 kHz samples; recognised characters (`{c, confidence}`, `c = 0` no match, `' '` word gap). |
| `ffeldhell_rx_flush` | Recognise what is still buffered at the end of input. |
| `ffeldhell_rx_take_columns(rx, out, max)` | The raster columns since the last call (`height` bytes each, fldigi's values). |
| `ffeldhell_rx_metric/carrier_hz/height/set_carrier/set_reverse/set_squelch/dropped/columns_dropped/footprint` | State and settings. |
| `ffeldhell_recognise(columns, n, height, mask, floor, out, max)` | The recogniser alone on a stored raster. |
| `ffeldhell_tx_config_default`, `ffeldhell_tx_create/destroy/bound` | Transmitter instance with fldigi's defaults; largest output of one call. |
| `ffeldhell_tx_begin/put/idle/end` | Preamble, one character, one idle step, postamble. |
| `ffeldhell_tx_columns`, `ffeldhell_tx_samples_per_column` | Columns of a character, mean samples per column. |

## Usage example

```c
#include "fldigi_feldhell.h"

ffeldhell_rx_config rc;
ffeldhell_rx_config_default(&rc, "FELDHELL");
ffeldhell_rx *rx = ffeldhell_rx_create(&rc);
ffeldhell_char ch[64];
size_t n = ffeldhell_rx_process(rx, samples, count, ch, 64);   /* 8 kHz, int16 / 32768 */
for (size_t i = 0; i < n; i++)
    if (ch[i].c)
        putchar(ch[i].c);
unsigned char raster[64 * 20];
size_t cols = ffeldhell_rx_take_columns(rx, raster, 64);         /* to draw it */
ffeldhell_rx_destroy(rx);

ffeldhell_tx_config tc;
ffeldhell_tx_config_default(&tc, "FELDHELL");
ffeldhell_tx *tx = ffeldhell_tx_create(&tc);
size_t bound = ffeldhell_tx_bound(tx);
double *buf = malloc(bound * sizeof *buf);
size_t k = ffeldhell_tx_begin(tx, buf, bound);                   /* preamble */
k = ffeldhell_tx_put(tx, 'C', buf, bound);                       /* ... per character */
k = ffeldhell_tx_end(tx, buf, bound);                            /* postamble */
ffeldhell_tx_destroy(tx);
```

## Contract and versioning

`ffeldhell_version()` returns `fldigi-feldhell-core <version> contract=feldhell-c1 abi=1 tx=1
dsp=<fldigi-dsp-core version>`. The contract tag changes only with an incompatible API
change; wave-owl refuses a library whose contract or ABI differs. Versions follow semantic
versioning ([CHANGELOG.md](CHANGELOG.md)).

`dsp/` is the [fldigi-dsp-core](https://github.com/wave-owl/fldigi-dsp-core) submodule,
pinned in [`dsp.lock`](dsp.lock) (version, commit, content checksum of the linked files);
`tools/check_dsp.sh` refuses to build against anything else (wave-owl R-920.3).

## Verification

`make test` runs:

- `tests/test_feldhell.c`: API and parameter checks (Q6), fonts, TX framing (Q8, Q9, Q15),
  loopback of every submode in fonts "hell" and "7x7" (the sent text, every character at or
  above the floor), FSK reverse (Q5), chunk invariance (1, 37, 512 samples and whole),
  reset, blackboard (Q4), squelch (Q3), memory bound, and the recogniser's "no match" rule
  on noise and solid ink.
- `tests/test_feldhell_fldigi.c` against goldens from fldigi 4.2.13 itself
  (`tests/golden/make_fldigi_golden.py`, the headless fldigi bench of wave-owl's archived
  change `fix-psk-rtty-vs-fldigi` plus the raster dump `tests/golden/fldigi-bench-feld-raster.patch`):
  - **TX**, 37 overs: all 7 submodes (CQ and pangram), all 15 fonts, key shapes 0-3 (Q12),
    TX widths 2 and 3: identical int16 output (FNV-1a hash) on the reference platform.
  - **RX**, 32 rasters: all 7 submodes clean and at -5 and -10 dB SNR in 2500 Hz, heights 14
    and 42, AGC 1 and 3 (Q2), blackboard (Q4), and fldigi's stock 245 Hz filter on Feld
    Hell, X9 and FSKH245 (Q10): every column fldigi hands to `put_rx_data` byte for byte on
    the reference platform; elsewhere the same column count and the same recognised text.
    The text recognised from fldigi's own clean rasters is the sent text.

The reference platform is macOS arm64 with Apple clang (the fldigi bench was built there);
other platforms (Linux/gcc, glibc's libm) are held to the same column count and text, and to
at most 1 LSB of TX difference. `make sanitize` runs both under ASan and UBSan.

## Performance and memory

`make bench` (Apple M-series, one core, 60 s of signal per submode):

| Submode | RX x real time | RX CPU | TX x real time | RX memory |
|---|---|---|---|---|
| FELDHELL | 1724 | 0.06 % | 15070 | 399 KiB |
| SLOWHELL | 3073 | 0.03 % | 36162 | 1024 KiB |
| HELLX5 | 1554 | 0.06 % | 22469 | 327 KiB |
| HELLX9 | 1120 | 0.09 % | 15920 | 319 KiB |
| FSKH245 | 2704 | 0.04 % | 36479 | 399 KiB |
| FSKH105 | 2667 | 0.04 % | 35818 | 399 KiB |
| HELL80 | 2240 | 0.04 % | 27926 | 354 KiB |

Budget (wave-owl R-901.5): below 5 % of one core and 2 MB per receiver for every submode;
Slow Hell's memory is mostly fldigi's 91428-sample AGC average (Q7).

## Building

```
git clone --recursive https://github.com/wave-owl/fldigi-feldhell-core
cd fldigi-feldhell-core
make            # build/libfldigi_feldhell.a and .so/.dylib
make test       # unit tests and fldigi goldens
make sanitize   # the same under ASan + UBSan
make bench      # CPU and memory
```

Plain make, C99, no dependencies besides libm and `dsp/`.

## Maintenance and fldigi updates

- Fonts: `python3 tools/gen_fonts.py /path/to/fldigi > src/feldhell_fonts.c`.
- Goldens: build the fldigi bench (wave-owl `fix-psk-rtty-vs-fldigi` bench/BUILD.md), apply
  `tests/golden/fldigi-bench-feld-raster.patch`, then
  `FLDIGI_BENCH_BIN=/path/to/src-bench/src/fldigi python3 tests/golden/make_fldigi_golden.py`.
- A new fldigi release: diff the cited ranges (wave-owl `scripts/fldigi_upstream_check.py`),
  port the changes, regenerate fonts and goldens.

## License and credits

GPL-3.0-or-later, as fldigi ([LICENSE](LICENSE)). Ported from fldigi 4.2.13: `feld.cxx`,
`feld.h`, `fontdef.h` and the fonts (c) 2006-2014 Dave Freese W1HKJ, with much owed to Joe
Veldhuis N8FQ, adapted from gmfsk (c) 2001-2004 Tomi Manninen OH2BNS and Lawrence Glaister
VE7IT. The recogniser and the C port (c) 2026 Gerolf Ziegenhain.
