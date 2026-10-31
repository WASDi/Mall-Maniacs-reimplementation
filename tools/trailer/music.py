#!/usr/bin/env python3
"""Synthesized score for the Mall Maniacs HD trailer (Trailer.md, phase T1).

Every sound is generated from formulas (numpy + stdlib only, no samples).
125 BPM in 4/4: one beat = 0.48 s = 12 video frames, one bar = 1.92 s = 48
frames; 31.25 bars = 60 s. Arc: calm 1999 opening in D major (bars 1-4), the
reveal hit on the green start light (bar 5) into an epic D minor act, a
beauty section (13-16), the action act (17-24), a build (25-26) with one
beat of silence, the logo slam in D major (27-29) and a quiet tag (30-31).

Outputs (default directory build/trailer/):
  music.wav              48 kHz, 16-bit, stereo, exactly 60.0 s
  music_markers.txt      bar/beat times and section names for the edit
  music_spectrogram.png  log-frequency spectrogram with bar lines (review aid)

Usage:
  tools/trailer/music.py [--out DIR] [--preview A-B]
"""

import argparse
import os
import sys
import wave

import numpy as np

SR = 48000
BPM = 125
BEAT = 60.0 / BPM
BAR = 4 * BEAT
FPS = 25                      # trailer video frame rate (one frame per 40 ms sim tick)
LENGTH = 60.0                 # final length in seconds
RENDER = LENGTH + 2.0         # internal buffer length (room for note tails)
N = int(RENDER * SR)

RNG = np.random.default_rng(1999)


