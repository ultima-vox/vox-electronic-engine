#!/usr/bin/env python3
"""VOX Synth factory candidates. Original data, no third-party presets or MIDI.

Writes FactoryData.inc, MANIFEST.txt, COVERAGE.csv and midi/FP-*.mid.
Installed tables are candidates. Human approval stays at 0.
"""

import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "Parameters.h"
OUT = ROOT / "FactoryData.inc"
HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "MANIFEST.txt"
COVERAGE = HERE / "COVERAGE.csv"
MIDI_DIR = HERE / "midi"

# FactoryRole ordinals. Independent of ProgramCategory, which UBNK stores.
ROLE_BASS, ROLE_ACID, ROLE_LEAD, ROLE_PLUCK, ROLE_PAD, ROLE_ARP, ROLE_STAB = range(7)
ROLE_NAME = ["bass", "acid", "lead", "pluck-keys", "pad-atmos", "arp", "stab"]
ROLE_QUOTA = [120, 90, 110, 80, 90, 75, 35]
OUTPUT_INDEX = 14

STYLES = [
    ("classic90s", "Classic 90s", "classic"),
    ("dream", "Dream", "classic"),
    ("uplifting", "Uplifting", "classic"),
    ("anthem", "Anthem", "classic"),
    ("melodic", "Melodic", "classic"),
    ("balearic", "Balearic", "classic"),
    ("progressive", "Progressive", "progressive"),
    ("deepprog", "Deep Progressive", "progressive"),
    ("acidtrance", "Acid Trance", "progressive"),
    ("techtrance", "Tech Trance", "progressive"),
    ("hardtrance", "Hard Trance", "progressive"),
    ("peaktime", "Peak-Time", "progressive"),
    ("goa", "Goa", "psy"),
    ("progpsy", "Progressive Psy", "psy"),
    ("morning", "Morning Psy", "psy"),
    ("fullon", "Full-on", "psy"),
    ("twilight", "Twilight", "psy"),
    ("forest", "Forest", "psy"),
    ("darkpsy", "Dark Psy", "psy"),
    ("hitech", "Hi-Tech", "psy"),
    ("zenonesque", "Zenonesque", "psy"),
    ("psybient", "Psybient", "psy"),
]

FAMILY_BIAS = {
    "classic": dict(cutoff=0.62, resonance=0.30, drive=0.18, decay=0.30, release=0.34, slide=0.16),
    "progressive": dict(cutoff=0.44, resonance=0.26, drive=0.22, decay=0.42, release=0.48, slide=0.10),
    "psy": dict(cutoff=0.56, resonance=0.68, drive=0.36, decay=0.16, release=0.18, slide=0.40),
}

# Per-style nudge so two styles in one family are not the same patch.
STYLE_NUDGE = {
    "classic90s": (0.00, 0.00), "dream": (-0.10, 0.08), "uplifting": (0.12, -0.06),
    "anthem": (0.08, 0.04), "melodic": (0.02, -0.04), "balearic": (-0.06, 0.02),
    "progressive": (0.00, 0.00), "deepprog": (-0.08, -0.04), "acidtrance": (0.06, 0.10),
    "techtrance": (-0.04, 0.08), "hardtrance": (0.04, 0.06), "peaktime": (0.10, 0.02),
    "goa": (0.14, 0.04), "progpsy": (0.04, -0.06), "morning": (0.10, -0.02),
    "fullon": (0.06, 0.08), "twilight": (-0.06, 0.04), "forest": (-0.12, 0.10),
    "darkpsy": (-0.16, 0.06), "hitech": (0.18, -0.04), "zenonesque": (-0.08, 0.02),
    "psybient": (-0.14, -0.08),
}

STYLE_FX = {
    "classic": "chorus", "progressive": "delay", "psy": "phaser",
}

SCALES = {
    "classic90s": [0, 2, 3, 5, 7, 8, 10],
    "dream": [0, 2, 4, 7, 9],
    "uplifting": [0, 2, 4, 5, 7, 9, 11],
    "anthem": [0, 2, 4, 7, 9, 11],
    "melodic": [0, 2, 3, 5, 7, 9, 10],
    "balearic": [0, 2, 4, 5, 9, 10],
    "progressive": [0, 2, 3, 5, 7, 9, 10],
    "deepprog": [0, 2, 3, 7, 8, 10],
    "acidtrance": [0, 1, 3, 5, 7, 8, 10],
    "techtrance": [0, 1, 3, 6, 7, 10],
    "hardtrance": [0, 2, 3, 6, 7, 10],
    "peaktime": [0, 2, 4, 7, 10],
    "goa": [0, 2, 3, 5, 7, 8, 11],
    "progpsy": [0, 1, 3, 5, 7, 8, 10],
    "morning": [0, 2, 4, 5, 7, 11],
    "fullon": [0, 1, 4, 5, 7, 8, 10],
    "twilight": [0, 1, 3, 5, 8, 10],
    "forest": [0, 1, 3, 5, 6, 8, 10],
    "darkpsy": [0, 1, 3, 4, 7, 8, 10],
    "hitech": [0, 1, 4, 6, 7, 8, 11],
    "zenonesque": [0, 1, 5, 6, 7, 10],
    "psybient": [0, 2, 3, 7, 8, 10],
}

# Bass styles that use the VA engine instead of Psy Bass, so the matrix is not one engine.
VA_BASS = {"dream", "balearic", "deepprog", "psybient", "melodic"}

