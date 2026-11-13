# VOX Drums UI/UX v3 — Gate 1 reference sheets: docking schematic, key states,
# tokens and components. Reuses the panel renderers of gen.py so every sheet
# shows the same components as the workspace screens. Design reference only.
import os
from gen import *  # noqa: F401,F403  (renderers, data, CSS)

SHEET_CSS = """
.sheet{background:#030A10;color:#C9D6E0;padding:16px 20px 24px;font-size:12.5px;line-height:1.45}
.sheet h1{font-size:18px;color:#E9F3FA;font-weight:600;margin-bottom:2px}
.sheet h2{font-size:14px;color:#E9F3FA;font-weight:600;margin:22px 0 8px;padding-top:12px;border-top:1px solid #122C3D}
.sheet .sub{color:#6F8A9D;margin-bottom:6px}
.row{display:flex;gap:16px;flex-wrap:wrap;align-items:flex-start}
.card{display:flex;flex-direction:column;gap:6px}
.card .cv{position:relative;background:#040C14;border-radius:6px;overflow:hidden;line-height:1.3}
.card .cl{font-size:12px;color:#E9F3FA;font-weight:600}.card .cd{font-size:11.5px;color:#9AB2C5;max-width:var(--cw)}
table.t{border-collapse:collapse;font-size:12px;width:100%}
table.t th,table.t td{border:1px solid #122C3D;padding:5px 8px;text-align:left;vertical-align:top}
table.t th{background:#0B1925;color:#E9F3FA;font-weight:600}
table.t td{color:#C9D6E0}table.t td.k{color:#E9F3FA;white-space:nowrap}
.z{position:absolute;border:1.5px solid #2B4A60;border-radius:6px;background:#0B1925;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;font-size:12px;color:#C9D6E0;padding:6px}
.z b{color:#E9F3FA;font-size:13px}.z i{font-style:normal;color:#6F8A9D;font-size:11px}
.z.c{border-color:#7A4434;background:#120F10}.z.m{border-color:#087B92;background:#071A22}
.sw{display:flex;flex-direction:column;gap:3px;width:118px;font-size:11px}
.sw i{display:block;height:40px;border-radius:5px;border:1px solid #1D3B50}
.sw b{color:#E9F3FA;font-weight:600}.sw span{color:#9AB2C5}
.ol{padding-left:18px}.ol li{margin:3px 0}
"""

def sheet(name, W, title, sub, body):
    html = f'<!doctype html><meta charset="utf-8"><style>{CSS}{SHEET_CSS}</style><body style="width:{W}px"><div class="sheet"><h1>{title}</h1><div class="sub">{sub}</div>{body}</div></body>'
    open(os.path.join(OUT, name + ".html"), "w").write(html)

def card(w, h, inner, label, desc=""):
    return (f'<div class="card" style="--cw:{w}px"><div class="cl">{label}</div>'
            f'<div class="cv app" style="width:{w}px;height:{h}px">{inner}</div>'
            + (f'<div class="cd">{desc}</div>' if desc else "") + '</div>')

# ----------------------------------------------------------------- 00 docking schematic
def zone_map(W, H, scale, lw, rw, mix, rails=False, split=True, label=True):
    s = scale
    G2 = G * s
    top = (44 + G) * s
    bottom = (H - mix - G - G) * s
    ah = bottom - top
    out = [f'<div class="z" style="left:0;top:0;width:{W*s:.0f}px;height:{44*s:.0f}px;border-radius:0;flex-direction:row;gap:8px">'
           + ('<b>Top chrome</b><i>workspace · panels · Pattern/Song · pattern · host status (read-only) · outputs · undo · Panic</i>' if label else '<i>chrome</i>') + '</div>']
    x = G2
    if rails:
        out.append(f'<div class="z" style="left:{x:.0f}px;top:{top:.0f}px;width:{34*s:.0f}px;height:{ah:.0f}px;padding:2px">{'<i>rail</i>' if label else ''}</div>')
        x += (34 + G) * s
    if lw:
        out.append(f'<div class="z" style="left:{x:.0f}px;top:{top:.0f}px;width:{lw*s:.0f}px;height:{ah:.0f}px">'
                   + (f'<b>Left side zone</b><i>{lw} px</i><br><i>Pad Rack · Library</i>' if label else f'<i>L {lw}</i>') + '</div>')
        x += (lw + G) * s
    right_x = W * s - G2 - (34 * s if rails else rw * s)
    cw = right_x - G2 - x
    if split:
        h1 = ah * .42
        out.append(f'<div class="z c" style="left:{x:.0f}px;top:{top:.0f}px;width:{cw:.0f}px;height:{h1-G2/2:.0f}px">'
                   + ('<b>Central zone · editor slot 1</b><i>Step Sequencer · Song Arranger · Sound Editor · Sample Editor</i>' if label else '<i>editor 1</i>') + '</div>')
        out.append(f'<div class="z c" style="left:{x:.0f}px;top:{top+h1+G2/2:.0f}px;width:{cw:.0f}px;height:{ah-h1-G2/2:.0f}px">'
                   + ('<b>editor slot 2 (optional)</b><i>split vertical or horizontal · drag the divider · tabs when too small</i>' if label else '<i>editor 2</i>') + '</div>')
    else:
        out.append(f'<div class="z c" style="left:{x:.0f}px;top:{top:.0f}px;width:{cw:.0f}px;height:{ah:.0f}px">'
                   + (f'<b>Central zone</b><i>{cw/s:.0f} px</i>' if label else f'<i>C {cw/s:.0f}</i>') + '</div>')
    if rails:
        out.append(f'<div class="z" style="left:{right_x:.0f}px;top:{top:.0f}px;width:{34*s:.0f}px;height:{ah:.0f}px;padding:2px">{'<i>rail</i>' if label else ''}</div>')
    else:
        out.append(f'<div class="z" style="left:{right_x:.0f}px;top:{top:.0f}px;width:{rw*s:.0f}px;height:{ah:.0f}px">'
                   + (f'<b>Right side zone</b><i>{rw} px</i><br><i>Inspector · Generator · Routing · Settings</i>' if label else f'<i>R {rw}</i>') + '</div>')
    out.append(f'<div class="z m" style="left:{G2:.0f}px;top:{bottom+G2:.0f}px;width:{W*s-2*G2:.0f}px;height:{mix*s:.0f}px;flex-direction:row;gap:8px">'
               + (f'<b>Mixer dock (bottom only)</b><i>{mix} px · Collapsed 30 / Compact 150–200 / Expanded 300–460 · resize by the top edge</i>' if label else (f'<i>Mixer {mix}</i>' if mix * s >= 14 else '')) + '</div>')
    return "".join(out)

