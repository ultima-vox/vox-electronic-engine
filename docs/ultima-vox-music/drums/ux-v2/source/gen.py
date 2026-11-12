# VOX Drums UX/UI v2 (issue 127, stage 0): generates the reference screens as
# HTML. Design reference only; the product UI is native JUCE.
import os, sys
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'html')
os.makedirs(OUT, exist_ok=True)

PADS = [("Kick", "C1 (36)", "#FF8A3B"), ("Snare", "D1 (38)", "#9B7CE8"), ("Clap", "D#1 (39)", "#F06BA8"),
        ("Closed Hat", "F#1 (42)", "#32C5D2"), ("Open Hat", "A#1 (46)", "#4C9BF0"), ("Tom", "A1 (45)", "#7FD66B"),
        ("Rim", "C#1 (37)", "#D6C24A"), ("Cymbal", "C#2 (49)", "#E8B04A"), ("Perc 1", "D2 (50)", "#5FD1A8"),
        ("Perc 2", "D#2 (51)", "#5FD1A8"), ("Perc 3", "E2 (52)", "#8FCB5A"), ("Perc 4", "F2 (53)", "#8FCB5A"),
        ("Perc 5", "F#2 (54)", "#C9A26B"), ("Perc 6", "G2 (55)", "#C9A26B"), ("Perc 7", "G#2 (56)", "#7FB6E8"),
        ("Perc 8", "A2 (57)", "#7FB6E8")]
SHORT = ["Kick", "Snare", "Clap", "C.Hat", "O.Hat", "Tom", "Rim", "Cym", "Perc1", "Perc2", "Perc3", "Perc4", "Perc5", "Perc6", "Perc7", "Perc8"]

CSS = """
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#101318;--s1:#1C232C;--s2:#242D38;--s3:#2E3845;--line:#2E3845;--t1:#F0F2F4;--t2:#A9B3BF;--t3:#74808D;
--amber:#FFAA3B;--cyan:#32C5D2;--violet:#9B7CE8;--red:#F0584C;--green:#7FD66B}
body{background:var(--bg);color:var(--t1);font:14px/1.3 "Inter","Segoe UI","DejaVu Sans",sans-serif;-webkit-font-smoothing:antialiased}
.app{position:relative;overflow:hidden;background:var(--bg)}
.chrome{height:56px;display:flex;align-items:center;gap:16px;padding:0 20px;border-bottom:1px solid var(--line);background:#141920}
.brand{display:flex;flex-direction:column;line-height:1.05}.brand small{font-size:11px;color:var(--t3);letter-spacing:.08em}
.brand b{font-size:22px;font-weight:600}
.kit{display:flex;align-items:center;gap:8px;background:var(--s1);border:1px solid var(--line);border-radius:8px;height:36px;padding:0 12px;min-width:220px}
.kit span{color:var(--t3);font-size:12px}.kit b{font-weight:500;font-size:15px}
.seg{display:inline-flex;background:var(--s1);border:1px solid var(--line);border-radius:8px;padding:3px;gap:2px}
.seg i{font-style:normal;padding:0 14px;height:30px;display:flex;align-items:center;border-radius:6px;color:var(--t2);font-size:14px}
.seg i.on{background:var(--amber);color:#1b1305;font-weight:600}
.tstat{display:flex;align-items:center;gap:14px;margin-left:auto;font-size:14px;color:var(--t2)}
.tstat .dot{width:9px;height:9px;border-radius:50%;background:var(--cyan);box-shadow:0 0 0 3px rgba(50,197,210,.18)}
.tstat b{color:var(--t1);font-weight:500}.tstat .cy{color:var(--cyan)}
.ib,.sel,.chip,.seg i,.lbl,.drag,.sld .hd span,.crumb{white-space:nowrap}
.panel{min-width:0}
.ib{height:36px;min-width:36px;padding:0 10px;border-radius:8px;border:1px solid var(--line);background:var(--s1);color:var(--t2);
display:inline-flex;align-items:center;justify-content:center;font-size:13px;gap:6px}
.ib.pri{background:var(--amber);border-color:var(--amber);color:#1b1305;font-weight:600}
.ib.dis{opacity:.4}.ib.warn{border-color:#5a2f2b;color:var(--red)}.ib.tog{background:#33404f;color:var(--t1);border-color:#46566a}
.nav{height:48px;display:flex;align-items:center;gap:4px;padding:0 20px;border-bottom:1px solid var(--line)}
.nav i{font-style:normal;height:36px;padding:0 20px;display:flex;align-items:center;border-radius:8px;color:var(--t2);font-size:15px;letter-spacing:.02em}
.nav i.on{background:var(--s2);color:var(--t1);box-shadow:inset 0 -2px 0 var(--amber)}
.nav .sp{flex:1}.nav .tool{font-size:13px;height:32px;padding:0 12px;border:1px solid var(--line);border-radius:8px;color:var(--t2)}
.nav .tool.on{border-color:var(--amber);color:var(--amber)}
.body{padding:16px 20px;display:flex;gap:16px}
.panel{background:var(--s1);border:1px solid var(--line);border-radius:8px}
.ph{height:40px;display:flex;align-items:center;gap:10px;padding:0 14px;border-bottom:1px solid var(--line);font-size:13px;color:var(--t2);letter-spacing:.06em;text-transform:uppercase}
.ph b{color:var(--t1);font-weight:600;letter-spacing:0;text-transform:none;font-size:16px}
.row{display:flex;align-items:center;gap:8px}
.lbl{font-size:12px;color:var(--t3);letter-spacing:.04em;text-transform:uppercase}
.sel{height:32px;padding:0 28px 0 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--t1);display:inline-flex;align-items:center;font-size:14px;position:relative;white-space:nowrap}
.sel:after{content:"";position:absolute;right:10px;top:13px;border:5px solid transparent;border-top-color:var(--t3)}
.crumb{font-size:14px;color:var(--t2)}.crumb b{color:var(--t1)}.crumb em{font-style:normal;color:var(--amber)}
.knob{width:64px;display:flex;flex-direction:column;align-items:center;gap:3px}
.knob .k{width:40px;height:40px;border-radius:50%;background:conic-gradient(from 225deg,var(--amber) 0 var(--v),#3a4656 var(--v) 75%,transparent 75%);
position:relative}.knob .k:after{content:"";position:absolute;inset:5px;border-radius:50%;background:var(--s1)}
.knob span{font-size:12px;color:var(--t3)}.knob b{font-size:13px;font-weight:500}
.sld{display:flex;flex-direction:column;gap:4px}.sld .tr{height:6px;border-radius:3px;background:#33404f;position:relative}
.sld .tr i{position:absolute;left:0;top:0;bottom:0;border-radius:3px;background:var(--amber)}
.sld .tr u{position:absolute;top:-5px;width:16px;height:16px;border-radius:50%;background:#F0F2F4;margin-left:-8px}
.sld .hd{display:flex;justify-content:space-between;font-size:12px;color:var(--t3)}.sld .hd b{color:var(--t1);font-weight:500;font-size:13px}
.mk{position:absolute;z-index:9;width:20px;height:20px;border-radius:50%;background:#FFF3D6;color:#3b2a05;font-size:12px;font-weight:700;display:flex;align-items:center;justify-content:center;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.legend{position:absolute;left:20px;right:20px;bottom:8px;display:flex;gap:18px;font-size:12px;line-height:1.35;color:#d9c9a3;z-index:8}
.legend div{flex:1}.legend b{display:inline-flex;width:17px;height:17px;border-radius:50%;background:#FFF3D6;color:#3b2a05;font-size:11px;align-items:center;justify-content:center;margin-right:6px}
.drawer{position:absolute;z-index:10;top:104px;right:0;bottom:0;width:440px;background:#161c24;border-left:1px solid #3a4656;box-shadow:-18px 0 40px rgba(0,0,0,.45);padding:0;z-index:5}
.chip{display:inline-flex;align-items:center;height:22px;padding:0 8px;border-radius:11px;font-size:12px;background:var(--s3);color:var(--t2)}
.chip.ok{background:#1d3b3e;color:var(--cyan)}.chip.off{background:#2a2f37;color:var(--t3)}.chip.bad{background:#40231f;color:var(--red)}
.ms{display:flex;gap:4px}.ms i{font-style:normal;width:26px;height:24px;border-radius:5px;border:1px solid var(--line);font-size:12px;color:var(--t3);display:flex;align-items:center;justify-content:center}
.ms i.m{background:#5a2f2b;color:#ffd9d5;border-color:#5a2f2b}.ms i.s{background:var(--amber);color:#1b1305;border-color:var(--amber)}
"""

