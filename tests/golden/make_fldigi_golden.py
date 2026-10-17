#!/usr/bin/env python3
"""Golden data from fldigi 4.2.13 itself for fldigi-feldhell-core (stdlib only).

Drives the headless fldigi bench build (configure --enable-benchmark plus the TX extension
of wave-owl's archived change fix-psk-rtty-vs-fldigi, bench/BUILD.md, and the raster dump
of wave-owl's change add-fldigi-hellschreiber-core, bench/fldigi-bench-feld-raster.patch)
for every Hellschreiber submode of fldigi 4.2.13 (src/globals/globals.cxx:93-99):

* TX: fldigi renders the text (bench-tx, txlevel -12 dB, PCM16, 8000 Hz) with the font,
  pulse shape and width of the case (fldigi_def.xml FELDFONTNBR, HELLPULSEFAST,
  HELLXMTWIDTH). The FNV-1a 64 hash of every vector goes to tx_hashes.txt; the stored
  overs are kept as raw little-endian int16 in tx/<stem>.s16.
* RX: fldigi receives 1 s silence + a stored TX vector + 2 s silence, clean and buried in
  deterministic noise at -5 and -10 dB (2500 Hz reference bandwidth, see noisy()), with the
  receive settings of the case (HELL_BW0, HELLRCVHEIGHT, HELLAGC, HELLBLACKBOARD). Every
  column fldigi hands to put_rx_data goes to rx/<stem>_<snr>.raster (height bytes each).

    FLDIGI_BENCH_BIN=/path/to/src-bench/src/fldigi python3 tests/golden/make_fldigi_golden.py

The C tests (tests/test_feldhell_fldigi.c) rebuild the same noisy inputs bit for bit.
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations

import array
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
FLDIGI = os.environ.get("FLDIGI_BENCH_BIN", "fldigi")
CARRIER_HZ = 1500
TAIL_S = 2
TEXTS = {"cq": "CQ CQ DE DG6FL DG6FL K",
         "pangram": "the quick brown fox jumps over the lazy dog THE QUICK BROWN FOX "
                    "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ 73"}

# (sname, fldigi trx_mode id = index into mode_info[], fldigi HELL_BW_* default)
SUB = {"FELDHELL": (31, 245.0), "SLOWHELL": (32, 30.0), "HELLX5": (33, 1225.0),
       "HELLX9": (34, 2205.0), "FSKH245": (35, 250.0), "FSKH105": (36, 120.0),
       "HELL80": (37, 450.0)}
SNRS = ("clean", "m05", "m10")


def tx_cases():
    """(stem, sname, text, font, pulse, width, stored)"""
    out = [(s.lower(), s, "cq", 4, 1, 1, True) for s in SUB]
    out += [(s.lower() + "_pangram", s, "pangram", 4, 1, 1, False) for s in SUB]
    out += [(f"feldhell_font{f:02d}", "FELDHELL", "pangram", f, 1, 1, False)
            for f in range(15)]
    out += [(f"feldhell_pulse{p}", "FELDHELL", "cq", 4, p, 1, False) for p in (0, 2, 3)]
    out += [("slowhell_pulse0", "SLOWHELL", "cq", 4, 0, 1, False)]
    out += [(f"feldhell_width{w}", "FELDHELL", "cq", 4, 1, w, False) for w in (2, 3)]
    out += [("fskh245_font00", "FSKH245", "cq", 0, 1, 1, False),
            ("hellx9_font00", "HELLX9", "cq", 0, 1, 1, False)]
    return out


def rx_cases():
    """(stem, source tx stem, sname, hell_bw, height, agc, blackboard, snrs)"""
    out = [(s.lower(), s.lower(), s, SUB[s][1], 20, 2, 0, SNRS) for s in SUB]
    out += [("feldhell_h14", "feldhell", "FELDHELL", 245.0, 14, 2, 0, ("clean",)),
            ("feldhell_h42", "feldhell", "FELDHELL", 245.0, 42, 2, 0, ("clean",)),
            ("feldhell_agc1", "feldhell", "FELDHELL", 245.0, 20, 1, 0, ("clean", "m05")),
            ("feldhell_agc3", "feldhell", "FELDHELL", 245.0, 20, 3, 0, ("clean", "m05")),
            ("feldhell_bb", "feldhell", "FELDHELL", 245.0, 20, 2, 1, ("clean",)),
            ("feldhell_bw295", "feldhell", "FELDHELL", 295.0, 20, 2, 0, ("clean",)),
            ("hellx9_bw245", "hellx9", "HELLX9", 245.0, 20, 2, 0, ("clean",)),
            ("fskh245_bw245", "fskh245", "FSKH245", 245.0, 20, 2, 0, ("clean",)),
            ("fskh245_bb", "fskh245", "FSKH245", 250.0, 20, 2, 1, ("clean",))]
    return out


def config_xml(**tags):
    body = "".join(f"<{k}>{v}</{k}>\n" for k, v in tags.items())
    return "<FLDIGI_DEFS>\n" + body + "</FLDIGI_DEFS>\n"


def run_fldigi(sname, args, xml, env_extra=None):
    cfg = tempfile.mkdtemp(prefix="fldigi-feld-golden-")
    Path(cfg, "fldigi_def.xml").write_text(xml)
    env = {k: v for k, v in os.environ.items() if not k.startswith("FLDIGI_BENCH_")}
    env.update(env_extra or {})
    try:
        p = subprocess.run([FLDIGI, "--config-dir", cfg, "--benchmark-modem",
                            str(SUB[sname][0]), "--benchmark-frequency", str(CARRIER_HZ),
                            *args], env=env, capture_output=True, text=True, timeout=3600)
    finally:
        shutil.rmtree(cfg, ignore_errors=True)
    if f"mode={sname} " not in p.stderr:
        raise RuntimeError(f"fldigi did not run {sname}: {p.stderr[-400:]}")
    return p.stderr


def read_wav(path):
    with wave.open(str(path)) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        sr = w.getframerate()
        a = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        a.byteswap()
    return sr, a


def write_wav(path, sr, pcm):
    a = array.array("h", pcm)
    if sys.byteorder == "big":
        a.byteswap()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(a.tobytes())


def fnv1a64(pcm):
    h = 0xCBF29CE484222325
    for v in pcm:
        u = v & 0xFFFF
        for byte in (u & 0xFF, u >> 8):
            h ^= byte
            h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Lcg:
    """32-bit LCG (Numerical Recipes); the same in tests/test_feldhell_fldigi.c."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def uniform(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s / 4294967296.0