RECIPES = {
    ROLE_BASS: ["offbeat", "rolling", "sub", "glide", "growl", "short"],
    ROLE_ACID: ["slide", "accent", "rest", "squelch", "octave", "soft"],
    ROLE_LEAD: ["hook", "wide", "solo", "bright", "dark", "fifth"],
    ROLE_PLUCK: ["pluck", "bell", "mute", "epiano", "organ", "stack"],
    ROLE_PAD: ["swell", "choir", "dark", "air", "bed", "glass"],
    ROLE_ARP: ["gate", "up", "octave", "tight", "fifth", "muted"],
    ROLE_STAB: ["rave", "minor", "seventh", "short", "low", "bright"],
}

MOODS = ["Warm", "Bright", "Dark", "Hypnotic", "Raw", "Airy"]
ROLE_OUTPUT = [0.78, 0.74, 0.70, 0.72, 0.58, 0.68, 0.70]
AUDITION = [36, 48, 67, 72, 60, 64, 63]


def catalog():
    text = HEADER.read_text()
    rows = re.findall(
        r'\{ "([^"]+)", "[^"]+", ([0-9.]+)f, ([0-9.]+)f, ([0-9.]+)f, (true|false) \}',
        text,
    )
    if len(rows) < 75:
        raise SystemExit(f"expected at least 75 frozen parameters, found {len(rows)}")
    # Factory rows stay 75 wide. Appended VA ids load as defaults.
    rows = rows[:75]
    return [(i, float(lo), float(hi), float(init), choice == "true") for i, lo, hi, init, choice in rows]


def blank(rows):
    return {ident: init for ident, _, _, init, _ in rows}


def clamp_row(rows, values):
    out = []
    for ident, lo, hi, _, _ in rows:
        value = values[ident]
        if value < lo - 1.0e-6 or value > hi + 1.0e-6:
            raise SystemExit(f"{ident} out of range: {value}")
        out.append(min(hi, max(lo, value)))
    return out


def c01(value):
    return max(0.0, min(1.0, value))


def set_fx(values, slot, mix, amount, rate, character):
    for name in ("distortion", "wavefolder", "phaser", "flanger", "chorus", "bitcrusher", "delay", "reverb"):
        values[f"syn.fx.{name}"] = 0.0
        values[f"syn.fx.{name}.mix"] = 0.0
    values[f"syn.fx.{slot}"] = 1.0
    values[f"syn.fx.{slot}.mix"] = mix
    values[f"syn.fx.{slot}.amount"] = amount
    values[f"syn.fx.{slot}.rate"] = rate
    values[f"syn.fx.{slot}.character"] = character