def schematic():
    big = card(1060, 663, zone_map(1440, 900, 1060 / 1440, 392, 280, 184), "Default zones at 1440×900 (Beatmaking)",
               "Side zones hold side panels only, as dock tabs (one visible per zone). The central zone holds 1–2 editors. The Mixer has exactly one place: the bottom dock. Nothing floats over the work area.")
    rules = '''<div class="card" style="--cw:330px;width:330px"><div class="cl">Docking rules</div><ol class="ol">
<li><b>Side panels</b> (Pad Rack, Library, Inspector, Generator, Routing, Settings) dock into the left or right zone, as a tab or as a stacked section. Never into the central zone.</li>
<li><b>Editors</b> (Step Sequencer, Song Arranger, Sound Editor, Sample Editor) dock only in the central zone: 1 editor, or 2 split vertical/horizontal.</li>
<li><b>Mixer</b> docks only at the bottom. Three states; it pushes the work area, never overlays it.</li>
<li>Every panel header: grip (drag), title or tabs, Maximize (editors), Hide. Inspector adds Pin.</li>
<li>Drag a header → valid targets light up (cyan); invalid targets do not. Esc cancels. Dropping elsewhere changes nothing.</li>
<li>Each panel exists once. Opening a hidden panel from <b>Panels</b> restores it to its last zone.</li>
<li>Undo (chrome) covers project edits only; layout changes are not part of the project undo history. <b>Reset Workspace</b> restores the preset.</li></ol></div>'''
    mats = [("1600×1000", zone_map(1600, 1000, .2, 392, 280, 184, label=False, split=False)),
            ("1440×900", zone_map(1440, 900, .2, 392, 280, 184, label=False, split=False)),
            ("1280×720", zone_map(1280, 720, .2, 250, 250, 150, label=False, split=False)),
            ("1040×700", zone_map(1040, 700, .2, 196, 0, 30, rails=True, label=False, split=False))]
    thumbs = '<div class="row">' + "".join(card(int(W * .2), int(H * .2), m, n) for n, m, W, H in
                                          [(n, m, int(n.split("×")[0]), int(n.split("×")[1])) for n, m in mats]) + '</div>'
    table = '''<table class="t"><tr><th>Window</th><th>Left zone</th><th>Right zone</th><th>Central</th><th>Mixer default</th><th>Pad Rack</th><th>Step lanes</th><th>Parameter lanes</th></tr>
<tr><td class="k">≥ 1400 × ≥ 860<br>(1600×1000, 1440×900)</td><td>392 px (Pad Rack Grid + Library stacked)</td><td>280 px</td><td>≥ 744 px, 1–2 editors</td><td>Compact 184; 16 strips at 1600, bank of 8 at 1440</td><td>Grid 4×4, square pads ≥ 80 px</td><td>16; full names when the editor is ≥ 900 px wide (1600), short names below (1440)</td><td>2</td></tr>
<tr><td class="k">1240–1399 × 720–859<br>(1280×720)</td><td>250 px</td><td>250 px</td><td>≥ 716 px; second editor as tab if &lt; 2 × min height</td><td>Compact 150; bank of 8</td><td>List, full names</td><td>16, short names</td><td>1 (editor 508 px high)</td></tr>
<tr><td class="k">1040–1239 × 700–719<br>(1040×700, minimum)</td><td colspan="2">Both zones become rails (34 px). One side panel open at a time, docked at its minimum width (left 196 / right 240), the last one used.</td><td>≥ 700 px; one editor + tab</td><td>Collapsed 30</td><td>List, short names, no note column</td><td>16, short names</td><td>2 (editor 608 px high)</td></tr>
<tr><td class="k">&lt; 1040 × 700</td><td colspan="7">Not supported: the window cannot be made smaller than 1040×700 (host resize is limited to the minimum). Larger windows follow the rows above.</td></tr></table>'''
    order = '''<ol class="ol"><li>Mixer Compact → Collapsed when the central zone would drop below its editors' minimum height (Step 380, Song 230, Sound Editor 440, Sample Editor 170).</li>
<li>Second central editor → tab in the same slot when two minimums do not fit (vertical split: heights; horizontal split: widths, 700 each).</li>
<li>Parameter lanes by Step editor height: 2 at ≥ 560 px, 1 at 420–559 px, hidden below 420 px (the lane selector stays).</li>
<li>Pad Rack Grid → List when pads would be smaller than 80 px; Step lane names → short names.</li><li>Mixer strips: all 16 at ≥ 80 px (Compact) / 108 px (Expanded), else a bank of 8, else horizontal scroll.</li>
<li>Right zone → rail, then left zone → rail (one side panel open at a time).</li>
<li>Expanded Mixer is clamped so the central zone keeps its minimum; the dock never covers an editor.</li>
<li>Growing the window reverses the same steps in the opposite order; user choices (hidden panels, chosen tab, Mixer state) are kept, only forced changes are undone.</li></ol>'''
    presets = '''<table class="t"><tr><th>Workspace preset</th><th>Left</th><th>Central</th><th>Right (tabs)</th><th>Mixer</th></tr>
<tr><td class="k">Beatmaking (default)</td><td>Pad Rack · Grid, Library stacked below</td><td>Step Sequencer + 2 parameter lanes</td><td>Inspector · Generator · Routing · Settings</td><td>Compact</td></tr>
<tr><td class="k">Composition</td><td>Pad Rack · List</td><td>Song Arranger over Step Sequencer (vertical)</td><td>Inspector · Generator</td><td>Collapsed</td></tr>
<tr><td class="k">Sound Design</td><td>Pad Rack · Grid · Library</td><td>Sound Editor over Sample Editor (vertical)</td><td>Inspector (Pin offered)</td><td>Compact</td></tr>
<tr><td class="k">Mixing</td><td>hidden</td><td>Step Sequencer</td><td>Routing · Inspector · Settings</td><td>Expanded with FX returns and Main</td></tr></table>
<div class="sub" style="margin-top:6px">Presets are read-only templates. The current layout is saved with the project (UI-only state). "Save Workspace As…" stores a named user layout; "Reset Workspace" restores the active preset. Factory presets cannot be overwritten.</div>'''
    body = (f'<div class="row">{big}{rules}</div><h2>Responsive degradation</h2><div class="row" style="align-items:flex-end">{thumbs}</div>'
            f'<div style="margin-top:12px">{table}</div><h2>Degradation order (deterministic)</h2>{order}<h2>Workspace presets</h2>{presets}')
    sheet("00-docking-schematic", 1480, "Docking zones and responsive degradation", "Spec v3 · zone model, sizes in px at 100 % scale (DPI scaling multiplies them) · applies to every workspace", body)

