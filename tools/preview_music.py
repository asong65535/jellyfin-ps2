#!/usr/bin/env python3
"""
Mockups for the album track list and the player, drawn with the same font,
palette and coordinates the console uses.
"""

import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import preview as p
from PIL import Image, ImageDraw

W, H = p.W, p.H

ALBUM  = "Symphony No. 9"
ARTIST = "Dvorak"
TRACKS = [
    ("Adagio - Allegro molto",        "12:41"),
    ("Largo",                          "13:08"),
    ("Scherzo. Molto vivace",           "8:22"),
    ("Allegro con fuoco",              "11:55"),
    ("Slavonic Dance No. 1",            "4:03"),
    ("Slavonic Dance No. 2",            "5:17"),
    ("Carnival Overture",               "9:44"),
    ("Song to the Moon",                "6:31"),
]


def chrome(d, title_right=None):
    p.text(d, 24, 16, "Jellyfin", p.WHITE)
    p.text(d, 24 + p.tw("Jellyfin") + 6, 16, "PS2", p.PURPLE)

    if title_right:
        p.text(d, W - 24 - p.tw(title_right), 16, title_right, p.DIM)

    p.box(d, 24, 44, W - 48, 1, p.RULE)


def cover(d, x, y, s):
    """Stand-in for album art."""
    p.round_box(d, x, y, s, s, 4, (0x3A, 0x33, 0x5E))
    p.box(d, x, y, s, 3, p.PURPLE)


# ---------------------------------------------------------------- tracks

def track_list(sel=2):
    im, d = p.new()
    p.waves(d)
    chrome(d, "8 tracks")

    cover(d, 40, 60, 96)

    tx = 152
    p.text(d, tx, 58, ALBUM, p.WHITE, 2.0)
    p.text(d, tx, 106, f"{ARTIST}   .   1893   .   Album", p.DIM)

    p.box(d, 40, 140, W - 80, 1, p.RULE)

    for i, (name, dur) in enumerate(TRACKS):
        y = 152 + i * 26

        if i == sel:
            p.round_box(d, 40, y - 3, W - 80, 26, 4, p.ROWSEL)
            p.box(d, 40, y - 3, 3, 26, p.PURPLE)

        num = f"{i + 1:02d}"
        p.text(d, 56, y, num, p.PURPLE if i == sel else p.RULE)
        p.text(d, 92, y, name, p.WHITE if i == sel else p.DIM)
        p.text(d, W - 56 - p.tw(dur), y, dur, p.DIM)

    p.hint_row(d, [("cross", "Play"), ("circle", "Back"), ("start", "Quit")])
    return im


# ---------------------------------------------------------------- player

def transport(d, x, y, kind, enabled, focused):
    """One transport button: prev, play, pause, stop, next."""
    bw, bh = 52, 42

    if focused:
        p.round_box(d, x - 3, y - 3, bw + 6, bh + 6, 7, p.PURPLE)

    fill = (0x2C, 0x28, 0x56) if enabled else (0x1C, 0x1E, 0x3C)
    p.round_box(d, x, y, bw, bh, 5, fill)

    col = p.WHITE if enabled else p.RULE
    cx, cy = x + bw / 2, y + bh / 2

    if kind == "play":
        d.polygon([(cx - 7, cy - 10), (cx - 7, cy + 10), (cx + 10, cy)],
                  fill=col)
    elif kind == "pause":
        p.box(d, cx - 8, cy - 10, 6, 20, col)
        p.box(d, cx + 2, cy - 10, 6, 20, col)
    elif kind == "stop":
        p.box(d, cx - 9, cy - 9, 18, 18, col)
    elif kind == "prev":
        d.polygon([(cx + 2, cy - 9), (cx + 2, cy + 9), (cx - 8, cy)], fill=col)
        p.box(d, cx - 11, cy - 9, 3, 18, col)
    elif kind == "next":
        d.polygon([(cx - 2, cy - 9), (cx - 2, cy + 9), (cx + 8, cy)], fill=col)
        p.box(d, cx + 8, cy - 9, 3, 18, col)


def player(playing, sel, has_prev=True, has_next=True):
    im, d = p.new()
    p.waves(d)
    chrome(d, "3 of 8")

    cover(d, (W - 120) // 2, 58, 120)

    title = TRACKS[2][0]
    p.text_c(d, W / 2, 190, title, p.WHITE)
    p.text_c(d, W / 2, 214, f"{ARTIST}   .   {ALBUM}", p.DIM)

    # progress
    bx, bw = 110, W - 220
    p.box(d, bx, 250, bw, 5, p.RULE)
    if playing:
        p.box(d, bx, 250, int(bw * 0.38), 5, p.PURPLE)
    p.text(d, bx, 264, "5:02" if playing else "0:00", p.DIM)
    p.text(d, bx + bw - p.tw("13:08"), 264, "13:08", p.DIM)

    # transport row
    kinds = ["prev", "pause" if playing else "play", "stop", "next"]
    enabled = [has_prev, True, playing, has_next]

    gap = 14
    total = len(kinds) * 52 + (len(kinds) - 1) * gap
    x = (W - total) / 2

    for i, k in enumerate(kinds):
        transport(d, x, 300, k, enabled[i], i == sel)
        x += 52 + gap

    p.hint_row(d, [("cross", "Select"), ("dpad", "Move"),
                   ("circle", "Back"), ("start", "Quit")])
    return im


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__))
    shots = [
        ("preview_tracks.png",       track_list()),
        ("preview_player_idle.png",  player(False, 1)),
        ("preview_player_play.png",  player(True, 1)),
    ]
    for name, im in shots:
        im.convert("RGB").resize((W * 2, H * 2), Image.NEAREST) \
          .save(os.path.join(out, name))
        print("wrote", name)