def apply_recipe(role, recipe, values):
    a, b = "syn.a.", "syn.b."
    values[b + "level"] = 0.0
    if role == ROLE_BASS:
        values[a + "level"] = 0.84
        values[a + "sub"] = 0.48
        if recipe == "offbeat":
            values[a + "slide"] = 0.08
            values[a + "decay"] = 0.08
            values[a + "sustain"] = 0.15
            values[a + "cutoff"] = 0.40
        elif recipe == "rolling":
            values[a + "slide"] = 0.22
            values[a + "cutoff"] = 0.48
            values[a + "sustain"] = 0.62
        elif recipe == "sub":
            values[a + "sub"] = 0.92
            values[a + "cutoff"] = 0.16
            values[a + "drive"] = 0.08
            values[a + "resonance"] = 0.2
        elif recipe == "glide":
            values[a + "slide"] = 0.9
            values[a + "portamento"] = 0.4
            values[a + "cutoff"] = 0.36
        elif recipe == "growl":
            values[a + "drive"] = 0.86
            values[a + "pitch"] = 0.7
            values[a + "env"] = 0.3
            values[a + "cutoff"] = 0.5
        elif recipe == "short":
            values[a + "decay"] = 0.04
            values[a + "sustain"] = 0.0
            values[a + "release"] = 0.08
            values[a + "cutoff"] = 0.55
    elif role == ROLE_ACID:
        values[a + "engine"] = 1
        values[a + "level"] = 0.8
        values[a + "wave"] = 0.7
        values[a + "env"] = 0.9
        values[a + "resonance"] = 0.74
        if recipe == "slide":
            values[a + "slide"] = 0.85
            values[a + "cutoff"] = 0.5
        elif recipe == "accent":
            values[a + "wave"] = 0.95
            values[a + "decay"] = 0.08
            values[a + "cutoff"] = 0.62
        elif recipe == "rest":
            values[a + "decay"] = 0.04
            values[a + "sustain"] = 0.0
            values[a + "slide"] = 0.15
        elif recipe == "squelch":
            values[a + "resonance"] = 0.95
            values[a + "cutoff"] = 0.42
            values[a + "drive"] = 0.55
        elif recipe == "octave":
            values[b + "engine"] = 1
            values[b + "level"] = 0.34
            values[b + "cutoff"] = 0.28
            values[a + "slide"] = 0.45
        elif recipe == "soft":
            values[a + "drive"] = 0.06
            values[a + "wave"] = 0.2
            values[a + "resonance"] = 0.28
            values[a + "cutoff"] = 0.58
    elif role == ROLE_LEAD:
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.72
        values[a + "attack"] = 0.02
        values[a + "sustain"] = 0.7
        if recipe == "hook":
            values[a + "detune"] = 0.12
            values[b + "level"] = 0.12
            values[a + "decay"] = 0.22
        elif recipe == "wide":
            values[a + "detune"] = 0.62
            values[b + "detune"] = 0.7
            values[b + "level"] = 0.48
        elif recipe == "solo":
            values[a + "detune"] = 0.02
            values[b + "level"] = 0.0
            values[a + "cutoff"] = 0.6
        elif recipe == "bright":
            values[a + "cutoff"] = 0.9
            values[a + "resonance"] = 0.2
            values[b + "level"] = 0.2
        elif recipe == "dark":
            values[a + "cutoff"] = 0.24
            values[a + "drive"] = 0.4
            values[a + "detune"] = 0.08
        elif recipe == "fifth":
            values[b + "level"] = 0.42
            values[b + "cutoff"] = 0.66
            values[a + "detune"] = 0.16
    elif role == ROLE_PLUCK:
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.74
        values[a + "attack"] = 0.0
        values[a + "sustain"] = 0.05
        values[b + "level"] = 0.0
        if recipe == "pluck":
            values[a + "decay"] = 0.12
            values[a + "cutoff"] = 0.7
        elif recipe == "bell":
            values[a + "decay"] = 0.2
            values[a + "cutoff"] = 0.92
            values[a + "resonance"] = 0.4
        elif recipe == "mute":
            values[a + "decay"] = 0.05
            values[a + "cutoff"] = 0.28
        elif recipe == "epiano":
            values[a + "attack"] = 0.03
            values[a + "decay"] = 0.36
            values[a + "sustain"] = 0.25
        elif recipe == "organ":
            values[a + "sustain"] = 0.92
            values[a + "decay"] = 0.4
            values[a + "detune"] = 0.04
            values[a + "release"] = 0.12
        elif recipe == "stack":
            values[b + "level"] = 0.36
            values[b + "decay"] = 0.1
            values[a + "decay"] = 0.14
    elif role == ROLE_PAD:
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.5
        values[b + "level"] = 0.36
        values[a + "sustain"] = 0.92
        values[b + "sustain"] = 0.9
        values[a + "attack"] = 0.28
        values[a + "release"] = 0.7
        if recipe == "swell":
            values[a + "attack"] = 0.45
            values[a + "cutoff"] = 0.5
        elif recipe == "choir":
            values[a + "detune"] = 0.66
            values[b + "detune"] = 0.72
            values[a + "cutoff"] = 0.58
        elif recipe == "dark":
            values[a + "cutoff"] = 0.18
            values[b + "cutoff"] = 0.16
            values[a + "drive"] = 0.2
        elif recipe == "air":
            values[a + "noise"] = 0.28
            values[a + "cutoff"] = 0.74
            values[a + "level"] = 0.4
        elif recipe == "bed":
            values[a + "detune"] = 0.1
            values[a + "attack"] = 0.2
            values[a + "cutoff"] = 0.34
        elif recipe == "glass":
            values[a + "cutoff"] = 0.86
            values[a + "attack"] = 0.16
            values[a + "resonance"] = 0.28
    elif role == ROLE_ARP:
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.7
        values[a + "attack"] = 0.0
        values[a + "sustain"] = 0.2
        values[a + "decay"] = 0.12
        values[b + "level"] = 0.0
        if recipe == "gate":
            values[a + "decay"] = 0.08
            values[a + "cutoff"] = 0.62
        elif recipe == "up":
            values[a + "cutoff"] = 0.78
            values[a + "decay"] = 0.16
        elif recipe == "octave":
            values[b + "level"] = 0.3
            values[b + "decay"] = 0.1
            values[a + "cutoff"] = 0.7
        elif recipe == "tight":
            values[a + "decay"] = 0.04
            values[a + "sustain"] = 0.0
            values[a + "cutoff"] = 0.5
        elif recipe == "fifth":
            values[b + "level"] = 0.28
            values[b + "cutoff"] = 0.8
        elif recipe == "muted":
            values[a + "cutoff"] = 0.26
            values[a + "decay"] = 0.07
    elif role == ROLE_STAB:
        values[a + "engine"] = 2
        values[b + "engine"] = 2
        values[a + "level"] = 0.76
        values[b + "level"] = 0.5
        values[a + "attack"] = 0.0
        values[a + "sustain"] = 0.12
        values[a + "decay"] = 0.14
        values[a + "release"] = 0.16
        if recipe == "rave":
            values[a + "cutoff"] = 0.8
            values[a + "detune"] = 0.2
        elif recipe == "minor":
            values[a + "cutoff"] = 0.55
            values[a + "decay"] = 0.18
        elif recipe == "seventh":
            values[b + "level"] = 0.55
            values[a + "cutoff"] = 0.64
            values[a + "sustain"] = 0.3
        elif recipe == "short":
            values[a + "decay"] = 0.05
            values[a + "sustain"] = 0.0
            values[b + "level"] = 0.2
        elif recipe == "low":
            values[a + "cutoff"] = 0.3
            values[a + "drive"] = 0.35
        elif recipe == "bright":
            values[a + "cutoff"] = 0.92
            values[a + "resonance"] = 0.22


def apply_style(values, style_id, family):
    bias = FAMILY_BIAS[family]
    nudge_c, nudge_r = STYLE_NUDGE[style_id]
    for part in ("a", "b"):
        values[f"syn.{part}.cutoff"] = c01(values[f"syn.{part}.cutoff"] * 0.82 + bias["cutoff"] * 0.18 + nudge_c * 0.5)
        values[f"syn.{part}.resonance"] = c01(values[f"syn.{part}.resonance"] * 0.82 + bias["resonance"] * 0.18 + nudge_r * 0.5)
        values[f"syn.{part}.drive"] = c01(values[f"syn.{part}.drive"] * 0.85 + bias["drive"] * 0.15)
        values[f"syn.{part}.slide"] = c01(values[f"syn.{part}.slide"] * 0.85 + bias["slide"] * 0.15)
        values[f"syn.{part}.decay"] = c01(values[f"syn.{part}.decay"] * 0.82 + bias["decay"] * 0.18)
        values[f"syn.{part}.release"] = c01(values[f"syn.{part}.release"] * 0.82 + bias["release"] * 0.18)
    slot = STYLE_FX[family]
    mix = 0.16 + (STYLE_NUDGE[style_id][0] + 0.2) * 0.4
    set_fx(values, slot, c01(mix), c01(0.35 + nudge_r), c01(0.3 + nudge_c), c01(0.45 + nudge_r))


