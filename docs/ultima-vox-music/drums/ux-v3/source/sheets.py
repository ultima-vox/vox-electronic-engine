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
.card .cv{position:relative;background:#040C14;border-radius:6px;overflow:hidden}
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
               + (f'<b>Mixer dock (bottom only)</b><i>{mix} px · Collapsed 30 / Compact 120–200 / Expanded 300–460 · resize by the top edge</i>' if label else (f'<i>Mixer {mix}</i>' if mix * s >= 14 else '')) + '</div>')
    return "".join(out)

def schematic():
    big = card(1060, 663, zone_map(1440, 900, 1060 / 1440, 300, 290, 168), "Default zones at 1440×900 (Beatmaking)",
               "Side zones hold side panels only, as dock tabs (one visible per zone). The central zone holds 1–2 editors. The Mixer has exactly one place: the bottom dock. Nothing floats over the work area.")
    rules = '''<div class="card" style="--cw:330px;width:330px"><div class="cl">Docking rules</div><ol class="ol">
<li><b>Side panels</b> (Pad Rack, Library, Inspector, Generator, Routing, Settings) dock into the left or right zone, as a tab or as a stacked section. Never into the central zone.</li>
<li><b>Editors</b> (Step Sequencer, Song Arranger, Sound Editor, Sample Editor) dock only in the central zone: 1 editor, or 2 split vertical/horizontal.</li>
<li><b>Mixer</b> docks only at the bottom. Three states; it pushes the work area, never overlays it.</li>
<li>Every panel header: grip (drag), title or tabs, Maximize (editors), Hide. Inspector adds Pin.</li>
<li>Drag a header → valid targets light up (cyan); invalid targets do not. Esc cancels. Dropping elsewhere changes nothing.</li>
<li>Each panel exists once. Opening a hidden panel from <b>Panels</b> restores it to its last zone.</li>
<li>Undo (chrome) covers project edits only; layout changes are not part of the project undo history. <b>Reset Workspace</b> restores the preset.</li></ol></div>'''
    mats = [("1600×1000", zone_map(1600, 1000, .2, 300, 290, 200, label=False, split=False)),
            ("1440×900", zone_map(1440, 900, .2, 300, 290, 168, label=False, split=False)),
            ("1280×720", zone_map(1280, 720, .2, 250, 250, 138, label=False, split=False)),
            ("1040×700", zone_map(1040, 700, .2, 196, 0, 30, rails=True, label=False, split=False))]
    thumbs = '<div class="row">' + "".join(card(int(W * .2), int(H * .2), m, n) for n, m, W, H in
                                          [(n, m, int(n.split("×")[0]), int(n.split("×")[1])) for n, m in mats]) + '</div>'
    table = '''<table class="t"><tr><th>Window</th><th>Left zone</th><th>Right zone</th><th>Central</th><th>Mixer default</th><th>Pad Rack</th><th>Step lanes</th><th>Parameter lanes</th></tr>
<tr><td class="k">≥ 1400 × ≥ 860<br>(1600×1000, 1440×900)</td><td>300 px</td><td>290 px</td><td>≥ 786 px, 1–2 editors</td><td>Compact 168–200</td><td>Grid 4×4</td><td>16; full names when the editor is ≥ 900 px wide (1600), short names below (1440)</td><td>2</td></tr>
<tr><td class="k">1240–1399 × 720–859<br>(1280×720)</td><td>250 px</td><td>250 px</td><td>≥ 716 px; second editor as tab if &lt; 2 × min height</td><td>Compact 138–150</td><td>List, full names</td><td>16, short names</td><td>1 (selector switches)</td></tr>
<tr><td class="k">1040–1239 × 700–719<br>(1040×700, minimum)</td><td colspan="2">Both zones become rails (34 px). One side panel open at a time, docked at its minimum width (left 196 / right 240), the last one used.</td><td>≥ 700 px; one editor + tab</td><td>Collapsed 30</td><td>List, short names, no note column</td><td>16, short names</td><td>2</td></tr>
<tr><td class="k">&lt; 1040 × 700</td><td colspan="7">Not supported: the window cannot be made smaller than 1040×700 (host resize is limited to the minimum). Larger windows follow the rows above.</td></tr></table>'''
    order = '''<ol class="ol"><li>Mixer Compact → Collapsed when the central zone would drop below its editors' minimum height (Step 380, Song 230, Sound Editor 440, Sample Editor 170).</li>
<li>Second central editor → tab in the same slot when two minimums do not fit (vertical split: heights; horizontal split: widths, 700 each).</li>
<li>Parameter lanes 2 → 1 → hidden (the lane selector stays).</li>
<li>Pad Rack Grid → List; Step lane names → short names.</li>
<li>Right zone → rail, then left zone → rail (one side panel open at a time).</li>
<li>Expanded Mixer is clamped so the central zone keeps its minimum; the dock never covers an editor.</li>
<li>Growing the window reverses the same steps in the opposite order; user choices (hidden panels, chosen tab, Mixer state) are kept, only forced changes are undone.</li></ol>'''
    presets = '''<table class="t"><tr><th>Workspace preset</th><th>Left</th><th>Central</th><th>Right (tabs)</th><th>Mixer</th></tr>
<tr><td class="k">Beatmaking (default)</td><td>Pad Rack · Grid</td><td>Step Sequencer + 2 parameter lanes</td><td>Inspector · Generator · Routing · Settings</td><td>Compact</td></tr>
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
    rack = [card(300, 430, pad_rack(0, 0, 300, 430, "grid", sel=3, hit=(0,)), "Pad Rack · Grid", "White outline = selection (Pad 4). Ring in the pad colour = hit flash (Pad 1 playing). ▶ corner = audition. Three different signals. Pad 10: ‘● missing’ = its sample file is missing (reference kept, the pad plays its synthesis), as in the Library panel."),
            card(300, 430, pad_rack(0, 0, 300, 430, "list", sel=3, hit=(0,)), "Pad Rack · List · same selection", "Same pad selected, same M/S, same hit indicator (dot). Grid/List is a view switch, not a different panel."),
            '''<div class="card" style="--cw:520px;width:520px"><div class="cl">Pad interactions</div><table class="t">