def mtof(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def pan_gains(pan):
    """Constant-power pan, -1 = left .. +1 = right."""
    theta = (pan + 1.0) * np.pi / 4.0
    return np.cos(theta), np.sin(theta)


class Bus:
    """A stereo track that notes are mixed into."""

    def __init__(self):
        self.buf = np.zeros((N, 2))

    def add(self, t0, sig, pan=0.0, gain=1.0):
        start = int(round(t0 * SR))
        if start >= N:
            return
        if sig.ndim == 1:
            gl, gr = pan_gains(pan)
            sig = np.stack([sig * gl, sig * gr], axis=1)
        end = min(N, start + len(sig))
        self.buf[start:end] += sig[:end - start] * gain


# --- Frequency-domain filters (no scipy) -------------------------------------

def fft_filter(x, response, pad=SR // 2):
    """Zero-phase filter: multiply the spectrum by response(freqs)."""
    n = len(x) + pad
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    gain = response(freqs)
    if x.ndim == 2:
        gain = gain[:, None]
    y = np.fft.irfft(np.fft.rfft(x, n=n, axis=0) * gain, n=n, axis=0)
    return y[:len(x)]


def lowpass(fc, order=2):
    return lambda f: 1.0 / np.sqrt(1.0 + (f / fc) ** (2 * order))


def highpass(fc, order=2):
    return lambda f: 1.0 / np.sqrt(1.0 + (fc / np.maximum(f, 1e-3)) ** (2 * order))


def bandpass(lo, hi):
    lp, hp = lowpass(hi, 2), highpass(lo, 2)
    return lambda f: lp(f) * hp(f)


# --- Instruments --------------------------------------------------------------

def piano(midi, vel, dur):
    """Additive piano: inharmonic partials, two-stage per-partial decay,
    hammer transient. dur is how long the key (or pedal) holds the note."""
    f0 = mtof(midi)
    tau = float(np.clip(1.8 * (262.0 / f0) ** 0.6, 0.5, 6.0))
    length = min(dur, 4.0 * tau) + 0.4
    t = np.arange(int(length * SR)) / SR
    sig = np.zeros_like(t)
    brightness = 0.35 + 0.65 * vel
    inharm = 0.0004
    for k in range(1, 13):
        fk = k * f0 * np.sqrt(1.0 + inharm * k * k)
        if fk > 15000:
            break
        amp = k ** -1.15 * np.exp(-(k - 1) * (1.0 - brightness) * 0.8)
        tau_k = tau / (1.0 + 0.4 * (k - 1))
        env = 0.55 * np.exp(-t / (tau_k * 0.15)) + 0.45 * np.exp(-t / tau_k)
        detunes = (-0.45, 0.45) if k <= 4 else (0.0,)
        for cents in detunes:
            f = fk * 2.0 ** (cents / 1200.0)
            sig += amp / len(detunes) * env * np.sin(2 * np.pi * f * t + RNG.uniform(0, 2 * np.pi))
    sig *= np.minimum(1.0, t / 0.003)
    damp = t > dur
    sig[damp] *= np.exp(-(t[damp] - dur) / 0.12)
    hammer = RNG.standard_normal(len(t)) * np.exp(-t / 0.004)
    hammer = np.convolve(hammer, np.ones(6) / 6, mode="same")
    sig += hammer * 0.04 * vel
    return sig * vel


def celesta(midi, vel, length=2.5):
    """2-operator FM bell (ratio 1:3.5) plus a fast-decaying octave sine."""
    fc = mtof(midi)
    t = np.arange(int(length * SR)) / SR
    index = 2.2 * np.exp(-t / 0.25)
    sig = np.sin(2 * np.pi * fc * t + index * np.sin(2 * np.pi * 3.5 * fc * t))
    sig *= np.exp(-t / 0.9)
    sig += 0.3 * np.sin(2 * np.pi * 2 * fc * t) * np.exp(-t / 0.3)
    sig *= np.minimum(1.0, t / 0.002)
    return sig * vel


def saw_polyblep(freq, phase0):
    """Band-limited sawtooth for a per-sample frequency array."""
    inc = freq / SR
    phase = (phase0 + np.cumsum(inc)) % 1.0
    s = 2.0 * phase - 1.0
    lo = phase < inc
    x = phase[lo] / inc[lo]
    s[lo] -= x + x - x * x - 1.0
    hi = phase > 1.0 - inc
    x = (phase[hi] - 1.0) / inc[hi]
    s[hi] -= x * x + x + x + 1.0
    return s


def string_note(midi, dur, attack, release, voices=7):
    """String ensemble note: detuned PolyBLEP saws with slow vibrato,
    spread across the stereo field. Returns a stereo array."""
    f0 = mtof(midi)
    length = dur + release
    n = int(length * SR)
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for v in range(voices):
        cents = (v - (voices - 1) / 2) / ((voices - 1) / 2) * 8.0 + RNG.uniform(-1.5, 1.5)
        rate = RNG.uniform(4.6, 5.6)
        depth = 0.0012 * np.minimum(1.0, t / 1.5)
        freq = f0 * 2.0 ** (cents / 1200.0) * (1.0 + depth * np.sin(2 * np.pi * rate * t + RNG.uniform(0, 6.3)))
        s = saw_polyblep(freq, RNG.uniform())
        gl, gr = pan_gains((v / (voices - 1)) * 1.6 - 0.8)
        out[:, 0] += s * gl
        out[:, 1] += s * gr
    env = np.ones(n)
    a = int(attack * SR)
    env[:a] = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, a))
    r0 = int(dur * SR)
    env[r0:] = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, n - r0))
    return out * env[:, None] / voices


def timpani(vel, f_end=73.4, decay=0.9):
    """Pitched drum: sine with a downward pitch envelope plus a noise thump."""
    length = decay * 4.0
    t = np.arange(int(length * SR)) / SR
    freq = f_end * (1.0 + 0.5 * np.exp(-t / 0.06))
    body = np.sin(2 * np.pi * np.cumsum(freq) / SR) * np.exp(-t / decay)
    body += 0.35 * np.sin(2 * np.pi * np.cumsum(freq * 1.5) / SR) * np.exp(-t / (decay * 0.5))
    thump = fft_filter(RNG.standard_normal(len(t)), bandpass(60, 900), pad=1024)
    thump *= np.exp(-t / 0.035) * 0.6
    return (body + thump) * np.minimum(1.0, t / 0.002) * vel