def quotas_per_style():
    """How many variants of each role land on each style. Every style gets at least one."""
    counts = []
    for quota in ROLE_QUOTA:
        base, extra = divmod(quota, len(STYLES))
        if base < 1:
            raise SystemExit("quota does not cover every style")
        row = [base + (1 if i < extra else 0) for i in range(len(STYLES))]
        counts.append(row)
    return counts


def programs(rows):
    per_style = quotas_per_style()
    made = []
    number = 1
    for role, quota_row in enumerate(per_style):
        for style_index, (style_id, style_name, family) in enumerate(STYLES):
            for variant in range(quota_row[style_index]):
                recipe = RECIPES[role][variant]
                values = blank(rows)
                apply_recipe(role, recipe, values)
                if role == ROLE_BASS:
                    values["syn.a.engine"] = 2 if style_id in VA_BASS else 0
                    if style_id not in VA_BASS:
                        values["syn.b.engine"] = 0 if recipe == "octave" else 1
                apply_style(values, style_id, family)
                # Style identity on the voice itself, not on the output trim.
                values["syn.a.sub"] = c01(values["syn.a.sub"] + ((style_index % 5) - 2) * 0.07)
                values["syn.a.env"] = c01(values["syn.a.env"] + ((style_index % 4) - 1) * 0.09)
                values["syn.a.detune"] = c01(values["syn.a.detune"] + ((style_index % 6) - 2) * 0.06)
                values["syn.a.pitch"] = c01(values["syn.a.pitch"] + ((style_index % 3) - 1) * 0.08)
                values["syn.a.attack"] = c01(values["syn.a.attack"] + (style_index % 4) * 0.025)
                values["syn.a.cutoff"] = c01(values["syn.a.cutoff"] + (variant - 2) * 0.07)
                values["syn.a.resonance"] = c01(values["syn.a.resonance"] + ((variant % 3) - 1) * 0.14)
                values["syn.a.decay"] = c01(values["syn.a.decay"] + ((variant * 2) % 5 - 2) * 0.09)
                values["syn.b.level"] = c01(values["syn.b.level"] + (0.18 if variant % 2 == 0 else 0.0))
                values["syn.output"] = ROLE_OUTPUT[role]
                ident = f"FS-{number:03d}"
                name = f"{style_name} {recipe.title()} {ROLE_NAME[role].title()} {number:03d}"
                made.append({
                    "id": ident,
                    "name": name,
                    "style": style_id,
                    "style_name": style_name,
                    "variant": recipe,
                    "purpose": f"{style_name} {recipe} {ROLE_NAME[role]} candidate, not approved",
                    "role": role,
                    "mood": MOODS[(variant + style_index) % len(MOODS)],
                    "audition": AUDITION[role],
                    "values": clamp_row(rows, values),
                })
                number += 1
    if len(made) != 600:
        raise SystemExit(f"program count {len(made)}")
    if [sum(1 for p in made if p["role"] == i) for i in range(7)] != ROLE_QUOTA:
        raise SystemExit("quota mismatch")
    return made


def _feat(**kwargs):
    base = dict(
        unison=0.0, detune=0.0, spread=0.0, wt=0.0, pos=0.0,
        fm=0.0, ring=0.0, osc3=0.0, semi=0.5,
        mod_src=0, mod_dst=0, mod_amt=0.5,
        extra=-1, extra_src=0, extra_dst=0, extra_amt=0.5,
    )
    base.update(kwargs)
    return base


