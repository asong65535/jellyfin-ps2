#!/usr/bin/env python3
"""
Render the app's screens at 640x448 using the same font and the same
coordinates the C code uses, so the layout can be judged without a
console. Not a simulator: it mirrors the draw calls by hand.
"""

import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TTF  = os.path.join(ROOT, "assets", "mania.ttf")

W, H = 640, 448
SIZE = 20                        # matches genfont.py

BG      = (0x10, 0x12, 0x2A)
WHITE   = (0xFF, 0xFF, 0xFF)
PURPLE  = (0xAA, 0x5C, 0xC3)
LILAC   = (0xC7, 0xB8, 0xE8)
FIELD   = (0x5A, 0x4E, 0x8E)
FLINE   = (0x9B, 0x87, 0xE0)
ROWSEL  = (0x2C, 0x28, 0x56)
KEY     = (0xF2, 0xF2, 0xF8)
KEYSEL  = (0xCE, 0xCE, 0xD8)
KEYTEXT = (0x24, 0x24, 0x3A)
DIM     = (0x8A, 0x8E, 0xB0)
RULE    = (0x30, 0x32, 0x52)
WAVE1   = (0x39, 0x2E, 0x63)
WAVE2   = (0x55, 0x45, 0x8E)

font = ImageFont.truetype(TTF, SIZE)
_m   = ImageDraw.Draw(Image.new("RGBA", (1, 1)))

CELL_H = sum(font.getmetrics()) + 2


def adv(ch):
    """Matches genfont.py: rounded advance plus one."""
    return int(round(_m.textlength(ch, font=font))) + 1


def tw(s, scale=1.0):
    return sum(adv(c) for c in s) * scale


def text(d, x, y, s, col, scale=1.0):
    """Mirrors font_draw: cell top at y, left bearing stripped."""
    pen = x
    for ch in s:
        if ch != " ":
            bb = _m.textbbox((0, 0), ch, font=font)
            if scale == 1.0:
                d.text((pen + 1 - bb[0], y + 1), ch, font=font, fill=col)
            else:
                cw = int(14 * scale)
                cell = Image.new("RGBA", (14, CELL_H), (0, 0, 0, 0))
                ImageDraw.Draw(cell).text((1 - bb[0], 1), ch, font=font,
                                          fill=col)
                cell = cell.resize((cw, int(CELL_H * scale)), Image.NEAREST)
                d._image.alpha_composite(cell, (int(pen), int(y)))
        pen += adv(ch) * scale


def text_c(d, cx, y, s, col, scale=1.0):
    text(d, cx - tw(s, scale) / 2, y, s, col, scale)


def box(d, x, y, w, h, col):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=col)


def round_box(d, x, y, w, h, r, col):
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=col)


def waves(d):
    import math
    for base, amp, per, ph, col in ((386, 10, 90, 0.0, WAVE1),
                                    (404, 8, 115, 2.1, WAVE2)):
        for x in range(0, W, 2):
            yy = base + math.sin(x / per + ph) * amp
            box(d, x, yy, 2, H - yy, col)


def hint_row(d, items):
    """items: list of (glyph, label)."""
    gap = 22
    total = sum(22 + tw(l) for _, l in items) + gap * (len(items) - 1)
    x = (W - total) / 2
    y = H - 34

    for g, label in items:
        cx, cy = x + 10, y + 6
        if g in ("cross", "square", "circle", "triangle"):
            d.ellipse([cx - 9, cy - 9, cx + 9, cy + 9], outline=LILAC, width=2)
        if g == "cross":
            d.line([cx - 5, cy - 5, cx + 5, cy + 5], fill=LILAC, width=2)
            d.line([cx - 5, cy + 5, cx + 5, cy - 5], fill=LILAC, width=2)
        elif g == "square":
            d.rectangle([cx - 5, cy - 5, cx + 4, cy + 4], outline=LILAC)
        elif g == "circle":
            d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], outline=LILAC)
        elif g == "triangle":
            d.polygon([(cx, cy - 5), (cx + 5, cy + 4), (cx - 5, cy + 4)],
                      outline=LILAC)
        elif g == "start":
            d.rounded_rectangle([cx - 12, cy - 5, cx + 12, cy + 5], radius=4,
                                fill=LILAC)
        elif g == "dpad":
            box(d, cx - 3, cy - 8, 6, 16, LILAC)
            box(d, cx - 8, cy - 3, 16, 6, LILAC)
        elif g == "l1r1":
            d.rounded_rectangle([cx - 11, cy - 6, cx + 11, cy + 2], radius=3,
                                fill=LILAC)

        text(d, x + 24, y - 1, label, LILAC)
        x += 22 + tw(label) + gap


