#!/usr/bin/env python3
"""Turn assets/level/level.txt into a Tiled JSON map the engine can load.

The ASCII file is the single source of truth. This script writes:

- a tile layer with the '#' cells. Every tile in a tile layer is solid for the
  engine's Tilemap, and this is what the player collides with;
- an object layer of tile objects for the decoration ('v' stalactite,
  'm' mushroom, 'c' crystal). The engine draws tile objects and gives them no
  collision, which is its way of having non-solid tiles;
- assets/gfx/decor.png, the tileset image for those objects.

The game still reads level.txt itself for things it has to know about (player
start, spores, spikes, shrine, slime, door, and where the crystals glow),
because the Tilemap does not expose tile ids or objects.

Tile choice is a 4-bit "which sides are open" mask (up=1, right=2, down=4,
left=8) that indexes into assets/gfx/tiles.png (see make_art.py).
"""
import json
import pathlib

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEVEL = ROOT / "assets" / "level"
TILE = 32
COLUMNS, ROWS = 8, 3          # layout of tiles.png
INTERIOR_VARIANTS = [0, 16, 17, 18, 19]

# Decoration tileset: one row, in this order. firstgid follows the cave tiles.
DECOR = {"v": "stalactite", "m": "mushroom", "c": "crystal"}
DECOR_FIRSTGID = COLUMNS * ROWS + 1


def main():
    rows = (LEVEL / "level.txt").read_text().splitlines()
    rows = [r for r in rows if r]
    width, height = len(rows[0]), len(rows)
    assert all(len(r) == width for r in rows), "ragged level rows"

    def solid(c, r):
        if c < 0 or r < 0 or c >= width or r >= height:
            return True
        return rows[r][c] == "#"

    data = []
    for r in range(height):
        for c in range(width):
            if not solid(c, r):
                data.append(0)
                continue
            mask = (
                (0 if solid(c, r - 1) else 1)
                | (0 if solid(c + 1, r) else 2)
                | (0 if solid(c, r + 1) else 4)
                | (0 if solid(c - 1, r) else 8)
            )
            index = mask
            if mask == 0:
                index = INTERIOR_VARIANTS[(c * 7 + r * 13 + (c * r) % 5) % len(INTERIOR_VARIANTS)]
            data.append(index + 1)  # firstgid is 1

    # Decoration as tile objects. In Tiled's format a tile object's (x, y) is
    # its bottom-left corner with y pointing down.
    gfx = ROOT / "assets" / "gfx"
    decor_names = list(DECOR.values())
    decor_image = Image.new("RGBA", (len(decor_names) * TILE, TILE), (0, 0, 0, 0))
    for i, name in enumerate(decor_names):
        decor_image.paste(Image.open(gfx / f"{name}.png").convert("RGBA"), (i * TILE, 0))
    decor_image.save(gfx / "decor.png")

    objects = []
    for r in range(height):
        for c in range(width):
            kind = rows[r][c]
            if kind in DECOR:
                objects.append({
                    "id": len(objects) + 1,
                    "gid": DECOR_FIRSTGID + list(DECOR).index(kind),
                    "name": DECOR[kind],
                    "type": "",
                    "rotation": 0,
                    "visible": True,
                    "width": TILE,
                    "height": TILE,
                    "x": c * TILE,
                    "y": (r + 1) * TILE,
                })

    tmj = {
        "width": width,
        "height": height,
        "tilewidth": TILE,
        "tileheight": TILE,
        "infinite": False,
        "orientation": "orthogonal",
        "renderorder": "right-down",
        "type": "map",
        "version": "1.10",
        "layers": [
            {
                "id": 1,
                "name": "decoration",
                "type": "objectgroup",
                "draworder": "topdown",
                "objects": objects,
                "opacity": 1,
                "visible": True,
                "x": 0,
                "y": 0,
            },
            {
                "id": 2,
                "name": "solid",
                "type": "tilelayer",
                "width": width,
                "height": height,
                "x": 0,
                "y": 0,
                "opacity": 1,
                "visible": True,
                "data": data,
            },
        ],
        "tilesets": [
            {"firstgid": 1, "source": "tiles.tsj"},
            {"firstgid": DECOR_FIRSTGID, "source": "decor.tsj"},
        ],
    }
    decor_tsj = {
        "type": "tileset",
        "version": "1.10",
        "name": "decor",
        "image": "../gfx/decor.png",
        "imagewidth": len(decor_names) * TILE,
        "imageheight": TILE,
        "tilewidth": TILE,
        "tileheight": TILE,
        "columns": len(decor_names),
        "tilecount": len(decor_names),
        "margin": 0,
        "spacing": 0,
    }
    tsj = {
        "type": "tileset",
        "version": "1.10",
        "name": "cave",
        "image": "../gfx/tiles.png",
        "imagewidth": COLUMNS * TILE,
        "imageheight": ROWS * TILE,
        "tilewidth": TILE,
        "tileheight": TILE,
        "columns": COLUMNS,
        "tilecount": COLUMNS * ROWS,
        "margin": 0,
        "spacing": 0,
    }
    (LEVEL / "cave.tmj").write_text(json.dumps(tmj))
    (LEVEL / "tiles.tsj").write_text(json.dumps(tsj, indent=1))
    (LEVEL / "decor.tsj").write_text(json.dumps(decor_tsj, indent=1))
    print(f"wrote cave.tmj ({width}x{height}, {len(objects)} decoration objects), "
          "tiles.tsj, decor.tsj and gfx/decor.png")


if __name__ == "__main__":
    main()
