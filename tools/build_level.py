#!/usr/bin/env python3
"""Turn assets/level/level.txt into a Tiled JSON map the engine can load.

The engine's Tilemap treats every tile in every tile layer as solid and does
not expose tile ids or object data, so the ASCII file stays the single source
of truth: '#' cells become tiles here, and the game reads every other
character (player start, spores, spikes, ...) from level.txt itself.

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
                "name": "solid",
                "type": "tilelayer",
                "width": width,
                "height": height,
                "x": 0,
                "y": 0,
                "opacity": 1,
                "visible": True,
                "data": data,
            }
        ],
        "tilesets": [{"firstgid": 1, "source": "tiles.tsj"}],
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
    # The engine's Tilemap::render() draws every tile of the map each frame,
    # one draw call per tile and with no culling. Bake the same picture into a
    # single image the game can draw as one sprite instead; the Tilemap is
    # still what the player collides with.
    atlas = Image.open(ROOT / "assets" / "gfx" / "tiles.png").convert("RGBA")
    baked = Image.new("RGBA", (width * TILE, height * TILE), (0, 0, 0, 0))
    for i, gid in enumerate(data):
        if gid == 0:
            continue
        index = gid - 1
        sx, sy = (index % COLUMNS) * TILE, (index // COLUMNS) * TILE
        baked.paste(atlas.crop((sx, sy, sx + TILE, sy + TILE)), ((i % width) * TILE, (i // width) * TILE))
    baked.save(ROOT / "assets" / "gfx" / "cave.png")

    (LEVEL / "cave.tmj").write_text(json.dumps(tmj))
    (LEVEL / "tiles.tsj").write_text(json.dumps(tsj, indent=1))
    print(f"wrote cave.tmj ({width}x{height}), tiles.tsj and gfx/cave.png")


if __name__ == "__main__":
    main()
