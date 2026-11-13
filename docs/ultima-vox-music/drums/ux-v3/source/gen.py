# VOX Drums UI/UX v3 — Gate 1 visual proposal (issue 127, spec v3).
# Renders design references as HTML for screenshots. NOT product code: the
# product UI is native JUCE. Layout is computed here with explicit rectangles
# so the same dock model is drawn at every window size.
import math, os, random

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "html")
os.makedirs(OUT, exist_ok=True)

# ----------------------------------------------------------------- data (real product values)
PADS = [("Kick", "C1", 36), ("Snare", "D1", 38), ("Clap", "D#1", 39), ("Closed Hat", "F#1", 42), ("Open Hat", "A#1", 46),
        ("Tom", "A1", 45), ("Rim", "C#1", 37), ("Cymbal", "C#2", 49), ("Perc 1", "D2", 50), ("Perc 2", "D#2", 51),
        ("Perc 3", "E2", 52), ("Perc 4", "F2", 53), ("Perc 5", "F#2", 54), ("Perc 6", "G2", 55), ("Perc 7", "G#2", 56), ("Perc 8", "A2", 57)]
SHORT = ["Kick", "Snare", "Clap", "C.Hat", "O.Hat", "Tom", "Rim", "Cym", "Perc 1", "Perc 2", "Perc 3", "Perc 4", "Perc 5", "Perc 6", "Perc 7", "Perc 8"]
MODEL = ["Kick", "Snare", "Clap", "Hat", "Hat", "Tom", "Rim", "Cymbal"] + ["Perc"] * 8
# Pad identity colours (proposed semantic tokens pad.1..pad.16): eight hues, the second
# eight pads repeat them one step darker. Never the only cue: number and name always shown.
HUES = ["#F0A35E", "#9C8CF2", "#E77FB0", "#56C2A6", "#6FA6EE", "#A6CE63", "#D9C35E", "#C99A78"]
DARK = ["#C9874D", "#8072C9", "#C26A93", "#479E89", "#5B89C4", "#88A951", "#B39F4D", "#A57E63"]
PADC = HUES + DARK
# factory defaults from products/drums/parameters.csv (drm.pNN.pan / decay)
PAN = [0, 0, -.1, .2, .2, -.25, .3, -.3] + [0] * 8
DECAY = [.38, .25, .35, .07, .45, .5, .08, 1.8] + [.3] * 8
def pan_txt(p): return "C" if abs(p) < .005 else ("L" if p < 0 else "R") + f"{abs(p)*100:.0f}"
def ms_txt(d): return f"{d*1000:.0f} ms" if d < 1 else f"{d:.2f} s"
LEVEL = [85, 60, 55, 50, 45, 60, 50, 40, 60, 60, 60, 60, 60, 60, 60, 60]
OUTS = [0, 0, 0, 0, 2, 2, 0, 5, 0, 0, 0, 0, 7, 0, 0, 0]          # 0 = Main, n = Out n+1
HOST_ON = {0, 2, 5}                                                # host enabled: Main, Out 3, Out 6
STYLES = ["Full-on / Psy", "Goa", "House", "Techno", "Trap", "Breaks", "Ambient"]
PATTERNS = ["Full-on Basic", "Full-on Driving", "Rolling Offbeat", "Breakdown", "Kick & Offbeat"]