<tr><th>Gesture</th><th>Result</th></tr>
<tr><td class="k">Click pad body</td><td>Select (Inspector follows unless pinned). No sound.</td></tr>
<tr><td class="k">Click ▶ / Enter</td><td>Audition at velocity 100 (Shift = 127). Selection unchanged.</td></tr>
<tr><td class="k">Ctrl/Cmd-click</td><td>Add to selection (for M/S/output edits across pads).</td></tr>
<tr><td class="k">M / S</td><td>Pad mute / solo (host parameters drm.pNN.mute / solo).</td></tr>
<tr><td class="k">Double-click</td><td>Open the Sound Editor for this pad (central zone).</td></tr>
<tr><td class="k">Drop audio file</td><td>Load into player A of that pad (B with Alt). Same as Load…</td></tr>
<tr><td class="k">Arrows</td><td>Move selection when the Pad Rack has keyboard focus. Space/transport keys are never taken.</td></tr></table></div>''']
    rows.append('<h2>Pad Rack: grid and list, one selection</h2><div class="row">' + "".join(rack) + '</div>')
    mixw = 1420
    mix = [card(mixw, 30, mixer(0, 0, mixw, 30, "collapsed"), "Mixer · Collapsed (30 px)", "Status line: Main meter, level, peak, enabled outputs, state switch."),
           card(mixw, 150, mixer(0, 0, mixw, 150, "compact"), "Mixer · Compact (default 150–168 px)", "All 16 strips: vertical fader, meter, Pan, M/S. Main on the right. Sends and outputs are in Expanded or the Inspector."),
           card(1040, 420, mixer(0, 0, 1040, 420, "expanded", bank=1), "Mixer · Expanded · bank 1–8 (narrow window)", "When 16 strips no longer fit their minimum width (60 px), the bank switch shows 1–8 / 9–16. FX returns (Delay: time, feedback, tone, return; Reverb: size, damp, return) and Main stay visible.")]
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
    sheet("07-states", 1480, "Key states", "Inspector, Pad Rack views, Mixer states, split editors, dock-target previews, menus, Drag MIDI, song edit target", "".join(rows))

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
    typo = [("Product name", 15, 600), ("Panel title / tab", 12, 600), ("Body / values", 12, 400), ("Label", 11, 400), ("Caption (uppercase)", 10, 400)]
    body += '<h2>Typography (Inter in renders; JUCE uses the platform UI font: Segoe UI on Windows)</h2><table class="t"><tr><th>Role</th><th>Size / weight</th><th>Sample</th></tr>' + "".join(
        f'<tr><td class="k">{r}</td><td>{s} px / {w}</td><td style="font-size:{s}px;font-weight:{w};{"text-transform:uppercase;letter-spacing:.09em;color:#6F8A9D" if s == 10 else ""}">Closed Hat · 145.0 BPM · −6.2 dB</td></tr>' for r, s, w in typo) + '</table>'
    body += '''<h2>Sizes</h2><table class="t"><tr><th>Item</th><th>Value</th></tr>
