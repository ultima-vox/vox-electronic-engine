# 4-bar, 145 BPM, channel-1 Full-on groove for Vox Full-on Drums.
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
tempo = round(60_000_000 / 145)
trk = bytearray(b"\x00\xFF\x51\x03" + tempo.to_bytes(3, "big"))
trk += b"\x00\xFF\x58\x04\x04\x02\x18\x08"
name = b"Full-on Drums 4-bar groove"
trk += b"\x00\xFF\x03" + bytes([len(name)]) + name
last = 0
for t, b in ev:
    trk += vlq(t - last) + b; last = t
trk += vlq(64 * S - last) + b"\xFF\x2F\x00"
open(sys.argv[1], "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(trk)) + trk)
print(len(ev) // 2, "hits")