def chrome(mode="Pattern", status="Playing", bar="Bar 3.2", outs="Main + 2 of 8 outs"):
    return f"""<div class="chrome">
<div class="brand"><small>ULTIMA VOX MUSIC</small><b>VOX Drums</b></div>
<div class="seg"><i class="{'on' if mode=='Pattern' else ''}">Pattern</i><i class="{'on' if mode=='Song' else ''}">Song</i></div>
<div class="tstat"><span class="dot"></span><span class="cy">Host {status}</span><b>145.0 BPM</b><span>4/4</span><b>{bar}</b>
<span class="chip ok">{outs}</span></div>
<div class="row"><span class="ib">Undo</span><span class="ib dis">Redo</span><span class="ib warn">Panic</span></div></div>"""

def nav(active, tool=None):
    tabs = "".join(f'<i class="{"on" if t==active else ""}">{t}</i>' for t in ["SEQUENCE", "PLAY / KIT", "MIX", "LIBRARY"])
    tools = "".join(f'<span class="tool {"on" if t==tool else ""}">{t}</span>' for t in ["Generate", "Routing", "Settings"])
    return f'<div class="nav">{tabs}<span class="sp"></span>{tools}</div>'

def page(name, w, h, inner, extra_css=""):
    html = f'<!doctype html><meta charset="utf-8"><style>{CSS}{extra_css}</style><div class="app" style="width:{w}px;height:{h}px">{inner}</div>'
    open(os.path.join(OUT, name + '.html'), 'w').write(html)

def notes(items, right=20):
    """items: (x, y, text): a numbered marker at x,y and the text in the legend at the bottom."""
    marks = "".join(f'<span class="mk" style="left:{x}px;top:{y}px">{i+1}</span>' for i, (x, y, t) in enumerate(items))
    leg = "".join(f'<div><b>{i+1}</b>{t}</div>' for i, (x, y, t) in enumerate(items))
    return f'{marks}<div class="legend" style="right:{right}px">{leg}</div>'

def knob(label, value, v=55):
    return f'<div class="knob"><span>{label}</span><div class="k" style="--v:{v*0.75}%"></div><b>{value}</b></div>'

def slider(label, value, pct, color="var(--amber)"):
    return f'<div class="sld"><div class="hd"><span>{label}</span><b>{value}</b></div><div class="tr"><i style="width:{pct}%;background:{color}"></i><u style="left:{pct}%"></u></div></div>'

