#!/usr/bin/env python3
"""Deterministic VOX Synth factory bank.

Original project data. No third-party presets or MIDI.
Run from the repository root or from this directory. Writes
../FactoryData.inc, MANIFEST.txt and checks uniqueness before exit.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "Parameters.h"
OUT = ROOT / "FactoryData.inc"
MANIFEST = Path(__file__).resolve().parent / "MANIFEST.txt"

STYLES = [
    ("classic", "Classic"),
    ("uplifting", "Uplifting"),
    ("progressive", "Progressive"),
    ("tech", "Tech"),
    ("goa", "Goa"),
    ("fullon", "Full-on"),
    ("psy", "Psy"),
    ("forest", "Forest"),
    ("dark", "Dark Psy"),
    ("hitech", "Hi-Tech"),
]

# Musical bias per style. Applied after the role recipe, then clamped.
STYLE = {
    "classic": dict(cutoff=0.58, resonance=0.32, drive=0.22, decay=0.28, release=0.30, slide=0.22),
    "uplifting": dict(cutoff=0.74, resonance=0.26, drive=0.16, decay=0.36, release=0.55, slide=0.12),
    "progressive": dict(cutoff=0.46, resonance=0.22, drive=0.14, decay=0.48, release=0.62, slide=0.08),
    "tech": dict(cutoff=0.38, resonance=0.48, drive=0.55, decay=0.14, release=0.16, slide=0.18),
    "goa": dict(cutoff=0.82, resonance=0.58, drive=0.28, decay=0.22, release=0.24, slide=0.35),
    "fullon": dict(cutoff=0.64, resonance=0.52, drive=0.40, decay=0.18, release=0.20, slide=0.48),
    "psy": dict(cutoff=0.52, resonance=0.70, drive=0.34, decay=0.16, release=0.18, slide=0.42),
    "forest": dict(cutoff=0.33, resonance=0.84, drive=0.36, decay=0.20, release=0.22, slide=0.30),
    "dark": dict(cutoff=0.24, resonance=0.62, drive=0.58, decay=0.12, release=0.14, slide=0.28),
    "hitech": dict(cutoff=0.90, resonance=0.40, drive=0.30, decay=0.08, release=0.10, slide=0.15),
}

# Which FX slot is the style's send. Others stay off.
STYLE_FX = {
    "classic": "chorus",
    "uplifting": "reverb",
    "progressive": "delay",
    "tech": "distortion",
    "goa": "phaser",
    "fullon": "wavefolder",
    "psy": "delay",
    "forest": "phaser",
    "dark": "distortion",
    "hitech": "bitcrusher",
}

SCALES = {
    "classic": [0, 2, 3, 5, 7, 8, 10],
    "uplifting": [0, 2, 4, 5, 7, 9, 11],
    "progressive": [0, 2, 3, 5, 7, 9, 10],
    "tech": [0, 1, 3, 5, 6, 8, 10],
    "goa": [0, 2, 3, 5, 7, 8, 11],
    "fullon": [0, 1, 4, 5, 7, 8, 10],
    "psy": [0, 1, 3, 5, 7, 8, 10],
    "forest": [0, 1, 3, 5, 6, 8, 10],
    "dark": [0, 1, 3, 4, 7, 8, 10],
    "hitech": [0, 1, 4, 6, 7, 8, 11],
}

# category, count must sum to 600. variant count * 10 styles = count.
PLAN = [
    ("bass", 10),
    ("acid", 8),
    ("lead", 8),
    ("pluck", 6),
    ("keys", 6),
    ("pad", 8),
    ("atmos", 7),
    ("fx", 7),
]

BASS_ROLES = [
    ("rolling", "rolling foundation under a straight kick"),
    ("offbeat", "offbeat pulse that leaves the downbeat empty"),
    ("octave", "octave stack, upper part quieter"),
    ("glide", "long slide between roots"),
    ("scream", "high resonance and drive"),
    ("sub", "sub-heavy low fundamental"),
    ("hollow", "thin resonant body, little sub"),
    ("growl", "short pitch drop and heavy drive"),
    ("layer", "psy body with a quiet acid layer"),
    ("hold", "sustained root with slow filter motion"),
]
ACID_ROLES = [
    ("slide", "TB-303 slide line"),
    ("accent", "accented square peaks"),
    ("rest", "short notes with gaps"),
    ("squelch", "high resonance squelch"),
    ("octave", "acid line with a lower octave"),
    ("drive", "overdriven ladder"),
    ("soft", "low drive, rounded square"),
    ("run", "fast cutoff decay"),
]
LEAD_ROLES = [
    ("hook", "short unison hook"),
    ("fifth", "lead plus a quieter fifth"),
    ("wide", "detuned stacked lead"),
    ("solo", "single-voice solo, little detune"),
    ("bright", "open filter solo"),
    ("dark", "closed filter solo"),
    ("delay", "lead written for a tempo delay"),
    ("answer", "call-and-response second part"),
]
PLUCK_ROLES = [
    ("tick", "very short tick"),
    ("arp", "arpeggio pluck"),
    ("wood", "softer attack, little noise"),
    ("glass", "bright short decay"),
    ("mute", "low sustain mute"),
    ("stack", "pluck plus a quiet octave"),
]
KEY_ROLES = [
    ("stab", "trance stab"),
    ("chord", "held chord tone"),
    ("epiano", "slower attack, medium decay"),
    ("organ", "high sustain, little detune"),
    ("bell", "short bright decay"),
    ("padkey", "key that blooms into a pad"),
]
PAD_ROLES = [
    ("swell", "slow attack swell"),
    ("choir", "wide detune, long release"),
    ("glass", "brighter pad, shorter attack"),
    ("dark", "closed filter pad"),
    ("motion", "pad with a slow phaser or delay"),
    ("fifth", "pad plus a fifth"),
    ("air", "noise and a thin oscillator"),
    ("bed", "static bed, almost no modulation"),
]
ATMOS_ROLES = [
    ("wash", "low level wash"),
    ("drip", "sparse high partials"),
    ("rumble", "low noise bed"),
    ("shimmer", "bright quiet layer"),
    ("cave", "dark long reverb tail"),
    ("wind", "noise-led texture"),
    ("pulse", "slow amplitude-less filter pulse"),
]
FX_ROLES = [
    ("riser", "rising noise wash"),
    ("downer", "falling pitch zap"),
    ("impact", "short distorted impact"),
    ("zap", "hi-tech zap"),
    ("crackle", "bitcrushed texture"),
    ("sweep", "filter sweep stab"),
    ("echo", "delay throw"),
]

ROLES = {
    "bass": BASS_ROLES,
    "acid": ACID_ROLES,
    "lead": LEAD_ROLES,
    "pluck": PLUCK_ROLES,
    "keys": KEY_ROLES,
    "pad": PAD_ROLES,
    "atmos": ATMOS_ROLES,
    "fx": FX_ROLES,
}

CATEGORY_ID = {
    "bass": 0, "acid": 1, "lead": 2, "pluck": 3, "keys": 4,
    "pad": 5, "atmos": 6, "fx": 7,
}

PHRASE_ROLES = [
    ("offbeat-bass", "bass", "offbeat 16ths with the downbeat left for the kick"),
    ("held-bass", "bass", "long root and fifth, few attacks"),
    ("acid-slide", "acid", "chromatic approaches with slides"),
    ("acid-rest", "acid", "acid line with rests on the beat"),
    ("lead-motif", "lead", "four-note motif and a variation"),
    ("lead-answer", "lead", "the motif answered a fifth up"),
    ("pluck-arp", "pluck", "upward arpeggio"),
    ("key-stab", "keys", "offbeat chord stabs"),
    ("pad-chord", "pad", "whole-note chord tones"),
    ("atmos-swell", "atmos", "single long tone"),
    ("fx-hit", "fx", "one accent and a tail"),
    ("fill", "lead", "last-bar 16th fill in the style scale"),
]


def catalog():
    text = HEADER.read_text()
    rows = re.findall(
        r'\{ "([^"]+)", "[^"]+", ([0-9.]+)f, ([0-9.]+)f, ([0-9.]+)f, (true|false) \}',
        text,
    )
    if len(rows) != 75:
        raise SystemExit(f"expected 75 parameters, found {len(rows)}")
    return [(i, float(lo), float(hi), float(init), choice == "true") for i, lo, hi, init, choice in rows]


def blank(rows):
    return {ident: init for ident, _, _, init, _ in rows}


def clamp_to(rows, values):
    by_id = {ident: (lo, hi) for ident, lo, hi, _, _ in rows}
    out = []
    for ident, _, _, _, _ in rows:
        lo, hi = by_id[ident]
        value = values[ident]
        if value < lo or value > hi:
            raise SystemExit(f"{ident} out of range: {value}")
        out.append(value)
    return out


def set_fx(values, slot_name, mix, amount, rate, character):
    for name in ("distortion", "wavefolder", "phaser", "flanger", "chorus", "bitcrusher", "delay", "reverb"):
        values[f"syn.fx.{name}"] = 0.0
        if name in ("delay", "reverb"):
            values[f"syn.fx.{name}.mix"] = 0.0
        else:
            values[f"syn.fx.{name}.mix"] = 0.0
    enable = "syn.fx.delay" if slot_name == "delay" else (
        "syn.fx.reverb" if slot_name == "reverb" else f"syn.fx.{slot_name}")
    values[enable] = 1.0
    values[f"syn.fx.{slot_name}.mix"] = mix
    values[f"syn.fx.{slot_name}.amount"] = amount
    values[f"syn.fx.{slot_name}.rate"] = rate
    values[f"syn.fx.{slot_name}.character"] = character


def apply_style(values, style_id, variant_index):
    bias = STYLE[style_id]
    for key in ("cutoff", "resonance", "drive", "slide"):
        for part in ("a", "b"):
            ident = f"syn.{part}.{key}"
            values[ident] = (values[ident] * 0.45) + (bias[key] * 0.55)
    values["syn.a.decay"] = (values["syn.a.decay"] * 0.5) + (bias["decay"] * 0.5)
    values["syn.b.decay"] = (values["syn.b.decay"] * 0.5) + (bias["decay"] * 0.5)
    values["syn.a.release"] = (values["syn.a.release"] * 0.5) + (bias["release"] * 0.5)
    values["syn.b.release"] = (values["syn.b.release"] * 0.5) + (bias["release"] * 0.5)
    slot = STYLE_FX[style_id]
    mix = 0.18 + (variant_index % 5) * 0.06
    amount = 0.28 + (variant_index % 4) * 0.12
    rate = 0.22 + (variant_index % 3) * 0.15
    character = 0.35 + (variant_index % 6) * 0.08
    set_fx(values, slot, mix, amount, rate, character)
    # A second, quieter slot on even variants so FX programs are not one-knob copies.
    if variant_index % 2 == 0 and slot != "reverb":
        values["syn.fx.reverb"] = 1.0
        values["syn.fx.reverb.mix"] = 0.12 + (variant_index % 3) * 0.04
        values["syn.fx.reverb.amount"] = 0.40
        values["syn.fx.reverb.rate"] = 0.35
        values["syn.fx.reverb.character"] = 0.48


def recipe(category, role, values):
    a, b = "syn.a.", "syn.b."
    if category == "bass":
        values[a + "engine"] = 0
        values[a + "level"] = 0.82
        values[b + "level"] = 0.0
        values[b + "engine"] = 1
        values[a + "sub"] = 0.55
        values[a + "env"] = 0.7
        if role == "rolling":
            values[a + "slide"] = 0.18
            values[a + "cutoff"] = 0.42
        elif role == "offbeat":
            values[a + "slide"] = 0.06
            values[a + "decay"] = 0.08
            values[a + "sustain"] = 0.2
        elif role == "octave":
            values[b + "engine"] = 0
            values[b + "level"] = 0.38
            values[b + "cutoff"] = 0.7
            values[b + "sub"] = 0.1
        elif role == "glide":
            values[a + "slide"] = 0.92
            values[a + "portamento"] = 0.35
        elif role == "scream":
            values[a + "resonance"] = 0.9
            values[a + "drive"] = 0.72
            values[a + "cutoff"] = 0.66
        elif role == "sub":
            values[a + "sub"] = 0.92
            values[a + "cutoff"] = 0.18
            values[a + "drive"] = 0.08
        elif role == "hollow":
            values[a + "sub"] = 0.05
            values[a + "resonance"] = 0.8
            values[a + "cutoff"] = 0.58
        elif role == "growl":
            values[a + "drive"] = 0.88
            values[a + "pitch"] = 0.62
            values[a + "env"] = 0.35
        elif role == "layer":
            values[b + "level"] = 0.28
            values[b + "cutoff"] = 0.55
            values[b + "drive"] = 0.4
        elif role == "hold":
            values[a + "cutoff"] = 0.3
            values[a + "env"] = 1.0
            values[a + "slide"] = 0.4
    elif category == "acid":
        values[a + "engine"] = 1
        values[a + "level"] = 0.78
        values[b + "level"] = 0.0
        values[a + "wave"] = 0.62
        values[a + "env"] = 0.85
        values[a + "resonance"] = 0.72
        if role == "slide":
            values[a + "slide"] = 0.8
        elif role == "accent":
            values[a + "wave"] = 0.9
            values[a + "env"] = 1.0
            values[a + "decay"] = 0.1
        elif role == "rest":
            values[a + "decay"] = 0.05
            values[a + "sustain"] = 0.1
            values[a + "slide"] = 0.2
        elif role == "squelch":
            values[a + "resonance"] = 0.95
            values[a + "cutoff"] = 0.48
            values[a + "drive"] = 0.5
        elif role == "octave":
            values[b + "engine"] = 1
            values[b + "level"] = 0.32
            values[b + "cutoff"] = 0.3
        elif role == "drive":
            values[a + "drive"] = 0.9
            values[a + "wave"] = 0.4
        elif role == "soft":
            values[a + "drive"] = 0.08
            values[a + "wave"] = 0.25
            values[a + "resonance"] = 0.35
        elif role == "run":
            values[a + "env"] = 0.25
            values[a + "cutoff"] = 0.8
            values[a + "decay"] = 0.04
    elif category == "lead":
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.7
        values[b + "level"] = 0.22
        values[a + "attack"] = 0.02
        values[a + "sustain"] = 0.7
        values[a + "detune"] = 0.18
        if role == "hook":
            values[a + "decay"] = 0.2
            values[b + "level"] = 0.1
        elif role == "fifth":
            values[b + "level"] = 0.4
            values[b + "detune"] = 0.08
            values[b + "cutoff"] = 0.6
        elif role == "wide":
            values[a + "detune"] = 0.55
            values[b + "detune"] = 0.7
            values[b + "level"] = 0.45
        elif role == "solo":
            values[a + "detune"] = 0.02
            values[b + "level"] = 0.0
        elif role == "bright":
            values[a + "cutoff"] = 0.88
            values[a + "noise"] = 0.02
        elif role == "dark":
            values[a + "cutoff"] = 0.28
            values[a + "resonance"] = 0.4
        elif role == "delay":
            values[a + "decay"] = 0.16
            values[a + "sustain"] = 0.45
        elif role == "answer":
            values[b + "level"] = 0.55
            values[b + "attack"] = 0.08
            values[a + "level"] = 0.5
    elif category == "pluck":
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.72
        values[b + "level"] = 0.0
        values[a + "attack"] = 0.0
        values[a + "decay"] = 0.1
        values[a + "sustain"] = 0.0
        values[a + "release"] = 0.12
        if role == "tick":
            values[a + "decay"] = 0.03
        elif role == "arp":
            values[a + "decay"] = 0.14
            values[a + "cutoff"] = 0.75
        elif role == "wood":
            values[a + "noise"] = 0.0
            values[a + "cutoff"] = 0.4
        elif role == "glass":
            values[a + "cutoff"] = 0.92
            values[a + "resonance"] = 0.35
        elif role == "mute":
            values[a + "cutoff"] = 0.3
            values[a + "decay"] = 0.06
        elif role == "stack":
            values[b + "level"] = 0.3
            values[b + "decay"] = 0.08
            values[b + "sustain"] = 0.0
    elif category == "keys":
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.66
        values[b + "level"] = 0.4
        values[a + "attack"] = 0.01
        values[a + "sustain"] = 0.55
        if role == "stab":
            values[a + "decay"] = 0.12
            values[a + "sustain"] = 0.15
        elif role == "chord":
            values[a + "sustain"] = 0.8
            values[a + "release"] = 0.4
        elif role == "epiano":
            values[a + "attack"] = 0.04
            values[a + "decay"] = 0.34
        elif role == "organ":
            values[a + "sustain"] = 0.95
            values[a + "detune"] = 0.04
        elif role == "bell":
            values[a + "decay"] = 0.18
            values[a + "cutoff"] = 0.86
            values[a + "sustain"] = 0.05
        elif role == "padkey":
            values[a + "attack"] = 0.2
            values[a + "release"] = 0.6
    elif category == "pad":
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.55
        values[b + "level"] = 0.4
        values[a + "sustain"] = 0.9
        values[b + "sustain"] = 0.9
        values[a + "attack"] = 0.45
        values[a + "release"] = 0.75
        values[a + "detune"] = 0.32
        if role == "swell":
            values[a + "attack"] = 0.7
        elif role == "choir":
            values[a + "detune"] = 0.6
            values[b + "detune"] = 0.66
        elif role == "glass":
            values[a + "attack"] = 0.2
            values[a + "cutoff"] = 0.8
        elif role == "dark":
            values[a + "cutoff"] = 0.22
            values[b + "cutoff"] = 0.18
        elif role == "motion":
            values[a + "resonance"] = 0.45
        elif role == "fifth":
            values[b + "level"] = 0.48
            values[b + "cutoff"] = 0.7
        elif role == "air":
            values[a + "noise"] = 0.22
            values[a + "level"] = 0.4
        elif role == "bed":
            values[a + "detune"] = 0.12
            values[a + "attack"] = 0.3
    elif category == "atmos":
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.32
        values[b + "level"] = 0.22
        values[a + "attack"] = 0.55
        values[a + "sustain"] = 1.0
        values[a + "noise"] = 0.18
        if role == "wash":
            values[a + "cutoff"] = 0.4
        elif role == "drip":
            values[a + "cutoff"] = 0.9
            values[a + "decay"] = 0.2
            values[a + "noise"] = 0.02
        elif role == "rumble":
            values[a + "cutoff"] = 0.12
            values[a + "noise"] = 0.4
        elif role == "shimmer":
            values[a + "cutoff"] = 0.84
            values[b + "level"] = 0.3
        elif role == "cave":
            values[a + "release"] = 0.9
            values[a + "cutoff"] = 0.2
        elif role == "wind":
            values[a + "noise"] = 0.7
            values[a + "sub"] = 0.0
        elif role == "pulse":
            values[a + "resonance"] = 0.75
            values[a + "env"] = 0.4
            values[a + "cutoff"] = 0.36
    elif category == "fx":
        values[a + "engine"] = 2
        values[b + "engine"] = 0
        values[a + "level"] = 0.6
        values[b + "level"] = 0.0
        values[a + "noise"] = 0.35
        if role == "riser":
            values[a + "attack"] = 0.8
            values[a + "cutoff"] = 0.7
        elif role == "downer":
            values[a + "pitch"] = 0.9
            values[a + "decay"] = 0.15
            values[b + "engine"] = 0
            values[b + "level"] = 0.4
        elif role == "impact":
            values[a + "attack"] = 0.0
            values[a + "decay"] = 0.08
            values[a + "drive"] = 0.8
        elif role == "zap":
            values[a + "cutoff"] = 0.95
            values[a + "decay"] = 0.04
            values[a + "resonance"] = 0.7
        elif role == "crackle":
            values[a + "noise"] = 0.8
            values[a + "sustain"] = 0.4
        elif role == "sweep":
            values[a + "env"] = 1.0
            values[a + "cutoff"] = 0.5
            values[a + "resonance"] = 0.65
        elif role == "echo":
            values[a + "decay"] = 0.1
            values[a + "sustain"] = 0.0


def programs(rows):
    made = []
    number = 1
    for category, variants in PLAN:
        roles = ROLES[category]
        if len(roles) != variants:
            raise SystemExit(f"{category} role count {len(roles)} != {variants}")
        for style_id, style_name in STYLES:
            for index, (role, purpose) in enumerate(roles):
                values = blank(rows)
                recipe(category, role, values)
                apply_style(values, style_id, index)
                # Stable per-program trim so two styles cannot collapse to one vector.
                values["syn.output"] = 0.62 + ((number * 17) % 23) * 0.01
                ident = f"FS-{number:03d}"
                name = f"{style_name} {role.replace('-', ' ').title()} {category.title()} {number:03d}"
                made.append({
                    "id": ident,
                    "name": name,
                    "style": style_id,
                    "style_name": style_name,
                    "role": role,
                    "purpose": f"{style_name} {purpose}",
                    "category": CATEGORY_ID[category],
                    "values": clamp_to(rows, values),
                })
                number += 1
    if len(made) != 600:
        raise SystemExit(f"program count {len(made)}")
    return made


def degree(scale, step):
    return scale[(step % len(scale))] + 12 * (step // len(scale))


def add_note(notes, tick, note, velocity, duration, slide, accent):
    notes.append((tick, note, velocity, duration, 1 if slide else 0, 1 if accent else 0))


def phrase_notes(style_id, role):
    scale = SCALES[style_id]
    style_index = next(i for i, item in enumerate(STYLES) if item[0] == style_id)
    root = 36 + style_index
    notes = []
    if role == "offbeat-bass":
        for step in range(16):
            if step % 4 == 0:
                continue
            add_note(notes, step * 240, root, 112 if step % 4 == 2 else 90, 200, step % 8 == 7, step % 4 == 2)
    elif role == "held-bass":
        add_note(notes, 0, root, 100, 1920, False, False)
        add_note(notes, 1920, root + degree(scale, 4), 96, 1800, True, False)
    elif role == "acid-slide":
        pattern = [0, 0, 1, 3, 4, 3, 1, 0, 7, 4, 3, 1, 0, -1, 0, 3]
        for step, deg in enumerate(pattern):
            slide = step in (2, 5, 9, 14)
            add_note(notes, step * 240, root + 12 + degree(scale, deg), 122 if step % 4 == 2 else 80,
                     260 if slide else 180, slide, step % 4 == 2)
    elif role == "acid-rest":
        for step in (1, 2, 4, 6, 9, 11, 13, 14):
            add_note(notes, step * 240, root + 12 + degree(scale, step % 5), 88, 160, step % 3 == 0, False)
    elif role == "lead-motif":
        motif = [0, 2, 4, 2, 7, 4, 2, 0]
        for step, deg in enumerate(motif):
            add_note(notes, step * 480, root + 24 + degree(scale, deg), 104, 400, False, step == 4)
    elif role == "lead-answer":
        motif = [4, 4, 7, 5, 4, 2, 0, 2]
        for step, deg in enumerate(motif):
            add_note(notes, 240 + step * 480, root + 24 + degree(scale, deg), 98, 360, False, False)
    elif role == "pluck-arp":
        for step in range(12):
            add_note(notes, step * 240, root + 12 + degree(scale, step), 100, 140, False, step % 3 == 0)
    elif role == "key-stab":
        for beat in (1, 3, 5, 7):
            for voice, deg in enumerate((0, 2, 4)):
                add_note(notes, beat * 480, root + 12 + degree(scale, deg), 108 - voice * 6, 220, False, True)
    elif role == "pad-chord":
        for voice, deg in enumerate((0, 2, 4, 7)):
            add_note(notes, 0, root + 12 + degree(scale, deg), 78, 3600, False, False)
    elif role == "atmos-swell":
        add_note(notes, 0, root + 24 + degree(scale, 4), 64, 3600, False, False)
    elif role == "fx-hit":
        add_note(notes, 0, root + 12, 120, 120, False, True)
        add_note(notes, 480, root + 36, 70, 960, False, False)
    elif role == "fill":
        add_note(notes, 0, root + 12 + degree(scale, 0), 80, 140, False, False)
        for step in range(8, 16):
            add_note(notes, step * 240, root + 12 + degree(scale, step), 96 + (step % 3) * 8, 140,
                     step > 13, step % 2 == 0)
    else:
        raise SystemExit(role)
    notes = [
        (tick, note, velocity, duration + style_index, slide, accent)
        for tick, note, velocity, duration, slide, accent in notes
    ]
    if not notes or notes[0][0] > 480:
        raise SystemExit(f"{style_id} {role} has no early note")
    return notes


def phrases():
    made = []
    number = 1
    for style_id, style_name in STYLES:
        for role, category, purpose in PHRASE_ROLES:
            notes = phrase_notes(style_id, role)
            made.append({
                "id": f"FP-{number:03d}",
                "name": f"{style_name} {role.replace('-', ' ').title()} {number:03d}",
                "style": style_id,
                "role": role,
                "purpose": f"{style_name}: {purpose}",
                "category": CATEGORY_ID[category],
                "notes": notes,
            })
            number += 1
    if len(made) != 120:
        raise SystemExit(f"phrase count {len(made)}")
    return made


def fingerprint(values):
    return tuple(round(v, 5) for v in values)


def note_key(notes):
    return tuple(notes)


def emit(rows, bank, phrase_bank):
    seen = {}
    for program in bank:
        key = fingerprint(program["values"])
        if key in seen:
            raise SystemExit(f"duplicate program vector {program['id']} and {seen[key]}")
        seen[key] = program["id"]
        if not program["purpose"] or not program["name"] or not program["role"]:
            raise SystemExit("empty text")
    names = [p["name"] for p in bank]
    if len(set(names)) != len(names):
        raise SystemExit("duplicate program name")
    ids = [p["id"] for p in bank]
    if len(set(ids)) != len(ids):
        raise SystemExit("duplicate program id")
    seen_notes = {}
    for phrase in phrase_bank:
        key = note_key(phrase["notes"])
        if key in seen_notes:
            raise SystemExit(f"duplicate phrase {phrase['id']} and {seen_notes[key]}")
        seen_notes[key] = phrase["id"]
        for tick, note, velocity, duration, slide, accent in phrase["notes"]:
            if tick < 0 or not (0 <= note <= 127) or not (1 <= velocity <= 127) or duration <= 0:
                raise SystemExit(f"bad note in {phrase['id']}")
            if slide not in (0, 1) or accent not in (0, 1):
                raise SystemExit("bad flag")

    lines = ["// Generated by factory/generate_factory.py. Do not edit.", ""]
    lines.append("static const float kFactoryValues[][75] = {")
    for program in bank:
        body = ", ".join(f"{v:.5f}f" for v in program["values"])
        lines.append(f"    {{ {body} }},")
    lines.append("};")
    lines.append("")
    lines.append("static const PhraseNote kFactoryNotes[] = {")
    offsets = []
    cursor = 0
    for phrase in phrase_bank:
        offsets.append(cursor)
        for tick, note, velocity, duration, slide, accent in phrase["notes"]:
            lines.append(
                f"    {{ {tick}, {note}, {velocity}, {duration}, {bool(slide).__str__().lower()}, {bool(accent).__str__().lower()} }},"
            )
        cursor += len(phrase["notes"])
    lines.append("};")
    lines.append("")
    lines.append("static const FactoryProgram kFactoryPrograms[] = {")
    for index, program in enumerate(bank):
        purpose = program["purpose"].replace('"', "'")
        name = program["name"].replace('"', "'")
        lines.append(
            "    { "
            f"\"{program['id']}\", \"{name}\", \"{program['style']}\", \"{program['role']}\", "
            f"\"{purpose}\", static_cast<ProgramCategory> ({program['category']}), kFactoryValues[{index}] "
            "},"
        )
    lines.append("};")
    lines.append("")
    lines.append("static const FactoryPhrase kFactoryPhrases[] = {")
    for index, phrase in enumerate(phrase_bank):
        purpose = phrase["purpose"].replace('"', "'")
        lines.append(
            "    { "
            f"\"{phrase['id']}\", \"{phrase['name']}\", \"{phrase['style']}\", \"{phrase['role']}\", "
            f"\"{purpose}\", static_cast<ProgramCategory> ({phrase['category']}), 960, "
            f"kFactoryNotes + {offsets[index]}, {len(phrase['notes'])} "
            "},"
        )
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")

    counts = {}
    for program in bank:
        counts[program["category"]] = counts.get(program["category"], 0) + 1
    manifest = ["VOX Trance Synthesis factory manifest", "author: original project data, no third-party presets or MIDI",
                f"programs: {len(bank)}", f"phrases: {len(phrase_bank)}", "programs-by-category:"]
    names_by_id = {0: "bass", 1: "acid", 2: "lead", 3: "pluck", 4: "keys", 5: "pad", 6: "atmos", 7: "fx"}
    for key in range(8):
        manifest.append(f"  {names_by_id[key]}: {counts.get(key, 0)}")
    manifest.append("styles: " + ", ".join(style for style, _ in STYLES))
    manifest.append("ids: FS-001..FS-600 programs, FP-001..FP-120 phrases")
    MANIFEST.write_text("\n".join(manifest) + "\n")
    print(f"wrote {len(bank)} programs and {len(phrase_bank)} phrases")


def main():
    rows = catalog()
    emit(rows, programs(rows), phrases())


if __name__ == "__main__":
    main()