def boom(vel):
    """Trailer hit: deep pitch-dropping sine, timpani layer and noise burst."""
    length = 4.0
    t = np.arange(int(length * SR)) / SR
    freq = 36.7 * (1.0 + 2.0 * np.exp(-t / 0.09))
    sub = np.sin(2 * np.pi * np.cumsum(freq) / SR) * np.exp(-t / 1.4)
    sub = np.tanh(sub * 1.6) / np.tanh(1.6)
    sig = sub + 0.5 * timpani(1.0, 73.4, 1.2)[:len(t)]
    burst = fft_filter(RNG.standard_normal(len(t)), lowpass(2500), pad=2048) * np.exp(-t / 0.12)
    return (sig + 0.4 * burst) * vel


def tom(vel):
    """Low pulse: short pitched-down sine with a click."""
    t = np.arange(int(0.8 * SR)) / SR
    freq = 58.0 * (1.0 + 0.8 * np.exp(-t / 0.03))
    sig = np.sin(2 * np.pi * np.cumsum(freq) / SR) * np.exp(-t / 0.22)
    sig += RNG.standard_normal(len(t)) * np.exp(-t / 0.002) * 0.15
    return sig * vel


def reverse_cymbal(length):
    """Bright noise swelling (reversed exponential) up to a hard stop."""
    n = int(length * SR)
    t = np.arange(n) / SR
    noise = fft_filter(RNG.standard_normal((n, 2)), highpass(2500), pad=2048)
    env = np.exp((t - length) / (length * 0.25))
    return noise * env[:, None]


# --- Reverb -------------------------------------------------------------------

def make_ir(t60, early, predelay=0.02):
    """Synthetic stereo impulse response: decorrelated noise with
    frequency-dependent decay (highs die first) plus early reflections."""
    length = t60 * 1.1
    n = int(length * SR)
    t = np.arange(n) / SR
    ir = np.zeros((n, 2))
    bands = [((20, 500), 1.0), ((500, 3000), 0.75), ((3000, 12000), 0.4)]
    for (lo, hi), decay_scale in bands:
        noise = fft_filter(RNG.standard_normal((n, 2)), bandpass(lo, hi), pad=4096)
        ir += noise * np.exp(-6.91 * t / (t60 * decay_scale))[:, None]
    ir *= np.minimum(1.0, t / 0.01)[:, None]
    for _ in range(early):
        d = RNG.uniform(0.008, 0.09)
        i = int(d * SR)
        ir[i, RNG.integers(0, 2)] += RNG.uniform(0.3, 0.8) * (1.0 - d * 8)
    pre = int(predelay * SR)
    ir = np.concatenate([np.zeros((pre, 2)), ir])
    return ir / np.sqrt(np.sum(ir ** 2) / 2)


def convolve_stereo(dry, ir):
    """FFT convolution of a stereo send with a stereo IR (L->L, R->R)."""
    n = len(dry) + len(ir)
    size = 1 << int(np.ceil(np.log2(n)))
    wet = np.fft.irfft(np.fft.rfft(dry, n=size, axis=0) * np.fft.rfft(ir, n=size, axis=0),
                       n=size, axis=0)
    return wet[:len(dry)]


# --- Epic voices ----------------------------------------------------------------

def staccato(midi, vel, length=0.16):
    """Short bowed-string hit for the 16th-note ostinato: 3 detuned saws,
    fast attack, quick decay, darkened in the mix."""
    n = int(length * SR)
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for v, cents in enumerate((-7.0, 0.0, 7.0)):
        f = np.full(n, mtof(midi) * 2.0 ** (cents / 1200.0))
        s = saw_polyblep(f, RNG.uniform())
        gl, gr = pan_gains((v - 1) * 0.6)
        out[:, 0] += s * gl
        out[:, 1] += s * gr
    env = np.minimum(1.0, t / 0.004) * np.exp(-t / 0.055)
    return out * (env * vel / 3.0)[:, None]