# VA-only. Psy Bass and Acid stay on the defaults above.
# Unison is the host knob: 0 is one voice, 1 is two, 2 is three.
FEATURE = {
    ROLE_BASS: {
        "offbeat": _feat(wt=0.12, pos=0.20),
        "rolling": _feat(wt=0.16, pos=0.28, mod_src=1, mod_dst=1, mod_amt=0.58),
        "sub": _feat(wt=0.08, pos=0.12),
        "glide": _feat(wt=0.10, pos=0.24, fm=0.06),
        "growl": _feat(wt=0.22, pos=0.16, fm=0.14, mod_src=1, mod_dst=1, mod_amt=0.64),
        "short": _feat(wt=0.18, pos=0.40),
    },
    ROLE_LEAD: {
        "hook": _feat(unison=1, detune=0.28, spread=0.55, wt=0.22, pos=0.40, fm=0.12, osc3=0.10, mod_src=1, mod_dst=1, mod_amt=0.66),
        "wide": _feat(unison=2, detune=0.48, spread=0.82, wt=0.38, pos=0.55, ring=0.08, mod_src=1, mod_dst=1, mod_amt=0.60),
        "solo": _feat(fm=0.42, osc3=0.55, semi=0.62, mod_src=2, mod_dst=4, mod_amt=0.64),
        "bright": _feat(unison=1, detune=0.22, spread=0.48, wt=0.62, pos=0.78, osc3=0.15, mod_src=1, mod_dst=1, mod_amt=0.58),
        "dark": _feat(unison=1, detune=0.18, spread=0.40, wt=0.16, pos=0.18, fm=0.08, mod_src=1, mod_dst=1, mod_amt=0.70),
        "fifth": _feat(detune=0.10, spread=0.30, wt=0.20, pos=0.48, ring=0.32, osc3=0.28, semi=0.75, mod_src=1, mod_dst=4, mod_amt=0.62),
    },
    ROLE_PLUCK: {
        "pluck": _feat(wt=0.34, pos=0.62, mod_src=1, mod_dst=1, mod_amt=0.60),
        "bell": _feat(fm=0.20, osc3=0.70, semi=0.75, pos=0.80, mod_src=1, mod_dst=3, mod_amt=0.58),
        "mute": _feat(wt=0.12, pos=0.18),
        "epiano": _feat(fm=0.18, osc3=0.25, semi=0.58, wt=0.10, pos=0.46, mod_src=1, mod_dst=1, mod_amt=0.56),
        "organ": _feat(unison=1, detune=0.08, spread=0.22, mod_src=1, mod_dst=1, mod_amt=0.54),
        "stack": _feat(unison=1, detune=0.24, spread=0.46, wt=0.25, pos=0.50, mod_src=1, mod_dst=1, mod_amt=0.60),
    },
    ROLE_PAD: {
        "swell": _feat(unison=2, detune=0.36, spread=0.80, wt=0.48, pos=0.42, mod_src=1, mod_dst=1, mod_amt=0.68, extra=4, extra_src=2, extra_dst=5, extra_amt=0.64),
        "choir": _feat(unison=2, detune=0.42, spread=0.90, wt=0.55, pos=0.58, mod_src=1, mod_dst=1, mod_amt=0.66, extra=4, extra_src=2, extra_dst=1, extra_amt=0.60),
        "dark": _feat(unison=1, detune=0.20, spread=0.48, wt=0.12, pos=0.15, mod_src=1, mod_dst=1, mod_amt=0.72),
        "air": _feat(unison=1, detune=0.30, spread=0.70, wt=0.42, pos=0.66, mod_src=1, mod_dst=1, mod_amt=0.62),
        "bed": _feat(unison=1, detune=0.16, spread=0.40, wt=0.20, pos=0.30, mod_src=1, mod_dst=1, mod_amt=0.58),
        "glass": _feat(wt=0.72, pos=0.82, osc3=0.20, semi=0.70, mod_src=2, mod_dst=1, mod_amt=0.60),
    },
    ROLE_ARP: {
        "gate": _feat(wt=0.30, pos=0.48, mod_src=1, mod_dst=1, mod_amt=0.74),
        "up": _feat(wt=0.45, pos=0.70, unison=1, detune=0.16, spread=0.36, mod_src=1, mod_dst=1, mod_amt=0.66),
        "octave": _feat(wt=0.28, pos=0.40, ring=0.12, mod_src=1, mod_dst=1, mod_amt=0.62),
        "tight": _feat(wt=0.15, pos=0.22, mod_src=1, mod_dst=1, mod_amt=0.70),
        "fifth": _feat(wt=0.24, pos=0.52, ring=0.22, osc3=0.16, semi=0.75, mod_src=1, mod_dst=3, mod_amt=0.60),
        "muted": _feat(wt=0.10, pos=0.16, mod_src=1, mod_dst=1, mod_amt=0.56),
    },
    ROLE_STAB: {
        "rave": _feat(unison=2, detune=0.30, spread=0.72, wt=0.40, pos=0.60, mod_src=1, mod_dst=1, mod_amt=0.64),
        "minor": _feat(unison=1, detune=0.22, spread=0.50, wt=0.30, pos=0.36, mod_src=1, mod_dst=1, mod_amt=0.60),
        "seventh": _feat(unison=1, detune=0.26, spread=0.58, wt=0.35, pos=0.44, mod_src=1, mod_dst=1, mod_amt=0.62),
        "short": _feat(unison=1, detune=0.14, spread=0.34, wt=0.20, pos=0.28, mod_src=1, mod_dst=1, mod_amt=0.58),
        "low": _feat(wt=0.12, pos=0.18, fm=0.08, mod_src=1, mod_dst=1, mod_amt=0.66),
        "bright": _feat(unison=1, detune=0.20, spread=0.55, wt=0.55, pos=0.80, mod_src=1, mod_dst=1, mod_amt=0.60),
    },
}


def feature_for(program):
    if int(program["values"][0]) != 2:
        return _feat()
    table = FEATURE.get(program["role"], {})
    spec = dict(table.get(program["variant"], _feat(wt=0.2, pos=0.4, mod_src=1, mod_dst=1, mod_amt=0.6)))
    style_index = next(i for i, row in enumerate(STYLES) if row[0] == program["style"])
    if spec["wt"] > 0.0:
        spec["pos"] = c01(spec["pos"] + (style_index - 10) * 0.012)
    return spec


