#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CONDUCTOR's wind and brass tone: each instrument's spectral envelope (harmonic level against frequency) as measured
from recordings, soft (p) and loud (f), into firmware/gen/winds_env.h. The firmware builds its wave tables from these
at start (one per pitch band and dynamic). The recordings are not part of the repo: pass the folder of the
"all-samples" set (Philharmonia-style per-note recordings) and a trumpet scale recording.

    python tools/gen_winds.py SAMPLES_DIR TRUMPET_SCALE.mp3

High wind: oboe (E4 p, and a phrase of 19 notes). Low wind: bassoon (F3 p, and a legato phrase of 19 notes).
High brass: trumpet (its mf from the scale, 29 notes; its f from D4 f). Low brass: horn (D#4 p, A#1 p).
Each envelope: the 75th percentile of every steady note's harmonic levels in 1/3-octave bands, smoothed, on a grid of
1/6 octave from 50 Hz; past the last measured band it keeps falling 18 dB an octave. Soft and loud: the measured
dynamic, made darker (soft) or brighter (loud) above the formant by the step measured for the trumpet (mf -> f) and
judged for the others."""
import sys
from pathlib import Path
import numpy as np
import soundfile as sf

SR = 44100


def mono(p):
    x, sr = sf.read(str(p))
    x = x.mean(1) if x.ndim > 1 else x
    if sr != SR:
        n = int(len(x) * SR / sr); x = np.interp(np.arange(n) * sr / SR, np.arange(len(x)), x)
    return x


def yin(x, lo, hi, hop=0.005, win=0.04, th=0.2):
    n = int(win * SR); h = int(hop * SR); f = []
    for i in range(0, len(x) - n, h):
        s = x[i:i + n] - x[i:i + n].mean()
        if np.sqrt((s ** 2).mean()) < 1e-3:
            f.append(0); continue
        b = int(SR / lo); a = int(SR / hi)
        d = np.array([((s[:n - t] - s[t:]) ** 2).sum() for t in range(1, b + 1)])
        cm = d * np.arange(1, b + 1) / np.maximum(np.cumsum(d), 1e-12)
        w = np.where(cm[a - 1:b] < th)[0]; t = a - 1 + (w[0] if len(w) else np.argmin(cm[a - 1:b]))
        while t + 1 < b and cm[t + 1] < cm[t]:
            t += 1
        f.append(SR / (t + 1))
    return np.array(f)


def harmonics(seg, f0, kmax=40):
    N = len(seg); S = np.abs(np.fft.rfft(seg * np.hanning(N))); fq = np.fft.rfftfreq(N, 1 / SR); H = []
    for k in range(1, kmax + 1):
        if f0 * k > 9000:
            break
        b = (fq > f0 * k * 0.97) & (fq < f0 * k * 1.03); H.append(S[b].max())
    H = np.array(H)
    return list(zip(f0 * np.arange(1, len(H) + 1), 20 * np.log10(H / H.max())))


def notes(x, lo, hi, minms=140):
    f = yin(x, lo, hi); m = np.where(f > 0, np.round(12 * np.log2(np.maximum(f, 1) / 440) + 69), 0).astype(int)
    out = []; i = 0
    while i < len(m):
        j = i
        while j < len(m) and m[j] == m[i]:
            j += 1
        if m[i] > 0 and (j - i) * 5 >= minms:
            out.append((i * 5, (j - i) * 5))
        i = j
    pts = []
    for st, du in out:
        fr = f[(st + du // 5) // 5:(st + du * 85 // 100) // 5]; fr = fr[fr > 0]
        if len(fr) > 5:
            pts += harmonics(x[int((st + 0.2 * du) * SR / 1000):int((st + 0.85 * du) * SR / 1000)], np.median(fr))
    return pts


def steady(x, f0):
    e = np.abs(x); on = np.argmax(e > 0.1 * e.max()); off = len(x) - np.argmax(e[::-1] > 0.1 * e.max())
    return harmonics(x[on + (off - on) // 4:on + 3 * (off - on) // 4], f0)


GRID = 50 * 2 ** (np.arange(49) / 6)


def envelope(points):
    hz = np.array([p[0] for p in points]); db = np.array([p[1] for p in points])
    g = 2 ** np.arange(np.log2(50), np.log2(12800), 1 / 6); e = []
    for c in g:
        s = (hz > c / 2 ** (1 / 6)) & (hz < c * 2 ** (1 / 6))
        e.append(np.percentile(db[s], 75) if s.any() else np.nan)
    e = np.array(e); ok = ~np.isnan(e)
    e = np.interp(np.log2(g), np.log2(g[ok]), e[ok])
    e = np.convolve(np.pad(e, 2, mode='edge'), np.ones(5) / 5, 'valid')
    top = np.log2(g[ok][-1]); e = np.where(np.log2(g) > top, e - 18 * (np.log2(g) - top), e)
    return np.interp(np.log2(GRID), np.log2(g), e - e.max())


def tilt(e, peak, db_oct, max_oct=2.0):
    """brighter (or darker) above the formant, by db_oct an octave, for at most max_oct octaves: past that the measured
    roll-off goes on (an unlimited tilt lifted the far top, 6-20 kHz, into an airy hiss at loud dynamics)"""
    return e + db_oct * np.clip(np.log2(GRID / peak), 0, max_oct)


def main():
    D = Path(sys.argv[1]); scale = mono(sys.argv[2])
    ob = envelope(steady(mono(D / "oboe/oboe_E4_15_piano_normal.mp3"), 329.6) +
                  notes(mono(D / "oboe/oboe_E4_phrase_piano_normal.mp3"), 230, 1300))
    bn = envelope(steady(mono(D / "bassoon/bassoon_F3_025_piano_normal.mp3"), 174.6) +
                  notes(mono(D / "bassoon/bassoon_F3_phrase_mezzo-forte_legato.mp3"), 80, 400))
    hn = envelope(steady(mono(D / "french horn/french-horn_Ds4_025_piano_normal.mp3"), 311.1) +
                  steady(mono(D / "french horn/french-horn_As1_025_piano_normal.mp3"), 58.3))
    tf = envelope(steady(mono(D / "trumpet/trumpet_D4_05_forte_normal.mp3"), 294.0))
    tm = envelope(notes(scale, 150, 1200, 120))
    # soft = p, loud = f: oboe measured p (+1.5 dB/oct a step), bassoon measured between p and mf (+2), horn p (+3);
    # the trumpet: its soft table is mf as measured (the scale), its loud f as measured (the firmware maps mf onto soft)
    E = [(ob, tilt(ob, 1100, 4.5)), (tilt(bn, 400, -2), tilt(bn, 400, 4)), (tm, tf), (hn, tilt(hn, 400, 9))]
    out = ["/* generated by tools/gen_winds.py: the winds' and brass' spectral envelopes, dB (0 = the formant), on a grid",
           " * of 1/6 octave from 50 Hz (49 points, to 12.8 kHz); [part][0 soft (p; the trumpet's mf), 1 loud (f)]. Parts: high wind (oboe),",
           " * low wind (bassoon), high brass (trumpet), low brass (horn), from recordings (see the script) */",
           "#pragma once", "static const int8_t WENV[4][2][49] = {"]
    for soft, loud in E:
        out.append("    {{" + ", ".join(str(int(round(max(-90, v)))) for v in soft) + "},")
        out.append("     {" + ", ".join(str(int(round(max(-90, v)))) for v in loud) + "}},")
    out.append("};")
    out.append("static const uint16_t DBQ15[96] = {   /* 0..95 dB down -> Q15 */")
    v = [int(round(32767 * 10 ** (-d / 20))) for d in range(96)]
    out += ["    " + ", ".join(map(str, v[i:i + 16])) + "," for i in range(0, 96, 16)]
    out.append("};")
    out.append("static const uint16_t LOG2K[128] = {   /* 6 log2(k), Q4: harmonic k's place on the grid */")
    v = [0] + [int(round(6 * np.log2(k) * 16)) for k in range(1, 128)]
    out += ["    " + ", ".join(map(str, v[i:i + 16])) + "," for i in range(0, 128, 16)]
    out.append("};")
    p = Path(__file__).resolve().parent.parent / "firmware" / "gen" / "winds_env.h"
    p.write_text("\n".join(out) + "\n")
    print(p)


if __name__ == "__main__":
    main()
