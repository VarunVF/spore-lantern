#!/usr/bin/env python3
"""Generate every image in assets/gfx. All art is original, made by this script.

Pixel art is authored at half resolution and upscaled 2x with nearest
neighbour, because the engine samples textures with GL_LINEAR and draws one
world unit per screen pixel: a texture drawn at its native size stays crisp.
"""
import math
import pathlib
import random

from PIL import Image

OUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "gfx"
SCALE = 2


def rgba(hexstr, a=255):
    hexstr = hexstr.lstrip("#")
    return (int(hexstr[0:2], 16), int(hexstr[2:4], 16), int(hexstr[4:6], 16), a)


def save(img, name, scale=SCALE):
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(OUT / name)


def from_ascii(rows, palette, width=None):
    width = width or max(len(r) for r in rows)
    img = Image.new("RGBA", (width, len(rows)), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        row = row.ljust(width, ".")
        assert len(row) == width, f"row {y} is {len(row)} wide, wanted {width}"
        for x, ch in enumerate(row):
            if ch != ".":
                img.putpixel((x, y), palette[ch])
    return img


def sheet(frames):
    w, h = frames[0].size
    out = Image.new("RGBA", (w * len(frames), h), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (i * w, 0))
    return out


# --------------------------------------------------------------------------
# Tiles
# --------------------------------------------------------------------------
OUTLINE = rgba("#110d1a")
ROCK = [rgba("#2b2443"), rgba("#3a315a"), rgba("#4e4374"), rgba("#64588f")]
RIM = rgba("#8a7cb6")
MOSS = [rgba("#1f5a4a"), rgba("#2f8563"), rgba("#58b87c"), rgba("#9be08f")]


def noise(x, y, seed):
    return random.Random((x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)).random()


def make_tile(mask, seed):
    up, right, down, left = mask & 1, mask & 2, mask & 4, mask & 8
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            # chunky stone texture: low frequency blobs plus a few speckles
            blob = noise(x // 4, y // 3 + (x // 8), seed)
            shade = 0 if blob < 0.55 else 1
            if noise(x, y, seed + 7) > 0.93:
                shade = min(shade + 1, 2)
            # distance to the nearest open side brightens the rock
            d = 99
            if up:
                d = min(d, y)
            if down:
                d = min(d, 15 - y)
            if left:
                d = min(d, x)
            if right:
                d = min(d, 15 - x)
            if d <= 3:
                shade = min(shade + 1, 3)
            if d == 1:
                shade = 3
            col = ROCK[shade]
            if mask == 0:
                col = tuple(int(v * 0.72) for v in col[:3]) + (255,)
            if d == 1 and noise(x, y, seed + 3) > 0.35:
                col = RIM
            if d == 0:
                col = OUTLINE
            img.putpixel((x, y), col)
    if up:
        for x in range(16):
            depth = 3 + int(noise(x // 2, 0, seed + 11) * 3)
            for y in range(depth):
                if y == 0:
                    col = MOSS[3] if noise(x, 1, seed + 5) > 0.5 else MOSS[2]
                elif y == 1:
                    col = MOSS[2]
                elif y == depth - 1:
                    col = MOSS[0]
                else:
                    col = MOSS[1]
                img.putpixel((x, y), col)
        if left:
            for y in range(4):
                img.putpixel((0, y), OUTLINE if y else MOSS[1])
        if right:
            for y in range(4):
                img.putpixel((15, y), OUTLINE if y else MOSS[1])
    return img


def make_tiles():
    atlas = Image.new("RGBA", (8 * 16, 3 * 16), (0, 0, 0, 0))
    for mask in range(16):
        atlas.paste(make_tile(mask, 100 + mask), ((mask % 8) * 16, (mask // 8) * 16))
    for i in range(16, 20):
        atlas.paste(make_tile(0, 300 + i), ((i % 8) * 16, (i // 8) * 16))
    save(atlas, "tiles.png")


def make_background():
    size = 64
    img = Image.new("RGBA", (size, size))
    base = [rgba("#19152c"), rgba("#201b37"), rgba("#292344")]
    for y in range(size):
        for x in range(size):
            bx, by = (x // 8) % 8, ((y + (x // 16) * 3) // 6) % 10
            v = noise(bx, by, 900)
            shade = 0 if v < 0.5 else 1
            if noise(x % size, y % size, 901) > 0.96:
                shade = 2
            img.putpixel((x, y), base[shade])
    save(img, "background.png")


# --------------------------------------------------------------------------
# Player
# --------------------------------------------------------------------------
PLAYER_PAL = {
    "k": rgba("#15101f"),
    "h": rgba("#3d5f96"),
    "H": rgba("#6f9ad6"),
    "c": rgba("#2a406e"),
    "f": rgba("#f4cfa6"),
    "e": rgba("#15101f"),
    "b": rgba("#6b4630"),
    "s": rgba("#b98a5a"),
    "w": rgba("#ffffff"),
}

PLAYER_BODY = [
    "................",
    "....kkkkkk......",
    "...khhhhhHk.....",
    "..khhhhhhHHk....",
    "..khhkffffffk...",
    "..khhkfeffefk...",
    "..khhkffffffk...",
    "...khhkffffk....",
    "...kkcccckkk....",
    "..kcchhhhcksk...",
    "..kcchhhhcckssk.",
    "..kcchhhhcckkk..",
    "..kccchhccck....",
]

PLAYER_LEGS = {
    "idle": ["...kkbkkbkk.....", "....kbk.kbk.....", "....kkk.kkk....."],
    "run_a": ["...kkbkkkbkk....", "...kbk...kbk....", "...kkk...kkk...."],
    "run_b": ["...kkkbbkkk.....", ".....kbbk.......", ".....kkkk......."],
    "run_c": ["...kkbkkbkkk....", "..kbk....kbk....", "..kkk....kkk...."],
    "jump": ["...kkbkkbkk.....", "...kbk..kbk.....", "...kk....kk....."],
    "fall": ["..kkbkkkkbkk....", "..kbk....kbk....", "..kkk....kkk...."],
}


def player_frame(legs, bob=0):
    rows = list(PLAYER_BODY) + PLAYER_LEGS[legs]
    if bob:
        # squash by one pixel: drop the top row, repeat a cloak row
        rows = ["................"] + rows[0:11] + rows[12:]
    return from_ascii(rows, PLAYER_PAL, 16)


def make_player():
    frames = [
        player_frame("idle"),
        player_frame("idle", bob=1),
        player_frame("run_a"),
        player_frame("run_b", bob=1),
        player_frame("run_c"),
        player_frame("run_b", bob=1),
        player_frame("jump"),
        player_frame("fall"),
    ]
    save(sheet(frames), "player.png")
    # white silhouette used while flashing after a hit
    white = []
    for f in frames:
        g = f.copy()
        for y in range(g.height):
            for x in range(g.width):
                if g.getpixel((x, y))[3]:
                    g.putpixel((x, y), (255, 255, 255, 255))
        white.append(g)
    save(sheet(white), "player_flash.png")

    lantern_pal = {
        "k": rgba("#15101f"),
        "l": rgba("#8a5a2b"),
        "y": rgba("#ffc94a"),
        "Y": rgba("#fff4c2"),
    }
    lantern = from_ascii(
        [
            "..........",
            "....kk....",
            "...k..k...",
            "...kllk...",
            "..klyylk..",
            "..kyYYyk..",
            "..kyYYyk..",
            "..klyylk..",
            "...kllk...",
            "....kk....",
            "..........",
        ],
        lantern_pal,
        10,
    )
    save(lantern, "lantern.png")


# --------------------------------------------------------------------------
# Slime
# --------------------------------------------------------------------------
def make_slime_frame(blink=False, flash=False):
    w, h = 30, 22
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cx, base = 14.5, 20.0
    rx, ry = 13.0, 17.5

    def inside(x, y):
        if y > base:
            return False
        dx = (x - cx) / rx
        dy = (base - y) / ry
        # a dome that bulges near the floor
        return dx * dx + dy ** 2.4 <= 1.0

    body = rgba("#8a35b0")
    dark = rgba("#5c2180")
    darker = rgba("#3f1560")
    light = rgba("#bb6ae0")
    outline = rgba("#1c0a2b")
    for y in range(h):
        for x in range(w):
            if not inside(x, y):
                continue
            edge = not all(inside(x + ox, y + oy) for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if flash:
                col = (255, 255, 255, 255)
            elif edge:
                col = outline
            else:
                t = (base - y) / ry
                side = (x - cx) / rx
                if t < 0.18:
                    col = darker
                elif t < 0.4 or side > 0.55:
                    col = dark
                elif t > 0.72 and side < 0.1:
                    col = light
                else:
                    col = body
            img.putpixel((x, y), col)
    if not flash:
        # glossy highlight
        for x, y in ((8, 7), (9, 6), (10, 6), (7, 8), (7, 9)):
            img.putpixel((x, y), rgba("#f0d0ff"))
        # eyes
        eye = rgba("#fff7b0")
        pupil = rgba("#1c0a2b")
        for ex in (9, 18):
            if blink:
                for x in range(ex, ex + 4):
                    img.putpixel((x, 13), pupil)
            else:
                for y in range(11, 15):
                    for x in range(ex, ex + 4):
                        img.putpixel((x, y), eye)
                for y in range(12, 15):
                    for x in range(ex + 1, ex + 3):
                        img.putpixel((x, y), pupil)
        # mouth
        for x in range(12, 18):
            img.putpixel((x, 17), pupil)
        img.putpixel((11, 16), pupil)
        img.putpixel((18, 16), pupil)
    return img


def make_slime():
    frames = [make_slime_frame(), make_slime_frame(blink=True), make_slime_frame(flash=True)]
    save(sheet(frames), "slime.png")


# --------------------------------------------------------------------------
# Pickups and props
# --------------------------------------------------------------------------
def make_spore():
    pal = {
        "o": rgba("#1b7f6a"),
        "g": rgba("#35d9a2"),
        "G": rgba("#8dffd6"),
        "w": rgba("#f2fff9"),
        "d": rgba("#35d9a2", 150),
    }
    a = [
        "............",
        "....d..d....",
        "..d.oooo.d..",
        "...ogGGgo...",
        ".dogGwwGgod.",
        "..ogGwwGgo..",
        "..ogGGGGgo..",
        ".dogGGGGgod.",
        "...oggggo...",
        "..d.oooo.d..",
        "....d..d....",
        "............",
    ]
    b = [
        "....d..d....",
        "............",
        ".d..oooo..d.",
        "...ogGGgo...",
        "..ogGwwGgo..",
        "d.ogGwwGgo.d",
        "d.ogGGGGgo.d",
        "..ogGGGGgo..",
        "...oggggo...",
        ".d..oooo..d.",
        "............",
        "....d..d....",
    ]
    save(sheet([from_ascii(a, pal, 12), from_ascii(b, pal, 12)]), "spore.png")
    slot = {"o": rgba("#3a4a5a"), "g": rgba("#1c2630"), "G": rgba("#1c2630"), "w": rgba("#1c2630"), "d": (0, 0, 0, 0)}
    save(from_ascii(a, slot, 12), "spore_slot.png")


def make_potion():
    pal = {
        "k": rgba("#15101f"),
        "c": rgba("#b98a5a"),
        "g": rgba("#cfe9ff"),
        "l": rgba("#7df0ff"),
        "L": rgba("#e8fdff"),
        "t": rgba("#35b8d9"),
    }
    rows = [
        "..........",
        "....kk....",
        "...kcck...",
        "...kggk...",
        "..kkggkk..",
        ".kglLllgk.",
        ".klLLlltk.",
        ".klLllltk.",
        ".ktlllttk.",
        "..kkkkkk..",
        "..........",
    ]
    save(from_ascii(rows, pal, 10), "potion.png")
    slot = {k: rgba("#1c2630") for k in pal}
    slot["k"] = rgba("#3a4a5a")
    save(from_ascii(rows, slot, 10), "potion_slot.png")


def make_spikes():
    pal = {"k": rgba("#15101f"), "w": rgba("#e6e0f2"), "g": rgba("#a69fc0"), "d": rgba("#6e6790")}
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "..........k.....",
        "...k.....kwk....",
        "..kwk....kwk....",
        "..kwk...kwgdk...",
        ".kwgdk..kwgdk.k.",
        ".kwgdk..kwgdkkwk",
        ".kwgdk.kwggdkwgk",
        "kwggdkkkwggdkwgd",
        "kwggddkwgggdkwgd",
        "kwgggdkwgggdwggd",
        "kkkkkkkkkkkkkkkk",
    ]
    save(from_ascii(rows, pal, 16), "spikes.png")


def make_decor():
    rock = {"k": OUTLINE, "a": ROCK[1], "b": ROCK[2], "c": ROCK[3], "r": RIM}
    stalactite = [
        "kkkkkkkkkkkkkkkk",
        "kbbcbbbkabbbcbbk",
        ".kbcbbk.kabbcbk.",
        ".kbcbbk.kabbcbk.",
        "..kbcbk..kabck..",
        "..kbcbk..kabck..",
        "..kbcbk...kak...",
        "...kbk....kak...",
        "...kbk.....k....",
        "...kbk..........",
        "....k...........",
        "................",
        "................",
        "................",
        "................",
        "................",
    ]
    save(from_ascii(stalactite, rock, 16), "stalactite.png")

    mush = {
        "k": rgba("#15101f"),
        "r": rgba("#b5533c"),
        "R": rgba("#e08a5b"),
        "w": rgba("#f3e3c8"),
        "s": rgba("#cdb89a"),
    }
    mushroom = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "....kkkk........",
        "..kkRRRrkk......",
        ".kRRwRRrrrk.....",
        ".krrrrrwrrk.kkk.",
        "..kkkskkkk.kRrrk",
        "....kwsk..krwrrk",
        "....kwsk...kksk.",
        "....kwsk....ksk.",
        "...kwwssk..kwsk.",
        "...kkkkkk..kkkk.",
    ]
    save(from_ascii(mushroom, mush, 16), "mushroom.png")

    cry = {
        "k": rgba("#15101f"),
        "a": rgba("#3d4fb8"),
        "b": rgba("#6f8cff"),
        "c": rgba("#b6d0ff"),
        "w": rgba("#f4faff"),
    }
    crystal = [
        "................",
        "................",
        "................",
        ".......k........",
        "......kck.......",
        "......kcbk......",
        ".....kwcbk......",
        ".....kcbbk..k...",
        "..k..kcbak.kck..",
        ".kck.kcbak.kcbk.",
        ".kcbkkcbakkwbak.",
        "kwcbkkcbakkcbak.",
        "kcbakkcbakkcbak.",
        "kcbakcbbaakcbaak",
        "kcbaacbbaakcbaak",
        "kkkkkkkkkkkkkkkk",
    ]
    save(from_ascii(crystal, cry, 16), "crystal.png")


def make_shrine():
    pal = {
        "k": rgba("#15101f"),
        "a": rgba("#4b4763"),
        "b": rgba("#6d6989"),
        "c": rgba("#9793b5"),
        "g": rgba("#35d9a2"),
        "m": rgba("#2f8563"),
    }
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        ".kkkkkkkkkkkkkk.",
        ".kcccbbbbbbbbak.",
        "..kcbbbbbbbbak..",
        "...kkkkkkkkkk...",
        ".....kcbak......",
        ".....kcbak......",
        "....kcbbaak.....",
        "....kcbgaak.....",
        "....kcgbgak.....",
        "....kcbgaak.....",
        "....kcbbaak.....",
        "...kkcbbaakk....",
        "..kccbbbbaaak...",
        ".kmcbbbbbbamak..",
        ".kmmbbbbbbammk..",
        ".kkkkkkkkkkkkk..",
    ]
    save(from_ascii(rows, pal, 16), "shrine.png")

    fl = {"o": rgba("#2f8fd9"), "b": rgba("#7dd3ff"), "w": rgba("#f2fbff"), "d": rgba("#2f8fd9", 140)}
    f1 = [
        "....d.....",
        "....o.....",
        "...obo....",
        "...obo....",
        "..obwbo...",
        "..obwbo...",
        ".obwwwbo..",
        ".obwwwbo..",
        "..obwbo...",
        "...ooo....",
    ]
    f2 = [
        "......d...",
        ".....o....",
        "....obo...",
        "...obbo...",
        "...obwbo..",
        "..obwwbo..",
        ".obwwwbo..",
        ".obwwwbo..",
        "..obwbo...",
        "...ooo....",
    ]
    f3 = [
        "..d.......",
        "...o......",
        "...obo....",
        "..obbo....",
        "..obwbo...",
        "..obwwbo..",
        ".obwwwbo..",
        ".obwwwbo..",
        "..obwbo...",
        "...ooo....",
    ]
    ember = {"o": rgba("#5a3a2a"), "b": rgba("#a8572e"), "w": rgba("#e09050"), "d": (0, 0, 0, 0)}
    e1 = [
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "...obo....",
        "..obwbo...",
        "...ooo....",
    ]
    frames = [from_ascii(f1, fl, 10), from_ascii(f2, fl, 10), from_ascii(f3, fl, 10), from_ascii(e1, ember, 10)]
    save(sheet(frames), "flame.png")


def make_door():
    w, h = 24, 32
    stone = [rgba("#4b4763"), rgba("#6d6989"), rgba("#9793b5")]
    wood = [rgba("#4a2f22"), rgba("#6b4630"), rgba("#8a5d3e")]
    k = rgba("#15101f")
    goo = [rgba("#5c2180"), rgba("#8a35b0"), rgba("#bb6ae0")]

    def arch(x, y):
        # returns "frame", "inside" or None
        cx = 11.5
        if y >= 12:
            if 2 <= x <= 21:
                return "inside" if 5 <= x <= 18 else "frame"
            return None
        d_out = math.hypot(x - cx, (y - 12))
        if d_out <= 10.2:
            return "inside" if d_out <= 7.2 else "frame"
        return None

    def base(open_):
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        for y in range(h):
            for x in range(w):
                kind = arch(x, y)
                if kind is None:
                    continue
                if kind == "frame":
                    v = noise(x // 3, y // 4, 500)
                    col = stone[0] if v < 0.3 else stone[1] if v < 0.8 else stone[2]
                    if (y % 4 == 3 and y >= 12) or (x + y) % 9 == 0:
                        col = stone[0]
                elif open_:
                    # warm daylight beyond the door
                    t = max(0.0, 1.0 - math.hypot(x - 11.5, y - 20) / 16.0)
                    col = (255, int(215 + 35 * t), int(140 + 100 * t), 255)
                else:
                    plank = (x - 5) // 3
                    col = wood[1] if plank % 2 == 0 else wood[2]
                    if (x - 5) % 3 == 0:
                        col = wood[0]
                    if y in (14, 15, 26, 27):
                        col = rgba("#2e2a3d")
                img.putpixel((x, y), col)
        # outline
        src = img.copy()
        for y in range(h):
            for x in range(w):
                if src.getpixel((x, y))[3] == 0:
                    continue
                for ox, oy in ((1, 0), (-1, 0), (0, -1)):
                    nx, ny = x + ox, y + oy
                    if nx < 0 or ny < 0 or nx >= w or src.getpixel((nx, ny))[3] == 0:
                        img.putpixel((x, y), k)
        return img

    closed = base(False)
    # purple goo sealing the closed door
    rnd = random.Random(42)
    for x in range(4, 20):
        length = rnd.randint(5, 16)
        for y in range(5, 5 + length):
            if arch(x, y) is None:
                continue
            col = goo[1]
            if y == 5 + length - 1:
                col = goo[0]
            elif rnd.random() > 0.8:
                col = goo[2]
            closed.putpixel((x, y), col)
    for x, y in ((8, 22), (9, 22), (8, 23), (9, 23), (15, 19), (16, 19), (15, 20), (16, 20), (12, 27), (13, 27)):
        closed.putpixel((x, y), goo[1])
    save(sheet([closed, base(True)]), "door.png")


def make_hud():
    pal = {"k": rgba("#15101f"), "r": rgba("#e0453a"), "R": rgba("#ff8a7a"), "d": rgba("#9c2a2a")}
    rows = [
        "...........",
        "..kk...kk..",
        ".kRrk.krrk.",
        "kRRrrkrrrdk",
        "kRrrrrrrrdk",
        "krrrrrrrrdk",
        ".krrrrrrdk.",
        "..krrrrdk..",
        "...krrdk...",
        "....kdk....",
        ".....k.....",
    ]
    save(from_ascii(rows, pal, 11), "heart.png")
    empty = {"k": rgba("#3a4a5a"), "r": rgba("#1c2630"), "R": rgba("#1c2630"), "d": rgba("#1c2630")}
    save(from_ascii(rows, empty, 11), "heart_empty.png")


# --------------------------------------------------------------------------
# Light, particles (smooth, saved at native size)
# --------------------------------------------------------------------------
DARK_LEVELS = 16


def make_light():
    # Solid colour swatches with increasing alpha. The game covers the screen
    # with a grid of these to fake darkness with several light sources.
    for i in range(DARK_LEVELS):
        a = int(round(255 * (i / (DARK_LEVELS - 1))))
        Image.new("RGBA", (4, 4), (5, 4, 12, a)).save(OUT / f"dark_{i:02d}.png")

    def glow(name, colour, strength, power=2.0, size=96):
        img = Image.new("RGBA", (size, size))
        c = (size - 1) / 2
        for y in range(size):
            for x in range(size):
                d = min(1.0, math.hypot(x - c, y - c) / c)
                a = (1.0 - d) ** power * strength
                img.putpixel((x, y), (*colour, int(255 * a)))
        img.save(OUT / name)

    glow("glow_warm.png", (255, 180, 90), 0.38)
    glow("glow_teal.png", (90, 255, 190), 0.55)
    glow("glow_blue.png", (120, 170, 255), 0.45)
    glow("glow_cyan.png", (170, 245, 255), 0.75)
    glow("glow_purple.png", (200, 110, 255), 0.5)
    # potion burst: bright disc, drawn at growing size over a few frames
    for i, strength in enumerate((1.0, 0.75, 0.5, 0.28)):
        glow(f"burst_{i}.png", (215, 252, 255), strength, power=0.9 + i * 0.5)

    for name, colour in (
        ("pix_teal.png", rgba("#8dffd6")),
        ("pix_amber.png", rgba("#ffc94a")),
        ("pix_white.png", rgba("#ffffff")),
        ("pix_purple.png", rgba("#bb6ae0")),
        ("pix_blue.png", rgba("#7dd3ff")),
        ("pix_cyan.png", rgba("#7df0ff")),
        ("pix_red.png", rgba("#ff6a5a")),
        ("pix_rock.png", rgba("#8a7cb6")),
    ):
        Image.new("RGBA", (4, 4), colour).save(OUT / name)


def make_icon():
    pal = {"o": rgba("#1b7f6a"), "g": rgba("#35d9a2"), "G": rgba("#8dffd6"), "w": rgba("#f2fff9"), "k": rgba("#15101f")}
    rows = [
        "................",
        "....kkkkkkkk....",
        "...kkooooookk...",
        "..kkoggggggokk..",
        ".kkogGGGGGGgokk.",
        ".kogGGwwwGGGgok.",
        ".kogGwwwwGGGgok.",
        ".kogGwwwGGGGgok.",
        ".kogGGGGGGGGgok.",
        ".kogGGGGGGGGgok.",
        ".kogGGGGGGGGgok.",
        ".kkogGGGGGGgokk.",
        "..kkoggggggokk..",
        "...kkooooookk...",
        "....kkkkkkkk....",
        "................",
    ]
    save(from_ascii(rows, pal, 16), "icon.png", scale=4)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    make_tiles()
    make_background()
    make_player()
    make_slime()
    make_spore()
    make_potion()
    make_spikes()
    make_decor()
    make_shrine()
    make_door()
    make_hud()
    make_light()
    make_icon()
    print("wrote", len(list(OUT.glob("*.png"))), "images to", OUT)


if __name__ == "__main__":
    main()
