#!/usr/bin/env python3
"""Synthesise every sound in assets/sfx. All audio is original, made by this script.

Pure standard library (wave, math, random). The engine's Mixer has no volume
control, so loudness is baked into each file here.
"""
import math
import pathlib
import random
import struct
import wave

OUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "sfx"
RATE = 22050
TAU = math.tau


def write(name, samples, volume=1.0):
    peak = max(1e-9, max(abs(s) for s in samples))
    scale = min(1.0, 1.0 / peak) * volume * 32767
    with wave.open(str(OUT / f"{name}.wav"), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale / 32767)) * 32767)) for s in samples))


def silence(seconds):
    return [0.0] * int(seconds * RATE)


def add(dst, src, at=0.0, gain=1.0):
    start = int(at * RATE)
    if start + len(src) > len(dst):
        dst.extend([0.0] * (start + len(src) - len(dst)))
    for i, s in enumerate(src):
        dst[start + i] += s * gain
    return dst


def tone(freq, seconds, shape="sine", attack=0.005, decay=None, freq_end=None, vibrato=0.0):
    """One enveloped oscillator. decay=None means a linear fade over the whole note."""
    n = int(seconds * RATE)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / RATE
        k = i / max(1, n - 1)
        f = freq if freq_end is None else freq * (freq_end / freq) ** k
        if vibrato:
            f *= 1.0 + vibrato * math.sin(TAU * 9.0 * t)
        phase += TAU * f / RATE
        if shape == "sine":
            v = math.sin(phase)
        elif shape == "square":
            v = 1.0 if math.sin(phase) >= 0 else -1.0
        elif shape == "tri":
            v = 2.0 / math.pi * math.asin(math.sin(phase))
        else:  # bell: sine plus inharmonic partials
            v = math.sin(phase) + 0.45 * math.sin(phase * 2.76) + 0.2 * math.sin(phase * 5.4)
        env = min(1.0, t / attack) if attack > 0 else 1.0
        env *= math.exp(-t / decay) if decay else (1.0 - k)
        # short release so nothing clicks at the end
        env *= min(1.0, (n - i) / (0.004 * RATE))
        out.append(v * env)
    return out


def noise(seconds, cutoff=0.2, cutoff_end=None, decay=None, seed=1, attack=0.002):
    """Low-passed white noise (one-pole), cutoff as a 0..1 smoothing factor."""
    rng = random.Random(seed)
    n = int(seconds * RATE)
    out = []
    y = 0.0
    for i in range(n):
        t = i / RATE
        k = i / max(1, n - 1)
        a = cutoff if cutoff_end is None else cutoff + (cutoff_end - cutoff) * k
        y += a * (rng.uniform(-1, 1) - y)
        env = min(1.0, t / attack)
        env *= math.exp(-t / decay) if decay else (1.0 - k)
        env *= min(1.0, (n - i) / (0.004 * RATE))
        out.append(y * env)
    return out


def note(semitones_from_a4):
    return 440.0 * 2 ** (semitones_from_a4 / 12.0)


C5, D5, E5, G5, A5 = note(3), note(5), note(7), note(10), note(12)
C6, D6, E6, G6, C7 = note(15), note(17), note(19), note(22), note(27)


