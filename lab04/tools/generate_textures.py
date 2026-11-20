#!/usr/bin/env python3
"""Генерирует текстуры для лабораторной работы №3 в папку textures/.

Для каждого материала создаются альбедо (<имя>_albedo.png), а также, где это нужно, карты бликов
(<имя>_specular.png) и свечения (<имя>_emissive.png). Все текстуры бесшовные: шум периодичен по обоим осям.
Нужны numpy и Pillow:  python3 tools/generate_textures.py
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SIZE = 512
OUT = Path(__file__).resolve().parent.parent / "textures"


def fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def perlin(res_x, res_y, seed):
    """Периодический шум Перлина на сетке res_x на res_y ячеек, значения примерно в [-1, 1]."""
    rng = np.random.default_rng(seed)
    angles = rng.uniform(0.0, 2.0 * np.pi, (res_y, res_x))
    gx, gy = np.cos(angles), np.sin(angles)

    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    x = xs / SIZE * res_x
    y = ys / SIZE * res_y
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx, fy = x - x0, y - y0

    def dot(ix, iy, dx, dy):
        return gx[iy % res_y, ix % res_x] * dx + gy[iy % res_y, ix % res_x] * dy

    n00 = dot(x0, y0, fx, fy)
    n10 = dot(x0 + 1, y0, fx - 1.0, fy)
    n01 = dot(x0, y0 + 1, fx, fy - 1.0)
    n11 = dot(x0 + 1, y0 + 1, fx - 1.0, fy - 1.0)
    u, v = fade(fx), fade(fy)
    top = n00 + u * (n10 - n00)
    bottom = n01 + u * (n11 - n01)
    return (top + v * (bottom - top)) * 1.4142


def fbm(res_x, res_y, seed, octaves=5):
    total = np.zeros((SIZE, SIZE))
    amplitude, norm = 1.0, 0.0
    for octave in range(octaves):
        total += amplitude * perlin(res_x * 2 ** octave, res_y * 2 ** octave, seed + octave)
        norm += amplitude
        amplitude *= 0.5
    return total / norm


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def mix(a, b, t):
    return a + (b - a) * t[..., None]


def color(r, g, b):
    return np.array([r, g, b], dtype=float) / 255.0


def save(name, kind, data):
    OUT.mkdir(parents=True, exist_ok=True)
    pixels = (np.clip(data, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(pixels).save(OUT / f"{name}_{kind}.png", optimize=True)


def gray(values):
    return np.repeat(values[..., None], 3, axis=2)


def brick():
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    brick_w, brick_h, mortar = 128, 64, 8
    row = ys // brick_h
    shifted = xs + (row % 2) * (brick_w // 2)
    column = shifted // brick_w
    xo, yo = shifted % brick_w, ys % brick_h

    # Расстояние до ближайшего шва: фаска даёт объём, шов углублён.
    edge = np.minimum(np.minimum(xo, brick_w - 1 - xo), np.minimum(yo, brick_h - 1 - yo)).astype(float)
    in_brick = smoothstep(mortar - 2.0, mortar + 1.0, edge)
    bevel = 0.8 + 0.2 * smoothstep(mortar, mortar + 8.0, edge)

    rng = np.random.default_rng(7)
    tint = rng.uniform(0.75, 1.15, (SIZE // brick_h, SIZE // brick_w + 1))
    per_brick = tint[row % tint.shape[0], column % tint.shape[1]]
    grain = fbm(8, 8, 11) * 0.5 + 0.5
    fine = perlin(64, 64, 21) * 0.5 + 0.5

    brick_color = color(158, 62, 44)[None, None, :] * (per_brick * bevel * (0.75 + 0.35 * grain))[..., None]
    mortar_color = color(176, 170, 158)[None, None, :] * (0.75 + 0.4 * fine)[..., None]
    albedo = mix(mortar_color, brick_color, in_brick)
    specular = gray(in_brick * (0.18 + 0.22 * grain))
    return albedo, specular, None


def lava():
    base = fbm(6, 6, 31) * 0.5 + 0.5
    warp = perlin(4, 4, 41)
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    # Трещины — гребни шума: где значение близко к нулю, 1 - |шум| близко к единице.
    ridge = 1.0 - np.abs(fbm(5, 5, 51, octaves=4) + 0.15 * warp) * 2.2
    cracks = smoothstep(0.55, 0.92, ridge)
    glow_core = smoothstep(0.8, 1.0, ridge)

    rock = mix(np.broadcast_to(color(26, 22, 22), (SIZE, SIZE, 3)), np.broadcast_to(color(72, 62, 58), (SIZE, SIZE, 3)), base)
    hot = mix(np.broadcast_to(color(255, 80, 10), (SIZE, SIZE, 3)), np.broadcast_to(color(255, 210, 90), (SIZE, SIZE, 3)), glow_core)
    albedo = mix(rock, rock * 0.35, cracks)
    emissive = hot * (cracks * (0.75 + 0.25 * perlin(16, 16, 61)))[..., None]
    specular = gray((1.0 - cracks) * (0.2 + 0.3 * base))
    return albedo, specular, emissive


def marble():
    turbulence = fbm(4, 4, 71, octaves=6)
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    phase = xs / SIZE * 2.0 * np.pi * 3.0 + 7.0 * turbulence
    vein = np.abs(np.sin(phase))
    vein = smoothstep(0.0, 0.5, vein)
    fine = perlin(48, 48, 81) * 0.5 + 0.5

    light = np.broadcast_to(color(238, 236, 230), (SIZE, SIZE, 3)) * (0.94 + 0.06 * fine)[..., None]
    dark = np.broadcast_to(color(52, 62, 78), (SIZE, SIZE, 3))
    albedo = mix(dark, light, vein)
    specular = gray(0.8 + 0.2 * fine)
    return albedo, specular, None


def tiles():
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    tile = 128
    xo, yo = xs % tile, ys % tile
    edge = np.minimum(np.minimum(xo, tile - 1 - xo), np.minimum(yo, tile - 1 - yo)).astype(float)
    grout = 1.0 - smoothstep(2.0, 5.0, edge)
    checker = ((xs // tile + ys // tile) % 2).astype(float)

    stone = fbm(8, 8, 91) * 0.5 + 0.5
    fine = perlin(64, 64, 101) * 0.5 + 0.5
    dark = color(52, 58, 66)
    light = color(190, 178, 156)
    tile_color = mix(np.broadcast_to(dark, (SIZE, SIZE, 3)), np.broadcast_to(light, (SIZE, SIZE, 3)), checker)
    tile_color = tile_color * (0.8 + 0.3 * stone + 0.1 * fine)[..., None]
    grout_color = np.broadcast_to(color(34, 34, 36), (SIZE, SIZE, 3)) * (0.8 + 0.3 * fine)[..., None]
    albedo = mix(tile_color, grout_color, grout)
    specular = gray((1.0 - grout) * (0.75 + 0.25 * stone))
    return albedo, specular, None


def wood():
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    plank_h = 128
    plank = ys // plank_h
    yo = ys % plank_h
    seam = 1.0 - smoothstep(1.0, 4.0, np.minimum(yo, plank_h - 1 - yo).astype(float))

    rng = np.random.default_rng(111)
    tint = rng.uniform(0.8, 1.15, SIZE // plank_h)[plank]
    shift = rng.uniform(0.0, 1.0, SIZE // plank_h)[plank]

    warp = fbm(2, 16, 121, octaves=4)
    rings = np.sin((yo / plank_h * 5.0 + shift * 3.0) * 2.0 * np.pi + 4.0 * warp)
    grain = fbm(1, 48, 131, octaves=3) * 0.5 + 0.5
    value = 0.55 + 0.25 * rings + 0.2 * (grain - 0.5)

    light, dark = color(196, 140, 84), color(104, 62, 32)
    wood_color = mix(np.broadcast_to(dark, (SIZE, SIZE, 3)), np.broadcast_to(light, (SIZE, SIZE, 3)), np.clip(value, 0, 1))
    wood_color = wood_color * tint[..., None]
    albedo = mix(wood_color, wood_color * 0.25, seam)
    specular = gray((1.0 - seam) * (0.12 + 0.25 * np.clip(value, 0, 1)))
    return albedo, specular, None


def uvgrid():
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    u, v = xs / SIZE, ys / SIZE
    albedo = np.stack([0.25 + 0.75 * u, 0.25 + 0.75 * v, np.full_like(u, 0.35)], axis=2)

    cell = 64
    on_line = ((xs % cell < 2) | (ys % cell < 2)).astype(float)
    albedo = mix(albedo, np.ones_like(albedo), on_line * 0.8)

    image = Image.fromarray((albedo * 255.0 + 0.5).astype(np.uint8))
    draw = ImageDraw.Draw(image)
    big = ImageFont.load_default(size=300)
    small = ImageFont.load_default(size=36)
    draw.text((SIZE // 2, SIZE // 2), "F", font=big, fill=(20, 20, 20), anchor="mm")
    draw.text((10, 6), "(0,0)", font=small, fill=(10, 10, 10))
    draw.text((SIZE - 10, SIZE - 6), "(1,1)", font=small, fill=(10, 10, 10), anchor="rd")
    return np.asarray(image) / 255.0, None, None


MATERIALS = {
    "brick": brick,
    "lava": lava,
    "marble": marble,
    "tiles": tiles,
    "wood": wood,
    "uvgrid": uvgrid,
}


def main():
    for name, make in MATERIALS.items():
        albedo, specular, emissive = make()
        save(name, "albedo", albedo)
        if specular is not None:
            save(name, "specular", specular)
        if emissive is not None:
            save(name, "emissive", emissive)
        print("generated", name)


if __name__ == "__main__":
    main()