def degree(scale, step):
    return scale[step % len(scale)] + 12 * (step // len(scale))


def add(notes, tick, note, velocity, duration, slide=False, accent=False):
    notes.append((max(0, tick), max(0, min(127, note)), velocity, duration, slide, accent))


def bass_phrase(style_id, pattern, ordinal):
    scale = SCALES[style_id]
    root = 34 + ordinal
    notes = []
    swing = 30 if style_id in ("techtrance", "hardtrance", "darkpsy") else 0
    if pattern == "offbeat":
        for step in range(16):
            if step % 4 == 0:
                continue
            tick = step * 240 + (swing if step % 2 else 0)
            add(notes, tick, root + degree(scale, (step + ordinal) % 5), 108 if step % 4 == 2 else 86, 180,
                step % 8 == 7, step % 4 == 2)
    elif pattern == "rolling":
        for step in range(16):
            add(notes, step * 240, root + degree(scale, ordinal if step % 2 == 0 else ordinal + 3), 96, 200,
                step % 8 == 7, False)
    else:
        add(notes, 0, root, 100, 1440, False, False)
        add(notes, 1920, root + degree(scale, 2 + ordinal), 92, 1440, True, False)
    return notes


def acid_phrase(style_id, pattern, ordinal):
    scale = SCALES[style_id]
    root = 45 + ordinal
    motifs = {
        "slide": [0, 0, 1, 3, 4, 3, 1, 0],
        "accent": [0, 4, 3, 4, 7, 4, 3, 0],
        "rest": [0, None, 1, None, 3, 4, None, 0],
        "squelch": [0, 1, 0, 7, 5, 3, 1, 0],
        "octave": [0, 7, 3, 7, 4, 7, 3, 0],
    }
    notes = []
    for step, deg in enumerate(motifs[pattern]):
        if deg is None:
            continue
        slide = pattern == "slide" and step in (2, 5)
        add(notes, step * 240, root + degree(scale, deg + ordinal), 120 if step % 4 == 2 else 78,
            160 if pattern == "rest" else 220, slide, step % 4 == 2)
    return notes


def arp_phrase(style_id, pattern, ordinal):
    scale = SCALES[style_id]
    root = 55 + ordinal
    notes = []
    length = {"up": 8, "down": 8, "gate": 16, "octave": 8, "chord": 8}[pattern]
    for step in range(length):
        if pattern == "down":
            deg = 7 - (step % 8) + ordinal
        elif pattern == "octave":
            deg = (step % 4) + ordinal + (7 if step % 2 == 0 else 0)
        elif pattern == "chord":
            deg = (0, 2, 4, 7)[step % 4] + ordinal
        elif pattern == "gate":
            if step % 4 == 0:
                continue
            deg = (step % 5) + ordinal
        else:
            deg = step + ordinal
        tick = step * (120 if pattern == "gate" else 240)
        add(notes, tick, root + degree(scale, deg), 100, 100 if pattern == "gate" else 180, False, step % 4 == 0)
    return notes


def lead_phrase(style_id, pattern, ordinal):
    scale = SCALES[style_id]
    root = 62 + ordinal
    motifs = {
        "motif": [0, 2, 4, 2, 7, 4, 2, 0],
        "answer": [4, 4, 7, 5, 4, 2, 0, 2],
        "jump": [0, 7, 4, 12, 7, 4, 2, 0],
        "hold": [0, 2, 4, 4],
    }
    notes = []
    for step, deg in enumerate(motifs[pattern]):
        add(notes, step * (480 if pattern == "hold" else 240), root + degree(scale, deg + ordinal), 104,
            360 if pattern != "hold" else 420, False, step == 0)
    return notes


def harmony_phrase(style_id, pattern, ordinal):
    scale = SCALES[style_id]
    root = 52 + ordinal
    notes = []
    if pattern == "pad":
        for deg in (0, 2, 4, 6):
            add(notes, 0, root + degree(scale, deg + ordinal), 72, 2880)
    elif pattern == "stab":
        for beat in (0, 2, 4, 6):
            for voice, deg in enumerate((0, 2, 4)):
                add(notes, beat * 480, root + degree(scale, deg + ordinal), 112 - voice * 4, 200, False, True)
    else:
        for beat, chord in ((0, (0, 2, 4)), (2, (2, 4, 6)), (4, (0, 3, 4))):
            for deg in chord:
                add(notes, beat * 960, root + degree(scale, deg + ordinal), 96, 700, False, False)
    return notes


def build_phrases():
    made = []
    number = 1

    def push(role, style_id, pattern, notes, purpose):
        nonlocal number
        style_name = next(item[1] for item in STYLES if item[0] == style_id)
        made.append({
            "id": f"FP-{number:03d}",
            "name": f"{style_name} {pattern} {number:03d}",
            "style": style_id,
            "role": role,
            "pattern": pattern,
            "purpose": f"{style_name}: {purpose}",
            "notes": notes,
            "length": 3840,
        })
        number += 1

    bass_styles = [s[0] for s in STYLES if s[2] in ("psy", "classic")][:10]
    for ordinal, style_id in enumerate(bass_styles):
        for pattern in ("offbeat", "rolling", "held"):
            push(ROLE_BASS, style_id, pattern, bass_phrase(style_id, pattern, ordinal), f"bass {pattern}")
    acid_styles = ["acidtrance", "goa", "fullon", "darkpsy", "classic90s"]
    for ordinal, style_id in enumerate(acid_styles):
        for pattern in ("slide", "accent", "rest", "squelch", "octave"):
            push(ROLE_ACID, style_id, pattern, acid_phrase(style_id, pattern, ordinal), f"acid {pattern}")
    arp_styles = ["uplifting", "goa", "progressive", "hitech", "melodic"]
    for ordinal, style_id in enumerate(arp_styles):
        for pattern in ("up", "down", "gate", "octave", "chord"):
            push(ROLE_ARP, style_id, pattern, arp_phrase(style_id, pattern, ordinal), f"arp {pattern}")
    lead_styles = ["anthem", "morning", "techtrance", "dream", "forest"]
    for ordinal, style_id in enumerate(lead_styles):
        for pattern in ("motif", "answer", "jump", "hold"):
            push(ROLE_LEAD, style_id, pattern, lead_phrase(style_id, pattern, ordinal), f"lead {pattern}")
    harmony_styles = ["uplifting", "darkpsy", "balearic", "psybient"]
    for ordinal, style_id in enumerate(harmony_styles):
        push(ROLE_PAD, style_id, "pad", harmony_phrase(style_id, "pad", ordinal), "pad chord")
    stab_styles = ["classic90s", "peaktime", "fullon", "hardtrance", "goa", "forest"]
    for ordinal, style_id in enumerate(stab_styles):
        push(ROLE_STAB, style_id, "stab", harmony_phrase(style_id, "stab", ordinal + 4), "rave stab")
    chord_styles = ["progressive", "melodic", "twilight", "zenonesque", "dream", "techtrance", "anthem", "morning", "forest", "hardtrance"]
    for ordinal, style_id in enumerate(chord_styles):
        push(ROLE_STAB, style_id, "chord", harmony_phrase(style_id, "chord", ordinal + 2), "rhythmic chord")
    if len(made) != 120:
        raise SystemExit(f"phrase count {len(made)} {[p['id'] for p in made[:3]]}")
    counts = {}
    for phrase in made:
        counts[phrase["role"]] = counts.get(phrase["role"], 0) + 1
    expect = {ROLE_BASS: 30, ROLE_ACID: 25, ROLE_ARP: 25, ROLE_LEAD: 20}
    for role, count in expect.items():
        if counts.get(role) != count:
            raise SystemExit(f"phrase role {role} is {counts.get(role)}")
    pad_stab = counts.get(ROLE_PAD, 0) + counts.get(ROLE_STAB, 0)
    if pad_stab != 20:
        raise SystemExit(f"pad/stab phrases {pad_stab}")
    return made


def l1(a, b):
    return sum(abs(a[i] - b[i]) for i in range(len(a)) if i != OUTPUT_INDEX)


def pitch_key(notes):
    return tuple((tick, note, velocity, slide, accent) for tick, note, velocity, duration, slide, accent in notes)


def write_midi(path, notes):
    """Policy A. SMF keeps authored tick+duration and velocity. Slide does not move NoteOff."""
    events = []
    for tick, note, velocity, duration, _slide, _accent in notes:
        events.append((tick, 1, note, velocity))
        events.append((tick + duration, 0, note, 0))
    events.sort()
    data = bytearray()

    def varlen(value):
        buffer = [value & 0x7F]
        value >>= 7
        while value:
            buffer.append((value & 0x7F) | 0x80)
            value >>= 7
        data.extend(reversed(buffer))

    data.extend(b"\x00\xff\x51\x03")
    data.extend((500000).to_bytes(3, "big"))
    notice = b"slide/accent are not in this SMF"
    data.append(0x00)
    data.extend(b"\xff\x01")
    data.append(len(notice))
    data.extend(notice)
    last = 0
    for tick, on, note, velocity in events:
        varlen(tick - last)
        last = tick
        data.append(0x90 if on else 0x80)
        data.append(note & 0x7F)
        data.append(velocity & 0x7F)
    varlen(0)
    data.extend(b"\xff\x2f\x00")
    track = b"MTrk" + struct.pack(">I", len(data)) + data
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 960)
    path.write_bytes(header + track)


