#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

// The level is authored as ASCII (assets/level/level.txt). tools/build_level.py
// turns the '#' cells into the Tiled map the engine collides against; this
// class reads everything else, because the engine's Tilemap does not expose
// tile ids, objects or properties.
//
// World space follows the engine: +y is up, a sprite's position is its
// top-left corner, and the top-left of cell (0, 0) is the origin. The whole
// level therefore lives at negative y.
struct Level
{
    static constexpr float TILE = 32.0f;

    struct Cell { int c = 0, r = 0; };
    struct Decor { char kind; Cell cell; };

    bool load(const std::string& path);

    bool solid(int c, int r) const;
    float pixelWidth() const { return width * TILE; }
    float pixelHeight() const { return height * TILE; }

    // Top-left corner of a cell, and the point in the middle of its floor.
    static glm::vec2 cellTopLeft(Cell cell) { return { cell.c * TILE, -cell.r * TILE }; }
    static glm::vec2 cellFloorCenter(Cell cell) { return { (cell.c + 0.5f) * TILE, -(cell.r + 1) * TILE }; }

    int width = 0, height = 0;
    std::vector<std::string> rows;

    Cell playerStart;
    Cell shrine;
    Cell slime;
    Cell door;
    std::vector<Cell> spores;
    std::vector<Cell> spikes;
    std::vector<Decor> decor;
};