def noisy(pcm, sr, snr, seed):
    """1 s + pcm + TAIL_S s, plus Irwin-Hall Gaussian noise at -5 or -10 dB SNR in 2500 Hz.

    Only +, -, *, / and sqrt (all correctly rounded), so C reproduces it bit for bit.
    SNR = mean square of the burst / (noise variance * 2500 / (sr / 2)).
    """
    padded = [0.0] * sr + [float(v) for v in pcm] + [0.0] * (TAIL_S * sr)
    if snr == "clean":
        return [int(v) for v in padded]
    ps = 0.0
    for v in pcm:
        ps += float(v) * float(v)
    ps = ps / len(pcm)
    inv = {"m05": 1.0 / math.sqrt(10.0), "m10": 1.0 / 10.0}[snr]
    s2 = ps / inv * (sr / 2.0) / 2500.0
    sd = math.sqrt(s2)
    gain = 3276.7 / math.sqrt(ps * len(pcm) / len(padded) + s2)
    rng = Lcg(seed)
    out = []
    for v in padded:
        z = 0.0
        for _ in range(12):
            z += rng.uniform()
        z -= 6.0
        y = (v + sd * z) * gain
        q = math.floor(y + 0.5)
        out.append(max(-32768, min(32767, int(q))))
    return out


def one_tx(idx, case):
    stem, sname, key, font, pulse, width, stored = case
    xml = config_xml(FELDFONTNBR=font, HELLPULSEFAST=pulse, HELLXMTWIDTH=width)
    with tempfile.TemporaryDirectory() as td:
        wav = Path(td, stem + ".wav")
        run_fldigi(sname, ["--benchmark-input", "1"], xml,
                   {"FLDIGI_BENCH_TX_TEXT": TEXTS[key], "FLDIGI_BENCH_TX_WAV": str(wav)})
        sr, pcm = read_wav(wav)
    if stored:
        raw = array.array("h", pcm)
        if sys.byteorder == "big":
            raw.byteswap()
        Path(HERE, "tx", stem + ".s16").write_bytes(raw.tobytes())
    line = (f"{stem} {sname} {key} {idx} {sr} {len(pcm)} {font} {pulse} {width} "
            f"{fnv1a64(pcm):016x}")
    print(line, flush=True)
    return line


def one_rx(idx, case):
    stem, src, sname, bw, height, agc, bb, snrs = case
    raw = array.array("h", Path(HERE, "tx", src + ".s16").read_bytes())
    if sys.byteorder == "big":
        raw.byteswap()
    xml = config_xml(HELL_BW0=bw, HELLRCVHEIGHT=height, HELLAGC=agc,
                     HELLBLACKBOARD=bb)
    with tempfile.TemporaryDirectory() as td:
        for i, snr in enumerate(SNRS):
            if snr not in snrs:
                continue
            inp = Path(td, f"{stem}_{snr}.wav")
            write_wav(inp, 8000, noisy(raw, 8000, snr, 1000 * idx + i))
            out = Path(td, f"{stem}_{snr}.raster")
            run_fldigi(sname, ["--benchmark-input", str(inp)], xml,
                       {"FLDIGI_BENCH_FELD_RASTER": str(out)})
            data = out.read_bytes() if out.exists() else b""
            assert len(data) % height == 0
            Path(HERE, "rx", f"{stem}_{snr}.raster").write_bytes(data)
    line = f"{stem} {src} {sname} {idx} {bw:g} {height} {agc} {bb} {','.join(snrs)}"
    print(line, flush=True)
    return line


def main():
    (HERE / "tx").mkdir(exist_ok=True)
    (HERE / "rx").mkdir(exist_ok=True)
    jobs = int(os.environ.get("JOBS", "8"))
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        tx_lines = list(ex.map(lambda a: one_tx(*a), enumerate(tx_cases())))
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        rx_lines = list(ex.map(lambda a: one_rx(*a), enumerate(rx_cases())))
    (HERE / "tx_hashes.txt").write_text(
        "# stem fldigi-sname text index samplerate samples font pulse width fnv1a64(int16 LE)"
        " -- fldigi 4.2.13 bench TX, txlevel -12 dB\n" + "\n".join(tx_lines) + "\n")
    (HERE / "rx_cases.txt").write_text(
        "# stem source fldigi-sname index HELL_BW height hellagc blackboard snrs -- fldigi "
        "4.2.13 bench RX of 1 s + source + 2 s; noise seed = 1000 * index + snr step\n"
        + "\n".join(rx_lines) + "\n")
    (HERE / "texts.txt").write_text("".join(f"{k} {v}\n" for k, v in TEXTS.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