def emit(rows, bank, phrase_bank):
    closest = 99.0
    pair = None
    for i, left in enumerate(bank):
        for right in bank[i + 1:]:
            dist = l1(left["values"], right["values"])
            if dist < closest:
                closest = dist
                pair = (left["id"], right["id"], dist)
            single = max(abs(left["values"][n] - right["values"][n]) for n in range(75) if n != OUTPUT_INDEX)
            if dist < 0.25 or single < 0.08:
                raise SystemExit(
                    f"timbre too close {left['id']} {right['id']} {left['style']}/{left['variant']} "
                    f"vs {right['style']}/{right['variant']} L1 {dist:.3f} max {single:.3f}"
                )
    pitches = {}
    for phrase in phrase_bank:
        key = pitch_key(phrase["notes"])
        if key in pitches:
            raise SystemExit(f"phrase pitch duplicate {phrase['id']} {pitches[key]}")
        pitches[key] = phrase["id"]
        if not phrase["notes"] or phrase["notes"][0][0] > 480:
            raise SystemExit(f"{phrase['id']} has no early note")
        for tick, note, velocity, duration, slide, accent in phrase["notes"]:
            if not (0 <= note <= 127 and 1 <= velocity <= 127 and duration > 0 and tick >= 0):
                raise SystemExit(f"bad note {phrase['id']}")

    lines = [
        "// Generated by factory/generate_factory.py. Candidates, not human-approved.",
        "// Distinctness excludes syn.output. Do not edit.",
        "",
        "static const float kFactoryValues[][75] = {",
    ]
    for program in bank:
        body = ", ".join(f"{v:.5f}f" for v in program["values"])
        lines.append(f"    {{ {body} }},")
    lines.append("};")
    lines.append("")
    lines.append("static const FactoryFeature kFactoryFeatures[] = {")
    wavetable = unison = modulated = non_va = extra = 0
    for program in bank:
        feat = feature_for(program)
        engine = int(program["values"][0])
        if engine != 2 and (feat["wt"] > 0.0 or feat["unison"] > 0.0 or feat["mod_src"] != 0):
            non_va += 1
        if feat["wt"] >= 0.05:
            wavetable += 1
        if feat["unison"] >= 1.0:
            unison += 1
        if feat["mod_src"] > 0 and abs(feat["mod_amt"] - 0.5) > 0.02:
            modulated += 1
        if feat["extra"] >= 0:
            extra += 1
        lines.append(
            "    { "
            f"{feat['unison']:.2f}f, {feat['detune']:.2f}f, {feat['spread']:.2f}f, "
            f"{feat['wt']:.2f}f, {feat['pos']:.2f}f, {feat['fm']:.2f}f, {feat['ring']:.2f}f, "
            f"{feat['osc3']:.2f}f, {feat['semi']:.2f}f, "
            f"{int(feat['mod_src'])}, {int(feat['mod_dst'])}, {feat['mod_amt']:.2f}f, "
            f"{int(feat['extra'])}, {int(feat['extra_src'])}, {int(feat['extra_dst'])}, {feat['extra_amt']:.2f}f }},"
        )
    lines.append("};")
    if wavetable < 250 or unison < 120 or modulated < 180 or non_va != 0 or extra < 8:
        raise SystemExit(
            f"feature coverage wt {wavetable} unison {unison} mod {modulated} "
            f"extra {extra} non-va {non_va}"
        )
    lines.append("")
    lines.append("static const PhraseNote kFactoryNotes[] = {")
    offsets = []
    cursor = 0
    for phrase in phrase_bank:
        offsets.append(cursor)
        for tick, note, velocity, duration, slide, accent in phrase["notes"]:
            lines.append(
                f"    {{ {tick}, {note}, {velocity}, {duration}, {'true' if slide else 'false'}, {'true' if accent else 'false'} }},"
            )
        cursor += len(phrase["notes"])
    lines.append("};")
    lines.append("")
    lines.append("static const FactoryProgram kFactoryPrograms[] = {")
    for index, program in enumerate(bank):
        engine_a = int(program["values"][0])
        engine_b = int(program["values"][7])
        lines.append(
            "    { "
            f"\"{program['id']}\", \"{program['name']}\", \"{program['style']}\", \"{program['variant']}\", "
            f"\"{program['purpose']}\", static_cast<FactoryRole> ({program['role']}), {engine_a}, {engine_b}, "
            f"\"{program['mood']}\", {program['audition']}, kFactoryValues[{index}] }},"
        )
    lines.append("};")
    lines.append("")
    lines.append("static const FactoryPhrase kFactoryPhrases[] = {")
    for index, phrase in enumerate(phrase_bank):
        lines.append(
            "    { "
            f"\"{phrase['id']}\", \"{phrase['name']}\", \"{phrase['style']}\", \"{phrase['pattern']}\", "
            f"\"{phrase['purpose']}\", static_cast<FactoryRole> ({phrase['role']}), 960, {phrase['length']}, "
            f"kFactoryNotes + {offsets[index]}, {len(phrase['notes'])} }},"
        )
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")

    MIDI_DIR.mkdir(exist_ok=True)
    for old in MIDI_DIR.glob("*.mid"):
        old.unlink()
    for phrase in phrase_bank:
        write_midi(MIDI_DIR / f"{phrase['id']}.mid", phrase["notes"])

    role_counts = [sum(1 for p in bank if p["role"] == i) for i in range(7)]
    manifest = [
        "VOX Trance Synthesis factory ledger",
        "status: candidate / technically checked by the suite, NOT human-approved",
        "author: original project data, no third-party presets or MIDI",
        "installed_programs: 600",
        "candidate_programs: 600",
        "human_approved_programs: 0",
        "installed_phrases: 120",
        "human_approved_phrases: 0",
        "target_programs: 600",
        "programs_by_primary_role:",
    ]
    for index, count in enumerate(role_counts):
        manifest.append(f"  {ROLE_NAME[index]}: {count} target {ROLE_QUOTA[index]}")
    manifest.append("phrase_quotas: bass 30, acid 25, arp 25, lead 20, pad+chord+stab 20")
    manifest.append(f"nearest_timbre_L1_excluding_output: {closest:.4f} ({pair[0]} vs {pair[1]})")
    manifest.append("syn.output is role-level trim only and is excluded from distinctness")
    manifest.append("feature_layer: VA unison / wavetable / FM / host mod / MODX extra routes")
    manifest.append(f"feature_wavetable: {wavetable}")
    manifest.append(f"feature_unison_voices: {unison}")
    manifest.append(f"feature_mod_routes: {modulated}")
    manifest.append(f"feature_extra_routes: {extra}")
    manifest.append("feature_on_psy_or_acid: 0")
    manifest.append("provenance: original generator factory/generate_factory.py, no third-party presets")
    manifest.append("owner_listening: not started")
    manifest.append("ids: FS-001..FS-600, FP-001..FP-120")
    manifest.append("midi: factory/midi/FP-001.mid .. FP-120.mid")
    MANIFEST.write_text("\n".join(manifest) + "\n")

    coverage = ["role,style,engine,count,status,reason"]
    engines = ("psy", "acid", "va")
    for role in range(7):
        for style_id, _name, _family in STYLES:
            for engine in engines:
                if engine == "psy" and role != ROLE_BASS:
                    coverage.append(f"{ROLE_NAME[role]},{style_id},psy,0,N/A,Psy Bass adapter is a bass voice only")
                    continue
                if engine == "acid" and role != ROLE_ACID:
                    coverage.append(f"{ROLE_NAME[role]},{style_id},acid,0,N/A,303 adapter is the Acid role only")
                    continue
                if engine == "va" and role == ROLE_ACID:
                    coverage.append(f"{ROLE_NAME[role]},{style_id},va,0,N/A,Acid role uses the 303 adapter; VA leads are the Lead role")
                    continue
                if engine == "psy" and role == ROLE_BASS:
                    count = sum(1 for p in bank if p["role"] == ROLE_BASS and p["style"] == style_id and int(p["values"][0]) == 0)
                    if style_id in VA_BASS:
                        coverage.append(f"{ROLE_NAME[role]},{style_id},psy,0,N/A,this bass style is written for the VA engine")
                    else:
                        coverage.append(f"{ROLE_NAME[role]},{style_id},psy,{count},candidate,psy bass")
                    continue
                if engine == "acid" and role == ROLE_ACID:
                    count = sum(1 for p in bank if p["role"] == ROLE_ACID and p["style"] == style_id)
                    coverage.append(f"{ROLE_NAME[role]},{style_id},acid,{count},candidate,303")
                    continue
                if engine == "va":
                    count = sum(1 for p in bank if p["role"] == role and p["style"] == style_id and int(p["values"][0]) == 2)
                    if role == ROLE_BASS and style_id not in VA_BASS:
                        coverage.append(f"{ROLE_NAME[role]},{style_id},va,0,N/A,this bass style uses Psy Bass")
                    else:
                        coverage.append(f"{ROLE_NAME[role]},{style_id},va,{count},candidate,VA")
    COVERAGE.write_text("\n".join(coverage) + "\n")
    print(f"wrote {len(bank)} programs, {len(phrase_bank)} phrases, nearest L1 {closest:.3f}")


def main():
    rows = catalog()
    emit(rows, programs(rows), build_phrases())


if __name__ == "__main__":
    main()