def new():
    im = Image.new("RGBA", (W, H), BG + (255,))
    return im, ImageDraw.Draw(im)


# ---------------------------------------------------------------- movies

MOVIES = [
    ("Hajime no Ippo - Champion Road", 2003),
    ("Hajime no Ippo: Mashiba vs. Kimura", 2003),
    ("Karate Kid: Legends", 2025),
    ("Les Nouveaux Heros", 2014),
    ("Mrs Doubtfire (1993) [tvdbid", None),
    ("Test Jellyfin", None),
]


def movies(sel=0):
    im, d = new()

    text(d, 40, 30, "Movies  -  jimmy", WHITE)
    sub = "1-6 of 6"
    text(d, W - 40 - tw(sub), 30, sub, DIM)
    box(d, 40, 62, W - 80, 1, RULE)

    for i, (name, year) in enumerate(MOVIES):
        y = 78 + i * 24
        if i == sel:
            round_box(d, 40, y - 1, W - 80, 24, 4, ROWSEL)
            box(d, 40, y - 1, 3, 24, PURPLE)
        text(d, 58, y, name, WHITE if i == sel else DIM)
        if year:
            text(d, W - 96, y, str(year), DIM)

    hint_row(d, [("dpad", "Scroll"), ("l1r1", "Page"), ("circle", "Back")])
    return im


# --------------------------------------------------------------- connect

def connect(url="http://10.0.1.5:8096"):
    im, d = new()
    waves(d)

    nw, sw = tw("Jellyfin", 2.0), tw("PS2")
    x = (W - (nw + 8 + sw)) / 2
    text(d, x, 34, "Jellyfin", WHITE, 2.0)
    text(d, x + nw + 8, 34 + CELL_H * 2 - CELL_H - 4, "PS2", PURPLE)
    box(d, 70, 96, W - 140, 1, RULE)

    text_c(d, W / 2, 116, "Server URL:", WHITE)

    fx, fw = 90, W - 180
    round_box(d, fx, 148, fw, 30, 4, FIELD)
    box(d, fx, 178, fw, 3, FLINE)
    text(d, fx + 14, 152, url, LILAC)
    box(d, fx + 14 + tw(url) + 1, 153, 2, 20, WHITE)

    rows = [("1234567890", 44), ("qwertyuiop", 44), ("asdfghjkl", 44)]
    top, pitch, kh, gapk = 204, 30, 24, 6

    ri = 0
    for chars, kw in rows:
        total = len(chars) * kw + (len(chars) - 1) * gapk
        kx = (W - total) / 2
        for ci, ch in enumerate(chars):
            on = (ri == 1 and ci == 0)
            round_box(d, kx, top + ri * pitch, kw, kh, 4,
                      KEYSEL if on else KEY)
            if on:
                box(d, kx + 2, top + ri * pitch + kh - 3, kw - 4, 3, FLINE)
            text_c(d, kx + kw / 2, top + ri * pitch + 1, ch, KEYTEXT)
            kx += kw + gapk
        ri += 1

    specials = [("Caps", 74), ("z", 44), ("x", 44), ("c", 44), ("v", 44),
                ("b", 44), ("n", 44), ("m", 44), ("Backspace", 90)]
    total = sum(w for _, w in specials) + gapk * (len(specials) - 1)
    kx = (W - total) / 2
    for lbl, kw in specials:
        round_box(d, kx, top + 3 * pitch, kw, kh, 4, KEY)
        text_c(d, kx + kw / 2, top + 3 * pitch + 1, lbl, KEYTEXT)
        kx += kw + gapk

    specials = [("#+=", 74), ("Space", 240), ("Enter", 120)]
    total = sum(w for _, w in specials) + gapk * (len(specials) - 1)
    kx = (W - total) / 2
    for lbl, kw in specials:
        round_box(d, kx, top + 4 * pitch, kw, kh, 4, KEY)
        text_c(d, kx + kw / 2, top + 4 * pitch + 1, lbl, KEYTEXT)
        kx += kw + gapk

    ipline = "PS2  10.0.1.90"
    text(d, W - 40 - tw(ipline), 360, ipline, RULE)

    hint_row(d, [("cross", "Select"), ("square", "Delete"),
                 ("circle", "Clear"), ("start", "Connect")])
    return im


