# 4-bar, 145 BPM, channel-1 Full-on groove for Vox Full-on Drums.
# usage: make_groove_midi.py <groove.mid> [<eight-part-tracks.mid>]
import struct, sys
PPQ = 480; S = PPQ // 4  # sixteenth
KICK, RIM, SNARE, CLAP, CH, TOM, OH, CYM = 36, 37, 38, 39, 42, 45, 46, 49
ev = []
def hit(step, note, vel):
    t = step * S
    ev.append((t, bytes([0x90, note, vel]))); ev.append((t + S // 2, bytes([0x80, note, 0])))
for bar in range(4):
    b = bar * 16
    if bar == 0:
        hit(b, CYM, 100)
    for s in range(16):
        if s % 4 == 0 and not (bar == 3 and s == 12):
            hit(b + s, KICK, 120)
        if s % 4 == 2:
            hit(b + s, OH, 92)                       # off-beat open hat
        elif not (bar == 3 and s >= 12):
            hit(b + s, CH, 96 if s % 2 == 0 else 68) # 16th closed hats, accented
    hit(b + 4, SNARE, 110); hit(b + 4, CLAP, 100)
    hit(b + 12, SNARE, 110); hit(b + 12, CLAP, 100)
    if bar in (1, 3):
        hit(b + 7, RIM, 80); hit(b + 15, RIM, 70)
    if bar == 3:                                     # fill
        for s, v in zip((12, 13, 14, 15), (90, 100, 110, 120)):
            hit(b + s, TOM, v)
ev.sort(key=lambda e: (e[0], e[1][0] & 0xF0 == 0x90))
def vlq(x):
    out = [x & 0x7F]; x >>= 7
    while x: out.insert(0, (x & 0x7F) | 0x80); x >>= 7
    return bytes(out)
END = 64 * S  # four bars
tempo = round(60_000_000 / 145)
CONDUCTOR = b"\x00\xFF\x51\x03" + tempo.to_bytes(3, "big") + b"\x00\xFF\x58\x04\x04\x02\x18\x08"
def track(name, events, conductor=False):
    trk = bytearray(CONDUCTOR if conductor else b"")
    trk += b"\x00\xFF\x03" + bytes([len(name)]) + name
    last = 0
    for t, b in events:
        trk += vlq(t - last) + b; last = t
    trk += vlq(END - last) + b"\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(trk)) + trk
# One-track groove (SMF type 0).
open(sys.argv[1], "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) +
                              track(b"Full-on Drums 4-bar groove", ev, conductor=True))
print(len(ev) // 2, "hits")
# The same groove as eight named parts (SMF type 1): a conductor track with
# tempo and metre, then one track per pad, each on channel 1 with only its
# own fixed note. Cubase imports them as eight MIDI tracks.
if len(sys.argv) > 2:
    parts = [(b"Kick", KICK), (b"Rim", RIM), (b"Snare", SNARE), (b"Clap", CLAP),
             (b"Closed Hat", CH), (b"Tom", TOM), (b"Open Hat", OH), (b"Cymbal", CYM)]
    data = track(b"Full-on Drums 145 BPM", [], conductor=True)
    for name, note in parts:
        data += track(name, [e for e in ev if e[1][1] == note])
    open(sys.argv[2], "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1 + len(parts), PPQ) + data)
    print(len(parts), "named part tracks")