def pattern_cells(steps=16, seed=3):
    random.seed(seed)
    g = {}
    q = steps // 4
    for l in range(16):
        for s in range(steps):
            on = False
            if l == 0: on = s % q == 0
            elif l == 2: on = s in (q, 3 * q)
            elif l == 3: on = s % 2 == 1 or (s % 4 == 2 and random.random() < .5)
            elif l == 4: on = s % q == q // 2
            elif l == 6: on = s in (7 * steps // 16, 15 * steps // 16)
            elif l == 8: on = s in (3 * steps // 16, 11 * steps // 16)
            elif l == 9: on = s % 4 == 1 and s < 12 * steps // 16
            elif l == 7: on = s == 0
            if on:
                g[(l, s)] = dict(vel=int(70 + 57 * random.random()) if l not in (0, 2) else 118, acc=l in (0, 2) and s % (2 * q) == q,
                                 prob=100 if random.random() > .18 or l in (0, 2) else 60, rat=2 if (l == 3 and s == 13) else 1,
                                 micro=int((random.random() - .5) * 60) if l in (3, 9) else 0, lock=(l, s) in ((0, 0), (0, q * 2)))
    return g

# ----------------------------------------------------------------- styles
CSS = """
*{box-sizing:border-box;margin:0;padding:0}
:root{
 --bg:#06121D;--gutter:#040C14;--panel:#0B1925;--raised:#102230;--control:#0A1722;--graph:#051019;
 --border:#1D3B50;--subtle:#122C3D;--t1:#E9F3FA;--t2:#9AB2C5;--t3:#6F8A9D;--dis:#3D5566;
 --cyan:#00DDF5;--cyanDim:#087B92;--copper:#EF805D;--copperDim:#7A4434;--danger:#E4425D;--ok:#32D296;--warn:#E3B341}
body{background:#030A10;color:var(--t1);font:12.5px/1.3 "Inter","Segoe UI",sans-serif;-webkit-font-smoothing:antialiased;font-feature-settings:"tnum" 1}
.app{position:relative;overflow:hidden;background:var(--gutter)}
.abs{position:absolute}
/* chrome */
.chrome{position:absolute;left:0;top:0;right:0;height:44px;background:var(--bg);border-bottom:1px solid var(--border);display:flex;align-items:center;gap:10px;padding:0 12px;white-space:nowrap}
.logo{display:flex;align-items:center;gap:8px}.logo i{width:22px;height:22px;border-radius:5px;background:linear-gradient(135deg,var(--copper),#B65A3E);display:block;position:relative}
.logo i:after{content:"";position:absolute;inset:6px 5px;border-left:2px solid #06121D;border-right:2px solid #06121D}
.logo b{font-size:15px;font-weight:600;letter-spacing:.01em}.logo small{font-size:10px;color:var(--t3);letter-spacing:.12em;display:block;line-height:1}
.vsep{width:1px;height:22px;background:var(--border)}
.btn,.seg,.sel,.tg,.chip{flex:none}
.btn{height:28px;padding:0 11px;border-radius:6px;border:1px solid #0A141C;background:linear-gradient(#22343F,#15232D);box-shadow:inset 0 1px 0 rgba(255,255,255,.08),0 1px 2px rgba(0,0,0,.55);color:var(--t1);display:inline-flex;align-items:center;gap:6px;font-size:12.5px;white-space:nowrap}
.btn.ghost{background:transparent;border-color:transparent;box-shadow:none;color:var(--t2)}
.btn.hov{background:linear-gradient(#2A3F4C,#1A2A35)}.btn.prs{background:linear-gradient(#111C24,#18262F);box-shadow:inset 0 2px 3px rgba(0,0,0,.7);color:#C9D6E0}
.btn.pri{background:linear-gradient(#F59675,#D9694A);border-color:#7A3A26;box-shadow:inset 0 1px 0 rgba(255,255,255,.25),0 1px 2px rgba(0,0,0,.55);color:#1A0C06;font-weight:600}
.btn.dis{color:var(--dis);background:#0C1720;box-shadow:none;border-color:#122C3D}
.btn.danger{color:#FF8FA0;border-color:#3A1219}
.btn.on{color:var(--cyan);background:linear-gradient(#0A3440,#072530);box-shadow:inset 0 1px 3px rgba(0,0,0,.6);border-color:#0A5566}
.btn.sm{height:26px;padding:0 9px;font-size:12px;border-radius:5px}
.btn.ic{width:28px;padding:0;justify-content:center}
.dd:after{content:"";border:4px solid transparent;border-top-color:var(--t2);margin:4px 0 0 2px}
.seg{display:inline-flex;border:1px solid #0A141C;border-radius:6px;overflow:hidden;background:#08121A;box-shadow:inset 0 1px 3px rgba(0,0,0,.6),0 1px 0 #1A2D3B}
.seg i{font-style:normal;height:26px;padding:0 10px;display:flex;align-items:center;color:var(--t2);font-size:12.5px;white-space:nowrap}
.seg i+i{border-left:1px solid #0F1E28}.seg i.on{background:linear-gradient(#2A3C48,#1A2832);color:var(--t1);box-shadow:inset 0 1px 0 rgba(255,255,255,.1),inset 0 -2px 0 var(--copper);border-radius:4px}
.seg.lg i{height:32px;padding:0 14px;font-size:13px}
.host{display:flex;align-items:center;gap:8px;color:var(--t2)}.host .d{width:8px;height:8px;border-radius:50%;background:var(--cyan)}
.host b{color:var(--t1);font-weight:500}.host .ro{font-size:10px;color:var(--t3);border:1px solid var(--subtle);border-radius:3px;padding:1px 4px}
.chip{display:inline-flex;align-items:center;gap:4px;height:20px;padding:0 7px;border-radius:4px;font-size:11px;background:var(--raised);color:var(--t2);white-space:nowrap}
.chip.c{background:#06303A;color:var(--cyan)}.chip.w{background:#3A2E10;color:var(--warn)}.chip.x{background:#3A1219;color:#FF8FA0}.chip.k{background:#3A1F15;color:var(--copper)}
/* panels */
.pnl{position:absolute;background:var(--panel);border:1px solid var(--subtle);border-radius:6px;overflow:hidden}
.pnl.focus{border-color:var(--cyanDim)}
.ph{height:30px;display:flex;align-items:center;gap:6px;padding:0 6px 0 4px;border-bottom:1px solid var(--subtle);background:#0D1D2A;white-space:nowrap}
.grip{width:14px;height:16px;background:radial-gradient(circle,#4A6B80 1.2px,transparent 1.4px) 0 0/5px 5px;opacity:.9;flex:none}
.ttl{font-size:12px;font-weight:600;color:var(--t1)}
.tabs{display:flex;height:30px;align-items:stretch}.tabs i{font-style:normal;display:flex;align-items:center;padding:0 10px;color:var(--t2);font-size:12px;border-right:1px solid var(--subtle)}
.tabs i.on{color:var(--t1);background:var(--panel);box-shadow:inset 0 2px 0 var(--copper)}
.hb{width:22px;height:22px;border-radius:4px;display:flex;align-items:center;justify-content:center;color:var(--t3);font-size:12px;flex:none}
.hb.on{color:var(--copper);background:#2A1912}
.pb{position:absolute;left:0;right:0;bottom:0;top:30px;overflow:hidden}
.lbl{font-size:12px;color:var(--t2);white-space:nowrap}.cap{font-size:11px;letter-spacing:.09em;text-transform:uppercase;color:var(--t3);white-space:nowrap}
.val{font-size:12px;color:var(--t1);white-space:nowrap}
.sel{height:28px;padding:0 22px 0 9px;border:1px solid #0A141C;border-radius:5px;background:#08121A;box-shadow:inset 0 1px 3px rgba(0,0,0,.6),0 1px 0 #1A2D3B;display:inline-flex;align-items:center;font-size:12px;position:relative;white-space:nowrap;color:var(--t1)}
.sel:after{content:"";position:absolute;right:8px;top:11px;border:4px solid transparent;border-top-color:var(--t2)}
/* knob */
.kn{display:flex;flex-direction:column;align-items:center;gap:2px;width:var(--kw,56px)}
.kn .l{font-size:12px;color:var(--t2);white-space:nowrap}.kn .v{font-size:12px;color:var(--t1);white-space:nowrap;font-weight:500}
.kn svg{display:block}
/* toggles */
.tg{height:28px;min-width:28px;padding:0 7px;border-radius:5px;border:1px solid #0A141C;display:inline-flex;align-items:center;justify-content:center;gap:5px;font-size:12px;color:var(--t2);background:linear-gradient(#22343F,#15232D);box-shadow:inset 0 1px 0 rgba(255,255,255,.08),0 1px 2px rgba(0,0,0,.55);font-weight:600}
.tg.m{background:linear-gradient(#C23A50,#8E2236);border-color:#4A0F1A;color:#FFFFFF;box-shadow:inset 0 1px 0 rgba(255,255,255,.2),0 0 6px rgba(228,66,93,.35)}.tg.s{background:linear-gradient(#E8C34A,#B48F24);border-color:#4A3A08;color:#1A1404;box-shadow:inset 0 1px 0 rgba(255,255,255,.3),0 0 6px rgba(227,179,65,.35)}
.tg.on{background:linear-gradient(#0A3440,#072530);border-color:#0A5566;color:var(--cyan);box-shadow:inset 0 1px 3px rgba(0,0,0,.6)}
.tg.on:before{content:"";width:6px;height:6px;border-radius:50%;background:var(--cyan);box-shadow:0 0 5px var(--cyan)}
.tg.sm{height:26px;min-width:26px;font-size:11.5px;padding:0 5px}
.focusring{outline:2px solid var(--cyan);outline-offset:2px}
/* annotations */
.mk{position:absolute;z-index:50;width:20px;height:20px;border-radius:50%;background:#F3E6C8;color:#2B1D08;font:700 11px/20px Inter,sans-serif;text-align:center;box-shadow:0 0 0 2px #030A10}
.legend{display:grid;gap:8px 22px;padding:12px 16px;background:#030A10;color:#C9D6E0;font-size:12.5px;line-height:1.4}
.legend b.n{display:inline-block;width:18px;height:18px;border-radius:50%;background:#F3E6C8;color:#2B1D08;font-size:11px;text-align:center;line-height:18px;margin-right:6px}
.title{padding:10px 16px 0;background:#030A10;color:#E9F3FA;font-size:15px;font-weight:600}.title span{color:#6F8A9D;font-weight:400;font-size:12.5px;margin-left:10px}
"""

# ----------------------------------------------------------------- tactile control system (v3.1)
# Scales: "amount" = intensity gradient green→yellow→orange→red along the arc (level, velocity,
# drive, sends, feedback, amounts); "value" = neutral steel arc + unit readout (frequency, time,
# size, tone, split); "bipolar" = steel arc from the centre detent (pan, tune, fine, transient,
# microtiming). The red end of the amount scale never means clipping: CLIP is a separate lamp.
GRAD = [(0, (50, 210, 150)), (.5, (227, 209, 75)), (.75, (240, 138, 60)), (1, (228, 66, 93))]
STEEL = "#9CC3DA"
def grad_col(t):
    t = max(0.0, min(1.0, t))
    for (t0, c0), (t1, c1) in zip(GRAD, GRAD[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return "#%02X%02X%02X" % tuple(round(c0[i] + (c1[i] - c0[i]) * k) for i in range(3))
    return "#E4425D"
BIPOLAR_L = {"Pan", "Tune", "Fine", "Attack", "Sustain", "Micro", "Microtiming"}
VALUE_L = {"Cutoff", "Decay", "Tail", "Pitch dec", "Pitch decay", "Size", "Damp", "Tone", "Split", "Character", "Time", "Start", "End"}
_KID = [0]

def knob(label, value, frac, size=36, color=None, bipolar=False, kw=None, dim=False, scale=None, state=""):
    """Dimensional rotary: recessed bezel track, value arc on the track, matte body with a
    light pointer. The arc (value) and the body (control) are separate layers."""
    _KID[0] += 1
    kid = _KID[0]
    if scale is None:
        scale = "bipolar" if (bipolar or label in BIPOLAR_L) else ("value" if label in VALUE_L else "amount")
    R = size / 2 - 2.5
    cx = cy = size / 2
    a0, a1 = math.radians(135), math.radians(405)
    def pt(a, r=R): return cx + r * math.cos(a), cy + r * math.sin(a)
    def arc(fa, fb, r=R):
        x0, y0 = pt(fa, r); x1, y1 = pt(fb, r)
        large = 1 if fb - fa > math.pi else 0
        return f"M{x0:.1f},{y0:.1f} A{r:.1f},{r:.1f} 0 {large} 1 {x1:.1f},{y1:.1f}"
    av = a0 + (a1 - a0) * frac
    sw = 3.2 if size >= 34 else 2.6
    parts = [f'<path d="{arc(a0, a1)}" stroke="#040B11" stroke-width="{sw+1.6}" fill="none" stroke-linecap="round"/>',
             f'<path d="{arc(a0, a1)}" stroke="#13283A" stroke-width="{sw}" fill="none" stroke-linecap="round"/>']
    if dim:
        pass
    elif scale == "amount":
        n = 24
        for i in range(n):
            fa = a0 + (a1 - a0) * i / n
            fb = a0 + (a1 - a0) * min((i + 1) / n, frac)
            if fa >= av - 1e-4: break
            parts.append(f'<path d="{arc(fa, fb)}" stroke="{grad_col((i + .5) / n)}" stroke-width="{sw}" fill="none" stroke-linecap="butt"/>')
    elif scale == "bipolar":
        mid = (a0 + a1) / 2
        if abs(av - mid) > 1e-3:
            parts.append(f'<path d="{arc(min(mid, av), max(mid, av))}" stroke="{STEEL}" stroke-width="{sw}" fill="none" stroke-linecap="butt"/>')
    else:
        if frac > 0.003:
            parts.append(f'<path d="{arc(a0, av)}" stroke="{STEEL}" stroke-width="{sw}" fill="none" stroke-linecap="butt"/>')
    if scale == "bipolar":
        tx0, ty0 = pt(math.radians(270), R - sw); tx1, ty1 = pt(math.radians(270), R + 1.5)
        parts.append(f'<line x1="{tx0:.1f}" y1="{ty0:.1f}" x2="{tx1:.1f}" y2="{ty1:.1f}" stroke="#587487" stroke-width="1.2"/>')
    rb = R - sw - 2.2
    edge = "#6A90AA" if state == "hover" else "#2C4456"
    body_top, body_mid, body_bot = ("#22313C", "#141F28", "#0D161D") if state == "pressed" else (("#42586B", "#22323E", "#121E27") if state == "hover" else ("#33475A", "#1A2833", "#0E1820"))
    parts.append(f'<defs><radialGradient id="kb{kid}" cx="38%" cy="30%" r="75%"><stop offset="0" stop-color="{body_top}"/><stop offset=".6" stop-color="{body_mid}"/><stop offset="1" stop-color="{body_bot}"/></radialGradient></defs>')
    parts.append(f'<circle cx="{cx}" cy="{cy+1.6:.1f}" r="{rb:.1f}" fill="#000" opacity=".55"/>')
    parts.append(f'<circle cx="{cx}" cy="{cy}" r="{rb:.1f}" fill="url(#kb{kid})" stroke="{edge}" stroke-width="1"/>')
    parts.append(f'<circle cx="{cx}" cy="{cy}" r="{rb*0.62:.1f}" fill="none" stroke="#000" stroke-opacity=".35" stroke-width="1"/>')
    parts.append(f'<path d="M{cx-rb*.7:.1f},{cy-rb*.45:.1f} A{rb:.1f},{rb:.1f} 0 0 1 {cx+rb*.55:.1f},{cy-rb*.65:.1f}" stroke="#FFFFFF" stroke-opacity=".10" stroke-width="1.2" fill="none"/>')
    p0x, p0y = pt(av, rb * 0.22); p1x, p1y = pt(av, rb * 0.9)
    parts.append(f'<line x1="{p0x:.1f}" y1="{p0y:.1f}" x2="{p1x:.1f}" y2="{p1y:.1f}" stroke="{"#3D5566" if dim else "#E9F3FA"}" stroke-width="2" stroke-linecap="round"/>')
    if state == "focus":
        parts.append(f'<circle cx="{cx}" cy="{cy}" r="{R+2.2:.1f}" fill="none" stroke="#00DDF5" stroke-width="1.6"/>')
    svg = f'<svg width="{size}" height="{size}" style="overflow:visible{";opacity:.55" if dim else ""}">' + "".join(parts) + '</svg>'
    w = kw or max(size + 8, 52)
    return f'<div class="kn" style="--kw:{w}px"><span class="l">{label}</span>{svg}<span class="v">{value}</span></div>'

def mini(label, value, frac, color="var(--copper)"):
    """Inline rotary for toolbars: 20 px arc, label left, value right (drag = edit, double-click = default)."""
    r, c = 8, 10
    a0, a1 = math.radians(135), math.radians(135 + 270 * frac)
    def pt(a): return c + r * math.cos(a), c + r * math.sin(a)
    x0, y0 = pt(math.radians(135)); xe, ye = pt(math.radians(405)); x1, y1 = pt(a1)
    big = 1 if 270 * frac > 180 else 0
    svg = (f'<svg width="20" height="20"><path d="M{x0:.1f},{y0:.1f} A{r},{r} 0 1 1 {xe:.1f},{ye:.1f}" fill="none" stroke="#1D3B50" stroke-width="3" stroke-linecap="round"/>'
           + (f'<path d="M{x0:.1f},{y0:.1f} A{r},{r} 0 {big} 1 {x1:.1f},{y1:.1f}" fill="none" stroke="{grad_col(frac)}" stroke-width="3" stroke-linecap="round"/>' if frac > 0 else "")
           + f'<circle cx="10" cy="10.8" r="5" fill="#000" opacity=".5"/><circle cx="10" cy="10" r="5" fill="#22323E" stroke="#2C4456"/>'
           + f'<line x1="10" y1="10" x2="{10+4.2*math.cos(a1):.1f}" y2="{10+4.2*math.sin(a1):.1f}" stroke="#E9F3FA" stroke-width="1.5" stroke-linecap="round"/></svg>')
    return f'<span style="display:inline-flex;align-items:center;gap:4px"><span class="lbl">{label}</span>{svg}<span class="val" style="font-size:12px">{value}</span></span>'

def mk(n, x, y):
    return f'<span class="mk" style="left:{x}px;top:{y}px">{n}</span>'

def panel(x, y, w, h, header, body, cls=""):
    return f'<div class="pnl {cls}" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"><div class="ph">{header}</div><div class="pb">{body}</div></div>'

def head(title=None, tabs=None, active=0, pin=None, maxi=False, close=True, extra=""):
    t = f'<span class="ttl">{title}</span>' if title else '<div class="tabs">' + "".join(f'<i class="{"on" if i == active else ""}">{n}</i>' for i, n in enumerate(tabs)) + "</div>"
    p = "" if pin is None else f'<span class="hb {"on" if pin else ""}" title="Pin">{"📌" if False else "⊙" if not pin else "●"}</span>'
    return (f'<span class="grip"></span>{t}<span style="flex:1"></span>{extra}'
            + (f'<span class="hb {"on" if pin else ""}" style="font-size:10px;width:auto;padding:0 6px;border:1px solid {"#7A4434" if pin else "#1D3B50"}">{"Pinned" if pin else "Pin"}</span>' if pin is not None else "")
            + ('<span class="hb">⤢</span>' if maxi else "") + ('<span class="hb">✕</span>' if close else ""))

# ----------------------------------------------------------------- chrome
def chrome(workspace="Beatmaking", mode="Pattern", status="Playing", pos="3 . 2 . 1", compact=False):
    pat = '<span class="sel" style="min-width:150px">P2 · Full-on Driving · A</span>' if not compact else '<span class="sel">P2 · A</span>'
    outs = '<span class="chip c">Main + 2 outs</span>'
    ws = f'<span class="btn dd">{"Workspace: " if not compact else ""}{workspace}</span><span class="btn dd">Panels</span>'
    return f"""<div class="chrome"><div class="logo"><i></i><div><small>ULTIMA VOX</small><b>VOX Drums</b></div></div><span class="vsep"></span>
{ws}<span class="vsep"></span><div class="seg"><i class="{'on' if mode=='Pattern' else ''}">Pattern</i><i class="{'on' if mode=='Song' else ''}">Song</i></div>{pat}
<span style="flex:1"></span><div class="host"><span class="d"></span><span>Host {status}</span><b>145.0 BPM</b>{'' if compact else '<span>4/4</span>'}<b>{pos}</b><span class="ro">from host</span></div>
{outs if not compact else ''}<span class="vsep"></span><span class="btn ic ghost" title="Undo">↶</span><span class="btn ic ghost dis" title="Redo">↷</span><span class="btn danger sm">Panic</span></div>"""

# ----------------------------------------------------------------- pad rack
HITV = {0: 118, 3: 72}   # illustrative velocities of the last hits (needs velocity telemetry, see README)
def _hits(hit):
    if isinstance(hit, dict): return hit
    return {p: HITV.get(p, 100) for p in hit}

def pad_tile(i, cs, sel=False, hitv=None, mut=False, solo=False, miss=False, peak=0.0, over=False, state=""):
    """One rubber pad. Label bar (number, note) = select zone; strike surface = audition;
    identity LED on top; M/S bottom-left; measured post-fader peak bottom-right."""
    col = PADC[i]
    name = PADS[i][0] if cs >= 100 else SHORT[i]
    v = (hitv or 0) / 127
    glow = (f"box-shadow:inset 0 0 0 1px {col},inset 0 0 {10+18*v:.0f}px {col}{int(40+150*v):02X},0 0 {6+10*v:.0f}px {col}{int(30+90*v):02X},0 2px 3px rgba(0,0,0,.6);" if hitv else
            "box-shadow:inset 0 1px 0 rgba(255,255,255,.07),inset 0 -3px 5px rgba(0,0,0,.45),0 2px 3px rgba(0,0,0,.6);")
    if state == "pressed":
        glow = "box-shadow:inset 0 3px 6px rgba(0,0,0,.7),0 1px 1px rgba(0,0,0,.6);"
    bg = "radial-gradient(120% 90% at 50% 25%,#26343F,#141F27 70%)" if not mut else "radial-gradient(120% 90% at 50% 25%,#18222A,#0E161C 70%)"
    if state == "hover": bg = "radial-gradient(120% 90% at 50% 25%,#2D3D49,#17232C 70%)"
    ring = "outline:2px solid #E9F3FA;outline-offset:1px;" if sel else ""
    led_op = 1 if hitv else (.35 if mut else .75)
    ph = cs - 40
    pk = '' if peak <= 0 else f'<i style="position:absolute;left:0;right:0;bottom:0;height:{min(peak,1)*100:.0f}%;background:{"#F08A3C" if peak > .9 else ("#E3D14B" if peak > .7 else "#2FC58A")}"></i>'
    sub = ('<span style="color:#FF8FA0">● missing</span>' if miss else '') + (lamp("over", True, True, small=True) if over else '')
    body = f'''<div style="position:absolute;left:9px;right:9px;top:4px;height:3px;border-radius:2px;background:{col};opacity:{led_op};{f"box-shadow:0 0 6px {col};" if hitv else ""}"></div>
<div style="position:absolute;left:7px;right:9px;top:10px;display:flex;justify-content:space-between;font-size:11px;color:var(--t3)" title="click here to select without sound"><span>{i+1}</span><span>{PADS[i][1]} {PADS[i][2]}</span></div>
<div style="position:absolute;left:6px;right:10px;top:{cs*0.31:.0f}px;text-align:center;font-size:{14 if cs >= 80 else 13}px;font-weight:600;{"color:var(--t3);text-decoration:line-through;" if mut else ""}white-space:nowrap;overflow:hidden">{name}</div>
{f'<div style="position:absolute;left:6px;right:10px;top:{cs*0.31+18:.0f}px;text-align:center;font-size:11px;font-weight:600">{sub}</div>' if sub else ''}
<div style="position:absolute;left:6px;bottom:6px;display:flex;gap:3px"><span class="tg sm {"m" if mut else ""}" style="height:20px;min-width:24px">M</span><span class="tg sm {"s" if solo else ""}" style="height:20px;min-width:24px">S</span></div>
<div style="position:absolute;right:4px;top:26px;width:4px;height:{ph:.0f}px;border-radius:2px;background:#03090E;box-shadow:inset 0 1px 1px #000;overflow:hidden" title="measured post-fader peak">{pk}</div>'''
    return f'<div style="position:relative;width:{cs:.0f}px;height:{cs:.0f}px;border-radius:9px;background:{bg};border:1px solid #070E14;{glow}{ring}overflow:hidden;flex:none">{body}</div>'

def pad_grid(w, h, sel=0, hit=(3,), pinned=None):
    gap = 8
    cs = min((w - 16 - 3 * gap) / 4, (h - 16 - 3 * gap) / 4)
    x0 = (w - (4 * cs + 3 * gap)) / 2
    hv = _hits(hit)
    out = []
    for i in range(16):
        x = x0 + (i % 4) * (cs + gap); y = 8 + (i // 4) * (cs + gap)
        out.append(f'<div style="position:absolute;left:{x:.0f}px;top:{y:.0f}px">{pad_tile(i, cs, sel=i == sel, hitv=hv.get(i), mut=i == 11, miss=i == 9, peak=MET[i])}</div>')
    return "".join(out)

def pad_grid_height(w):
    gap = 8
    cs = (w - 16 - 3 * gap) / 4
    return 16 + 4 * cs + 3 * gap

def pad_list(w, h, sel=0, hit=(3,), rows=16):
    narrow = w < 220
    short = w < 260
    rh = max(26, min(34, (h - 8) / rows))
    out = []
    for i in range(rows):
        col = PADC[i]
        bg = "#13283A" if i == sel else "transparent"
        bd = "box-shadow:inset 2px 0 0 #E9F3FA;" if i == sel else ""
        act = f'<span title="hit" style="width:7px;height:7px;border-radius:50%;background:{col if i in _hits(hit) else "#1D3B50"};{("box-shadow:0 0 6px " + col) if i in _hits(hit) else ""}"></span>'
        mut = i == 11
        out.append(f'''<div style="height:{rh:.0f}px;display:flex;align-items:center;gap:{4 if short else 7}px;padding:0 8px;background:{bg};{bd}border-bottom:1px solid #0E2231">
<span style="width:3px;height:{rh-10:.0f}px;background:{col};border-radius:2px"></span><span style="width:16px;font-size:11px;color:var(--t3);text-align:right">{i+1}</span>
<span style="flex:1;font-size:12px;{'color:var(--t3);text-decoration:line-through' if mut else ''};white-space:nowrap;overflow:hidden">{SHORT[i] if short else PADS[i][0]}</span>{act}
{'' if not OUTS[i] else f'<span style="font-size:10px;color:{"var(--cyan)" if OUTS[i] in HOST_ON else "var(--warn)"};white-space:nowrap" title="output">{("O%d" % (OUTS[i]+1)) if short else ("Out %d" % (OUTS[i]+1))}{"" if OUTS[i] in HOST_ON else "→M"}</span>'}{'' if narrow else f'<span style="font-size:10px;color:var(--t2);width:{38 if short else 46}px;text-align:right;white-space:nowrap">{PADS[i][1]} {PADS[i][2]}</span>'}
<span class="tg sm {'m' if mut else ''}" style="height:24px;min-width:24px">M</span><span class="tg sm" style="height:24px;min-width:24px">S</span></div>''')
    return "".join(out)

def pad_rack(x, y, w, h, view="grid", sel=0, tabs=None, active=0, hit=(3,)):
    gl, ll = ("▦", "≡") if w < 220 else ("Grid", "List")
    seg = f'<div class="seg" style="transform:scale(.88);transform-origin:right"><i class="{"on" if view=="grid" else ""}" title="Grid">{gl}</i><i class="{"on" if view=="list" else ""}" title="List">{ll}</i></div>'
    hd = head("Pad Rack" if not tabs else None, tabs=tabs, active=active, extra=seg)
    body = pad_grid(w - 2, h - 32, sel, hit) if view == "grid" else pad_list(w - 2, h - 32, sel, hit)
    return panel(x, y, w, h, hd, f'<div style="position:relative;width:100%;height:100%">{body}</div>')

# ----------------------------------------------------------------- inspector
def insp_pad(w, pad=0, pinned=False, other=None):
    n, note, num = PADS[pad]
    banner = ""
    if pinned:
        banner = f'<div style="margin:8px;padding:6px 8px;border:1px solid #7A4434;background:#1E130E;border-radius:5px;font-size:11px;color:#F3C2AE">Pinned to <b>Pad {pad+1} · {n}</b>' + (f'. Selected elsewhere: Pad {other+1} {PADS[other][0]}.' if other is not None else '') + '</div>'
    kw = max(54, (w - 24) // 4)
    return f'''{banner}<div style="padding:10px 10px 6px;display:flex;align-items:center;gap:8px"><span style="width:4px;height:30px;border-radius:2px;background:{PADC[pad]}"></span>
<div style="flex:1"><div style="font-size:14px;font-weight:600">Pad {pad+1} · {n}</div><div class="lbl">{MODEL[pad]} model · {note} ({num})</div></div><span class="tg">M</span><span class="tg">S</span></div>
<div style="padding:2px 8px 4px" class="cap">Quick controls</div>
<div style="display:flex;justify-content:space-between;padding:0 6px 8px">{knob("Level", f"{LEVEL[pad]} %", LEVEL[pad]/100, 44, kw=kw)}{knob("Pan",pan_txt(PAN[pad]),.5 + PAN[pad] / 2,44,bipolar=True,kw=kw)}{knob("Tune","0.0 st",.5,44,bipolar=True,kw=kw)}{knob("Decay",ms_txt(DECAY[pad]),min(1, DECAY[pad] / 2),44,kw=kw)}</div>
<div style="padding:0 10px 10px;display:flex;gap:6px;flex-wrap:wrap"><span class="sel">Output: {'Main' if not OUTS[pad] else 'Out %d'%(OUTS[pad]+1)}</span><span class="btn sm" style="margin-left:auto">Open Sound Editor</span></div>'''

def insp_steps(w):
    rowv = lambda l, v, note="": f'<div style="display:flex;align-items:center;gap:8px;height:30px;padding:0 10px;border-top:1px solid #0E2231"><span class="lbl" style="width:84px">{l}</span><span class="val" style="flex:1">{v}</span><span style="font-size:10px;color:var(--t3)">{note}</span></div>'
    return f'''<div style="padding:10px"><div style="font-size:14px;font-weight:600">4 steps · Closed Hat</div><div class="lbl">Pattern 2 A · bar 1 · steps 5–8</div></div>
{rowv("Velocity","mixed  88–112","drag = relative")}{rowv("Chance","100 %")}{rowv("Ratchet","mixed  1× / 2×")}{rowv("Micro","mixed  −8 … +10 %")}
<div style="display:flex;gap:6px;padding:8px 10px;border-top:1px solid #0E2231"><span class="tg">Accent</span><span class="tg on">Lock 2/4</span></div>
<div style="padding:0 10px 10px;font-size:10.5px;color:var(--t3);line-height:1.4">Values apply to the 4 selected steps only. Unsupported mass edits are shown disabled.</div>'''

def insp_song():
    return f'''<div style="padding:10px"><div style="font-size:14px;font-weight:600">Song bar 37</div><div class="lbl">plays P5 · Kick &amp; Offbeat · B · bar 1</div></div>
<div style="padding:0 10px 8px;display:flex;gap:6px;flex-wrap:wrap"><span class="chip">linked · 4 occurrences</span><span class="chip c">loop 33–40</span></div>
<div style="padding:8px 10px;border-top:1px solid #0E2231;white-space:normal;line-height:1.4" class="lbl">Edit target is chosen in the Song Arranger.<br><span style="color:var(--t1)">Edit This Bar</span>: the first real edit makes bar 37 unique; selecting does not.</div>'''

def inspector(x, y, w, h, mode="pad", pinned=False, other=None, tabs=None, active=0, pad=0):
    hd = head("Inspector" if not tabs else None, tabs=tabs, active=active, pin=pinned)
    body = {"pad": lambda: insp_pad(w, pad, pinned, other), "steps": lambda: insp_steps(w), "song": insp_song}[mode]()
    return panel(x, y, w, h, hd, body)

# ----------------------------------------------------------------- step sequencer
def step_editor(x, y, w, h, steps=16, sel_steps=((3, 4), (3, 5), (3, 6), (3, 7)), cur_lane=3, play=9, song=False, rowmin=16, title_tabs=None, active=0, seed=3, focus=False):
    g = pattern_cells(steps, seed)
    tb_h = 40
    lh = 170 if w > 900 else 146
    gw = w - 2 - lh - 10
    cell = gw / steps
    param_h = 0 if h < 420 else (112 if h >= 560 else 70)
    avail = h - 32 - tb_h - 22 - param_h - 8
    rows = 16
    rh = max(rowmin, min(28, avail / 16))
    vis = min(16, int(avail // rh))
    scroll = vis < 16
    hd = head(None, tabs=title_tabs or ["Step Sequencer"], active=active, maxi=True,
              extra='<span class="btn sm ghost">Split ▾</span>')
    if not song:
        tb = f'''<div style="height:{tb_h}px;display:flex;align-items:center;gap:{6 if w > 900 else 4}px;padding:0 8px;border-bottom:1px solid var(--subtle);white-space:nowrap;overflow:hidden">
{'<span class="cap">Bar</span>' if w > 900 else ''}<div class="seg"><i class="on">1</i><i>2</i><i>3</i><i>4</i></div><span class="btn sm dd">Bar</span><span class="sel">{steps} steps</span><span class="sel">4 bars</span>
<span class="vsep"></span><div class="seg"><i class="on">A</i><i>B</i></div>{'<span class="btn sm">A ⇄ B</span><span class="btn sm">A → B</span>' if w > 900 else ''}<span class="vsep"></span>{mini("Swing", "0 %", 0)}{mini("Accent", "50 %", .5)}
<span style="flex:1"></span><span style="display:inline-flex"><span class="btn pri sm" style="height:26px;border-radius:5px 0 0 5px" title="Drag onto a DAW track · scope: this pattern">⠿ Drag MIDI</span><span class="btn pri sm" style="height:26px;border-radius:0 5px 5px 0;border-left:1px solid #B65A3E;padding:0 6px" title="Save MIDI file…">▾</span></span></div>'''
    else:
        tb = f'''<div style="height:{tb_h}px;display:flex;align-items:center;gap:8px;padding:0 8px;border-bottom:1px solid var(--subtle);white-space:nowrap;overflow:hidden">
<span class="chip k">Editing: This Bar · 37</span><span class="lbl">plays P5 B · bar 1 · becomes unique on first edit</span><span style="flex:1"></span><span class="sel">{steps} steps</span></div>'''
    ruler = "".join(f'<div style="position:absolute;left:{lh + 6 + s*cell:.1f}px;width:{cell:.1f}px;top:0;height:22px;display:flex;align-items:center;justify-content:center;font-size:10px;'
                    f'color:{"#06121D" if s==play else ("var(--t1)" if s % (steps//4)==0 else "var(--t3)")};{"background:var(--cyan);border-radius:3px;font-weight:700;" if s==play else ""}">'
                    f'{s+1 if (steps <= 16 or s % 2 == 0) else ""}</div>' for s in range(steps))
    lanes = []
    for l in range(vis):
        ytop = 22 + l * rh
        col = PADC[l]
        cur = l == cur_lane
        hdr = f'''<div style="position:absolute;left:4px;top:{ytop:.0f}px;width:{lh-2}px;height:{rh:.0f}px;display:flex;align-items:center;gap:4px;padding:0 5px;{'background:#13283A;border-radius:4px 0 0 4px;' if cur else ''}">
<span style="width:3px;height:{rh-8:.0f}px;background:{col};border-radius:2px"></span><span title="Lane on (internal sequencer)" style="width:12px;height:10px;border-radius:3px;flex:none;border:1px solid #0A141C;{'background:#0C1820;box-shadow:inset 0 1px 2px #000' if l==15 else 'background:radial-gradient(circle,#9FF6FF 0,#00C8DE 55%,#077083 100%);box-shadow:0 0 3px rgba(0,221,245,.35)'}"></span><span style="width:14px;font-size:10px;color:var(--t3);text-align:right">{l+1}</span>
<span style="flex:1;font-size:{12 if rh>=20 else 11}px;{'font-weight:600;' if cur else ''}{'color:var(--t3);text-decoration:line-through;' if l==11 else ''}white-space:nowrap;overflow:hidden">{PADS[l][0] if lh>=140 else SHORT[l]}</span>
{'<span class="chip x" style="height:18px;font-size:10.5px">muted</span>' if l==11 else ''}</div>'''
        cells = []
        for s in range(steps):
            cx = lh + 6 + s * cell
            on = g.get((l, s))
            alt = (s // (steps // 4)) % 2 == 1
            selc = (l, s) in sel_steps
            base = "#13283A" if not alt else "#0F2131"
            inner = ""
            if on:
                vh = (rh - 6) * on["vel"] / 127
                hatch = "background-image:repeating-linear-gradient(135deg,rgba(6,18,29,.55) 0 2px,transparent 2px 5px);" if on["prob"] < 100 else ""
                inner = f'<div style="position:absolute;left:0;right:0;bottom:0;height:{vh:.0f}px;background:{col};{hatch}border-radius:2px"></div>'
                if on["acc"]: inner += '<div style="position:absolute;left:3px;right:3px;top:1px;height:2px;background:#E9F3FA;border-radius:1px"></div>'
                if on["rat"] > 1: inner += '<div style="position:absolute;left:3px;bottom:2px;display:flex;gap:2px"><i style="width:3px;height:3px;background:#06121D;border-radius:50%"></i><i style="width:3px;height:3px;background:#06121D;border-radius:50%"></i></div>'
                if on["lock"]: inner += '<div style="position:absolute;right:2px;top:3px;width:6px;height:5px;border:1.5px solid #06121D;border-radius:1px"></div>'
            out = "box-shadow:0 0 0 2px #E9F3FA;" if selc else ""
            cells.append(f'<div style="position:absolute;left:{cx+1:.1f}px;top:{ytop+2:.0f}px;width:{cell-2:.1f}px;height:{rh-4:.0f}px;background:{base};border-radius:3px;{out}{"opacity:.4;" if l in (11, 15) else ""}overflow:hidden">{inner}</div>')
        lanes.append(hdr + "".join(cells))
    playx = lh + 6 + play * cell + cell / 2
    playline = f'<div style="position:absolute;left:{playx:.0f}px;top:22px;width:1px;height:{vis*rh:.0f}px;background:rgba(0,221,245,.55)"></div>'
    grid_h = 22 + vis * rh
    sb = f'<div style="position:absolute;right:2px;top:24px;width:4px;height:{vis*rh-4:.0f}px;background:#0E2231;border-radius:2px"><div style="height:{(vis/16)*100:.0f}%;background:#2B4A60;border-radius:2px"></div></div>' if scroll else ""
    pl = ""
    if param_h:
        two = param_h >= 100
        lane_h = (param_h - 26) / (2 if two else 1)
        tabs_html = '<span class="cap">Parameter lanes</span> <span class="btn sm ghost" style="height:18px">+ lane</span>'
        bars = []
        for k, kind in enumerate(["vel", "micro"][: 2 if two else 1]):
            top = 26 + k * lane_h
            bars.append(f'<div style="position:absolute;left:8px;top:{top+2:.0f}px;width:{lh-12}px"><span class="sel" style="height:20px;font-size:11px">{"Velocity" if kind=="vel" else "Microtiming"}</span><div style="font-size:10px;color:var(--t3);margin-top:2px">{PADS[cur_lane][0]}</div></div>')
            if kind == "micro":
                bars.append(f'<div style="position:absolute;left:{lh+6}px;right:12px;top:{top+lane_h/2:.0f}px;height:1px;background:#1D3B50"></div>')
            for s in range(steps):
                on = g.get((cur_lane, s))
                cx = lh + 6 + s * cell
                selc = (cur_lane, s) in sel_steps
                if not on:
                    continue
                c = "#E9F3FA" if selc else (grad_col(on["vel"] / 127) if kind == "vel" else STEEL)
                if kind == "vel":
                    hh = (lane_h - 8) * on["vel"] / 127
                    bars.append(f'<div style="position:absolute;left:{cx+cell*0.3:.0f}px;width:{max(3,cell*0.4):.0f}px;top:{top+lane_h-4-hh:.0f}px;height:{hh:.0f}px;background:{c};border-radius:2px 2px 0 0;opacity:.9"></div>'
                                f'<div style="position:absolute;left:{cx+cell*0.3-1:.0f}px;width:{max(5,cell*0.4)+2:.0f}px;top:{top+lane_h-6-hh:.0f}px;height:3px;background:#E9F3FA;border-radius:2px"></div>')
                else:
                    mid = top + lane_h / 2
                    hh = (lane_h / 2 - 4) * on["micro"] / 50
                    t0 = mid - max(hh, 0); hgt = abs(hh) or 1
                    bars.append(f'<div style="position:absolute;left:{cx+cell*0.38:.0f}px;width:{max(3,cell*0.24):.0f}px;top:{t0:.0f}px;height:{hgt:.0f}px;background:{c};opacity:.85"></div>')
        pl = f'''<div style="position:absolute;left:0;right:0;top:{grid_h+6:.0f}px;height:{param_h}px;border-top:1px solid var(--subtle);background:#09171F">
<div style="position:absolute;left:8px;top:3px">{tabs_html}</div><div style="position:absolute;right:12px;top:5px;font-size:10px;color:var(--t3)">aligned to the step ruler</div>{''.join(bars)}</div>'''
    body = f'{tb}<div style="position:absolute;left:0;right:0;top:{tb_h}px;bottom:0">{ruler}{"".join(lanes)}{playline}{sb}{pl}</div>'
    return panel(x, y, w, h, hd, body, "focus" if focus else "")

# ----------------------------------------------------------------- song arranger
SONG_COL = ["#3F6FAE", "#B4654A", "#4F8F5F", "#7A5FB0", "#A88A3E"]
def song_tile_data(b):
    slot = (b // 8) % 5; var = (b // 4) % 2
    if b in (20, 39, 63): return ("u", slot, var)
    if b in (30, 31): return ("e", slot, var)
    return ("p", slot, var)

def song_editor(x, y, w, h, bars=64, sel=36, target="bar", title_tabs=None, active=0, compact_rows=False):
    hd = head(None, tabs=title_tabs or ["Song Arranger"], active=active, maxi=True, extra='<span class="btn sm ghost">Split ▾</span>')
    tb = f'''<div style="height:72px;display:flex;flex-wrap:wrap;align-content:center;align-items:center;gap:5px;padding:0 8px;border-bottom:1px solid var(--subtle);white-space:nowrap;overflow:hidden">
<span class="sel">64 bars</span><span class="vsep"></span><span class="sel">P2</span><div class="seg"><i class="on">A</i><i>B</i></div><span class="btn sm" title="Place P2 A at bar 37">Place</span><span class="vsep"></span><span class="tg on">Loop</span><span class="btn sm" title="Set the loop to the selected bars">Loop sel</span><span style="flex:1"></span><span style="display:inline-flex;flex:none"><span class="btn pri sm" style="height:26px;border-radius:5px 0 0 5px" title="Drag onto a DAW track · scope: entire song">⠿ Drag Song MIDI</span><span class="btn pri sm" style="height:26px;border-radius:0 5px 5px 0;border-left:1px solid #B65A3E;padding:0 6px" title="Save MIDI file…">▾</span></span>
<div style="flex-basis:100%;height:0"></div><span class="btn sm">Insert</span><span class="btn sm">Delete</span><span class="btn sm">Dup</span><span class="btn sm">Clear</span><span class="btn sm">Copy</span><span class="btn sm dis">Paste</span><span class="btn sm">Make Unique</span></div>'''
    tgt = f'''<div style="height:48px;display:flex;align-items:center;gap:10px;padding:0 8px;border-bottom:1px solid var(--subtle);white-space:nowrap">
<span class="cap">Grid edits</span><div class="seg lg"><i class="{'on' if target=='src' else ''}">Edit Source Pattern</i><i class="{'on' if target=='bar' else ''}">Edit This Bar</i></div>
<span class="lbl" style="white-space:normal;line-height:1.35">{'<b style="color:var(--t1)">P5 B bar 1</b> · changes reach all 4 linked bars: 33, 37, 45, 53' if target=='src' else '<b style="color:var(--t1)">Bar 37 only</b> · linked to P5 B bar 1 until the first real edit; other 3 occurrences stay linked'}</span></div>'''
    per_row = 16
    rows = (bars + per_row - 1) // per_row
    top0 = 72 + 48 + 6
    th = min(34, (h - 32 - top0 - 8) / rows - 4)
    tw = (w - 2 - 16 - (per_row - 1) * 3) / per_row
    tiles = []
    for b in range(bars):
        k, slot, var = song_tile_data(b)
        tx = 8 + (b % per_row) * (tw + 3); ty = top0 + (b // per_row) * (th + 4)
        if k == "p":
            bg = SONG_COL[slot] if var else SONG_COL[slot] + "99"
            txt = f'P{slot+1}{"B" if var else "A"} · {b%4+1}'
            st = f"background:{bg};"
        elif k == "u":
            st = "background:#1B3346;background-image:repeating-linear-gradient(135deg,rgba(233,243,250,.10) 0 3px,transparent 3px 7px);border:1px solid #9AB2C5;"
            txt = "Unique"
        else:
            st = "background:transparent;border:1px dashed #23445A;"; txt = "empty"
        if b == sel: st += "box-shadow:0 0 0 2px #E9F3FA;"
        linked = target in ("src", "bar") and k == "p" and slot == 4 and var == 1 and b % 4 == 0 and b != sel
        if linked: st += "box-shadow:inset 0 0 0 2px rgba(233,243,250,.55);"
        loop = 32 <= b < 40
        tiles.append(f'<div style="position:absolute;left:{tx:.0f}px;top:{ty:.0f}px;width:{tw:.0f}px;height:{th:.0f}px;border-radius:3px;{st}padding:2px 4px;overflow:hidden;font-size:10px;line-height:1.15;color:#E9F3FA">'
                     f'<b style="font-size:10px">{b+1}</b><br><span style="opacity:.9">{txt}</span>{"<i style=position:absolute;left:0;right:0;bottom:0;height:3px;background:var(--cyan)></i>" if loop else ""}</div>')
    playx = 8 + (38 % per_row) * (tw + 3) + tw * 0.4
    playy = top0 + (38 // per_row) * (th + 4)
    tiles.append(f'<div style="position:absolute;left:{playx:.0f}px;top:{playy-2:.0f}px;width:2px;height:{th+4:.0f}px;background:var(--cyan)"></div>')
    body = tb + tgt + "".join(tiles)
    return panel(x, y, w, h, hd, body)

# ----------------------------------------------------------------- sound editor (signal flow)
def sec(title, inner, w, note="", collapsed=False, adv=False):
    chev = "▸" if collapsed else "▾"
    return f'''<div style="width:{w}px;flex:none;background:#0D1E2C;border:1px solid var(--subtle);border-radius:6px;overflow:hidden">
<div style="height:28px;display:flex;align-items:center;gap:6px;padding:0 8px;border-bottom:1px solid var(--subtle)"><span style="color:var(--t3)">{chev}</span><span class="cap" style="color:var(--t2);letter-spacing:.05em">{title}</span>
{'<span class="chip" style="height:16px;font-size:10px;padding:0 4px">adv.</span>' if adv else ''}<span style="flex:1"></span><span style="font-size:11px;color:var(--t3)">{note}</span></div>{'' if collapsed else inner}</div>'''

def waveform(w, h, start=.03, end=.82):
    pts = []
    for i in range(240):
        t = i / 239
        a = math.exp(-t * 4.2) * (1 if i > 3 else i / 3)
        pts.append(f"{t*w:.1f},{h/2 - a*(h/2-6)*math.sin(i*(0.62-i*0.0011)):.1f}")
    sx, ex = start * w, end * w
    return (f'<svg width="{w}" height="{h}" style="display:block;background:#051019;border-radius:4px">'
            f'<rect x="0" y="0" width="{sx:.0f}" height="{h}" fill="rgba(5,16,25,.75)"/><rect x="{ex:.0f}" y="0" width="{w-ex:.0f}" height="{h}" fill="rgba(5,16,25,.75)"/>'
            f'<polyline points="{" ".join(pts)}" fill="none" stroke="#F0A35E" stroke-width="1.3"/>'
            f'<rect x="{sx:.0f}" y="0" width="{ex-sx:.0f}" height="{h}" fill="none" stroke="#1D3B50"/>'
            f'<line x1="{sx:.0f}" y1="0" x2="{sx:.0f}" y2="{h}" stroke="#E9F3FA" stroke-width="2"/><rect x="{sx-5:.0f}" y="0" width="10" height="12" rx="2" fill="#E9F3FA"/>'
            f'<line x1="{ex:.0f}" y1="0" x2="{ex:.0f}" y2="{h}" stroke="#E9F3FA" stroke-width="2"/><rect x="{ex-5:.0f}" y="{h-12}" width="10" height="12" rx="2" fill="#E9F3FA"/></svg>')

def sound_editor(x, y, w, h, title_tabs=None, active=0, pad=0, sample_open=False):
    hd = head(None, tabs=title_tabs or ["Sound Editor · Pad 1 Kick"], active=active, maxi=True, extra='<span class="btn sm ghost">Split ▾</span>')
    inner_w = w - 2 - 16
    arrow = '<div style="flex:none;align-self:center;color:#2B4A60;font-size:18px;width:14px;text-align:center">→</div>'
    cols = 4
    cw = (inner_w - 3 * 14) / cols
    k = lambda *a, **kw: knob(*a, size=44, kw=int(cw/2.2), **kw)
    k3 = lambda *a, **kw: knob(*a, size=40, kw=int(cw/3.35), **kw)
    src = f'''<div style="padding:8px;display:flex;flex-direction:column;gap:8px">
<div style="display:flex;gap:6px;align-items:center"><span class="lbl">Model</span><span class="sel">Kick</span><span style="flex:1"></span></div>
<div style="display:flex;gap:6px;align-items:center"><span class="lbl">Source</span><span class="sel">Layer</span></div>
<div style="display:flex;gap:6px;align-items:center"><span class="lbl">Players</span><span class="sel">A + B</span></div>
{{SRC_SAMPLE}}</div>'''
    if sample_open:
        src = src.replace("{SRC_SAMPLE}", f'''<div style="padding:8px;border:1px dashed #23445A;border-radius:5px;font-size:11px;color:var(--t2);line-height:1.45">A: kick_psy_01.wav · 3–82 %<br>B: none<br><span style="color:var(--t1)">Range, level, split and player options ↓ in the Sample Editor</span></div>''')
    else:
        src = src.replace("{SRC_SAMPLE}", f'''{waveform(int(cw - 18), 70)}
<div style="display:flex;justify-content:space-between;font-size:10.5px;color:var(--t2)"><span>3 %</span><span>kick_psy_01.wav</span><span>82 %</span></div>
<div style="display:flex;justify-content:space-around">{k3("Sample lvl","100 %",.5)}{k3("Split","vel 64",.5)}</div>
<div style="display:flex;gap:4px;flex-wrap:wrap"><span class="tg on">Loop</span><span class="tg">Reverse</span><span class="tg on">Normalize</span><span style="flex:1"></span><span class="btn sm">Load…</span></div>''')
    tone = f'''<div style="padding:8px;display:grid;grid-template-columns:1fr 1fr;row-gap:10px;justify-items:center">{k("Tune","0.0 st",.5,bipolar=True)}{k("Fine","0 ct",.5,bipolar=True)}{k("Decay","380 ms",.42)}{k("Character","50 %",.5)}</div>'''
    body = f'''<div style="padding:8px;display:grid;grid-template-columns:1fr 1fr 1fr;row-gap:10px;justify-items:center">{k3("Pitch env","36.0 st",.75)}{k3("Pitch dec","22 ms",.3)}{k3("Drive","2.20×",.2)}{k3("Sub","25 %",.25)}{k3("Tail","200 ms",.25)}</div>'''
    filt = f'''<div style="padding:8px;display:flex;flex-direction:column;gap:8px"><div style="display:flex;gap:6px;align-items:center"><span class="lbl">Filter</span><span class="sel">LP 24</span></div>
<div style="display:flex;justify-content:space-around">{k("Cutoff","4.80 kHz",.7)}{k("Reso","20 %",.2)}</div><div class="cap" style="padding-top:2px">Transient</div>
<div style="display:flex;justify-content:space-around">{k("Attack","+20 %",.6,bipolar=True)}{k("Sustain","−10 %",.45,bipolar=True)}</div></div>'''
    outp = f'''<div style="padding:8px;display:flex;flex-direction:column;gap:8px"><div style="display:grid;grid-template-columns:1fr 1fr;row-gap:8px;justify-items:center">{k("Level","85 %",.85)}{k("Pan","C",.5,bipolar=True)}{k("Delay send","0 %",0,color="var(--cyan)")}{k("Reverb send","15 %",.15,color="#9C8CF2")}</div>
<div style="display:grid;grid-template-columns:auto 1fr;gap:6px 8px;align-items:center"><span class="lbl">Output</span><span class="sel">Main</span><span class="lbl">Note</span><span class="sel">C1 (36)</span><span class="lbl">Choke</span><span class="sel">Off</span><span class="lbl">Voices</span><span class="sel">Mono</span></div></div>'''
    colA = sec("1 · Source", src, cw, "sample + synth")
    colB = f'<div style="display:flex;flex-direction:column;gap:10px;width:{cw:.0f}px">{sec("2 · Tone", tone, cw)}{sec("Kick body", body, cw, "", adv=True)}</div>'
    colC = sec("3 · Filter &amp; transient", filt, cw)
    colD = sec("4 · Output", outp, cw, "to mixer")
    return panel(x, y, w, h, hd, f'<div style="display:flex;gap:0;padding:8px;align-items:flex-start">{colA}{arrow}{colB}{arrow}{colC}{arrow}{colD}</div>')

def sample_editor(x, y, w, h, title_tabs=None, active=0):
    hd = head(None, tabs=title_tabs or ["Sample Editor · Pad 1 Kick"], active=active, maxi=True)
    ww = w - 2 - 24 - 200
    wh = h - 32 - 40 - 28
    side = f'''<div style="position:absolute;right:12px;top:50px;width:186px;display:flex;flex-direction:column;gap:6px">
<div style="display:flex;justify-content:space-around">{knob("Sample level","100 %",.5,34,kw=88)}{knob("Split","vel 64",.5,34,kw=88)}</div>
<div style="font-size:10.5px;color:var(--t2);white-space:nowrap">Start <b style="color:var(--t1)">3.0 %</b> · End <b style="color:var(--t1)">82.0 %</b> · 0.51 s</div></div>'''
    return panel(x, y, w, h, hd, f'''<div style="height:40px;display:flex;align-items:center;gap:6px;padding:0 10px;border-bottom:1px solid var(--subtle)"><span class="cap">Player</span><div class="seg"><i class="on">A · kick_psy_01.wav</i><i>B · empty</i></div>
<span class="vsep"></span><span class="tg on">Loop</span><span class="tg">Reverse</span><span class="tg on">Normalize</span><span style="flex:1"></span><span class="btn sm pri">Load…</span><span class="btn sm">Copy to library</span><span class="btn sm">Clear</span></div>
<div style="position:absolute;left:12px;top:50px">{waveform(int(ww), int(wh))}</div>{side}''')

# ----------------------------------------------------------------- mixer dock
def meter(h, frac, col=None, w=6, peak=None):
    """Measured audio peak (post-fader for pads, post-return for Main). Colours follow dBFS
    (green below -12, yellow to -3, orange to 0); no red: CLIP is a separate lamp."""
    p = f'<i style="position:absolute;left:0;right:0;bottom:{min(peak, 1)*(h-2):.0f}px;height:2px;background:#E9F3FA"></i>' if peak else ""
    return (f'<div style="position:relative;width:{w}px;height:{h}px;background:#030A10;border-radius:2px;overflow:hidden;box-shadow:inset 0 1px 2px #000,0 1px 0 #1A2D3B">'
            f'<i style="position:absolute;left:0;right:0;bottom:0;height:{frac*h:.0f}px;background:linear-gradient(to top,#2FC58A 0,#2FC58A {0.7*h:.0f}px,#E3D14B {0.7*h:.0f}px,#E3D14B {0.9*h:.0f}px,#F08A3C {0.9*h:.0f}px)"></i>'
            f'<i style="position:absolute;left:0;right:0;bottom:{0.7*h:.0f}px;height:1px;background:#030A10"></i><i style="position:absolute;left:0;right:0;bottom:{0.9*h:.0f}px;height:1px;background:#030A10"></i>{p}</div>')

def lamp(kind, on=False, latched=False, small=False):
    """OVER/CLIP/-1 dB lamps: unlit = dark outlined; lit = high-contrast fill with text."""
    txt = {"clip": "CLIP", "over": "OVER", "hot": "−1"}[kind]
    if not on:
        st = "background:#06101A;color:#2E4556;border:1px solid #1D3346"
    elif kind == "clip":
        st = "background:#FF2D55;color:#FFFFFF;border:1px solid #FF8FA0;box-shadow:0 0 8px rgba(255,45,85,.6);font-weight:800"
    elif kind == "over":
        st = "background:#E4425D;color:#FFFFFF;border:1px solid #FF8FA0;font-weight:700"
    else:
        st = "background:#3A2A08;color:#FFD46A;border:1px solid #8A6B1A;font-weight:700"
    lt = '<i style="position:absolute;right:-3px;top:-3px;width:6px;height:6px;border-radius:50%;background:#fff"></i>' if (on and latched) else ""
    return f'<span style="position:relative;display:inline-flex;align-items:center;justify-content:center;height:{14 if small else 16}px;padding:0 {3 if small else 5}px;border-radius:3px;font-size:{9 if small else 10}px;letter-spacing:.04em;{st}">{txt}{lt}</span>'

def vfader(h, frac, w=18, dim=False):
    """Level fader: recessed slot, intensity fill inside the slot, metal cap with grip lines."""
    cap_h = 16
    cy = (1 - frac) * (h - cap_h)
    fill = f'<i style="position:absolute;left:{w/2-1.5:.1f}px;width:3px;bottom:2px;height:{frac*(h-cap_h):.0f}px;border-radius:2px;background:linear-gradient(to top,#32D296,#E3D14B {h*0.6:.0f}px,#F08A3C {h*0.85:.0f}px,#E4425D {h:.0f}px);opacity:{.35 if dim else .9}"></i>'
    ticks = "".join(f'<i style="position:absolute;left:0;width:3px;top:{cap_h/2 + k*(h-cap_h)/6:.0f}px;height:1px;background:#24394A"></i><i style="position:absolute;right:0;width:3px;top:{cap_h/2 + k*(h-cap_h)/6:.0f}px;height:1px;background:#24394A"></i>' for k in range(7))
    return (f'<div style="position:relative;width:{w}px;height:{h}px;flex:none">{ticks}'
            f'<i style="position:absolute;left:{w/2-3.5:.1f}px;width:7px;top:0;bottom:0;background:#03090E;border-radius:4px;box-shadow:inset 0 1px 3px #000,0 1px 0 #1A2D3B"></i>{fill}'
            f'<i style="position:absolute;left:{max(0, w/2-13):.0f}px;width:{min(26, w):.0f}px;top:{cy:.0f}px;height:{cap_h}px;border-radius:3px;'
            f'background:linear-gradient(#5A6C79,#2C3B46 45%,#1E2A33 55%,#3A4B57);border:1px solid #0A1219;box-shadow:0 2px 3px rgba(0,0,0,.6),inset 0 1px 0 rgba(255,255,255,.18)"></i>'
            f'<i style="position:absolute;left:{max(0, w/2-9):.0f}px;width:{min(18, w-8):.0f}px;top:{cy+cap_h/2-.5:.0f}px;height:1px;background:#E9F3FA;opacity:.85"></i></div>')

MET = [.72, .2, .38, .55, .3, 0, .12, .06, 0, .18, 0, 0, 0, 0, 0, 0]

MIN_STRIP = {"compact": 80, "expanded": 108}
DLY = [0, 15, 10, 0, 20, 15, 25, 0]
REV = [0, 15, 20, 0, 10, 10, 0, 30]

def mixer_layout(w, state, bank=None):
    """Strips visible: all 16 when each gets at least its minimum width; otherwise a bank of
    8 (strips widen); below 8 a horizontal scroll viewport. Returns (count, strip width, first)."""
    side = 112 + 6 if state == "compact" else (2 * 100 + 132 + 12)
    avail = w - 2 - 16 - side
    gap = 6
    fit = int((avail + gap) // (MIN_STRIP[state] + gap))
    n = 16 if (fit >= 16 and bank is None) else (8 if fit >= 8 else max(fit, 1))
    sw = (avail + gap) / n - gap
    if n < 16: sw = min(sw, 170)
    first = 8 if bank == 2 else 0
    return n, sw, first, avail

def mixer(x, y, w, h, state="compact", bank=None, show_fx=True, hot=False, clip=False, over=()):
    lamps_main = f'{lamp("hot", hot)}{lamp("clip", clip, latched=clip)}'
    if state == "collapsed":
        bars = "".join(f'<div style="width:160px;height:6px;background:#030A10;border-radius:2px;overflow:hidden;box-shadow:inset 0 1px 2px #000"><i style="display:block;width:{v}%;height:100%;background:linear-gradient(to right,#2FC58A 0,#2FC58A 112px,#E3D14B 112px,#E3D14B 144px,#F08A3C 144px)"></i></div>' for v in (62, 58))
        hd = f'''<span class="grip"></span><span class="ttl">Mixer</span><span class="chip">Collapsed</span><span style="flex:1"></span>
<span class="lbl">Main</span><div style="display:flex;flex-direction:column;gap:2px">{bars}</div>
<span class="val">−6.2 dB</span><span class="chip">peak −1.8</span>{lamps_main}<span class="chip c">Main + 2 outs</span><span class="vsep"></span><div class="seg"><i class="on">Collapsed</i><i>Compact</i><i>Expanded</i></div>'''
        return f'<div class="pnl" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"><div class="ph" style="border:0;height:{h-2}px">{hd}</div></div>'
    n, sw, first, avail = mixer_layout(w, state, bank)
    bh = h - 32 - 12
    seg = f'<div class="seg"><i>Collapsed</i><i class="{"on" if state=="compact" else ""}">Compact</i><i class="{"on" if state=="expanded" else ""}">Expanded</i></div>'
    if n == 16:
        bank_ui = '<span class="chip">all 16 strips</span>'
    elif n == 8:
        bank_ui = f'<span class="lbl">bank of 8 (width)</span><div class="seg"><i class="{"on" if first == 0 else ""}">1–8</i><i class="{"on" if first == 8 else ""}">9–16</i></div>'
    else:
        bank_ui = f'<span class="lbl">{n} visible · scroll</span>'
    hd = (f'<span class="grip" style="opacity:.3"></span><span class="ttl">Mixer</span><span class="lbl">bottom dock · drag the top edge to resize</span><span style="flex:1"></span>'
          f'<span class="btn sm" title="Clears latched OVER and CLIP lamps">Reset peaks</span><span class="vsep"></span>{bank_ui}<span class="vsep"></span>{seg}')
    strips = []
    for j in range(n):
        i = first + j
        col = PADC[i]
        sx = 8 + j * (sw + 6)
        out_txt = "Main" if not OUTS[i] else ("Out %d" % (OUTS[i] + 1) if OUTS[i] in HOST_ON else "Out %d→Main" % (OUTS[i] + 1))
        out_col = "var(--t3)" if not OUTS[i] else ("var(--cyan)" if OUTS[i] in HOST_ON else "var(--warn)")
        mut = i == 11
        sel = i == 0
        pk = MET[i] + .06 if MET[i] else None
        name = f'''<div style="display:flex;align-items:center;gap:5px;height:18px"><span style="width:4px;height:14px;background:{col};border-radius:1px"></span><span style="font-size:12px;font-weight:600;white-space:nowrap;overflow:hidden">{SHORT[i]}</span>{'<span style="margin-left:auto">' + lamp("over", True, True, small=True) + '</span>' if i in over else ''}</div>'''
        ms = f'''<div style="display:flex;gap:4px"><span class="tg sm {'m' if mut else ''}" style="flex:1;height:26px">M</span><span class="tg sm" style="flex:1;height:26px">S</span></div>'''
        if state == "compact":
            fh = bh - 68
            inner = f'''{name}<div style="display:flex;gap:4px;margin-top:4px;align-items:flex-start;height:{fh}px">{vfader(fh, LEVEL[i]/100, 26)}{meter(fh, MET[i], w=6, peak=pk)}
<div style="flex:1;display:flex;justify-content:center;align-items:center;height:{fh}px">{knob("Pan", pan_txt(PAN[i]), .5 + PAN[i] / 2, 32, kw=40)}</div></div>
<div style="margin-top:6px">{ms}</div>'''
        else:
            fh = bh - 274
            inner = f'''{name}
<div style="display:flex;flex-direction:column;align-items:center;gap:2px;margin-top:4px">{knob("Pan",pan_txt(PAN[i]),.5 + PAN[i] / 2,40,kw=int(sw-10))}<div style="display:flex;justify-content:space-between;width:100%">{knob("Delay",f"{DLY[i%8]} %",DLY[i%8]/100,34,kw=int(sw/2-6))}{knob("Reverb",f"{REV[i%8]} %",REV[i%8]/100,34,kw=int(sw/2-6))}</div></div>
<div style="display:flex;gap:6px;justify-content:center;margin-top:8px">{vfader(fh, LEVEL[i]/100, 28)}{meter(fh, MET[i], w=8, peak=pk)}</div>
<div style="text-align:center;font-size:12px;margin-top:4px">{LEVEL[i]} %</div>
<div style="margin-top:6px">{ms}</div>
<div style="margin-top:6px;height:26px;border:1px solid {'#6B5420' if (OUTS[i] and OUTS[i] not in HOST_ON) else '#1D3B50'};border-radius:5px;font-size:11.5px;color:{out_col};display:flex;align-items:center;justify-content:center;white-space:nowrap;background:#08121A">{out_txt}</div>'''
        strips.append(f'<div style="position:absolute;left:{sx:.0f}px;top:6px;width:{sw:.0f}px;height:{bh:.0f}px;background:{"linear-gradient(#17293A,#112130)" if sel else "linear-gradient(#112230,#0C1B27)"};border:1px solid {"#E9F3FA" if sel else "#163246"};border-radius:6px;padding:5px 6px;overflow:hidden;box-shadow:inset 0 1px 0 rgba(255,255,255,.04)">{inner}</div>')
    if n < 16:
        vis_w = n * (sw + 6) - 6
        strips.append(f'<div style="position:absolute;left:8px;top:{bh+8:.0f}px;width:{vis_w:.0f}px;height:3px;background:#0A1722;border-radius:2px"><i style="display:block;margin-left:{first/16*100:.0f}%;width:{n/16*100:.0f}%;height:3px;background:#2B4A60;border-radius:2px"></i></div>')
    fx = ""
    fxx = w - 2 - 8
    if state == "compact":
        mw = 112
        mh = bh - 58
        fx = f'''<div style="position:absolute;left:{fxx-mw:.0f}px;top:6px;width:{mw}px;height:{bh:.0f}px;background:linear-gradient(#132433,#0D1C28);border:1px solid #2B4A60;border-radius:6px;padding:5px 7px">
<div style="display:flex;align-items:center;gap:4px"><span style="font-size:12px;font-weight:600">Main</span><span style="margin-left:auto;display:flex;gap:3px">{lamps_main}</span></div>
<div style="display:flex;gap:4px;justify-content:center;margin-top:6px">{vfader(mh,.6,26)}{meter(mh,.62,w=6,peak=.7)}{meter(mh,.58,w=6,peak=.66)}</div><div style="text-align:center;font-size:12px;margin-top:4px">60 % · −6.2 dB</div></div>'''
    elif state == "expanded" and show_fx:
        def ret(name, colr, kn, left):
            return f'''<div style="position:absolute;top:6px;width:100px;height:{bh:.0f}px;background:linear-gradient(#112230,#0C1B27);border:1px solid #1D3B50;border-radius:6px;padding:6px;left:{left:.0f}px">
<div style="font-size:12px;font-weight:600;color:{colr}">{name}</div><div style="font-size:11px;color:var(--t3)">return → Main</div>
<div style="display:flex;flex-direction:column;align-items:center;gap:4px;margin-top:6px">{kn}</div></div>'''
        x0 = fxx - (2 * 100 + 132 + 12)
        dl = ret("Delay", "var(--t1)", '<span class="sel" style="font-size:12px;margin-bottom:4px">1/8D</span>' + knob("Feedback", "35 %", .41, 36, kw=88) + knob("Tone", "50 %", .5, 36, kw=88) + knob("Return", "80 %", .8, 36, kw=88), x0)
        rv = ret("Reverb", "var(--t1)", knob("Size", "50 %", .5, 36, kw=88) + knob("Damp", "50 %", .5, 36, kw=88) + knob("Return", "80 %", .8, 36, kw=88), x0 + 106)
        mh = bh - 128
        main = f'''<div style="position:absolute;left:{x0+212:.0f}px;top:6px;width:132px;height:{bh:.0f}px;background:linear-gradient(#132433,#0D1C28);border:1px solid #2B4A60;border-radius:6px;padding:7px">
<div style="font-size:13px;font-weight:600">Main</div><div style="font-size:10.5px;color:var(--t3);white-space:nowrap">post-return, pre-host</div>
<div style="display:flex;gap:4px;margin-top:6px">{lamps_main}</div>
<div style="display:flex;gap:5px;justify-content:center;margin-top:8px">{vfader(mh,.6,28)}{meter(mh,.62,w=8,peak=.7)}{meter(mh,.58,w=8,peak=.66)}</div><div style="text-align:center;font-size:12px;margin-top:5px">60 % · −6.2 dB</div><div style="text-align:center;font-size:11px;color:var(--t2)">peak −1.8 dB</div></div>'''
        fx = dl + rv + main
    return panel(x, y, w, h, hd, "".join(strips) + fx)

# ----------------------------------------------------------------- generator, routing, library, settings
def generator(x, y, w, h, tabs=None, active=0):
    hd = head("Generator" if not tabs else None, tabs=tabs, active=active)
    kw = int((w - 20) / 4)
    knobs = "".join(knob(l, f"{v} %", v / 100, 36, kw=kw) for l, v in [("Density", 60), ("Groove", 50), ("Syncopation", 35), ("Swing", 0), ("Humanize", 15), ("Complexity", 40), ("Fills", 50), ("Variation", 40)])
    locks = "".join(f'<span class="tg {"on" if i in (0, 2) else ""}" style="width:{(w-36)/8-4:.0f}px;height:22px;font-size:10px;margin:0 2px 4px 0">{i+1}</span>' for i in range(16))
    return panel(x, y, w, h, hd, f'''<div style="padding:10px;display:flex;flex-direction:column;gap:10px">
<div style="display:flex;gap:6px;align-items:center"><span class="lbl" style="width:38px">Style</span><span class="sel" style="flex:1">Full-on / Psy</span><span class="lbl">Seed</span><span class="sel" style="width:84px">1836745</span></div>
<div style="display:flex;flex-wrap:wrap;row-gap:8px">{knobs}</div>
<div><div class="cap" style="margin-bottom:6px">Locked lanes <span style="text-transform:none;letter-spacing:0;color:var(--t2)">· kept by every action</span></div><div style="display:flex;flex-wrap:wrap">{locks}</div></div>
<div style="padding:8px;border:1px solid var(--subtle);border-radius:6px;background:#0D1E2C"><div class="lbl" style="margin-bottom:6px">Target: <b style="color:var(--t1)">Pattern 2 A</b> · unlocked steps · 4 bars</div>
<div style="display:flex;gap:6px;flex-wrap:wrap"><span class="btn pri">Generate Pattern</span><span class="btn">Fill</span><span class="btn">Variation A→B</span><span class="btn">Mutate lanes…</span></div></div>
<div style="padding:8px;border:1px solid #5A3A28;border-radius:6px"><div class="lbl" style="margin-bottom:6px;white-space:normal">Target: <b style="color:var(--copper)">whole song</b> · all 5 patterns, B variations, timeline</div><span class="btn">Generate Song…</span></div>
<div style="font-size:11px;color:var(--t2)"><span class="chip" style="background:#0C2A22;color:var(--ok)">done</span> Pattern 2 A generated · seed 1836745 · Undo restores it</div>
</div>''')

def routing(x, y, w, h, tabs=None, active=0):
    hd = head("Routing" if not tabs else None, tabs=tabs, active=active)
    cols = ["Main"] + [str(n) for n in range(2, 10)]
    cw = (w - 2 - 150) / 9
    head_row = "".join(f'<span style="width:{cw:.0f}px;text-align:center;font-size:10px;color:{"var(--t2)" if (k in HOST_ON) else "var(--dis)"}">{c}</span>' for k, c in enumerate(cols))
    rows = []
    for i in range(16):
        dots = "".join(f'<span style="width:{cw:.0f}px;display:flex;justify-content:center"><i style="width:12px;height:12px;border-radius:50%;border:1.5px {"solid" if k in HOST_ON else "dashed"} {"#2B4A60" if k in HOST_ON else "#1F3445"};{"background:var(--copper);border-color:var(--copper);" if OUTS[i]==k else ""}{"background:#6B5420;border-color:#6B5420;" if OUTS[i]==k and k not in HOST_ON else ""}"></i></span>' for k in range(9))
        rows.append(f'<div style="display:flex;align-items:center;height:17.5px;border-top:1px solid #0E2231"><span style="width:84px;padding-left:8px;font-size:11px;white-space:nowrap;overflow:hidden"><i style="display:inline-block;width:3px;height:10px;background:{PADC[i]};margin-right:5px"></i>{SHORT[i]}</span><span style="width:66px;font-size:10.5px;color:var(--t2)">{PADS[i][1]} ({PADS[i][2]})</span>{dots}</div>')
    return panel(x, y, w, h, hd, f'''<div style="padding:8px;display:flex;gap:6px;align-items:center;flex-wrap:wrap"><span class="lbl">Sequencer</span><span class="sel">Internal</span><span class="lbl" style="margin-left:6px">Sync</span><span class="chip c">host 145.0 BPM</span></div>
<div style="padding:0 8px 6px;font-size:10.5px;color:var(--t2)">Dashed: not enabled by the host. A pad there plays on Main.</div>
<div style="display:flex;align-items:center;height:20px"><span style="width:150px;padding-left:8px" class="cap">Pad · note</span>{head_row}</div>{''.join(rows)}''')

def library(x, y, w, h, tabs=None, active=0):
    hd = head("Library" if not tabs else None, tabs=tabs, active=active)
    items = [("Factory", "Psy Full-on"), ("Factory", "House Techno"), ("Factory", "Hip-hop Breaks"), ("User", "My Kit 01")][: (3 if h < 300 else 4)]
    lst = "".join(f'<div style="height:28px;display:flex;align-items:center;gap:8px;padding:0 10px;border-top:1px solid #0E2231;{"background:#13283A;box-shadow:inset 2px 0 0 var(--copper);" if k==0 else ""}"><span class="cap" style="width:50px">{t}</span><span style="font-size:12px">{n}</span></div>' for k, (t, n) in enumerate(items))
    return panel(x, y, w, h, hd, f'''<div style="padding:8px"><div class="seg"><i class="on">Kits</i><i>Pads</i><i>Patterns</i><i>Songs</i></div></div>{lst}
{'' if h < 300 else '<div style="padding:8px 10px;font-size:11.5px;color:var(--t2);line-height:1.4">Kit replaces pads, mix, sends, returns. Keeps patterns, song, mutes.</div>'}
<div style="padding:{"8px" if h < 300 else "0"} 10px 0;display:flex;gap:5px;flex-wrap:wrap"><span class="btn pri sm">Load</span><span class="btn sm">Undo Load</span><span class="btn sm">Save…</span><span class="btn sm">Import .voxkit</span><span class="btn sm">Export .voxkit</span></div>
{'' if h < 300 else '<div style="padding:8px 10px;font-size:11.5px"><span class="chip x">missing</span> <span style="color:var(--t2)">Pad 10 A: perc_shaker.wav — reference kept</span></div>'}''')

def settings(x, y, w, h, tabs=None, active=0):
    hd = head("Settings" if not tabs else None, tabs=tabs, active=active)
    kv = lambda a, b: f'<div style="display:flex;justify-content:space-between;height:22px;align-items:center;padding:0 10px;border-top:1px solid #0E2231"><span class="lbl">{a}</span><span class="val">{b}</span></div>'
    return panel(x, y, w, h, hd, f'''<div style="padding:8px 10px;display:flex;gap:8px;align-items:center;flex-wrap:wrap"><span class="lbl">Voices</span><span class="sel">64</span><span class="tg on">Choke groups</span>{knob("Velocity","80 %",.8,32,kw=60)}<span class="btn danger sm">Panic</span></div>
<div class="cap" style="padding:6px 10px">Diagnostics · read-only</div>{kv("Active voices","12")}{kv("Dropped MIDI","0")}{kv("Sample memory","3.1 / 512 MB")}{kv("Rate / block","48000 / 256")}{kv("Host outputs","Main, Out 3, Out 6")}{kv("State schema","1")}''')

# ----------------------------------------------------------------- page assembly
def page(name, W, H, inner, legend=None, title=None, cols=3):
    leg = ""
    if legend:
        leg = f'<div class="legend" style="grid-template-columns:repeat({cols},1fr);width:{W}px">' + "".join(f"<div><b class=n>{i+1}</b>{t}</div>" for i, t in enumerate(legend)) + "</div>"
    ttl = f'<div class="title" style="width:{W}px">{title[0]}<span>{title[1]}</span></div>' if title else ""
    html = f'<!doctype html><meta charset="utf-8"><style>{CSS}</style><body style="width:{W}px">{ttl}<div style="{"padding-top:8px;background:#030A10" if title else ""}"><div class="app" style="width:{W}px;height:{H}px">{inner}</div></div>{leg}</body>'
    open(os.path.join(OUT, name + ".html"), "w").write(html)

G = 6   # gutter between docked panels
def frame(W, H, mixer_h):
    """Returns the work area under the chrome and above the mixer dock."""
    top = 44 + G
    return top, H - mixer_h - G - G

# ----------------------------------------------------------------- screens
def beatmaking(W=1440, H=900, name="01-beatmaking", legend=True):
    mix_h = 184 if H >= 860 else (150 if H >= 720 else 30)
    top, bottom = frame(W, H, mix_h)
    ah = bottom - top
    if W >= 1400:
        lw, rw = 392, 280
    elif W >= 1240:
        lw, rw = 250, 250
    else:
        lw, rw = 0, 0
    parts = [chrome(compact=W < 1240)]
    if W >= 1240:
        if W >= 1400:
            prh = int(pad_grid_height(lw - 2) + 32)
            parts.append(pad_rack(G, top, lw, prh, "grid"))
            parts.append(library(G, top + prh + G, lw, ah - prh - G))
        else:
            parts.append(pad_rack(G, top, lw, ah, "list"))
        parts.append(step_editor(G + lw + G, top, W - lw - rw - 4 * G, ah, focus=True))
        parts.append(inspector(W - rw - G, top, rw, ah, "steps"))
        mstate = "compact" if mix_h > 40 else "collapsed"
        parts.append(mixer(G, bottom + G, W - 2 * G, mix_h, mstate))
    else:
        # 1040 x 700: side zones collapse to rails, one central editor, mixer collapsed
        rail = 34
        parts.append(f'''<div class="pnl" style="left:{G}px;top:{top}px;width:{rail}px;height:{ah}px;display:flex;flex-direction:column;align-items:center;gap:6px;padding-top:8px">
<span class="hb on" title="Pad Rack" style="width:26px;height:26px;border:1px solid #7A4434">▦</span><span class="hb" style="width:26px;height:26px">◫</span><span class="hb" style="width:26px;height:26px">✦</span></div>''')
        lw2 = 196
        parts.append(pad_rack(G + rail + G, top, lw2, ah, "list"))
        parts.append(step_editor(G + rail + G + lw2 + G, top, W - (rail + lw2) - rail - 6 * G, ah, rowmin=16))
        parts.append(f'''<div class="pnl" style="left:{W-G-rail}px;top:{top}px;width:{rail}px;height:{ah}px;display:flex;flex-direction:column;align-items:center;gap:6px;padding-top:8px">
<span class="hb" title="Inspector" style="width:26px;height:26px">ⓘ</span><span class="hb" style="width:26px;height:26px">⇄</span><span class="hb" style="width:26px;height:26px">⚙</span></div>''')
        parts.append(mixer(G, bottom + G, W - 2 * G, mix_h, "collapsed"))
    legend_items = None
    if legend and W == 1440:
        parts += [mk(1, 735, 12), mk(2, 1000, top + 5), mk(3, 600, 583), mk(4, 1310, top + 46), mk(5, 1, bottom + G + 4), mk(6, 1250, top + 5), mk(7, 700, top + 5), mk(8, 110, top + 5)]
        legend_items = [
            "Top chrome: workspace preset and Panels menu, Pattern/Song and the active pattern, host status read-only (no Play, no tempo edit), enabled outputs, Undo/Redo, Panic.",
            "Drag MIDI: a drag handle whose scope is fixed by where it sits (here: this pattern; the Song Arranger’s handle: entire song). Saving a .mid file is a separate command in its ▾ menu; the handle itself never opens a save dialog. No fake ‘insert into track’.",
            "Parameter lanes under the same ruler: Velocity and Microtiming of the current lane (Closed Hat). Bars line up with the step columns at every zoom and width.",
            "Multi-step selection (steps 5–8) → the Inspector shows exact values, ‘mixed’ ranges and what an edit applies to.",
            "Mixer dock, Compact: one shared mixer at the bottom. It never covers the sequencer; it resizes the central zone.",
            "Every side panel has a header: grip to move, title or dock tabs, hide. The Inspector has Pin (not shown pinned here).",
            "Pattern toolbar: real Swing and Accent (host-automatable), Bar menu (Duplicate to next bar, Clear bar, New seed). Lit lane LED = lane on for the internal sequencer (lane 16 off). Velocity lane bars use the intensity scale.",
            "Pad Rack Grid: 16 equal square rubber pads. Top strip (number, note) selects without sound; the strike surface auditions and selects. Top LED and glow scale with the hit velocity (Pad 1 = 118, Pad 4 = 72); right-edge bar = measured post-fader peak; M/S bottom-left; ‘● missing’ = sample file missing. Library is stacked below in the same zone."]
    sub = {1440: "Pad Rack (grid) · Step Sequencer + parameter lanes · Inspector (steps) · Mixer compact",
           1600: "Pad Rack (grid) · Step Sequencer + parameter lanes · Inspector (steps) · Mixer compact",
           1280: "Pad Rack (list) · Step Sequencer + 1 parameter lane · Inspector (steps) · Mixer compact",
           1040: "minimum size · side zones as rails · Pad Rack (list, short names) · Step Sequencer · Mixer collapsed"}[W]
    if not legend:
        legend_items = {
            1600: ["Same layout as 1440×900. The extra width lets all 16 Compact strips reach their 80 px minimum, so the Mixer shows 1–16 without banks; the step grid gets the rest. Nothing new appears."],
            1280: ["Pad Rack switches to List (same selection, same M/S). Step lanes use short names. One parameter lane fits; the second is reached from the lane selector.",
                   "16 Compact strips would be narrower than 80 px here, so the Mixer shows a bank of 8 (1–8 / 9–16). Parameter lanes: the Step editor is 508 px high, so 1 lane (rule: 2 lanes at ≥ 560 px, 1 at 420–559, hidden below 420)."],
            1040: ["Minimum supported size. The left and right zones collapse to rails: their dock tabs become icons; clicking one opens that tab as a docked panel (not an overlay). Pad Rack is open here; Inspector, Routing and Settings are on the right rail.",
                   "Mixer collapsed to its one-line summary (Main meter, peak, enabled outputs, state switch). Compact/Expanded take height from the central zone; the Step grid keeps all 16 lanes and 16 columns.",
                   "The Step editor is 608 px high here, so both parameter lanes stay (same rule as above: by editor height, not window height). Lane headers carry the lane LED and name; M/S are in the Pad Rack, Mixer and Inspector. Note numbers are hidden in the narrow list (shown in the Inspector)."]}[W]
    page(name, W, H, "".join(parts), legend_items, ("Beatmaking", f"{'default workspace · ' if W == 1440 else ''}{W}×{H} · {sub}"), 3 if W == 1440 else 1)

def composition():
    W, H = 1440, 900
    mix_h = 30
    top, bottom = frame(W, H, mix_h)
    ah = bottom - top
    lw, rw = 230, 270
    cx, cw = G + lw + G, W - lw - rw - 4 * G
    sh = 300
    parts = [chrome("Composition", "Song", pos="39 . 1 . 1"), pad_rack(G, top, lw, ah, "list"),
             song_editor(cx, top, cw, sh, sel=36, target="bar"),
             step_editor(cx, top + sh + G, cw, ah - sh - G, steps=32, sel_steps=((8, 14),), cur_lane=8, play=11, song=True, rowmin=13, seed=8, focus=True),
             inspector(W - rw - G, top, rw, ah, "song"), mixer(G, bottom + G, W - 2 * G, mix_h, "collapsed")]
    parts += [mk(1, 318, 160), mk(2, 236, 196), mk(3, 236, 410), mk(4, 520, 30), mk(5, 1, 856)]
    page("02-composition", W, H, "".join(parts), [
        "Edit target, always visible: <b>Edit Source Pattern</b> (P5 B bar 1 → all 4 linked bars) or <b>Edit This Bar</b> (bar 37 only). The text says which bars an edit reaches.",
        "Timeline: colour = pattern slot, stronger = B, hatched + outline = unique, dashed = empty, cyan underline = loop, cyan line = playhead, white ring = selected, faint ring = linked occurrences.",
        "Second central editor (vertical split): Step Sequencer edits what the target says. Selecting a bar never makes it unique; the first real edit does, and Undo restores the link.",
        "Song mode and position come from the chrome; MIDI scope ‘Entire Song’ on the arranger’s Drag MIDI handle.",
        "Mixer collapsed to a status strip (Main meter, peak, outputs) — still one click from Compact."], ("Composition", "1440×900 · Pad Rack (list) · Song Arranger over Step Sequencer · Inspector (song bar) · Mixer collapsed"), 3)

def sound_design():
    W, H = 1440, 900
    mix_h = 176
    top, bottom = frame(W, H, mix_h)
    ah = bottom - top
    lw, rw = 380, 250
    SE = 482
    parts = [chrome("Sound Design", status="Stopped", pos="1 . 1 . 1"), pad_rack(G, top, lw, ah, "grid", sel=0, hit=()),
             sound_editor(G + lw + G, top, W - lw - rw - 4 * G, SE, sample_open=True),
             sample_editor(G + lw + G, top + SE + G, W - lw - rw - 4 * G, ah - SE - G),
             inspector(W - rw - G, top, rw, ah, "pad", pinned=True, other=3, pad=0),
             mixer(G, bottom + G, W - 2 * G, mix_h, "compact")]
    parts += [mk(1, 645, top + 5), mk(2, 645, top + SE + G + 5), mk(3, 1282, top + 5), mk(4, 110, top + 5)]
    page("03-sound-design", W, H, "".join(parts), [
        "One signal-flow layout: Source → Tone → Filter &amp; transient → Output. Only real controls of this pad model; sound shaping, Pan and sends are rotary knobs. Kick body is an advanced section shown only for the Kick model.",
        "Second central editor: Sample Editor (split). Start/end are graphical handles on the real waveform of player A or B. While it is open the Source block shows a summary instead of a second copy.",
        "Inspector pinned to Pad 1 while Pad 4 is selected in the rack: the pinned content does not follow; Quick Controls and the Sound Editor show the same live values.",
        "Pad Rack Grid at 380 px: pads stay square (≥ 80 px); below that the zone switches to List. Clicking a pad’s top strip selects it without sound; striking the pad auditions it."], ("Sound Design", "1440×900 · Pad Rack (grid) · Sound Editor over Sample Editor · Inspector pinned · Mixer compact"), 2)

def mixing():
    W, H = 1440, 900
    mix_h = 440
    top, bottom = frame(W, H, mix_h)
    ah = bottom - top
    lw, rw = 0, 330
    parts = [chrome("Mixing"), step_editor(G, top, W - rw - 3 * G, ah, rowmin=10, sel_steps=(), focus=False),
             routing(W - rw - G, top, rw, ah, tabs=["Inspector", "Routing", "Settings"], active=1),
             mixer(G, bottom + G, W - 2 * G, mix_h, "expanded", hot=True, over=(0,))]
    parts += [mk(1, 1, bottom + G + 4), mk(2, 268, bottom + G + 38), mk(3, 1118, bottom + G + 38), mk(4, 640, bottom + G + mix_h - 48), mk(5, 1072, 120), mk(6, 1370, bottom + G + 86)]
    page("04-mixing", W, H, "".join(parts), [
        "Mixer expanded: same single dock, taller. The central zone shrinks (sequencer still readable) — nothing is overlaid.",
        "At this width 16 strips would fall below 108 px, so the Mixer shows a bank of 8 (1–8 here; 9–16 one click away; the scroll line shows the position). Each strip: vertical level fader, measured meter with peak hold, rotary Pan / Delay send / Reverb send, M/S, real output.",
        "FX returns (Delay, Reverb → Main) and Main are separate strips on the right.",
        "Output box: cyan = host-enabled output (O.Hat, Tom → Out 3; Cym → Out 6). An output the host has not enabled is shown in amber as ‘Out n→Main’ (Perc 5 → Out 8, bank 9–16): it plays on Main.",
        "Routing is a dock tab in the right zone, next to Inspector and Settings (tabs share one zone).",
        "Measured audio, separate from knob values: Kick ‘OVER’ is latched because its post-fader peak passed 0 dBFS (internal headroom, not output clipping). Main ‘−1’ is lit (peak above −1 dBFS); ‘CLIP’ stays dark. Reset peaks clears latches."], ("Mixing", "1440×900 · Step Sequencer reduced · Routing docked as a tab · Mixer expanded with FX returns and Main"), 3)

def generator_screen():
    W, H = 1440, 900
    mix_h = 176
    top, bottom = frame(W, H, mix_h)
    ah = bottom - top
    lw, rw = 230, 340
    parts = [chrome("Beatmaking"), pad_rack(G, top, lw, ah, "list"),
             step_editor(G + lw + G, top, W - lw - rw - 4 * G, ah, focus=True, seed=11, sel_steps=()),
             generator(W - rw - G, top, rw, ah, tabs=["Generator", "Inspector"], active=0),
             mixer(G, bottom + G, W - 2 * G, mix_h, "compact")]
    parts += [mk(1, 1096, 52), mk(2, 1300, 296), mk(3, 1232, 511), mk(4, 1245, 587)]
    page("05-generator-docked", W, H, "".join(parts), [
        "Generator docked beside the Step Sequencer as a tab in the right zone: the grid stays fully visible and updates on every action.",
        "Real controls only: style (the generator's actual style setting), seed, the eight amounts, lane locks.",
        "Each action states its target before it runs: Pattern 2 A (unlocked steps) vs whole song. Generate Song asks to confirm.",
        "Result and Undo feedback in the panel. Genre fidelity is functional work in issue 128, not claimed here."], ("Generator docked", "1440×900 · Generator beside Step · Mixer compact"), 2)

def responsive():
    for (W, H) in [(1040, 700), (1280, 720), (1600, 1000)]:
        beatmaking(W, H, f"06-beatmaking-{W}x{H}", legend=False)

if __name__ == "__main__":
    beatmaking(); composition(); sound_design(); mixing(); generator_screen(); responsive()
    print("\n".join(sorted(os.listdir(OUT))))