if __name__ == "__main__":
    out = os.path.join(ROOT, "tools")
    for name, im in (("preview_movies.png", movies()),
                     ("preview_connect.png", connect())):
        im.convert("RGB").resize((W * 2, H * 2), Image.NEAREST) \
          .save(os.path.join(out, name))
        print("wrote", name)


# ---------------------------------------------------------------- browse

TABS = ["MOVIES", "SERIES", "SEARCH"]

POSTER_W, POSTER_H = 90, 135
HGAP, VGAP = 26, 14
GRID_X = (W - (3 * POSTER_W + 2 * HGAP)) // 2
GRID_Y = 100


def poster_slot(d, x, y, title, selected, has_image):
    """A poster, or a placeholder carrying the title until images work."""
    if selected:
        round_box(d, x - 4, y - 4, POSTER_W + 8, POSTER_H + 8, 5, PURPLE)

    if has_image:
        # stand-in for real artwork
        round_box(d, x, y, POSTER_W, POSTER_H, 3, (0x3A, 0x33, 0x5E))
    else:
        round_box(d, x, y, POSTER_W, POSTER_H, 3, (0x1C, 0x1E, 0x3C))
        box(d, x, y, POSTER_W, 3, PURPLE if selected else RULE)

        # wrap the title inside the slot, since there is nothing else
        words, line, ly = title.split(), "", y + 12
        for wd in words:
            probe = (line + " " + wd).strip()
            if tw(probe) > POSTER_W - 12 and line:
                text_c(d, x + POSTER_W / 2, ly, line,
                       WHITE if selected else DIM)
                ly += 20
                line = wd
            else:
                line = probe
            if ly > y + POSTER_H - 24:
                break
        if line and ly <= y + POSTER_H - 24:
            text_c(d, x + POSTER_W / 2, ly, line, WHITE if selected else DIM)