def make_effects():
    write("jump", tone(280, 0.13, "tri", freq_end=620), 0.30)
    write("land", add(noise(0.09, 0.08, decay=0.03, seed=3), tone(90, 0.09, "sine", freq_end=50), gain=0.8), 0.30)
    write("step", noise(0.035, 0.25, decay=0.012, seed=5), 0.10)
    write("hop", tone(150, 0.12, "sine", freq_end=70), 0.22)
    write("throw", noise(0.22, 0.05, cutoff_end=0.5, seed=7, attack=0.04), 0.35)
    write("deny", add(tone(140, 0.07, "square"), tone(110, 0.09, "square"), at=0.09), 0.14)

    for i, (a, b) in enumerate(((C6, E6), (E6, G6), (G6, C7)), start=1):
        s = tone(a, 0.5, "bell", decay=0.16)
        add(s, tone(b, 0.5, "bell", decay=0.2), at=0.08)
        write(f"spore{i}", s, 0.42)

    s = silence(0.9)
    rng = random.Random(11)
    for k in range(9):
        f = 300 + k * 90 + rng.uniform(-30, 30)
        add(s, tone(f, 0.09, "sine", freq_end=f * 1.6), at=k * 0.07, gain=0.5)
    add(s, tone(C6, 0.45, "bell", decay=0.2), at=0.55)
    add(s, tone(G6, 0.4, "bell", decay=0.2), at=0.62, gain=0.7)
    write("brew", s, 0.42)

    s = noise(0.45, 0.7, cutoff_end=0.2, decay=0.09, seed=13)
    for k, f in enumerate((2400, 3100, 1900, 2800, 3600)):
        add(s, tone(f, 0.18, "sine", decay=0.05), at=0.01 + k * 0.035, gain=0.35)
    add(s, tone(C7, 0.6, "bell", decay=0.25), at=0.05, gain=0.5)
    write("shatter", s, 0.55)

    s = tone(420, 0.75, "tri", freq_end=60, vibrato=0.08)
    add(s, noise(0.6, 0.12, decay=0.25, seed=17), gain=0.6)
    write("slime_die", s, 0.5)

    write("hurt", add(tone(520, 0.24, "square", freq_end=130), noise(0.1, 0.4, decay=0.04, seed=19), gain=0.6), 0.26)

    s = silence(1.5)
    for k, f in enumerate((A5, note(8), E5, note(0), note(-5))):
        add(s, tone(f, 0.5, "tri", decay=0.22), at=k * 0.2)
    add(s, tone(note(-12), 0.9, "sine", decay=0.5), at=0.9, gain=0.8)
    write("lose", s, 0.4)

    s = silence(2.0)
    for k, f in enumerate((C5, E5, G5, C6, E6)):
        add(s, tone(f, 0.45, "bell", decay=0.2), at=k * 0.13)
    for f in (C6, E6, G6, C7):
        add(s, tone(f, 1.2, "bell", decay=0.5), at=0.72, gain=0.6)
    write("win", s, 0.5)

    s = tone(G5, 0.9, "bell", decay=0.35)
    add(s, tone(D6, 0.9, "bell", decay=0.4), at=0.14)
    add(s, tone(G6, 0.8, "bell", decay=0.4), at=0.28, gain=0.6)
    write("save", s, 0.42)

    s = noise(0.9, 0.03, decay=0.35, seed=23, attack=0.05)
    add(s, tone(70, 0.8, "sine", freq_end=45, decay=0.4), gain=0.8)
    add(s, tone(E6, 0.7, "bell", decay=0.3), at=0.35, gain=0.35)
    write("door", s, 0.5)

    s = silence(0.7)
    for k, f in enumerate((E6, G6, C7, note(31))):
        add(s, tone(f, 0.3, "sine", decay=0.1), at=k * 0.09, gain=0.6)
    write("regrow", s, 0.32)


def make_ambience():
    """Cave drone with water drips.

    The file is PERIOD + TAIL seconds long. Everything in it repeats with the
    period, the head fades in and the tail fades out linearly over TAIL, so
    starting a new copy every PERIOD seconds cross-fades into a seamless loop
    (the engine's Mixer cannot loop a sound).
    """
    period, tail = 24.0, 3.0
    n_period = int(period * RATE)
    loop = [0.0] * n_period

    # drone: partial frequencies are whole multiples of 1/period, so they wrap
    partials = [(55.0, 0.5), (82.5, 0.3), (110.0, 0.22), (164.75, 0.1), (220.0, 0.06), (329.5, 0.03)]
    for index, (freq, gain) in enumerate(partials):
        freq = round(freq * period) / period
        lfo = (index + 1) / period      # each partial swells at its own slow rate
        for i in range(n_period):
            t = i / RATE
            swell = 0.75 + 0.25 * math.sin(TAU * lfo * t + freq)
            loop[i] += math.sin(TAU * freq * t) * gain * swell

    # a slow, breathy layer of noise
    rng = random.Random(31)
    y = 0.0
    breath = []
    for i in range(n_period):
        y += 0.012 * (rng.uniform(-1, 1) - y)
        breath.append(y)
    # make the noise layer wrap by cross-fading its own ends
    fade = int(1.0 * RATE)
    for i in range(fade):
        k = i / fade
        breath[i] = breath[i] * k + breath[n_period - fade + i] * (1.0 - k)
    for i in range(n_period - fade):
        t = i / RATE
        loop[i] += breath[i] * 2.2 * (0.6 + 0.4 * math.sin(TAU * (2 / period) * t))

    # drips with a short echo, wrapped around the loop
    def drip(at, freq, gain):
        d = tone(freq, 0.16, "sine", freq_end=freq * 0.55, decay=0.04)
        for echo, g in ((0.0, 1.0), (0.21, 0.35), (0.42, 0.14)):
            start = int((at + echo) * RATE)
            for i, s in enumerate(d):
                loop[(start + i) % n_period] += s * gain * g

    for at, freq, gain in ((1.7, 1500, 0.22), (5.3, 1900, 0.16), (9.1, 1250, 0.2), (12.4, 2100, 0.12),
                           (15.2, 1650, 0.2), (18.9, 1400, 0.15), (21.6, 1800, 0.18)):
        drip(at, freq, gain)

    n_total = int((period + tail) * RATE)
    n_tail = int(tail * RATE)
    out = []
    for i in range(n_total):
        s = loop[i % n_period]
        if i < n_tail:
            s *= i / n_tail
        elif i >= n_period:
            s *= 1.0 - (i - n_period) / n_tail
        out.append(s)
    # normalise against the loop itself so the level does not depend on the fades
    peak = max(abs(s) for s in loop)
    write("ambience", [s / peak for s in out], 0.30)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    make_effects()
    make_ambience()
    total = sum(p.stat().st_size for p in OUT.glob("*.wav"))
    print(f"wrote {len(list(OUT.glob('*.wav')))} sounds, {total // 1024} KiB")


if __name__ == "__main__":
    main()