def braam(midis, vel, dur=3.0):
    """Trailer 'braam': a stack of detuned saws an octave apart, swelling in
    and saturating, low-passed; returns stereo."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for midi in midis:
        for v in range(5):
            cents = (v - 2) * 6.0 + RNG.uniform(-2, 2)
            f = mtof(midi) * 2.0 ** (cents / 1200.0) * (1.0 + 0.002 * np.sin(2 * np.pi * 0.7 * t + v))
            s = saw_polyblep(np.full(n, f) if np.isscalar(f) else f, RNG.uniform())
            gl, gr = pan_gains((v - 2) * 0.35)
            out[:, 0] += s * gl
            out[:, 1] += s * gr
    out = np.tanh(out * 0.6)
    out = fft_filter(out, lowpass(900, 2), pad=4096)
    env = np.minimum(1.0, t / 0.06) * np.exp(-t / (dur * 0.45))
    return out * (env * vel)[:, None] / len(midis)


def brass(midi, vel, dur):
    """Brass-like lead: saws + sub sine, slow attack, vibrato, low-passed."""
    length = dur + 0.4
    n = int(length * SR)
    t = np.arange(n) / SR
    vib = 1.0 + 0.004 * np.sin(2 * np.pi * 5.2 * t) * np.minimum(1.0, t / 0.4)
    f0 = mtof(midi) * vib
    s = sum(saw_polyblep(f0 * 2.0 ** (c / 1200.0), RNG.uniform()) for c in (-5.0, 0.0, 5.0)) / 3.0
    s += 0.5 * np.sin(2 * np.pi * np.cumsum(f0) / SR)
    s = fft_filter(s, lowpass(1800 + 900 * vel, 2), pad=2048)
    env = np.minimum(1.0, t / 0.07)
    rel = t > dur
    env[rel] *= np.exp(-(t[rel] - dur) / 0.12)
    return s * env * vel


def taiko(vel, f0=52.0, decay=0.45):
    """Big drum: pitch-dropping sine body, skin slap and low noise."""
    t = np.arange(int(decay * 5 * SR)) / SR
    freq = f0 * (1.0 + 0.9 * np.exp(-t / 0.035))
    body = np.sin(2 * np.pi * np.cumsum(freq) / SR) * np.exp(-t / decay)
    slap = fft_filter(RNG.standard_normal(len(t)), bandpass(200, 3000), pad=1024) * np.exp(-t / 0.015)
    return (np.tanh(body * 1.8) * 0.9 + 0.35 * slap) * vel


def snare(vel):
    t = np.arange(int(0.4 * SR)) / SR
    noise = fft_filter(RNG.standard_normal(len(t)), bandpass(900, 9000), pad=1024) * np.exp(-t / 0.09)
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t / 0.04)
    return (0.8 * noise + 0.5 * tone) * vel


def riser(length):
    """Noise swell plus an upward sine glide into a hard stop; stereo."""
    n = int(length * SR)
    t = np.arange(n) / SR
    x = t / length
    noise = fft_filter(RNG.standard_normal((n, 2)), highpass(1500), pad=2048)
    glide = np.sin(2 * np.pi * np.cumsum(180.0 * 8.0 ** x) / SR)
    env = x ** 2.5
    return (noise * 0.6 + glide[:, None] * 0.4) * env[:, None]


# --- Score (125 BPM, 4/4; bar = 1.92 s = 48 frames) ---------------------------------

def T(bar, beat=1.0):
    """Seconds at a 1-based bar and beat."""
    return ((bar - 1) * 4 + (beat - 1)) * BEAT


CHORDS = {
    "D":    [38, 45, 50, 54, 57, 62],      # D major (nostalgia / triumph)
    "Dadd9": [38, 45, 50, 57, 64, 66, 69],
    "G":    [43, 50, 55, 59, 62],
    "Asus4": [45, 52, 57, 62, 64],
    "Dm":   [38, 45, 50, 53, 57, 62],
    "Bb":   [34, 41, 46, 50, 53, 58],
    "F":    [41, 48, 53, 57, 60, 65],
    "C":    [36, 43, 48, 52, 55, 60],
    "Gm":   [43, 50, 55, 58, 62],
    "A":    [33, 45, 52, 57, 61, 64],
    "F/A":  [45, 48, 53, 57, 60, 65],
}
EPIC = ["Dm", "Bb", "F", "C", "Dm", "Bb", "Gm", "A"]

# (bar, chord) for the pad; one chord per bar unless noted.
HARMONY = (
    [(1, "Dadd9"), (2, "D"), (3, "G"), (4, "Asus4")]
    + [(5 + i, c) for i, c in enumerate(EPIC)]
    + [(13, "Bb"), (14, "F/A"), (15, "Gm"), (16, "Asus4")]
    + [(17 + i, c) for i, c in enumerate(EPIC)]
    + [(25, "Bb"), (26, "A")]
    + [(27, "Dadd9"), (28, "D"), (29, "Dadd9"), (30, "D"), (31, "Dadd9")]
)

# Heroic brass line (bar, beat, midi, beats): the chime motif A-F-D in minor.
LEAD = [
    (9, 1, 69, 2), (9, 3, 65, 1), (9, 4, 62, 1),
    (10, 1, 70, 2), (10, 3, 69, 1), (10, 4, 67, 1),
    (11, 1, 69, 3), (11, 4, 72, 1),
    (12, 1, 73, 2), (12, 3, 76, 2),
    (21, 1, 81, 2), (21, 3, 77, 1), (21, 4, 74, 1),
    (22, 1, 82, 2), (22, 3, 81, 1), (22, 4, 79, 1),
    (23, 1, 81, 2), (23, 3, 84, 2),
    (24, 1, 85, 2), (24, 3, 88, 2),
    (28, 1, 69, 2), (28, 3, 66, 1), (28, 4, 62, 1),     # logo: A-F#-D in major
    (29, 1, 69, 4),
]

# Beauty section piano melody (bar, beat, midi, beats).
BEAUTY = [
    (13, 1, 74, 1.5), (13, 2.5, 72, 0.5), (13, 3, 70, 2),
    (14, 1, 72, 1.5), (14, 2.5, 69, 0.5), (14, 3, 65, 2),
    (15, 1, 67, 1), (15, 2, 70, 1), (15, 3, 74, 2),
    (16, 1, 76, 2), (16, 3, 73, 2),
]

# Celesta (bar, beat, midi, velocity): opening chime, its echo, beauty sparkle, tag.
CELESTA = [
    (1, 2, 81, 0.8), (1, 2.6, 78, 0.75), (1, 3.2, 74, 0.8),
    (2, 4, 81, 0.5), (3, 1.6, 78, 0.45), (3, 2.2, 76, 0.5),
    (13, 1, 93, 0.3), (14, 1, 89, 0.25), (15, 1, 91, 0.25), (16, 1, 88, 0.3),
    (31, 1, 93, 0.5), (31, 1.6, 90, 0.45), (31, 2.2, 86, 0.5),
]

SECTIONS = [(0, "calm_1999"), (16, "reveal_epic"), (48, "beauty"), (64, "action"),
            (96, "build"), (104, "logo"), (116, "tag")]


def chord_at_bar(bar):
    name = None
    for b, c in HARMONY:
        if b <= bar:
            name = c
    return name


def score():
    buses = {name: Bus() for name in
             ("drone", "pad", "piano", "celesta", "drums", "fx", "strings", "brass")}
    t = np.arange(N) / SR

    # Drone under the calm opening only.
    env = np.interp(t, [0, 2, T(4, 4), T(5)], [0, 0.6, 0.7, 0])
    drone = np.sin(2 * np.pi * mtof(38) * t) + 0.4 * np.sin(2 * np.pi * mtof(50) * t + 1.0)
    buses["drone"].add(0, np.tanh(drone * 1.4) * env, 0.0, 0.08)

    # Pad (strings), quiet in the calm and the tag, full elsewhere.
    for bar, name in HARMONY:
        level = 0.45 if bar <= 4 else 0.8 if 13 <= bar <= 16 else 0.55 if bar >= 30 else 1.0
        lift = 12 if 17 <= bar <= 26 else 0
        dur = 2 * BAR if bar in (27, 29, 31) else BAR
        for i, midi in enumerate(CHORDS[name]):
            m = midi + (lift if i > 1 else 0)
            buses["pad"].add(T(bar), string_note(m, dur, 0.25 if bar > 4 else 1.2, 1.0),
                             gain=0.13 * level * (1.2 if i == 0 else 1.0))

    # Ostinato: 16ths on the chord root, octave-jumping pattern (epic sections).
    pattern = [0, 12, 7, 12, 0, 12, 7, 15]
    for bar in list(range(5, 13)) + list(range(17, 27)):
        root = CHORDS[chord_at_bar(bar)][0] % 12 + 50       # around D3
        for k in range(16):
            vel = (0.95 if k % 4 == 0 else 0.7) * (0.8 if bar < 9 else 1.0)
            if bar in (25, 26):
                vel *= 0.7 + 0.3 * k / 16
            buses["strings"].add(T(bar) + k * BEAT / 4, staccato(root + pattern[k % 8], vel),
                                 gain=0.42)

    # Braams on the big downbeats.
    for bar, name, vel in [(5, "Dm", 1.0), (9, "Dm", 0.8), (17, "Dm", 1.0), (21, "Dm", 0.85),
                           (25, "Bb", 0.8), (27, "D", 1.2)]:
        root = CHORDS[name][0]
        buses["brass"].add(T(bar), braam([root - 12, root, root + 7], vel, 3.5 if bar == 27 else 2.6),
                           gain=0.55)

    # Lead brass and beauty piano.
    for bar, beat, midi, beats in LEAD:
        buses["brass"].add(T(bar, beat), brass(midi, 0.8, beats * BEAT), 0.1, 0.32)
    for bar, beat, midi, beats in BEAUTY:
        buses["piano"].add(T(bar, beat), piano(midi, 0.6, beats * BEAT + 0.6), -0.1, 0.42)
    for bar in range(13, 17):                          # gentle arpeggio under the melody
        tones = CHORDS[chord_at_bar(bar)][1:]
        for k in range(8):
            midi = tones[[0, 2, 3, 4, 3, 2, 1, 2][k] % len(tones)] + 12
            buses["piano"].add(T(bar) + k * BEAT / 2, piano(midi, 0.35, 1.5), -0.3, 0.2)
    for bar, beat, midi, vel in CELESTA:
        gain = 0.12 if bar <= 4 else 0.25
        buses["celesta"].add(T(bar, beat), celesta(midi, vel), 0.35, gain)

    # Drums. Act 1: taiko on 1, 2&, 3, 4& (+ fills); action: 8th taikos + snare backbeat.
    for bar in range(5, 13):
        for beat, vel in [(1, 1.0), (2.5, 0.6), (3, 0.85), (4.5, 0.6)]:
            buses["drums"].add(T(bar, beat), taiko(vel), RNG.uniform(-0.2, 0.2), 0.5)
        if bar in (8, 12):
            for k in range(4):
                buses["drums"].add(T(bar, 4 + k / 4), taiko(0.4 + 0.15 * k, 70), 0.0, 0.4)
    for bar in range(17, 25):
        for k in range(8):
            buses["drums"].add(T(bar) + k * BEAT / 2, taiko(1.0 if k % 4 == 0 else 0.55), 0.0, 0.48)
        for beat in (2, 4):
            buses["drums"].add(T(bar, beat), snare(0.8), 0.1, 0.35)
    # Build: snare roll accelerating from 8ths to 32nds over bars 25-26, cut a beat early.
    tt, end = T(25), T(26, 4)
    while tt < end:
        x = (tt - T(25)) / (end - T(25))
        buses["drums"].add(tt, snare(0.25 + 0.6 * x), 0.0, 0.35)
        tt += BEAT / 2 * (1 - x) + BEAT / 8 * x
    buses["fx"].add(T(25), riser(end - T(25)), gain=0.3)
    tt = T(25)
    while tt < end:                                    # taikos accelerate with the roll
        x = (tt - T(25)) / (end - T(25))
        buses["drums"].add(tt, taiko(0.5 + 0.5 * x, 55 + 25 * x), 0.0, 0.5)
        tt += BEAT * (1 - x) + BEAT / 4 * x
    # Hits: reveal, beauty entry (soft), action entry, logo slam.
    for bar, vel in [(5, 1.1), (13, 0.4), (17, 1.0), (27, 1.3)]:
        buses["drums"].add(T(bar), boom(vel), 0.0, 0.6)
    # Calm-section tension: a soft reverse swell into the reveal.
    buses["fx"].add(T(4), reverse_cymbal(T(5) - T(4)), gain=0.05)

    # Silence beat before the logo: cut every bus over bar 26 beat 4.
    s0, s1 = int(T(26, 4) * SR), int(T(27) * SR)
    for name, bus in buses.items():
        ramp = np.linspace(1.0, 0.0, int(0.03 * SR))
        bus.buf[s0:s0 + len(ramp)] *= ramp[:, None]
        bus.buf[s0 + len(ramp):s1] = 0.0
    return buses


def brightness_curve():
    """Pad filter brightness over time (0 = dark, 1 = bright)."""
    t = np.arange(N) / SR
    return np.interp(t, [0, T(4), T(5), T(5) + 0.5, T(13), T(17), T(25), T(27), T(30), 62],
                        [0.05, 0.2, 0.5, 0.75, 0.55, 0.7, 0.85, 1.0, 0.35, 0.2])


def compress(x, threshold_db, ratio, smooth_hz=6.0):
    """Gentle master compressor. The level envelope is a zero-phase low-pass
    of the signal power, so gain changes anticipate hits (look-ahead)."""
    power = np.mean(x ** 2, axis=1)
    env = np.sqrt(np.maximum(fft_filter(power, lowpass(smooth_hz, 1)), 1e-12))
    env_db = 20 * np.log10(env / np.max(np.abs(x)))
    over = np.maximum(env_db - threshold_db, 0.0)
    gain_db = -over * (1.0 - 1.0 / ratio)
    return x * (10 ** (gain_db / 20.0))[:, None]


def mix(buses):
    pad = buses["pad"].buf
    pad = fft_filter(pad, highpass(70))
    dark = fft_filter(pad, lowpass(700))
    bright = fft_filter(pad, lowpass(5000))
    b = brightness_curve()[:, None]
    buses["pad"].buf = dark * (1.0 - b) + bright * b
    buses["strings"].buf = fft_filter(buses["strings"].buf, lowpass(3500))

    # Reverb sends: (bus, long hall, short room).
    sends = {"drone": (0.25, 0.1), "pad": (0.35, 0.0), "piano": (0.35, 0.05),
             "celesta": (0.5, 0.0), "drums": (0.22, 0.08), "fx": (0.3, 0.0),
             "strings": (0.15, 0.1), "brass": (0.3, 0.0)}
    # The 1999 opening sits in a smaller empty-mall room; the hall takes over at the reveal.
    t = np.arange(N) / SR
    room_mix = np.interp(t, [0, T(4, 4), T(5)], [1.0, 1.0, 0.0])[:, None]
    long_send = np.zeros((N, 2))
    short_send = np.zeros((N, 2))
    dry = np.zeros((N, 2))
    for name, bus in buses.items():
        hall, room = sends[name]
        dry += bus.buf
        long_send += bus.buf * hall * (1.0 - room_mix)
        short_send += bus.buf * (room + 0.6 * hall * room_mix)
    wet = convolve_stereo(long_send, make_ir(3.2, 6)) * 0.35
    wet += convolve_stereo(short_send, make_ir(1.6, 14, predelay=0.012)) * 0.4

    out = fft_filter(dry + wet, highpass(25))
    out = out[:int(LENGTH * SR)]
    out = compress(out, threshold_db=-16.0, ratio=3.0)
    out /= np.max(np.abs(out))
    out = np.tanh(out * 1.5) / np.tanh(1.5)
    fade = np.ones(len(out))
    f0 = int(59.0 * SR)
    fade[f0:] = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, len(out) - f0))
    out *= fade[:, None]
    return out * (10 ** (-1.0 / 20.0)) / np.max(np.abs(out))


# --- Output -------------------------------------------------------------------

def write_wav(path, audio):
    tpdf = (RNG.uniform(-0.5, 0.5, audio.shape) + RNG.uniform(-0.5, 0.5, audio.shape))
    pcm = np.clip(np.round(audio * 32767 + tpdf), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def write_markers(path):
    names = dict(SECTIONS)
    with open(path, "w") as f:
        f.write(f"# beat  seconds  frame@{FPS}fps  bar.beat  section\n")
        for beat in range(int(LENGTH / BEAT) + 1):
            sec = beat * BEAT
            label = names.get(beat, "")
            f.write(f"{beat:4d}  {sec:6.2f}  {int(round(sec * FPS)):5d}  "
                    f"{beat // 4 + 1:2d}.{beat % 4 + 1}  {label}\n")


def write_spectrogram(path, audio):
    from PIL import Image, ImageDraw
    mono = audio.mean(axis=1)
    win, hop = 4096, SR // FPS
    frames = (len(mono) - win) // hop
    window = np.hanning(win)
    spec = np.empty((frames, win // 2 + 1))
    for i in range(frames):
        spec[i] = np.abs(np.fft.rfft(mono[i * hop:i * hop + win] * window))
    db = 20 * np.log10(spec + 1e-9)
    height = 360
    freqs = np.fft.rfftfreq(win, 1.0 / SR)
    rows = np.geomspace(30, 16000, height)[::-1]
    img = np.empty((height, frames))
    for j, f in enumerate(rows):
        img[j] = db[:, np.searchsorted(freqs, f)]
    img = np.clip((img - (db.max() - 80)) / 80, 0, 1)
    rgb = np.stack([img ** 0.7, img ** 1.4, img ** 3 * 0.6 + 0.08 * (1 - img)], axis=2)
    pic = Image.fromarray((rgb * 255).astype(np.uint8), "RGB")
    draw = ImageDraw.Draw(pic)
    for bar in range(32):
        x = int(bar * BAR * FPS)
        draw.line([(x, 0), (x, height)], fill=(90, 90, 90))
        draw.text((x + 3, 2), str(bar + 1), fill=(200, 200, 200))
    pic.save(path)


def report(audio):
    peak = 20 * np.log10(np.max(np.abs(audio)))
    print(f"length {len(audio) / SR:.4f} s, peak {peak:.2f} dBFS")
    for bar in range(31):
        seg = audio[int(bar * BAR * SR):int((bar + 1) * BAR * SR)]
        rms = 20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-12)
        print(f"  bar {bar + 1:2d}  rms {rms:6.1f} dBFS")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="build/trailer", help="output directory")
    ap.add_argument("--preview", help="also write music_preview.wav for seconds A-B")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    audio = mix(score())
    write_wav(os.path.join(args.out, "music.wav"), audio)
    write_markers(os.path.join(args.out, "music_markers.txt"))
    write_spectrogram(os.path.join(args.out, "music_spectrogram.png"), audio)
    if args.preview:
        a, b = (float(v) for v in args.preview.split("-"))
        write_wav(os.path.join(args.out, "music_preview.wav"),
                  audio[int(a * SR):int(b * SR)])
    report(audio)
    return 0


if __name__ == "__main__":
    sys.exit(main())