def browse(tab=0, sel=0, with_images=False):
    im, d = new()
    waves(d)

    # brand, small, top left
    text(d, 24, 16, "Jellyfin", WHITE)
    text(d, 24 + tw("Jellyfin") + 6, 16, "PS2", PURPLE)

    # page counter, top right
    cnt = "1-6 of 6"
    text(d, W - 24 - tw(cnt), 16, cnt, DIM)

    # tabs, centred, active one underlined
    gap = 44
    total = sum(tw(t) for t in TABS) + gap * (len(TABS) - 1)
    x = (W - total) / 2
    for i, t in enumerate(TABS):
        text(d, x, 52, t, WHITE if i == tab else DIM)
        if i == tab:
            box(d, x, 76, tw(t), 3, PURPLE)
        x += tw(t) + gap

    box(d, 60, 88, W - 120, 1, RULE)

    for i, (name, _year) in enumerate(MOVIES):
        cx = GRID_X + (i % 3) * (POSTER_W + HGAP)
        cy = GRID_Y + (i // 3) * (POSTER_H + VGAP)
        poster_slot(d, cx, cy, name, i == sel, with_images)

    hint_row(d, [("cross", "Open"), ("triangle", "Info"),
                 ("l1r1", "Tab"), ("circle", "Back")])
    return im


# ---------------------------------------------------------------- detail

def detail():
    im, d = new()
    waves(d)

    text(d, 24, 16, "Jellyfin", WHITE)
    text(d, 24 + tw("Jellyfin") + 6, 16, "PS2", PURPLE)
    box(d, 24, 44, W - 48, 1, RULE)

    px, py, pw, ph = 40, 66, 130, 195
    round_box(d, px, py, pw, ph, 4, (0x3A, 0x33, 0x5E))

    tx = px + pw + 28
    text(d, tx, 66, "Hajime no Ippo", WHITE, 2.0)
    text(d, tx, 112, "Champion Road", WHITE, 2.0)
    text(d, tx, 164, "2003   .   1h 30m   .   Movie", DIM)

    body = ("Ippo faces Sanada Kazuki for the Featherweight title while "
            "Takamura prepares for his own bout. Adapted from the manga "
            "by George Morikawa.")
    words, line, ly = body.split(), "", 196
    for wd in words:
        probe = (line + " " + wd).strip()
        if tw(probe) > W - tx - 40 and line:
            text(d, tx, ly, line, DIM)
            ly += 22
            line = wd
        else:
            line = probe
    if line:
        text(d, tx, ly, line, DIM)

    hint_row(d, [("cross", "Play"), ("circle", "Back")])
    return im


# ------------------------------------------------------- browse with rail

RAIL_X   = 30
RAIL_TOP = 100
RAIL_PITCH = 22
RAIL_VIS = 12            # a window, since 26 x 22px will not fit 448 lines

GRID2_X = 178
GRID2_Y = 92


def rail(d, letter_idx, focused):
    """A-Z, windowed so the current letter stays near the middle."""
    first = max(0, min(letter_idx - RAIL_VIS // 2, 26 - RAIL_VIS))

    for row in range(RAIL_VIS):
        i = first + row
        ch = chr(ord('A') + i)
        y = RAIL_TOP + row * RAIL_PITCH

        if i == letter_idx:
            round_box(d, RAIL_X - 8, y - 1, 26, 22, 4,
                      PURPLE if focused else ROWSEL)
            text_c(d, RAIL_X + 5, y, ch, WHITE)
        else:
            text_c(d, RAIL_X + 5, y, ch, DIM if focused else RULE)

    # hint that the list continues
    if first > 0:
        text_c(d, RAIL_X + 5, RAIL_TOP - 20, "^", RULE)
    if first + RAIL_VIS < 26:
        text_c(d, RAIL_X + 5, RAIL_TOP + RAIL_VIS * RAIL_PITCH, "v", RULE)


S_MOVIES = [
    ("Saving Private Ryan", 1998),
    ("Scarface", 1983),
    ("Se7en", 1995),
    ("Shrek", 2001),
    ("Snatch", 2000),
    ("Spirited Away", 2001),
]


def browse_rail(letter_idx=18, sel=0, on_rail=False, items=None,
                counter="97-102 of 214"):
    im, d = new()
    waves(d)

    text(d, 24, 16, "Jellyfin", WHITE)
    text(d, 24 + tw("Jellyfin") + 6, 16, "PS2", PURPLE)
    text(d, W - 24 - tw(counter), 16, counter, DIM)

    gap = 44
    total = sum(tw(t) for t in TABS) + gap * (len(TABS) - 1)
    x = (W - total) / 2
    for i, t in enumerate(TABS):
        text(d, x, 52, t, WHITE if i == 0 else DIM)
        if i == 0:
            box(d, x, 76, tw(t), 3, PURPLE)
        x += tw(t) + gap

    box(d, 60, 88, W - 120, 1, RULE)

    rail(d, letter_idx, on_rail)

    for i, (name, _y) in enumerate(items or S_MOVIES):
        cx = GRID2_X + (i % 3) * (POSTER_W + HGAP)
        cy = GRID2_Y + (i // 3) * (POSTER_H + VGAP)
        poster_slot(d, cx, cy, name, (not on_rail) and i == sel, False)

    hint_row(d, [("cross", "Open"), ("l1r1", "Page"), ("circle", "Back")])
    return im


# ------------------------------------------------------------ music screens

TRACKS = [
    ("Go Straight",            "2:41"),
    ("In the Bar",             "3:12"),
    ("Never Return Alive",     "2:58"),
    ("Dreamer",                "3:26"),
    ("Slow Moon",              "4:03"),
    ("Under Logic",            "3:18"),
    ("Expander",               "2:47"),
    ("Alien Power",            "3:55"),
    ("Max Man",                "2:30"),
    ("Wave 131",               "3:41"),
]

ALBUM  = "Streets of Rage 2"
ARTIST = "Yuzo Koshiro"


def cover(d, x, y, size):
    """Stand-in for album art."""
    round_box(d, x, y, size, size, 3, (0x3A, 0x33, 0x5E))
    box(d, x, y, size, 3, PURPLE)
    # a couple of blocks so it reads as artwork rather than an empty panel
    box(d, x + size * 0.18, y + size * 0.30, size * 0.28, size * 0.40,
        (0x55, 0x45, 0x8E))
    box(d, x + size * 0.54, y + size * 0.18, size * 0.28, size * 0.52,
        (0x6A, 0x58, 0xA8))


def album(sel=2, first=0, visible=8):
    im, d = new()
    waves(d)

    text(d, 24, 16, "Jellyfin", WHITE)
    text(d, 24 + tw("Jellyfin") + 6, 16, "PS2", PURPLE)
    box(d, 24, 44, W - 48, 1, RULE)

    cover(d, 40, 62, 96)

    text(d, 156, 62, ALBUM, WHITE, 2.0)
    text(d, 156, 112, ARTIST, DIM)
    cnt = f"{len(TRACKS)} tracks   .   33:31"
    text(d, 156, 136, cnt, RULE)

    top = 180
    pitch = 24

    for row in range(visible):
        i = first + row
        if i >= len(TRACKS):
            break

        name, dur = TRACKS[i]
        y = top + row * pitch

        if i == sel:
            round_box(d, 40, y - 3, W - 80, 24, 4, ROWSEL)
            box(d, 40, y - 3, 3, 24, PURPLE)

        col = WHITE if i == sel else DIM
        text(d, 58, y, f"{i + 1:2d}", RULE)
        text(d, 92, y, name, col)
        text(d, W - 56 - tw(dur), y, dur, RULE)

    # scroll hint when the list runs past the window
    if first + visible < len(TRACKS):
        text_c(d, W / 2, top + visible * pitch, "v", RULE)

    hint_row(d, [("cross", "Play"), ("circle", "Back"), ("start", "Quit")])
    return im


def transport(d, cx, cy, kind, enabled=True, focused=False):
    """One transport control: a rounded square with a glyph."""
    s = 44
    x, y = cx - s / 2, cy - s / 2

    fill = PURPLE if focused else (0x2C, 0x28, 0x56)
    if not enabled:
        fill = (0x1C, 0x1E, 0x3C)

    round_box(d, x, y, s, s, 6, fill)
    if focused:
        round_box(d, x + 3, y + 2, s - 6, 3, 1, LILAC)

    g = WHITE if enabled else RULE

    if kind == "play":
        d.polygon([(cx - 6, cy - 9), (cx - 6, cy + 9), (cx + 9, cy)], fill=g)
    elif kind == "pause":
        box(d, cx - 7, cy - 9, 5, 18, g)
        box(d, cx + 2, cy - 9, 5, 18, g)
    elif kind == "stop":
        box(d, cx - 8, cy - 8, 16, 16, g)
    elif kind == "prev":
        box(d, cx - 9, cy - 8, 3, 16, g)
        d.polygon([(cx + 9, cy - 9), (cx + 9, cy + 9), (cx - 4, cy)], fill=g)
    elif kind == "next":
        box(d, cx + 6, cy - 8, 3, 16, g)
        d.polygon([(cx - 9, cy - 9), (cx - 9, cy + 9), (cx + 4, cy)], fill=g)


def player(playing=False, has_next=True, has_prev=True, focus="play",
           elapsed=0.35):
    im, d = new()
    waves(d)

    text(d, 24, 16, "Jellyfin", WHITE)
    text(d, 24 + tw("Jellyfin") + 6, 16, "PS2", PURPLE)
    box(d, 24, 44, W - 48, 1, RULE)

    cover(d, (W - 128) / 2, 62, 128)

    name, dur = TRACKS[2]
    text_c(d, W / 2, 204, name, WHITE, 2.0)
    text_c(d, W / 2, 252, f"{ARTIST}   .   {ALBUM}", DIM)

    # progress
    bx, bw = 110, W - 220
    box(d, bx, 288, bw, 4, RULE)
    if playing:
        box(d, bx, 288, bw * elapsed, 4, PURPLE)
        box(d, bx + bw * elapsed - 1, 284, 3, 12, LILAC)
    text(d, bx, 300, "1:02" if playing else "0:00", RULE)
    text(d, bx + bw - tw(dur), 300, dur, RULE)

    # transport row
    if playing:
        controls = [("prev", has_prev), ("pause", True),
                    ("stop", True), ("next", has_next)]
    else:
        controls = [("prev", has_prev), ("play", True), ("next", has_next)]

    gap = 62
    total = (len(controls) - 1) * gap
    x = W / 2 - total / 2

    for kind, enabled in controls:
        transport(d, x, 356, kind, enabled, focused=(kind == focus))
        x += gap

    hint_row(d, [("cross", "Select"), ("circle", "Back"), ("start", "Quit")])
    return im
