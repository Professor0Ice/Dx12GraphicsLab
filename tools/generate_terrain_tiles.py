#!/usr/bin/env python3
from __future__ import annotations

import math
from pathlib import Path

TILES = 4
TILE_CELLS = 128
SIZE = TILES * TILE_CELLS + 1
SEED = 0x5A17

def fade(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)

def lattice(x: int, y: int) -> float:
    value = (x * 0x1F123BB5) ^ (y * 0x05491333) ^ SEED
    value = (value ^ (value >> 16)) * 0x45D9F3B
    value = (value ^ (value >> 16)) * 0x45D9F3B
    value ^= value >> 16
    return (value & 0xFFFF) / 65535.0

def value_noise(x: float, y: float) -> float:
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = fade(x - ix), fade(y - iy)
    a = lattice(ix, iy) * (1 - fx) + lattice(ix + 1, iy) * fx
    b = lattice(ix, iy + 1) * (1 - fx) + lattice(ix + 1, iy + 1) * fx
    return a * (1 - fy) + b * fy

def terrain_height(u: float, v: float) -> float:
    total, amplitude, frequency, weight = 0.0, 1.0, 2.1, 0.0
    for _ in range(6):
        n = value_noise(u * frequency + 19.7, v * frequency - 8.3)
        total += n * amplitude
        weight += amplitude
        amplitude *= 0.51
        frequency *= 2.03
    h = total / weight
    ridge = 1.0 - abs(2.0 * value_noise(u * 5.3 - 4.0, v * 5.3 + 11.0) - 1.0)
    h = h * 0.72 + ridge * ridge * 0.28

    world_x = (u - 0.5) * 512.0
    world_z = (v - 0.5) * 512.0 + 64.0
    corridor = math.exp(-((world_x / 22.0) ** 4))
    along = fade(max(0.0, min(1.0, (world_z + 35.0) / 190.0)))
    corridor *= 1.0 - fade(max(0.0, min(1.0, (world_z - 125.0) / 70.0)))
    h = h * (1.0 - corridor * along * 0.9) + 0.19 * corridor * along * 0.9
    return max(0.0, min(1.0, (h - 0.22) / 0.63))

def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        stream.write(pixels)

def main() -> None:
    root = Path(__file__).resolve().parents[1]
    heights = [[terrain_height(x / (SIZE - 1), y / (SIZE - 1)) for x in range(SIZE)] for y in range(SIZE)]

    height_rgb = bytearray()
    color_rgb = bytearray()
    for row in heights:
        for h in row:
            value = round(h * 255)
            height_rgb.extend((value, value, value))
            low = (42, 67, 29)
            high = (114, 105, 78)
            t = fade(max(0.0, min(1.0, (h - 0.22) / 0.58)))
            color_rgb.extend(round(low[i] * (1 - t) + high[i] * t) for i in range(3))

    write_ppm(root / "assets/terrain_height_atlas.ppm", SIZE, SIZE, height_rgb)
    write_ppm(root / "assets/terrain_color_atlas.ppm", SIZE, SIZE, color_rgb)

    tile_dir = root / "assets/terrain_tiles"
    for tile_y in range(TILES):
        for tile_x in range(TILES):
            pixels = bytearray()
            for local_y in range(TILE_CELLS + 1):
                row = heights[tile_y * TILE_CELLS + local_y]
                for local_x in range(TILE_CELLS + 1):
                    value = round(row[tile_x * TILE_CELLS + local_x] * 255)
                    pixels.extend((value, value, value))
            write_ppm(tile_dir / f"height_{tile_x}_{tile_y}.ppm", TILE_CELLS + 1, TILE_CELLS + 1, pixels)

    print(f"Generated {TILES * TILES} tiles and {SIZE}x{SIZE} runtime atlases")

if __name__ == "__main__":
    main()