# ---------------------------------------------------------------- Sequence
import random
def grid(steps=16, seed=3, rowh=30, sel=(0, 4), play=5, lanes=16, w=None):
    random.seed(seed)
    rows = []
    for l in range(lanes):
        name, note, col = PADS[l]
        cells = []
        for s in range(steps):
            on = False
            if l == 0: on = s % (steps // 4) == 0
            elif l == 1: on = s in (steps // 4, 3 * steps // 4) and False
            elif l == 2: on = s in (steps // 4, 3 * steps // 4)
            elif l == 3: on = s % 2 == 1 or s % 4 == 0 and random.random() < .3
            elif l == 4: on = s % (steps // 4) == steps // 8
            elif l in (6, 8): on = s in (7 * steps // 16, 15 * steps // 16)
            elif l == 9: on = s % 4 == 1 and s < 12 * steps // 16
            cls = "c"
            st = ""
            if on:
                vel = 0.55 + 0.45 * random.random()
                st = f'background:{col};opacity:{vel:.2f}'
            if (s // (steps // 4)) % 2 == 1 and not on: cls += " alt"
            if (l, s) == sel: cls += " selc"
            mark = '<s></s>' if on and l in (0, 2) else ''
            lock = '<q></q>' if on and (l, s) in ((3, 5), (3, 9)) else ''
            cells.append(f'<div class="{cls}"><i style="{st}">{mark}{lock}</i></div>')
        muted = l in (11,)
        rows.append(f'''<div class="lane{' mut' if muted else ''}{' cur' if l==sel[0] else ''}" style="height:{rowh}px">
<div class="lh"><u style="background:{col}"></u><b>{l+1}</b><span>{name}</span>
<div class="ms"><i class="{'m' if muted else ''}">M</i><i class="{'s' if l==99 else ''}">S</i></div><em class="{'off' if l in (12,13,14,15) and False else ''}"></em></div>
<div class="cells" style="grid-template-columns:repeat({steps},1fr)">{''.join(cells)}</div></div>''')
    ruler = "".join(f'<div class="{"b" if s % (steps//4)==0 else ""}{" ph2" if s==play else ""}">{s+1 if steps==16 or s%(steps//16)==0 else ""}</div>' for s in range(steps))
    return f'<div class="grid"><div class="ruler"><div class="lh"></div><div class="cells" style="grid-template-columns:repeat({steps},1fr)">{ruler}</div></div>{"".join(rows)}</div>'

GRID_CSS = """
.grid{display:flex;flex-direction:column}
.lane,.ruler{display:flex;align-items:stretch}
.lh{width:var(--lh,236px);flex:none;display:flex;align-items:center;gap:8px;padding:0 10px 0 8px;font-size:14px}
.lh u{width:4px;height:18px;border-radius:2px}.lh b{width:18px;color:var(--t3);font-weight:500;font-size:12px;text-align:right}
.lh span{flex:1;white-space:nowrap}.lane.cur .lh{background:#232c37;border-radius:6px 0 0 6px}.lane.cur .lh span{color:#fff;font-weight:600}
.lane.mut .lh span{color:var(--t3);text-decoration:line-through}.lane.mut .cells{opacity:.35}
.lh em{width:28px;height:16px;border-radius:8px;background:var(--cyan);position:relative;flex:none}
.lh em:after{content:"";position:absolute;right:2px;top:2px;width:12px;height:12px;border-radius:50%;background:#0d1a1c}
.cells{flex:1;display:grid;gap:0;min-width:0}
.c{padding:3px 2px;border-left:1px solid transparent}.c i{display:block;height:100%;border-radius:4px;background:#222a34;position:relative}
.c.alt i{background:#1e252e}.c.selc i{outline:2px solid #F0F2F4;outline-offset:1px}
.c s{position:absolute;left:50%;top:50%;width:6px;height:6px;margin:-3px;border-radius:50%;background:#16100a}
.c q{position:absolute;right:3px;top:3px;width:6px;height:6px;border:1.5px solid #0b1516;border-radius:1px}
.ruler{height:26px;color:var(--t3);font-size:12px}.ruler .cells div{display:flex;align-items:center;justify-content:center}
.ruler .b{color:var(--t1);font-weight:600}.ruler .ph2{background:var(--cyan);color:#06181a;border-radius:4px;font-weight:700}
.tb{display:flex;align-items:center;gap:10px;padding:10px 14px;border-bottom:1px solid var(--line);flex-wrap:nowrap}
.tb .sep{width:1px;height:24px;background:var(--line);margin:0 2px}
.bars{display:inline-flex;gap:2px}.bars i{font-style:normal;width:32px;height:32px;border-radius:6px;display:flex;align-items:center;justify-content:center;background:var(--bg);border:1px solid var(--line);color:var(--t2)}
.bars i.on{background:#33404f;color:#fff;border-color:#46566a}.bars i.pl{box-shadow:inset 0 -3px 0 var(--cyan)}
.insp{display:flex;align-items:center;gap:22px;padding:8px 16px;border-top:1px solid var(--line)}
.insp .w{width:170px}.insp .who{width:210px}.insp .who b{font-size:16px}.insp .who span{display:block;color:var(--t3);font-size:12px;margin-top:2px}
.exp{display:flex;align-items:center;gap:8px;margin-left:auto}
.drag{height:36px;padding:0 14px 0 10px;border-radius:8px;border:1.5px dashed var(--amber);color:var(--amber);display:inline-flex;align-items:center;gap:8px;font-weight:600;background:rgba(255,170,59,.07)}
.drag:before{content:"";width:10px;height:16px;background:radial-gradient(circle,var(--amber) 1.4px,transparent 1.6px) 0 0/5px 5px}
.tl{display:grid;grid-template-columns:repeat(16,1fr);gap:3px;padding:12px 14px}
.tl div{height:34px;border-radius:5px;background:#222a34;font-size:12px;padding:3px 6px;display:flex;flex-direction:column;justify-content:center;line-height:1.15;position:relative;color:var(--t2)}
.tl div b{font-size:12px;color:#fff;font-weight:600}.tl div.em{background:#171c23;border:1px dashed #2e3845}
.tl div.un{background:#3b4452;border:1px solid #6b7888}.tl div.sl{outline:2px solid #F0F2F4;outline-offset:1px}
.tl div.pl:after{content:"";position:absolute;left:0;top:-4px;bottom:-4px;width:2px;background:var(--cyan)}
.tl div.lp{box-shadow:inset 0 -3px 0 var(--amber)}
"""

EXPORT = lambda scope: f"""<div class="exp"><span class="lbl">MIDI</span><div class="seg"><i class="{'on' if scope=='Pattern' else ''}">Pattern</i><i class="{'on' if scope!='Pattern' else ''}">Entire Song</i></div><span class="ib">Save .mid…</span><span class="drag">Drag to DAW</span></div>"""

def groove_row(crumb, scope="Pattern", extra=""):
    return f"""<div class="tb"><span class="crumb">{crumb}</span>{extra}<span class="sep"></span>
<div style="width:120px">{slider("Swing","20 %",40)}</div><div style="width:120px">{slider("Accent","50 %",50)}</div>
<span class="sel">Seq: Internal</span>{EXPORT(scope)}</div>"""

def seq_toolbar(song=False, compact=False):
    if not song:
        tail = '<span class="sel">Bar actions</span>' if compact else '<span class="ib">Duplicate bar</span><span class="ib">Clear bar</span><span class="ib">New seed</span>'
        return f"""<div class="tb"><span class="lbl">Pattern</span><span class="sel" style="min-width:190px">P2 · Full-on Driving</span>
<div class="seg"><i class="on">A</i><i>B</i></div><span class="ib">Swap A/B</span><span class="ib">Copy A → B</span><span class="sep"></span>
<span class="lbl">Bar</span><div class="bars"><i class="on pl">1</i><i>2</i><i>3</i><i>4</i></div>
<span class="sel">16 steps</span><span class="sel">4 bars</span><span class="sep"></span>{tail}</div>"""
    return f"""<div class="tb"><span class="lbl">Song</span><span class="sel">64 bars</span><span class="sep"></span>
<span class="lbl">Place</span><span class="sel">P2 · Full-on Driving</span><div class="seg"><i class="on">A</i><i>B</i></div><span class="ib pri">Place at bar 37</span><span class="sep"></span>
<span class="ib">Insert</span><span class="ib">Delete</span><span class="ib">Duplicate</span><span class="ib">Clear</span><span class="ib">Copy</span><span class="ib dis">Paste</span><span class="ib">Make unique</span><span class="sep"></span>
<span class="ib tog">Loop</span><span class="ib">Loop = selection</span></div>"""

def inspector(who="Closed Hat · step 5", sub="Pattern 2 A · bar 1"):
    return f"""<div class="insp"><div class="who"><b>{who}</b><span>{sub}</span></div>
<div class="w">{slider("Velocity","96",75)}</div><div class="w">{slider("Chance","100 %",100)}</div>
<div class="w">{slider("Ratchet","1×",4)}</div><div class="w">{slider("Micro","+10 %",60)}</div>
<span class="ib tog">Accent</span><span class="ib">Lock</span>
<span style="margin-left:auto;font-size:12px;color:var(--t3)">Arrows move · Space toggles · Shift-drag = fine · double-click resets</span></div>"""

def timeline(bars=64, sel=(36, 1), play=38, loop=(32, 8)):
    cols = ["#4C9BF0", "#FF8A3B", "#7FD66B", "#9B7CE8", "#E8B04A"]
    out = []
    for b in range(bars):
        slot = (b // 8) % 5
        var = (b // 4) % 2
        cls, txt, style = "", "", ""
        if b in (20, 39, 63): cls, txt = "un", "Unique"
        elif b in (30, 31): cls, txt = "em", "empty"
        else:
            txt = f"P{slot+1}{'B' if var else 'A'} · {b%4+1}"
            style = f"background:{cols[slot]}{'55' if not var else '88'}"
        if sel[0] <= b < sel[0] + sel[1]: cls += " sl"
        if b == play: cls += " pl"
        if loop[0] <= b < loop[0] + loop[1]: cls += " lp"
        out.append(f'<div class="{cls}" style="{style}"><b>{b+1}</b>{txt}</div>')
    return f'<div class="tl">{"".join(out)}</div>'

def sequence_pattern(w=1440, h=900, name="01-sequence-pattern", with_notes=True, rowh=30, compact=False):
    n = ""
    if with_notes:
        n = notes([(250, 232, "Lane header: colour, number, name, Mute and Solo (host parameters), lane switch (drm.seq.lane.*). Clicking a lane selects that pad in every workspace."),
                   (1180, 176, "Export is explicit: the scope, then two separate actions. Save opens a file dialog; Drag to DAW is a drag handle only and never opens a dialog."),
                   (640, 800, "Step inspector: sliders with value and unit. Lock protects the step from Generate. A dot marks an accent, a corner mark a locked step."),
                   (1290, 108, "Generate, Routing and Settings are drawers over the right side, opened from any workspace; they are not pages.")])
    lh = "--lh:196px;" if compact else ""
    body = f"""{chrome()}{nav("SEQUENCE")}
<div class="body" style="padding-bottom:0;{lh}"><div class="panel" style="flex:1">{seq_toolbar(compact=compact)}{groove_row("Pattern 2 A › <b>Bar 1</b>")}
<div style="padding:8px 10px 10px 6px">{grid(rowh=rowh)}</div>{inspector()}</div></div>{n}"""
    page(name, w, h, body, GRID_CSS)

def sequence_song():
    n = notes([(300, 392, "Breadcrumb states what the grid edits: this song bar (it becomes unique on the first change) or its source pattern."),
               (760, 250, "Timeline: 4 rows of 16 at 64 bars (2 rows at 32, 1 at 16). Colour = pattern slot, stronger = B, grey outline = unique, dashed = empty, amber underline = loop, cyan line = playhead, white outline = selection."),
               (1180, 392, "Entire Song export always writes the whole song once; the loop region does not change it.")])
    crumb = 'Song › <b>Bar 37</b> · <em>P5 B · bar 1</em>'
    extra = '<div class="seg" style="margin-left:12px"><i class="on">Edit this bar</i><i>Edit source pattern</i></div><span class="sel">32 steps</span>'
    body = f"""{chrome(mode="Song", bar="Song bar 39")}{nav("SEQUENCE")}
<div class="body" style="padding-bottom:0"><div class="panel" style="flex:1">{seq_toolbar(True)}{timeline()}
<div style="border-top:1px solid var(--line)">{groove_row(crumb, "Song", extra)}</div>
<div style="padding:6px 10px 8px 6px">{grid(steps=32, seed=8, rowh=19, sel=(8, 14), play=11)}</div>{inspector("Perc 1 · step 15","Song bar 37")}</div></div>{n}"""
    page("02-sequence-song", 1440, 900, body, GRID_CSS)

# ---------------------------------------------------------------- Play / Kit
PLAY_CSS = """
.pads{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;padding:14px}
.pad{height:124px;border-radius:8px;background:#222a34;border:1px solid var(--line);padding:10px 12px;display:flex;flex-direction:column;position:relative}
.pad u{position:absolute;left:0;top:10px;bottom:10px;width:4px;border-radius:2px}
.pad b{font-size:17px;font-weight:600}.pad small{color:var(--t3);font-size:12px}.pad .n{color:var(--t3);font-size:12px}
.pad .ft{margin-top:auto;display:flex;justify-content:space-between;align-items:center}
.pad.cur{border-color:#F0F2F4;background:#2a3440}.pad.hit{box-shadow:inset 0 0 0 2px var(--hc)}.pad.mut b{color:var(--t3);text-decoration:line-through}
.pad.miss:after{content:"sample missing";position:absolute;right:10px;top:10px;font-size:11px;color:var(--red)}
.sec{border-bottom:1px solid var(--line);padding:9px 14px}.sec h4{font-size:12px;letter-spacing:.06em;text-transform:uppercase;color:var(--t2);font-weight:600;margin-bottom:10px;display:flex;justify-content:space-between}
.sec h4 span{color:var(--t3);font-weight:400;text-transform:none;letter-spacing:0}
.g3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px 16px}.g4{display:grid;grid-template-columns:repeat(4,1fr);gap:8px 14px}.g5{display:grid;grid-template-columns:repeat(5,1fr);gap:8px 14px;align-items:end}
.wave{height:72px;border-radius:6px;background:#12171d;border:1px solid var(--line);position:relative;overflow:hidden}
.wave svg{position:absolute;inset:0;width:100%;height:100%}
.f{display:flex;flex-direction:column;gap:4px}.f .sel{width:100%}
"""
def pad_grid():
    out = []
    for i, (name, note, col) in enumerate(PADS):
        cls = "pad" + (" cur" if i == 0 else "") + (" hit" if i in (3,) else "") + (" mut" if i == 11 else "") + (" miss" if i == 9 else "")
        out.append(f'''<div class="{cls}" style="--hc:{col}"><u style="background:{col}"></u><small class="n">{i+1}</small><b>{name}</b><small>{note}</small>
<div class="ft"><span class="chip {'ok' if i in (4,5) else ''}">{'Out 3' if i in (4,5) else 'Main'}</span><div class="ms"><i class="{'m' if i==11 else ''}">M</i><i>S</i></div></div></div>''')
    return f'<div class="pads">{"".join(out)}</div>'

def wave():
    import math
    pts = []
    for x in range(0, 400):
        a = math.exp(-x / 110) * (0.9 if x > 6 else x / 6)
        y = 46 - a * 40 * math.sin(x * (0.55 - x * 0.0009))
        pts.append(f"{x},{y:.1f}")
    return f'<div class="wave"><svg viewBox="0 0 400 92" preserveAspectRatio="none"><polyline points="{" ".join(pts)}" fill="none" stroke="#FF8A3B" stroke-width="1.4"/><line x1="8" y1="0" x2="8" y2="92" stroke="#F0F2F4"/><line x1="330" y1="0" x2="330" y2="92" stroke="#F0F2F4"/></svg></div>'

def play_kit():
    macros = "".join(f'<div style="width:150px">{slider(l, v, p)}</div>' for l, v, p in [("Kit volume", "60 %", 60), ("Velocity sens", "80 %", 80), ("Swing", "20 %", 40), ("Accent", "50 %", 50), ("Delay feedback", "35 %", 41), ("Delay return", "80 %", 80), ("Reverb size", "50 %", 50), ("Reverb return", "80 %", 80)])
    insp = f"""<div class="ph"><u style="width:4px;height:18px;background:#FF8A3B;border-radius:2px"></u><b>Pad 1 · Kick</b><span class="chip" style="margin-left:auto">C1 (36)</span><span class="ib" style="height:28px">‹</span><span class="ib" style="height:28px">›</span></div>
<div class="sec"><h4>Sound<span>model and tone</span></h4>
<div class="g4" style="margin-bottom:12px"><div class="f"><span class="lbl">Model</span><span class="sel">Kick</span></div><div class="f"><span class="lbl">Note</span><span class="sel">C1 (36)</span></div>
<div class="f"><span class="lbl">Choke</span><span class="sel">Off</span></div><div class="f"><span class="lbl">Voices</span><span class="sel">Mono</span></div></div>
<div class="g3">{slider("Level","85 %",85)}{slider("Tune","0.0 st",50)}{slider("Fine","0 ct",50)}{slider("Decay","380 ms",45)}{slider("Pan","C",50)}{slider("Character","50 %",50)}</div></div>
<div class="sec"><h4>Filter and transient</h4><div class="g5"><div class="f"><span class="lbl">Filter</span><span class="sel">Off</span></div>
{slider("Cutoff","20.0 kHz",100)}{slider("Resonance","0 %",0)}{slider("Attack","+20 %",60)}{slider("Sustain","−10 %",45)}</div></div>
<div class="sec"><h4>Kick body<span>Kick model only</span></h4><div class="g5">{slider("Pitch env","36.0 st",75)}{slider("Pitch decay","22 ms",30)}{slider("Drive","2.20×",20)}{slider("Sub","25 %",25)}{slider("Tail","200 ms",25)}</div></div>
<div class="sec" style="border-bottom:0"><h4>Sample<span>two players, A and B</span></h4>
<div class="row" style="margin-bottom:8px"><span class="sel">Source: Layer</span><span class="sel">Players: A + B</span><div class="seg"><i class="on">A</i><i>B</i></div>
<span class="ib pri" style="height:32px;margin-left:auto">Load…</span><span class="ib" style="height:32px">Clear</span><span class="ib" style="height:32px">To library</span></div>
{wave()}<div class="row" style="margin:5px 0 8px;justify-content:space-between"><span style="font-size:12px;color:var(--t2)">kick_psy_01.wav · 48 kHz · stereo · 0.512 s</span><span class="chip ok">loaded</span></div>
<div class="g4">{slider("Start","2 %",2)}{slider("End","82 %",82)}{slider("Sample level","100 %",50)}{slider("Velocity split","vel 64",50)}</div>
<div class="row" style="margin-top:8px"><span class="ib tog" style="height:30px">Loop</span><span class="ib" style="height:30px">Reverse</span><span class="ib tog" style="height:30px">Normalize</span></div></div>"""
    n = notes([(40, 160, "Click selects and auditions the pad (as today). A pad shows number, name, note, output, Mute, Solo, a hit flash and a missing-sample warning."),
               (880, 112, "The Sound inspector replaces the SOUND page: every existing pad control, grouped. Kick body appears only for the Kick model."),
               (40, 730, "Macros: the eight existing host parameters as sliders. No new parameters.")])
    body = f"""{chrome(status="Stopped", bar="Bar 1.1")}{nav("PLAY / KIT")}
<div class="body"><div style="flex:1;display:flex;flex-direction:column;gap:16px">
<div class="panel"><div class="ph">Kit<b>16 pads</b><span class="sel" style="margin-left:auto;text-transform:none;letter-spacing:0">Load factory kit…</span><span class="ib" style="height:30px;text-transform:none">Open Library</span></div>{pad_grid()}</div>
<div class="panel"><div class="ph">Macros</div><div style="display:flex;flex-wrap:wrap;gap:16px 22px;padding:14px">{macros}</div></div></div>
<div class="panel" style="width:640px;align-self:flex-start">{insp}</div></div>{n}"""
    page("03-play-kit-sound", 1440, 900, body, PLAY_CSS)

# ---------------------------------------------------------------- Mix
MIX_CSS = """
.strips{display:flex;gap:6px;padding:12px}
.st{flex:1;min-width:0;background:#222a34;border:1px solid var(--line);border-radius:8px;padding:8px 6px;display:flex;flex-direction:column;align-items:center;gap:8px}
.st.cur{border-color:#F0F2F4}.st h5{font-size:12px;font-weight:600;width:100%;text-align:center;padding-bottom:5px;border-bottom:3px solid var(--c);white-space:nowrap}
.st h5 small{display:block;color:var(--t3);font-weight:400}
.fd{height:200px;display:flex;gap:7px;align-items:stretch}
.fd .t{width:8px;border-radius:4px;background:#12171d;position:relative}.fd .t i{position:absolute;left:-7px;width:22px;height:12px;border-radius:3px;background:#F0F2F4}
.fd .m{width:7px;border-radius:3px;background:#12171d;position:relative;overflow:hidden}.fd .m i{position:absolute;left:0;right:0;bottom:0;background:linear-gradient(to top,var(--c),var(--c) 80%,#F0584C)}
.st .v{font-size:12px;color:var(--t1)}.mini{width:100%}.mini .hd{font-size:11px}.mini .hd b{font-size:11px}
.st .out{width:100%;height:24px;border-radius:5px;border:1px solid var(--line);font-size:11px;display:flex;align-items:center;justify-content:center;color:var(--t2)}
.st .out.fb{border-color:#5a4a2a;color:var(--amber)}
.rt{width:100%;border-collapse:collapse;font-size:13px}.rt th{font-size:11px;color:var(--t3);font-weight:500;padding:6px 0;text-transform:uppercase}
.rt td{padding:3px 0;text-align:center;border-top:1px solid #232b35}.rt td:first-child{text-align:left;padding-left:14px;white-space:nowrap}
.rt .o{display:inline-block;width:16px;height:16px;border-radius:50%;border:1.5px solid #46566a}.rt .o.on{background:var(--amber);border-color:var(--amber)}
.rt .o.na{border-style:dashed;border-color:#3a4656}.rt .o.on.na{background:#6b5326;border-color:#6b5326}
.rt th.na{color:#4d5763}
"""
def strip(i, sel=False):
    name, note, col = PADS[i]
    lvl = [85, 60, 55, 50, 45, 60, 50, 40, 60, 60, 60, 60, 60, 60, 60, 60][i]
    met = [70, 20, 35, 55, 30, 0, 12, 5, 0, 18, 0, 0, 0, 0, 0, 0][i]
    out = "Main" if i not in (4, 5, 7) else ("Out 3" if i != 7 else "Out 6 → Main")
    return f'''<div class="st{' cur' if sel else ''}" style="--c:{col}"><h5>{SHORT[i]}<small>{i+1}</small></h5>
<div class="fd"><div class="t"><i style="bottom:{lvl*1.8}px"></i></div><div class="m"><i style="height:{met}%"></i></div></div><span class="v">{lvl} %</span>
<div class="mini">{slider("Pan", "C" if i%3 else "L 20", 50 if i%3 else 40, "#A9B3BF")}</div>
<div class="mini">{slider("Dly", f"{[0,15,10,0,20,15,25,0][i%8]} %", [0,15,10,0,20,15,25,0][i%8], "#32C5D2")}</div>
<div class="mini">{slider("Rev", f"{[0,15,20,0,10,10,0,30][i%8]} %", [0,15,20,0,10,10,0,30][i%8], "#9B7CE8")}</div>
<div class="ms"><i class="{'m' if i==11 else ''}">M</i><i class="{'s' if i==0 and False else ''}">S</i></div>
<div class="out{' fb' if i==7 else ''}">{out}</div></div>'''

def routing_drawer():
    head = "".join(f'<th class="{"na" if o in (1,3,4,6,7,8) else ""}">{"Main" if o==0 else o+1}</th>' for o in range(9))
    rows = []
    outs = [0, 0, 0, 0, 2, 2, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0]
    for i, (name, note, col) in enumerate(PADS):
        cells = "".join(f'<td><span class="o{" on" if outs[i]==o else ""}{" na" if o in (1,3,4,6,7,8) else ""}"></span></td>' for o in range(9))
        rows.append(f'<tr><td><span style="color:{col}">●</span> {i+1} {name}</td><td><span class="sel" style="height:26px;font-size:12px">{note}</span></td>{cells}</tr>')
    return f"""<div class="drawer" style="width:600px"><div class="ph"><b>Routing</b><span class="ib" style="margin-left:auto;height:28px">Close</span></div>
<div style="padding:12px 14px;display:flex;gap:8px;flex-wrap:wrap;align-items:center"><span class="lbl">Host outputs</span><span class="chip ok">Main</span><span class="chip off">Out 2</span><span class="chip ok">Out 3</span><span class="chip off">Out 4</span><span class="chip off">Out 5</span><span class="chip ok">Out 6</span><span class="chip off">Out 7</span><span class="chip off">Out 8</span><span class="chip off">Out 9</span></div>
<div style="padding:0 14px 8px;font-size:12px;color:var(--t2)">Greyed columns are outputs the host has not enabled. A pad assigned there <b style="color:var(--amber)">plays on Main</b> until the host enables it. Sends always return to Main.</div>
<table class="rt"><tr><th style="text-align:left;padding-left:14px">Pad</th><th>MIDI note</th>{head}</tr>{"".join(rows)}</table>
<div style="padding:12px 14px;display:flex;gap:12px;align-items:center;border-top:1px solid var(--line);margin-top:8px"><div><div class="lbl" style="margin-bottom:4px">Sequencer</div><span class="sel">Internal</span></div>
<div style="font-size:13px;color:var(--t2)">Host playing · 145.0 BPM · Pattern mode</div></div></div>"""

def mix(drawer=True, name="04-mix-routing"):
    col = lambda items, c: "".join(f'<div style="width:150px">{slider(l, v, p, c)}</div>' for l, v, p in items)
    fx = f"""<div class="panel"><div class="ph">FX returns<span class="chip" style="text-transform:none">return to Main only</span></div>
<div style="display:flex;gap:28px;padding:14px;align-items:flex-end"><div class="lbl" style="width:90px;color:var(--cyan)">Delay<br>send 1</div>
<div class="f"><span class="lbl">Time</span><span class="sel">1/8 dotted</span></div>{col([("Feedback","35 %",41),("Tone","50 %",50),("Return","80 %",80)],"#32C5D2")}
<div class="sep" style="width:1px;height:40px;background:var(--line)"></div><div class="lbl" style="width:90px;color:var(--violet)">Reverb<br>send 2</div>
{col([("Size","50 %",50),("Damp","50 %",50),("Return","80 %",80)],"#9B7CE8")}</div></div>"""
    master = f"""<div class="panel" style="width:120px"><div class="ph">Main</div><div style="padding:12px;display:flex;flex-direction:column;align-items:center;gap:8px">
<div class="fd" style="height:400px;--c:#F0F2F4"><div class="t"><i style="bottom:230px"></i></div><div class="m"><i style="height:62%"></i></div><div class="m"><i style="height:58%"></i></div></div>
<b>60 %</b><span class="lbl">Kit volume</span></div></div>"""
    n = notes([(60, 150, "16 strips at 71 px on a 1440 canvas. Below about 1360 px the strips switch to banks of 8 (1–8, 9–16) instead of shrinking."),
               (566, 610, "Out 6 → Main: the pad is assigned to an output the host has not enabled, so it plays on Main. It is never shown as active."),
               (900, 150, "Routing drawer: pad × MIDI note × Main / Out 2–9. Greyed columns are outputs the host has not enabled.")][:2] if drawer else
              [(60, 150, "Level is a full-length fader with a meter in the pad colour; pan and the two sends are sliders with values."),
               (1330, 150, "Main is separate: kit volume and the L/R meter after the returns."),
               (60, 700, "Both returns go to Main only; send 1 is cyan, send 2 violet, here and on every strip.")], 620 if drawer else 20)
    body = f"""{chrome()}{nav("MIX", "Routing" if drawer else None)}
<div class="body" style="flex-direction:column;gap:12px"><div style="display:flex;gap:16px"><div class="panel" style="flex:1"><div class="ph">Channels<b>16 pads</b><div class="seg" style="margin-left:auto;text-transform:none"><i class="on">1–16</i><i>1–8</i><i>9–16</i></div></div>
<div class="strips">{"".join(strip(i, i==0) for i in range(16))}</div></div>{master}</div>{fx}</div>{routing_drawer() if drawer else ''}{n}"""
    page(name, 1440, 900, body, MIX_CSS + PLAY_CSS)

# ---------------------------------------------------------------- Library, drawers, states
LIB_CSS = """
.li{display:flex;align-items:center;gap:10px;height:40px;padding:0 14px;border-bottom:1px solid #232b35;font-size:14px}
.li.on{background:#2a3440;box-shadow:inset 3px 0 0 var(--amber)}.li small{color:var(--t3);margin-left:auto}
.li .tg{width:54px;font-size:11px;color:var(--t3);text-transform:uppercase;letter-spacing:.05em}
.ty{display:flex;flex-direction:column;gap:4px;padding:12px}.ty i{font-style:normal;height:40px;border-radius:8px;padding:0 12px;display:flex;align-items:center;justify-content:space-between;color:var(--t2)}
.ty i.on{background:var(--s2);color:#fff;box-shadow:inset 3px 0 0 var(--amber)}.ty i small{color:var(--t3)}
"""
def library():
    items = [("Factory", "Psy Full-on", "Psy / Full-on", True), ("Factory", "House Techno", "House / Techno", False), ("Factory", "Hip-hop Breaks", "Hip-hop / Breaks", False),
             ("User", "My Kit 01", "saved 2026-11-10", False), ("User", "Dark Room", "saved 2026-11-09", False)]
    lst = "".join(f'<div class="li{" on" if on else ""}"><span class="tg">{t}</span><b style="font-weight:500">{n}</b><small>{s}</small></div>' for t, n, s, on in items)
    n = notes([(40, 150, "Four content types only. What a preset of each type replaces and keeps is stated before loading."),
               (1000, 640, "A file of another type than the one selected is refused, and the message says what the file is (existing behaviour)."),
               (640, 420, "No search, tags or favourites: the product does not store them. Rename and delete are backlog (issue 126) and are not drawn.")])
    body = f"""{chrome(status="Stopped", bar="Bar 1.1")}{nav("LIBRARY")}
<div class="body"><div class="panel" style="width:240px"><div class="ph">Content</div><div class="ty"><i class="on">Kits<small>3 factory · 2 user</small></i><i>Pads<small>0</small></i><i>Patterns<small>4 user</small></i><i>Songs<small>3 factory · 1 user</small></i></div>
<div style="padding:0 14px 14px;font-size:12px;color:var(--t3);line-height:1.5">Kit: 16 pads, mix, sends, returns.<br>Pad: one pad, onto the selected pad.<br>Pattern: A and B, into the selected slot.<br>Song: five patterns, variations, timeline.</div></div>
<div class="panel" style="flex:1"><div class="ph">Kits<span class="chip" style="margin-left:auto;text-transform:none">loads into: whole kit</span></div>{lst}
<div style="padding:40px 14px;color:var(--t3);font-size:13px;text-align:center">Empty state (e.g. Pads): "No user pads yet. Save the selected pad with the button on the right."</div></div>
<div class="panel" style="width:420px"><div class="ph">Psy Full-on<span class="chip ok" style="margin-left:auto;text-transform:none">factory</span></div>
<div class="sec"><div style="font-size:13px;color:var(--t2);line-height:1.5">Family Psy / Full-on · 16 synthesized pads · written for 145 BPM.<br>Replaces: pad sounds, mix, sends, delay and reverb. Keeps: patterns, song, mutes, sequencer.</div>
<div class="row" style="margin-top:12px"><span class="ib pri">Load</span><span class="ib">Undo Load</span></div></div>
<div class="sec"><h4>Save</h4><div class="row"><span class="sel" style="flex:1;padding-right:10px">My Preset</span><span class="ib">Save kit</span></div>
<div style="font-size:12px;color:var(--t3);margin-top:8px">Saves to the user folder. A failed save says why and writes nothing.</div></div>
<div class="sec"><h4>Portable kit<span>kit + its sample files</span></h4><div class="row"><span class="ib">Import .voxkit…</span><span class="ib">Export .voxkit…</span></div></div>
<div class="sec" style="border-bottom:0"><h4>Status</h4><div style="display:flex;flex-direction:column;gap:6px;font-size:13px"><div><span class="chip ok">ok</span> Loaded Psy Full-on</div>
<div><span class="chip bad">missing</span> Pad 10 A: perc_shaker.wav not found; reference kept</div><div><span class="chip bad">refused</span> "x.voxpad" is a Song preset, not a Pad preset. Nothing changed.</div></div></div></div></div>{n}"""
    page("05-library", 1440, 900, body, LIB_CSS + PLAY_CSS)

def generate_drawer():
    amounts = "".join(slider(l, f"{v} %", v) for l, v in [("Density", 60), ("Groove", 50), ("Syncopation", 35), ("Swing", 0), ("Humanize", 15), ("Complexity", 40), ("Fills", 50), ("Variation", 40)])
    lanes = "".join(f'<span class="ib {"tog" if i in (0,2) else ""}" style="width:44px;height:30px;padding:0">{i+1}</span>' for i in range(16))
    targets = "".join(f'<span class="ib {"tog" if i>=3 else ""}" style="width:44px;height:30px;padding:0">{i+1}</span>' for i in range(16))
    dr = f"""<div class="drawer"><div class="ph"><b>Generate</b><span class="chip" style="margin-left:auto;text-transform:none">Seed 1836745</span><span class="ib" style="height:28px">Close</span></div>
<div class="sec"><div class="f" style="margin-bottom:12px"><span class="lbl">Style</span><span class="sel">Full-on / Psy</span></div><div style="display:grid;grid-template-columns:1fr 1fr;gap:12px 18px">{amounts}</div></div>
<div class="sec"><h4>Locked lanes<span>Generate leaves them as they are</span></h4><div style="display:flex;flex-wrap:wrap;gap:6px">{lanes}</div></div>
<div class="sec"><h4>Lanes to mutate</h4><div style="display:flex;flex-wrap:wrap;gap:6px">{targets}</div></div>
<div class="sec"><h4>Pattern 2 A<span>replaces unlocked steps</span></h4><div class="row" style="flex-wrap:wrap"><span class="ib pri">Generate pattern</span><span class="ib">Mutate lanes</span><span class="ib">Fill</span><span class="ib">Variation A → B</span></div></div>
<div class="sec" style="border-bottom:0"><h4>Whole song<span style="color:var(--amber)">replaces all 5 patterns and the timeline</span></h4><div class="row"><span class="ib">Generate song…</span><span style="font-size:12px;color:var(--t3)">asks to confirm; one Undo restores everything</span></div>
<div style="margin-top:14px;font-size:13px;color:var(--t2)"><span class="chip ok">done</span> Pattern generated (seed 1836745)</div></div></div>"""
    n = notes([(960, 150, "The drawer covers the right 440 px. The grid stays visible and updates on every Generate, so the result is seen without leaving the sequencer."),
               (1010, 640, "Each action names what it replaces. Generate song replaces all five patterns and the timeline, so it asks first; one Undo restores everything."),
               (1010, 470, "Locked lanes and locked steps are never changed; if everything targeted is locked the status says that nothing changed.")], 460)
    body = f"""{chrome()}{nav("SEQUENCE", "Generate")}
<div class="body" style="padding-bottom:0"><div class="panel" style="flex:1">{seq_toolbar()}
{groove_row("Pattern 2 A › <b>Bar 1</b>")}<div style="padding:8px 10px 10px 6px">{grid(seed=11, rowh=30)}</div>{inspector()}</div></div>{dr}{n}"""
    page("06-generate-drawer", 1440, 900, body, GRID_CSS + PLAY_CSS)

def settings_drawer():
    dr = f"""<div class="drawer"><div class="ph"><b>Settings and diagnostics</b><span class="ib" style="margin-left:auto;height:28px">Close</span></div>
<div class="sec"><h4>Engine</h4><div class="g3" style="grid-template-columns:1fr 1fr"><div class="f"><span class="lbl">Voice limit</span><span class="sel">64 voices</span></div>
{slider("Velocity sensitivity","80 %",80)}</div><div class="row" style="margin-top:12px"><span class="ib tog">Choke groups</span><span class="ib warn">Panic: stop all sound</span></div></div>
<div class="sec" style="border-bottom:0"><h4>Diagnostics<span>read-only</span></h4>
<div style="display:grid;grid-template-columns:1fr auto;gap:7px 12px;font-size:13px;color:var(--t2)">
<span>Active voices</span><b style="color:var(--t1)">12</b><span>Dropped MIDI events</span><b style="color:var(--t1)">0</b><span>Sample memory</span><b style="color:var(--t1)">3.1 MB of 512.0 MB</b>
<span>Host sample rate / block</span><b style="color:var(--t1)">48000 Hz / 256</b><span>Outputs enabled by the host</span><b style="color:var(--t1)">Main, Out 3, Out 6</b>
<span>Version / state schema</span><b style="color:var(--t1)">0.1.0 / 1</b><span>State sections</span><b style="color:var(--t1)">DPRM DPAD(2) DSMP(2) DSEQ(3) DSNG(1) DGEN(1) DUIS(1)</b></div>
<div style="margin-top:12px;font-size:13px"><span class="chip bad">last rejected state</span> <span style="color:var(--t2)">the state holds 614416 bytes in sections of an unknown writer: too large to keep and still save this project; nothing was restored</span></div>
<div class="row" style="margin-top:14px"><span class="ib">Copy diagnostics</span><span style="font-size:12px;color:var(--t3)">new, text only; needs owner approval</span></div></div></div>"""
    body = f"""{chrome()}{nav("PLAY / KIT", "Settings")}
<div class="body"><div style="flex:1" class="panel"><div class="ph">Kit<b>16 pads</b></div>{pad_grid()}</div><div class="panel" style="width:560px;opacity:.5"><div class="ph"><b>Pad 1 · Kick</b></div></div></div>{dr}"""
    page("07-settings-drawer", 1440, 900, body, PLAY_CSS)

def states():
    sw = "".join(f'<div style="width:150px"><div style="height:56px;border-radius:8px;background:{h};border:1px solid #2E3845"></div><div style="font-size:12px;margin-top:6px"><b>{n}</b><br><span style="color:var(--t3)">{h}</span></div></div>'
                 for n, h in [("bg", "#101318"), ("surface 1", "#1C232C"), ("surface 2", "#242D38"), ("line", "#2E3845"), ("text 1", "#F0F2F4"), ("text 2", "#A9B3BF"), ("text 3", "#74808D"), ("amber · primary", "#FFAA3B"), ("cyan · transport", "#32C5D2"), ("violet · reverb", "#9B7CE8"), ("red · error/clip", "#F0584C")])
    pc = "".join(f'<div style="width:78px"><div style="height:26px;border-radius:6px;background:{c}"></div><div style="font-size:11px;margin-top:4px">{i+1} {SHORT[i]}<br><span style="color:var(--t3)">{c}</span></div></div>' for i, (n, nn, c) in enumerate(PADS))
    def cell(title, inner): return f'<div><div class="lbl" style="margin-bottom:8px">{title}</div>{inner}</div>'
    stp = lambda st, extra="": f'<div class="c" style="width:56px;height:34px;padding:0"><i style="{st}">{extra}</i></div>'
    comp = "".join([
        cell("Button: normal / primary / toggled / disabled / danger", '<div class="row"><span class="ib">Clear</span><span class="ib pri">Load</span><span class="ib tog">Loop</span><span class="ib dis">Paste</span><span class="ib warn">Panic</span></div>'),
        cell("Segmented (2-state mode)", '<div class="seg"><i class="on">Pattern</i><i>Song</i></div>'),
        cell("Step: empty / on / accent / locked / selected / playhead column", f'<div class="row">{stp("")}{stp("background:#32C5D2;opacity:.6")}{stp("background:#FF8A3B","<s></s>")}{stp("background:#32C5D2","<q></q>")}<div class="c selc" style="width:56px;height:34px;padding:0"><i style="background:#9B7CE8"></i></div><div style="width:56px;height:34px;background:rgba(50,197,210,.16);border-radius:4px"></div></div>'),
        cell("Slider with value and unit (double-click resets, Shift = fine, type-in)", f'<div style="width:220px">{slider("Decay","380 ms",45)}</div>'),
        cell("Mute / Solo: off, muted, soloed", '<div class="row"><div class="ms"><i>M</i><i>S</i></div><div class="ms"><i class="m">M</i><i>S</i></div><div class="ms"><i>M</i><i class="s">S</i></div></div>'),
        cell("Output chip: Main / enabled output / assigned but not enabled", '<div class="row"><span class="chip">Main</span><span class="chip ok">Out 3</span><span class="chip" style="color:var(--amber);background:#3a2f1a">Out 6 → Main</span></div>'),
        cell("Status chip", '<div class="row"><span class="chip ok">loaded</span><span class="chip bad">missing</span><span class="chip off">disabled</span></div>'),
        cell("Drag handle (export)", '<span class="drag">Drag to DAW</span>'),
    ])
    body = f"""<div style="padding:28px 32px"><div style="font-size:24px;font-weight:600">VOX Drums UX v2 · tokens and component states</div>
<div style="color:var(--t3);margin:4px 0 22px">Spacing 4 / 8 / 12 / 16 / 24 · radius 8 (cells 4) · control height 32–36 · hit target 36–40 · type 12 / 13–14 / 15–16 / 18–22 / 24–28</div>
<div class="lbl" style="margin-bottom:10px">Palette</div><div style="display:flex;gap:14px;flex-wrap:wrap;margin-bottom:26px">{sw}</div>
<div class="lbl" style="margin-bottom:10px">Pad colours (lane, pad, strip, meter)</div><div style="display:flex;gap:8px;margin-bottom:28px">{pc}</div>
<div style="display:grid;grid-template-columns:1fr 1fr;gap:26px 40px">{comp}</div></div>"""
    page("08-tokens-states", 1440, 900, body, GRID_CSS + PLAY_CSS)

sequence_pattern()
sequence_song()
play_kit()
mix(True)
mix(False, "04b-mix-returns")
library()
generate_drawer()
settings_drawer()
states()
sequence_pattern(1280, 720, "09-sequence-compact-1280x720", with_notes=False, rowh=21, compact=True)
print("\n".join(sorted(os.listdir(OUT))))