# ----------------------------------------------------------------- 07 key states
def mini_ws(W, H, boxes):
    out = []
    for (x, y, w, h, t, cls) in boxes:
        out.append(f'<div class="z {cls}" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px;font-size:11px">{t}</div>')
    return "".join(out)

def states():
    rows = []
    insp = [("Follows selection", inspector(0, 0, 224, 330, "pad", pad=3), "Default: shows the selected pad (Pad 4). Quick controls are the pad’s host parameters (Level, Pan, Tune, Decay)."),
            ("Pinned", inspector(0, 0, 224, 330, "pad", pinned=True, other=3, pad=0), "Pin keeps Pad 1 while Pad 4 is selected elsewhere. The banner says so. Unpin returns to following."),
            ("Multi-step selection", inspector(0, 0, 224, 330, "steps"), "Exact values or ‘mixed’ ranges. Drag on a mixed value = relative change. Only per-step properties that exist: Velocity, Chance, Ratchet, Micro, Accent, Lock."),
            ("Song bar", inspector(0, 0, 224, 330, "song"), "Song bar 37: what it plays, links, loop. The edit target is set in the Song Arranger, not here."),
            ("Pinned object unavailable", panel(0, 0, 224, 330, head("Inspector", pin=True), '<div style="margin:8px;padding:8px;border:1px solid #6B5420;background:#1F1A0C;border-radius:5px;font-size:11.5px;color:#F3DFA6;line-height:1.45">Pinned to <b>Pattern 3 A · bar 4 · steps 9–12</b>. After loading a preset, Pattern 3 has 2 bars: these steps no longer exist.</div><div style="padding:4px 8px;display:flex;gap:6px"><span class="btn sm">Unpin</span><span class="btn sm">Pin current selection</span></div>'), "After a load/restore the pinned steps or song bar can disappear (shorter pattern or song). Honest state + two safe actions; nothing is edited until the user picks one."),
            ("Nothing selected", panel(0, 0, 224, 330, head("Inspector", pin=False), '<div style="padding:16px 12px;color:var(--t2);font-size:12px;line-height:1.5">Nothing selected.<br><span style="color:var(--t3)">Select a pad, steps or a song bar. Pinned content stays until unpinned.</span></div>'), "Empty state is text only; no disabled placeholder controls.")]
    rows.append('<h2>Inspector</h2><div class="row">' + "".join(card(224, 330, i, n, d) for n, i, d in insp) + '</div>')
    rack = [card(392, 560, pad_rack(0, 0, 392, 560, "grid", sel=3, hit={0: 118, 3: 72}), "Pad Rack · Grid", "White outline = selection (Pad 4). Ring in the pad colour = hit flash (Pad 1 playing). Top strip = select, surface = audition. Glow = velocity (Pad 1 hit at 118), right-edge bar = measured peak. Pad 10: ‘● missing’ = its sample file is missing (reference kept, the pad plays its synthesis), as in the Library panel."),
            card(300, 560, pad_rack(0, 0, 300, 560, "list", sel=3, hit=(0,)), "Pad Rack · List · same selection", "Same pad selected, same M/S, same hit indicator (dot). Grid/List is a view switch, not a different panel."),
            '''<div class="card" style="--cw:520px;width:520px"><div class="cl">Pad interactions</div><table class="t">
<tr><th>Gesture</th><th>Result</th></tr>
<tr><td class="k">Click the top strip (number, note)</td><td>Select only (Inspector follows unless pinned). No sound.</td></tr>
<tr><td class="k">Strike the pad surface / Enter</td><td>Audition at velocity 100 (Shift = 127) and select. Glow follows the velocity.</td></tr>
<tr><td class="k">Ctrl/Cmd-click</td><td>Add to selection (for M/S/output edits across pads).</td></tr>
<tr><td class="k">M / S</td><td>Pad mute / solo (host parameters drm.pNN.mute / solo).</td></tr>
<tr><td class="k">Double-click</td><td>Open the Sound Editor for this pad (central zone).</td></tr>
<tr><td class="k">Drop audio file</td><td>Load into player A of that pad (B with Alt). Same as Load…</td></tr>
<tr><td class="k">Arrows</td><td>Move selection when the Pad Rack has keyboard focus. Space/transport keys are never taken.</td></tr></table></div>''']
    rows.append('<h2>Pad Rack: grid and list, one selection</h2><div class="row">' + "".join(rack) + '</div>')
    mixw = 1600
    mix = [card(mixw, 30, mixer(0, 0, mixw, 30, "collapsed"), "Mixer · Collapsed (30 px)", "Status line: Main meter, level, peak, enabled outputs, state switch."),
           card(mixw, 184, mixer(0, 0, mixw, 184, "compact"), "Mixer · Compact at 1600 px (default 150–184 px high)", "All 16 strips fit their 80 px minimum: fader, measured meter with peak hold, Pan, M/S; Main with −1 dB and CLIP lamps. At 1440 px the same Compact Mixer shows a bank of 8 (Beatmaking)."),
           card(mixw, 440, mixer(0, 0, mixw, 440, "expanded", hot=True, clip=True, over=(0,)), "Mixer · Expanded at 1600 px · bank of 8", "16 strips would be narrower than 108 px, so a bank of 8 (1–8 / 9–16); below 8 the viewport scrolls. FX returns and Main stay visible. Shown with Kick OVER latched, Main −1 dB lit and Main OVER (≥ 0 dBFS before the host) latched; white dot = latched until Reset peaks. No CLIP lamp.")]
    rows.append('<h2>Mixer dock: three states of one dock</h2><div class="row">' + "".join(mix) + '</div>')
    S = 340
    def ed(t, extra=""):
        return f'<div class="z c" style="{extra}"><b>{t}</b></div>'
    splits = [("Vertical split", mini_ws(S, 220, [(6, 6, 64, 208, "Pad Rack", ""), (76, 6, 188, 96, "<b>Song Arranger</b>", "c"), (76, 108, 188, 106, "<b>Step Sequencer</b>", "c"), (270, 6, 64, 208, "Inspector", "")]), "Split ▾ → Below. Drag the divider; double-click it = 50/50."),
              ("Horizontal split", mini_ws(S, 220, [(6, 6, 64, 208, "Pad Rack", ""), (76, 6, 92, 208, "<b>Sound Editor</b>", "c"), (172, 6, 92, 208, "<b>Sample Editor</b>", "c"), (270, 6, 64, 208, "Inspector", "")]), "Split ▾ → Right. Only when both editors keep 700 px; otherwise the option is shown disabled with the reason."),
              ("Tab fallback", mini_ws(S, 220, [(6, 6, 64, 208, "Pad Rack", ""), (76, 6, 188, 208, "<b>Song Arranger</b> | Step Sequencer<br><i>two editors, one slot</i>", "c"), (270, 6, 64, 208, "Inspector", "")]), "When the window is too small for the split, both editors become tabs of one slot. Growing the window restores the split."),
              ("Maximized editor", mini_ws(S, 220, [(6, 6, 18, 208, "", ""), (30, 6, 280, 208, "<b>Step Sequencer</b><br><i>maximized · ⤡ Restore · Esc</i>", "c"), (316, 6, 18, 208, "", "")]), "⤢ or double-click the header: the editor takes the work area; side zones fold to rails, Mixer to Collapsed. Restore returns the exact previous layout.")]
    rows.append('<h2>Central zone: split, fallback, maximize</h2><div class="row">' + "".join(card(S, 220, b, n, d) for n, b, d in splits) + '</div>')
    ghost = lambda x, y, t, bad=False: f'<div style="position:absolute;left:{x}px;top:{y}px;padding:4px 8px;border-radius:5px;background:{"#2A1218" if bad else "#102230"};border:1px solid {"#7A2B39" if bad else "#087B92"};color:{"#FFC2CC" if bad else "#E9F3FA"};font-size:11px;box-shadow:0 6px 16px rgba(0,0,0,.5);z-index:5;white-space:nowrap">{"⃠ " if bad else "⠿ "}{t}</div>'
    hl = lambda x, y, w, h, t: f'<div style="position:absolute;left:{x}px;top:{y}px;width:{w}px;height:{h}px;border:2px solid #00DDF5;background:rgba(4,40,50,.92);border-radius:6px;display:flex;align-items:flex-start;justify-content:center;padding-top:8px;color:#00DDF5;font-size:11px;font-weight:600;z-index:4">{t}</div>'
    base = [(6, 6, 64, 170, "Pad Rack", ""), (76, 6, 188, 170, "<b>Step Sequencer</b>", "c"), (270, 6, 64, 170, "Inspector", ""), (6, 182, 328, 32, "Mixer", "m")]
    drops = [("Valid · tab in the right zone", mini_ws(S, 220, base) + hl(270, 6, 64, 170, "+ tab") + ghost(196, 96, "Generator"), "Generator dragged over the right zone: the whole zone highlights; the tab insertion point is shown in its header."),
             ("Valid · split the central zone", mini_ws(S, 220, base) + hl(76, 92, 188, 84, "split below") + ghost(120, 130, "Song Arranger"), "An editor over the lower half of the central zone: ‘split below’. Upper half: ‘split above’; centre: ‘tab’."),
             ("Invalid · Mixer to a side zone", mini_ws(S, 220, base) + ghost(118, 92, "Mixer docks only at the bottom", bad=True), "No target lights up; the ghost says why. Dropping returns the Mixer to the bottom dock. Same for an editor dropped on a side zone.")]
    rows.append('<h2>Dock-target preview while dragging a panel header</h2><div class="row">' + "".join(card(S, 220, b, n, d) for n, b, d in drops) + '</div>')
    def menu(items, w=240):
        li = []
        for it in items:
            if it == "-":
                li.append('<div style="height:1px;background:#1D3B50;margin:4px 0"></div>')
            else:
                t, chk, note = (it + ("", "", ""))[:3] if isinstance(it, tuple) else (it, "", "")
                li.append(f'<div style="height:26px;display:flex;align-items:center;gap:8px;padding:0 10px;font-size:12px;color:{"var(--dis)" if note == "dis" else "var(--t1)"}"><span style="width:12px;color:var(--cyan)">{chk}</span><span style="flex:1">{t}</span><span style="color:var(--t3);font-size:11px">{note if note != "dis" else ""}</span></div>')
        return f'<div style="position:absolute;left:8px;top:40px;width:{w}px;background:#102230;border:1px solid #2B4A60;border-radius:6px;padding:4px 0;box-shadow:0 10px 28px rgba(0,0,0,.6)">{"".join(li)}</div>'
    wsm = '<div style="position:absolute;left:8px;top:6px"><span class="btn dd">Workspace: Beatmaking</span></div>' + menu([("Beatmaking", "✓", "factory"), ("Composition", "", "factory"), ("Sound Design", "", "factory"), ("Mixing", "", "factory"), "-", ("My breakdown layout", "", "user"), "-", ("Save Workspace As…",), ("Reset Workspace", "", "to preset")], 270)
    pnm = '<div style="position:absolute;left:8px;top:6px"><span class="btn dd">Panels</span></div>' + menu([("Pad Rack", "✓", "left"), ("Library", "", "left"), ("Inspector", "✓", "right"), ("Generator", "", "right"), ("Routing", "", "right"), ("Settings", "", "right"), "-", ("Step Sequencer", "✓", "central"), ("Song Arranger", "", "central"), ("Sound Editor", "", "central"), ("Sample Editor", "", "central"), "-", ("Mixer", "✓", "bottom")], 250)
    rows.append('<h2>Workspace and Panels menus</h2><div class="row">' + card(300, 330, wsm, "Workspace menu", "Factory presets are templates; user layouts are listed separately. Reset restores the active preset; disabled items say why on hover.")
                + card(280, 410, pnm, "Panels menu", "Show/hide each panel; the column says where it opens. Hiding never deletes settings.") + '</div>')
    dm = lambda t, extra="", bg="var(--copper)": f'<div style="position:absolute;left:14px;top:14px;display:inline-flex"><span class="btn pri sm" style="height:26px;border-radius:5px 0 0 5px;{extra}">⠿ {t}</span><span class="btn pri sm" style="height:26px;border-radius:0 5px 5px 0;border-left:1px solid #B65A3E;padding:0 6px">▾</span></div>'
    tip = lambda t, y=48: f'<div style="position:absolute;left:14px;top:{y}px;width:230px;padding:6px 8px;background:#102230;border:1px solid #2B4A60;border-radius:5px;font-size:11px;color:var(--t1);line-height:1.4">{t}</div>'
    drag = [("Idle", dm("Drag MIDI"), "Copper = primary action of the editor."),
            ("Hover", dm("Drag MIDI", "filter:brightness(1.1)") + tip("Drag onto a MIDI or instrument track.<br><span style='color:var(--t2)'>Pattern 2 A · 4 bars · 16 lanes · 145 BPM</span>"), "The scope is stated before the drag."),
            ("Dragging", dm("Drag MIDI", "opacity:.55") + '<div style="position:absolute;left:96px;top:60px;padding:5px 9px;background:#102230;border:1px solid #087B92;border-radius:5px;font-size:11px;color:var(--t1);box-shadow:0 6px 16px rgba(0,0,0,.5)">♪ VOX Drums P2A 4bars.mid</div>', "OS file drag of a temporary .mid file. The host decides what the drop does."),
            ("After the drag", dm("Dragged · P2 A") + tip("The plugin cannot see whether the host accepted the drop. If nothing appeared, use ▾ → Save MIDI file…", 48), "No success claim. Shown for 3 s, then back to idle."),
            ("▾ menu", dm("Drag MIDI") + menu([("Save MIDI file…", "", "this pattern")], 230).replace("top:40px", "top:44px"), "Saving is a separate command with its own file dialog; the Song Arranger’s ▾ saves the entire song. Clicking the handle itself only starts a drag.")]
    rows.append('<h2>Drag MIDI states</h2><div class="row">' + "".join(card(264, 150, b, n, d) for n, b, d in drag) + '</div>')
    rows.append('<h2>Song edit target: Edit Source Pattern</h2><div class="row">' + card(1100, 330, song_editor(0, 0, 1100, 330, sel=36, target="src"), "Song Arranger · target = Edit Source Pattern",
                "Counterpart of the Composition screen (Edit This Bar). Here a grid edit changes P5 B bar 1 and every linked bar (33, 37, 45, 53); the text names them before the edit.") + '</div>')
    sheet("07-states", 1660, "Key states", "Inspector, Pad Rack views, Mixer states, split editors, dock-target previews, menus, Drag MIDI, song edit target", "".join(rows))