<tr><td class="k">Gutter between docked panels</td><td>6 px; panel radius 6; control radius 5–6</td></tr>
<tr><td class="k">Control heights</td><td>button 28 (small 24), segmented 26 (large 32), dropdown 26, toggle 24 (in rows 16–18)</td></tr>
<tr><td class="k">Panel header</td><td>30 px: grip 14, title/tabs, actions 22×22</td></tr>
<tr><td class="k">Knobs</td><td>44 (editor), 40 (sample), 32 (mixer expanded), 24 (mixer compact), 20 inline; 270° arc, label above, value below</td></tr>
<tr><td class="k">Hit targets</td><td>≥ 16 px for dense rows (M/S in lanes), ≥ 22 px elsewhere</td></tr></table>'''
    # components with states
    def st(label, html, w=120):
        return f'<div style="display:flex;flex-direction:column;align-items:flex-start;gap:6px;width:{w}px"><span class="cap">{label}</span>{html}</div>'
    btns = [st("default", '<span class="btn">Generate</span>'), st("hover", '<span class="btn" style="background:#13283A;border-color:#2B4A60">Generate</span>'),
            st("pressed", '<span class="btn" style="background:#06121D;border-color:#2B4A60">Generate</span>'), st("focused", '<span class="btn focusring">Generate</span>'),
            st("disabled", '<span class="btn dis">Paste</span>'), st("primary", '<span class="btn pri">Generate Pattern</span>', 150), st("danger", '<span class="btn danger">Panic</span>'), st("on (toggle)", '<span class="btn on">Loop</span>')]
    segs = [st("segmented", '<div class="seg"><i class="on">Grid</i><i>List</i></div>'), st("tabs", '<div class="ph" style="width:220px;border:1px solid var(--subtle)"><div class="tabs"><i class="on">Inspector</i><i>Generator</i></div></div>', 230),
            st("dropdown", '<span class="sel">LP 24</span>'), st("M / S", '<span style="display:flex;gap:4px"><span class="tg">M</span><span class="tg m">M</span><span class="tg">S</span><span class="tg s">S</span></span>', 140),
            st("toggle", '<span style="display:flex;gap:4px"><span class="tg">Reverse</span><span class="tg on">Loop</span></span>', 150), st("lane on/off", '<span style="display:flex;gap:8px;align-items:center"><i style="width:9px;height:9px;border-radius:50%;background:var(--cyan)"></i><i style="width:9px;height:9px;border-radius:50%;border:1.5px solid #3D5566"></i></span>')]
    knobs = [st("default", knob("Cutoff", "4.80 kHz", .62, 44, kw=80), 90), st("hover", knob("Cutoff", "4.80 kHz", .62, 44, kw=80).replace("#1D3B50", "#2B4A60"), 90),
             st("drag (value tip)", knob("Cutoff", "4.80 kHz", .62, 44, kw=80) + '<span class="chip" style="color:var(--t1)">4.80 kHz · Shift = fine</span>', 170),
             st("bipolar", knob("Pan", "L 20", .4, 44, bipolar=True, kw=80), 90), st("disabled", knob("Split", "vel 64", .5, 44, kw=80, dim=True), 90),
             st("mixer fader + meter", f'<div style="display:flex;gap:6px">{vfader(110, .6, 18)}{meter(110, .7, "#32D296", 6, peak=.8)}{meter(110, .97, "#32D296", 6, peak=1.0)}</div>', 120)]
    cells = []
    for lab, inner, extra in [("off", "", ""), ("on · vel 64", '<div style="position:absolute;left:0;right:0;bottom:0;height:50%;background:#56C2A6;border-radius:2px"></div>', ""),
                              ("on · vel 127", '<div style="position:absolute;left:0;right:0;bottom:0;height:100%;background:#56C2A6;border-radius:2px"></div>', ""),
                              ("accent", '<div style="position:absolute;left:0;right:0;bottom:0;height:90%;background:#F0A35E;border-radius:2px"></div><div style="position:absolute;left:3px;right:3px;top:1px;height:2px;background:#E9F3FA"></div>', ""),
                              ("chance < 100 %", '<div style="position:absolute;left:0;right:0;bottom:0;height:70%;background:#56C2A6;background-image:repeating-linear-gradient(135deg,rgba(6,18,29,.55) 0 2px,transparent 2px 5px);border-radius:2px"></div>', ""),
                              ("ratchet 2×", '<div style="position:absolute;left:0;right:0;bottom:0;height:70%;background:#56C2A6;border-radius:2px"></div><div style="position:absolute;left:3px;bottom:2px;display:flex;gap:2px"><i style="width:3px;height:3px;background:#06121D;border-radius:50%"></i><i style="width:3px;height:3px;background:#06121D;border-radius:50%"></i></div>', ""),
                              ("locked", '<div style="position:absolute;left:0;right:0;bottom:0;height:90%;background:#F0A35E;border-radius:2px"></div><div style="position:absolute;right:2px;top:3px;width:6px;height:5px;border:1.5px solid #06121D;border-radius:1px"></div>', ""),
                              ("selected", '<div style="position:absolute;left:0;right:0;bottom:0;height:60%;background:#56C2A6;border-radius:2px"></div>', "box-shadow:0 0 0 2px #E9F3FA;"),
                              ("playhead column", "", "background:#13283A;box-shadow:inset 0 0 0 1px rgba(0,221,245,.55);")]:
        cells.append(st(lab, f'<div style="position:relative;width:52px;height:30px;background:#13283A;border-radius:3px;overflow:hidden;{extra}">{inner}</div>', 96))
    heads = [st("panel header", '<div class="pnl" style="position:relative;width:260px;height:32px"><div class="ph">' + head("Generator") + '</div></div>', 270),
             st("focused panel", '<div class="pnl focus" style="position:relative;width:260px;height:32px"><div class="ph">' + head(None, tabs=["Step Sequencer"], maxi=True) + '</div></div>', 270),
             st("Inspector pinned", '<div class="pnl" style="position:relative;width:260px;height:32px"><div class="ph">' + head("Inspector", pin=True) + '</div></div>', 270)]
    body += ('<h2>Components and states</h2><div class="sub">Buttons</div><div class="row">' + "".join(btns) + '</div>'
             + '<div class="sub" style="margin-top:12px">Selection controls</div><div class="row">' + "".join(segs) + '</div>'
             + '<div class="sub" style="margin-top:12px">Knobs, fader, meter (knob: vertical drag, Shift = fine, double-click = default, wheel = step; automation from the host moves it live)</div><div class="row">' + "".join(knobs) + '</div>'
             + '<div class="sub" style="margin-top:12px">Step cell</div><div class="row">' + "".join(cells) + '</div>'
             + '<div class="sub" style="margin-top:12px">Panel headers (focus = cyan border; keyboard focus moves panel to panel with F6)</div><div class="row">' + "".join(heads) + '</div>')
    sheet("08-tokens-components", 1480, "Design tokens and components", "Family tokens from libs/vox-ui/Tokens.h plus the proposed Drums semantic layer · contrast computed against panel #0B1925", body)

if __name__ == "__main__":
    schematic(); states(); tokens()
    print("sheets ok")