# ----------------------------------------------------------------- 08 tokens and components
def lum(hx):
    hx = hx.lstrip("#")
    c = [int(hx[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    c = [v / 12.92 if v <= .03928 else ((v + .055) / 1.055) ** 2.4 for v in c]
    return .2126 * c[0] + .7152 * c[1] + .0722 * c[2]

def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + .05) / (lb + .05)

def tokens():
    surf = [("bg", "#06121D", "window, chrome"), ("gutter", "#040C14", "between docked panels"), ("panel", "#0B1925", "panel body"), ("raised", "#102230", "selected tab, menus"),
            ("control", "#0A1722", "buttons, fields"), ("graph", "#051019", "waveform, lanes"), ("border", "#1D3B50", "control border"), ("subtle", "#122C3D", "panel border, rows")]
    text = [("text", "#E9F3FA", "primary"), ("textSecondary", "#9AB2C5", "labels"), ("textMuted (family)", "#587487", "family value"), ("textMuted (proposed)", "#6F8A9D", "used in these renders"), ("disabled", "#3D5566", "disabled only")]
    sem = [("cyan · focus / active / transport", "#00DDF5", "focus ring, active toggle, host status, playhead, loop"), ("copper · Drums accent", "#EF805D", "primary action, selected tab underline, Pin"),
           ("danger", "#E4425D", "Panic, mute, missing sample"), ("success", "#32D296", "meters, done"), ("warning", "#E3B341", "fallback routing (Out n→Main), meter top")]
    def sw(n, c, d, ref="#0B1925"):
        cr = contrast(c, ref)
        return f'<div class="sw"><i style="background:{c}"></i><b>{n}</b><span>{c} · {d}</span><span>{cr:.1f}:1 on panel</span></div>'
    pads = "".join(f'<div class="sw" style="width:70px"><i style="background:{c};height:28px"></i><b>pad.{i+1}</b><span>{c}</span></div>' for i, c in enumerate(PADC))
    body = ('<h2>Surfaces (family tokens, libs/vox-ui/Tokens.h)</h2><div class="row">' + "".join(sw(*t) for t in surf) + '</div>'
            + '<h2>Text</h2><div class="row">' + "".join(sw(*t) for t in text) + '</div>'
            + f'<div class="sub">Family textMuted #587487 on panel = {contrast("#587487", "#0B1925"):.1f}:1, below 4.5:1 for 10–11 px text. These renders use #6F8A9D ({contrast("#6F8A9D", "#0B1925"):.1f}:1). Changing a family token affects every VOX product: proposed as a family change for the design-system owner, otherwise a Drums-local text token.</div>'
            + '<h2>Semantic colours</h2><div class="row">' + "".join(sw(*t) for t in sem) + '</div>'
            + f'<div class="sub">Danger text uses #FF8FA0 ({contrast("#FF8FA0", "#0B1925"):.1f}:1); #E4425D ({contrast("#E4425D", "#0B1925"):.1f}:1) is used only for fills and borders. One meaning each: cyan never marks a primary action; copper never marks focus or activity. Colour is never the only cue (text, shape or icon always present).</div>'
            + '<h2>Pad identity (proposed semantic tokens pad.1–pad.16)</h2><div class="row">' + pads + '</div>'
            + '<div class="sub">Eight hues; pads 9–16 repeat them one step darker. Used only as a 3–4 px identity bar, step fill and meter colour; pad number and name are always shown.</div>')
    typo = [("Product name", 15, 600), ("Panel title / tab", 12.5, 600), ("Values", 12, 500), ("Labels", 12, 400), ("Caption (uppercase)", 11, 400), ("Minimum (lamps, dense notes)", 10, 600)]
    body += '<h2>Typography (Inter in renders; JUCE uses the platform UI font: Segoe UI on Windows)</h2><table class="t"><tr><th>Role</th><th>Size / weight</th><th>Sample</th></tr>' + "".join(
        f'<tr><td class="k">{r}</td><td>{sz} px / {w}</td><td style="font-size:{sz}px;font-weight:{w};{"text-transform:uppercase;letter-spacing:.09em;color:#6F8A9D" if r.startswith("Caption") else ""}">Closed Hat · 145.0 BPM · −6.2 dB</td></tr>' for r, sz, w in typo) + '</table>'
    body += '''<div class="sub" style="margin-top:6px">Secondary text was raised from 10–11 px to 11–12 px. 10 px is used only inside lamps and for pad note numbers. A 100 % (1×) render of Beatmaking is included: <code>01-beatmaking@1x.png</code>.</div>'''
    # ---------- tactile material tokens
    mats = [("control.body", "radial #33475A → #1A2833 → #0E1820", "matte knob / pad body; light from top-left"),
            ("control.bezel", "#040B11 + #13283A ring", "recessed track the value arc sits on"),
            ("control.edge", "#2C4456 (hover #4A6A82)", "1 px body edge"),
            ("control.shadow", "0 1.6 px, black 55 %", "physical lift; pressed = inset shadow"),
            ("control.pointer", "#E9F3FA, 2 px", "position indicator; disabled #3D5566"),
            ("cap.metal", "linear #5A6C79 → #2C3B46 → #1E2A33 → #3A4B57", "fader cap with centre line"),
            ("slot.recess", "#03090E, inset shadow", "fader slot, meter well, pad peak bar"),
            ("raised.surface", "linear #22343F → #15232D, top highlight 8 %", "buttons, toggles, M/S"),
            ("pad.rubber", "radial #26343F → #141F27", "pad strike surface; muted: #18222A → #0E161C")]
    body += '<h2>Tactile material tokens (shared, for Drums, Synth and FX)</h2><table class="t"><tr><th>Token</th><th>Value</th><th>Use</th></tr>' + "".join(f'<tr><td class="k">{a}</td><td>{b}</td><td>{c}</td></tr>' for a, b, c in mats) + '</table>'
    body += '<div class="sub" style="margin-top:6px">Drawn with JUCE paths and gradients (no bitmaps), so every state, focus ring and automation update repaints on the message thread at any scale.</div>'
    # ---------- scales
    def st(label, html, w=120):
        return f'<div style="display:flex;flex-direction:column;align-items:flex-start;gap:6px;width:{w}px"><span class="cap" style="white-space:normal;line-height:1.3;min-height:28px">{label}</span>{html}</div>'
    grad = "".join(f'<i style="display:block;width:6px;height:14px;background:{grad_col(k/59)}"></i>' for k in range(60))
    body += ('<h2>Three value scales — the arc shows the parameter value, never measured audio</h2>'
             f'<div class="row" style="align-items:center"><div style="display:flex;border-radius:3px;overflow:hidden">{grad}</div><span class="sub" style="margin:0">amount scale: green → yellow → orange → red with increasing intensity; red = maximum setting, not clipping</span></div>'
             '<div class="row" style="margin-top:10px">'
             + st("amount · Level 85 %", knob("Level", "85 %", .85, 48, kw=96), 110)
             + st("amount · Velocity 118", knob("Velocity", "118", 118/127, 48, kw=96), 110)
             + st("amount · Drive 2.20×", knob("Drive", "2.20×", (2.2-1)/7, 48, kw=96), 110)
             + st("amount · Delay send 35 %", knob("Delay send", "35 %", .35, 48, kw=96), 120)
             + st("amount · Reso 100 %", knob("Reso", "100 %", 1, 48, kw=96), 110)
             + st("value · Cutoff", knob("Cutoff", "4.80 kHz", .62, 48, kw=96), 110)
             + st("value · Decay", knob("Decay", "380 ms", .42, 48, kw=96), 110)
             + st("bipolar · Pan L 20", knob("Pan", "L20", .4, 48, kw=96), 110)
             + st("bipolar · Tune +7 st", knob("Tune", "+7.0 st", .5 + 7/48, 48, kw=96), 110)
             + st("bipolar · Attack −30 %", knob("Attack", "−30 %", .35, 48, kw=96), 110)
             + '</div><div class="sub" style="margin-top:8px">Amount: level, velocity, drive, sub, sends, returns, feedback, resonance, accent, swing, sample level, generator amounts. Value (steel arc + unit): cutoff, decay, tail, pitch decay, tone, size, damp, character, split, delay time. Bipolar (steel arc from the centre detent): pan, tune, fine, transient attack/sustain, microtiming. No compressor or gain-reduction display: the product has no compressor.</div>')
    # ---------- control states
    states = ["", "hover", "focus", "pressed", "dim"]
    names = ["normal", "hover", "keyboard focus", "pressed / dragging", "disabled"]
    kn = "".join(st(n, knob("Cutoff", "4.80 kHz", .62, 48, kw=96, state=s_ if s_ != "dim" else "", dim=s_ == "dim") + ('<span class="chip" style="color:var(--t1)">4.80 kHz · Shift = fine</span>' if s_ == "pressed" else ""), 140) for s_, n in zip(states, names))
    bt = "".join(st(n, h) for n, h in [("normal", '<span class="btn">Generate</span>'), ("hover", '<span class="btn hov">Generate</span>'), ("pressed", '<span class="btn prs">Generate</span>'),
              ("focus", '<span class="btn focusring">Generate</span>'), ("disabled", '<span class="btn dis">Paste</span>'), ("primary", '<span class="btn pri">Generate Pattern</span>'), ("danger", '<span class="btn danger">Panic</span>')])
    tg = "".join(st(n, h, 130) for n, h in [("toggle off", '<span class="tg">Reverse</span>'), ("toggle on (lit LED)", '<span class="tg on">Loop</span>'), ("mute off / on", '<span style="display:flex;gap:4px"><span class="tg">M</span><span class="tg m">M</span></span>'),
              ("solo off / on", '<span style="display:flex;gap:4px"><span class="tg">S</span><span class="tg s">S</span></span>'), ("segmented", '<div class="seg"><i class="on">Grid</i><i>List</i></div>'), ("dropdown", '<span class="sel">LP 24</span>'),
              ("lane on / off", '<span style="display:flex;gap:8px"><span style="width:12px;height:10px;border-radius:3px;border:1px solid #0A141C;background:radial-gradient(circle,#9FF6FF 0,#00C8DE 55%,#077083 100%);box-shadow:0 0 3px rgba(0,221,245,.35)"></span><span style="width:12px;height:10px;border-radius:3px;border:1px solid #0A141C;background:#0C1820;box-shadow:inset 0 1px 2px #000"></span></span>')])
    hier = "".join(st(n, h, 130) for n, h in [("primary 52 px, tick scale", knob("Decay", "380 ms", .42, 52, kw=110)), ("primary, amount", knob("Drive", "2.20×", .17, 52, kw=110)),
              ("primary, bipolar", knob("Tune", "+7.0 st", .65, 52, kw=110)), ("secondary 34 px", knob("Fine", "0 ct", .5, 34, kw=110)), ("secondary, amount", knob("Sub", "25 %", .25, 34, kw=110)), ("inline 20 px", mini("Swing", "0 %", 0))])
    fd = "".join(st(n, f'<div style="display:flex;gap:6px;align-items:flex-end">{h}</div>', 120) for n, h in [
        ("fader + % scale", vfader(150, .85, 30, scale=True)), ("fader 40 %", vfader(150, .4, 30)), ("fader disabled", vfader(150, .6, 30, dim=True)),
        ("meter: −18 dBFS", meter(150, .45, w=8, peak=.5)), ("meter: −2 dBFS", meter(150, .93, w=8, peak=.95)), ("meter + peak hold", meter(150, .6, w=8, peak=.98))])
    lamps = "".join(st(n, h, 150) for n, h in [("−1 dB off / near threshold", f'<span style="display:flex;gap:6px">{lamp("hot")}{lamp("hot", True)}</span>'),
              ("OVER off / latched", f'<span style="display:flex;gap:6px">{lamp("over")}{lamp("over", True, True)}</span>'),
              ("CLIP: not shown", '<span style="font-size:11px;color:var(--t2);line-height:1.35">no clipping event exists in the product; a lamp would be fabricated</span>')])
    pads = "".join(st(n, pad_tile(i, 92, **kw), 104) for n, i, kw in [
        ("normal", 1, {}), ("face hover = play", 1, {"state": "hover"}), ("name bar hover = select", 1, {"state": "hover-label"}), ("pressed (face travels)", 1, {"state": "pressed", "hitv": 100}), ("hit v40 (illustr.)", 3, {"hitv": 40}), ("hit v80 (illustr.)", 3, {"hitv": 80}),
        ("hit v127 (illustr.)", 3, {"hitv": 127}), ("measured peak only", 3, {"peak": .85}), ("selected", 0, {"sel": True}), ("selected + hit", 0, {"sel": True, "hitv": 118}), ("muted", 11, {"mut": True}), ("solo", 2, {"solo": True}), ("missing", 9, {"miss": True}), ("OVER latched", 0, {"over": True, "peak": .98})])
    body += ('<h2>Components and states</h2><div class="sub">Knob (vertical drag; Shift = fine; double-click = default; wheel = one step; host automation moves it live; gesture begin/end around each drag)</div><div class="row">' + kn + '</div>'
             + '<div class="sub" style="margin-top:12px">Knob hierarchy by musical priority (Sound Editor: Tune, Decay, Drive, Cutoff, Level primary; trims secondary)</div><div class="row">' + hier + '</div>'
             + '<div class="sub" style="margin-top:12px">Buttons</div><div class="row">' + bt + '</div>'
             + '<div class="sub" style="margin-top:12px">Toggles and choices (booleans = toggles; real enumerations = segmented or dropdown)</div><div class="row">' + tg + '</div>'
             + '<div class="sub" style="margin-top:12px">Level fader: neutral recessed track (the setting is not loudness), % scale, raised metal cap; Shift = fine, double-click = default, value field when the existing binding offers text entry. Measured meters: green to −12, yellow to −3, orange to 0 dBFS, never red; peak hold with numeric value</div><div class="row" style="align-items:flex-end">' + fd + '</div>'
             + '<div class="sub" style="margin-top:12px">Overload lamps (separate from every gradient; white dot = latched until “Reset peaks” or a click on the lamp)</div><div class="row">' + lamps + '</div>'
             + '<div class="sub" style="margin-top:12px">Pad (92 px): rubber face ≈ 2/3 of the tile = play; name bar = select (no sound) + M/S. Glow and top LED = hit velocity (illustrative until telemetry exists); bottom bar = measured post-fader peak; white ring + light name bar = selected</div><div class="row">' + pads + '</div>')
    sem = '''<h2>What each indicator measures</h2><table class="t"><tr><th>Indicator</th><th>Source</th><th>Meaning</th></tr>
<tr><td class="k">Knob / fader arc and fill</td><td>parameter value</td><td>setting only; red = top of the range, never overload</td></tr>
<tr><td class="k">Pad glow, top LED</td><td>MIDI velocity of the hit (host MIDI, internal pattern/song, audition)</td><td>how hard the pad was triggered; not loudness. Needs per-pad velocity telemetry (README §6); until then a fixed-brightness flash, no velocity claim</td></tr>
<tr><td class="k">Pad peak bar, strip meter</td><td>pad output post-fader (existing <code>takePadMeter</code>)</td><td>measured audio of that pad</td></tr>
<tr><td class="k">OVER (pad / strip)</td><td>pad post-fader peak ≥ 0 dBFS</td><td>internal float headroom used; not output clipping. Latched</td></tr>
<tr><td class="k">Main meter</td><td>Main after FX returns, before the host (existing <code>takeMainMeter</code>)</td><td>measured plugin output</td></tr>
<tr><td class="k">−1 dB</td><td>Main peak ≥ −1 dBFS</td><td>near threshold. Not latched</td></tr>
<tr><td class="k">OVER (Main)</td><td>Main sample |x| ≥ 1.0, after FX returns, before the host</td><td>“≥ 0 dBFS (pre-host)”: full scale reached; a risk for a fixed-point stage, not proof of clipping. The DAW master bus is not measured. Latched</td></tr>
<tr><td class="k">CLIP</td><td>none</td><td>not shown: the product has no clipping or saturation event to report (Kick drive is a sound feature). Would need a documented source first</td></tr>
<tr><td class="k">Out 2–9</td><td>no meter today</td><td>a strip routed to Out n shows its pad meter; per-bus meters need read-only taps (Gate 2 proposal)</td></tr></table>'''
    sizes = '''<h2>Sizes and hit targets</h2><table class="t"><tr><th>Item</th><th>Visual</th><th>Hit rectangle</th></tr>
<tr><td class="k">Buttons, toggles, dropdowns, segmented</td><td>28 px high (small 26)</td><td>≥ 32 × 32</td></tr>
<tr><td class="k">Knobs</td><td>48 (sheet), 44 editor, 40 inspector/pan, 34–36 sends and returns, 32 compact pan, 20 inline</td><td>knob + label + value block, ≥ 36 × 48; inline ≥ 32 × 28</td></tr>
<tr><td class="k">Faders</td><td>slot 7, cap 26 × 16</td><td>whole strip column, ≥ 32 wide</td></tr>
<tr><td class="k">Mixer M/S</td><td>half strip × 26</td><td>≥ 32 × 32</td></tr>
<tr><td class="k">Pad M/S</td><td>24 × 20</td><td>28 × 26 (pad padding included); same actions in List (24 × 24 in a 32 px row), Mixer and Inspector, and keys M/S on the focused pad</td></tr>
<tr><td class="k">Pads</td><td>≥ 80 px square (Grid); below that the zone uses List</td><td>whole pad: top strip = select, rest = audition</td></tr>
<tr><td class="k">Step cells, lane bars</td><td>≥ 20 px high (dense grid)</td><td>cell; keyboard alternative while the grid has focus: arrows move, Enter toggles the step, +/− change its velocity (Space is never used)</td></tr>
<tr><td class="k">Mixer strip width</td><td>Compact ≥ 80, Expanded ≥ 108</td><td>else bank of 8, else horizontal scroll</td></tr>
<tr><td class="k">Gutter, radius</td><td>6 px between panels; panel radius 6, pad 9, control 5–6</td><td></td></tr></table>'''
    body += sem + sizes
    # ---------- static parts kept from v1
    pass
    sheet("08-tokens-components", 1560, "Design tokens and components", "Family tokens from libs/vox-ui/Tokens.h plus the proposed Drums semantic layer · contrast computed against panel #0B1925", body)

def diffsheet():
    def pair(title, a, b, w, changes):
        return (f'<h2>{title}</h2><div class="row"><div class="card"><div class="cl">v3.1 (52c2be2)</div><img src="../diff/{a}" style="width:{w}px;border-radius:6px"></div>'
                f'<div class="card"><div class="cl">v3.2</div><img src="../diff/{b}" style="width:{w}px;border-radius:6px"></div></div><ul class="ol" style="margin-top:8px">'
                + "".join(f"<li>{c}</li>" for c in changes) + "</ul>")
    body = pair("Pad Rack Grid (TL P1)", "pads-v31.png", "pads-v32.png", 400, [
        "Each pad = a raised rubber <b>face</b> (≈ 2/3 of the square, play) over a flat <b>name bar</b> (select without sound, M/S). Before: one surface with a thin top strip as the select zone.",
        "Number and note on the face corner; name in the bar; M/S moved into the bar; peak bar along the face bottom; hint line under the grid.",
        "Pressed = face travels into the housing; selected = white ring + light name bar; velocity glow labelled v* (illustrative until telemetry exists).",
        "Grid still exactly 4×4 equal squares (92 px at 1440×900; left zone 404 px)."])
    body += pair("Sound Editor (TL P2)", "sound-v31.png", "sound-v32.png", 800, [
        "Primary controls Tune, Decay, Drive, Cutoff, Level: 52 px knobs with an 11-step tick scale; secondary trims 34 px.",
        "Kick body: Drive primary + Pitch env; Pitch dec, Sub, Tail secondary.",
        "Values and units unchanged; amount / value / bipolar arcs unchanged."])
    body += pair("Mixer, overload semantics and faders (TL P1/P2)", "mixer-v31.png", "mixer-v32.png", 1440, [
        "Main lamp <b>CLIP → OVER (≥ 0 dBFS, pre-host)</b>; no CLIP lamp anywhere: the product has no clipping event. −1 dB near-threshold lamp kept.",
        "Fader track neutral (no colour scale: the setting is not loudness), % scale, larger metal cap; strips show numeric peak; Main shows L/R and per-channel peaks.",
        "8-strip bank, M/S size and Main/FX visibility unchanged from v3.1."])
    body += ('<h2>100 % (1×) crops</h2><div class="sub">Logical pixels as the native UI would draw them at 100 % scale.</div>'
             '<div class="row"><img src="../diff/beat-1x.png"><img src="../diff/sound-1x.png"></div><div class="row" style="margin-top:12px"><img src="../diff/mixer-1x.png"></div>')
    sheet("09-v32-changes", 1660, "v3.1 → v3.2 changes", "TL review 6861782128: pads, overload semantics, knob hierarchy, faders · architecture and palette unchanged", body)

if __name__ == "__main__":
    schematic(); states(); tokens(); diffsheet()
    print("sheets ok")
