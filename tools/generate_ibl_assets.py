"""Generate the small, precomputed IBL maps shipped with the sample.

The application never runs this script: it only loads the resulting DDS files.
Keeping the deterministic generator documents how the irradiance convolution,
GGX prefilter and split-sum BRDF integration maps were produced.
"""

from __future__ import annotations

import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
PI = math.pi


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, value):
    return (a[0] * value, a[1] * value, a[2] * value)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalize(value):
    length = math.sqrt(max(dot(value, value), 1e-20))
    return mul(value, 1.0 / length)


def radical_inverse_vdc(bits):
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = (((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)) & 0xFFFFFFFF
    bits = (((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)) & 0xFFFFFFFF
    bits = (((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)) & 0xFFFFFFFF
    bits = (((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)) & 0xFFFFFFFF
    return bits * 2.3283064365386963e-10


def hammersley(index, count):
    return (index / count, radical_inverse_vdc(index))


def basis(normal):
    up = (0.0, 0.0, 1.0) if abs(normal[2]) < 0.999 else (1.0, 0.0, 0.0)
    tangent = normalize(cross(up, normal))
    return tangent, cross(normal, tangent)


def tangent_to_world(sample, normal):
    tangent, bitangent = basis(normal)
    return normalize(add(add(mul(tangent, sample[0]), mul(bitangent, sample[1])),
                         mul(normal, sample[2])))


def cube_direction(face, x, y, size):
    u = 2.0 * ((x + 0.5) / size) - 1.0
    v = 2.0 * ((y + 0.5) / size) - 1.0
    directions = ((1.0, -v, -u), (-1.0, -v, u), (u, 1.0, v),
                  (u, -1.0, -v), (u, -v, 1.0), (-u, -v, -1.0))
    return normalize(directions[face])


def environment(direction):
    # A neutral outdoor studio environment with a warm sun and cool sky.
    up = max(direction[1], 0.0)
    down = max(-direction[1], 0.0)
    horizon = math.exp(-abs(direction[1]) * 7.0)
    sky = (0.055 + 0.20 * up, 0.085 + 0.32 * up, 0.14 + 0.55 * up)
    ground = (0.055 + 0.035 * down, 0.045 + 0.025 * down, 0.04 + 0.018 * down)
    base = sky if direction[1] >= 0.0 else ground
    base = add(base, mul((0.28, 0.20, 0.12), horizon))
    sun_direction = normalize((-0.25, 1.0, 0.20))
    sun = pow(max(dot(direction, sun_direction), 0.0), 768.0)
    return add(base, mul((1.0, 0.82, 0.58), sun))


def importance_sample_ggx(xi, roughness, normal):
    alpha = roughness * roughness
    phi = 2.0 * PI * xi[0]
    cos_theta = math.sqrt((1.0 - xi[1]) /
                          max(1.0 + (alpha * alpha - 1.0) * xi[1], 1e-8))
    sin_theta = math.sqrt(max(1.0 - cos_theta * cos_theta, 0.0))
    return tangent_to_world((math.cos(phi) * sin_theta,
                             math.sin(phi) * sin_theta, cos_theta), normal)


def integrate_irradiance(normal, sample_count=128):
    total = (0.0, 0.0, 0.0)
    for index in range(sample_count):
        xi = hammersley(index, sample_count)
        radius = math.sqrt(xi[0])
        phi = 2.0 * PI * xi[1]
        sample = (radius * math.cos(phi), radius * math.sin(phi),
                  math.sqrt(max(1.0 - xi[0], 0.0)))
        total = add(total, environment(tangent_to_world(sample, normal)))
    # Cosine-weighted sampling turns the estimator into pi * average radiance.
    return mul(total, PI / sample_count)


def prefilter_environment(reflection, roughness, sample_count=96):
    if roughness < 1e-4:
        return environment(reflection)
    total = (0.0, 0.0, 0.0)
    weight = 0.0
    view = reflection
    for index in range(sample_count):
        halfway = importance_sample_ggx(hammersley(index, sample_count), roughness, reflection)
        light = normalize(add(mul(halfway, 2.0 * dot(view, halfway)), mul(view, -1.0)))
        n_dot_l = max(dot(reflection, light), 0.0)
        if n_dot_l > 0.0:
            total = add(total, mul(environment(light), n_dot_l))
            weight += n_dot_l
    return mul(total, 1.0 / max(weight, 1e-6))


def geometry_schlick_ibl(n_dot_direction, roughness):
    k = roughness * roughness * 0.5
    return n_dot_direction / max(n_dot_direction * (1.0 - k) + k, 1e-6)


def integrate_brdf(n_dot_v, roughness, sample_count=256):
    view = (math.sqrt(max(1.0 - n_dot_v * n_dot_v, 0.0)), 0.0, n_dot_v)
    normal = (0.0, 0.0, 1.0)
    scale = 0.0
    bias = 0.0
    for index in range(sample_count):
        halfway = importance_sample_ggx(hammersley(index, sample_count), roughness, normal)
        light = normalize(add(mul(halfway, 2.0 * dot(view, halfway)), mul(view, -1.0)))
        n_dot_l = max(light[2], 0.0)
        n_dot_h = max(halfway[2], 0.0)
        v_dot_h = max(dot(view, halfway), 0.0)
        if n_dot_l > 0.0:
            geometry = (geometry_schlick_ibl(n_dot_v, roughness) *
                        geometry_schlick_ibl(n_dot_l, roughness))
            visibility = geometry * v_dot_h / max(n_dot_h * n_dot_v, 1e-6)
            fresnel = pow(1.0 - v_dot_h, 5.0)
            scale += (1.0 - fresnel) * visibility
            bias += fresnel * visibility
    return scale / sample_count, bias / sample_count


def rgba8(color):
    channels = [max(0, min(255, round(component * 255.0))) for component in color]
    return bytes((channels[0], channels[1], channels[2], 255))


def write_dds(path, width, height, mip_levels, faces):
    is_cube = len(faces) == 6
    flags = 0x0000100F | (0x00020000 if mip_levels > 1 else 0)
    caps = 0x00001000
    if is_cube:
        caps |= 0x00000008
    if mip_levels > 1:
        caps |= 0x00400008
    caps2 = 0x0000FE00 if is_cube else 0
    values = [124, flags, height, width, width * 4, 0, mip_levels]
    values += [0] * 11
    values += [32, 0x00000041, 0, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000]
    values += [caps, caps2, 0, 0, 0]
    with path.open("wb") as output:
        output.write(b"DDS ")
        output.write(struct.pack("<31I", *values))
        for face in faces:
            for mip in face:
                output.write(mip)


def generate_cube(size, mip_levels, evaluator):
    faces = []
    for face in range(6):
        face_mips = []
        for mip in range(mip_levels):
            mip_size = max(1, size >> mip)
            pixels = bytearray()
            roughness = mip / max(mip_levels - 1, 1)
            for y in range(mip_size):
                for x in range(mip_size):
                    pixels += rgba8(evaluator(cube_direction(face, x, y, mip_size), roughness))
            face_mips.append(bytes(pixels))
        faces.append(face_mips)
    return faces


def main():
    irradiance = generate_cube(16, 1, lambda direction, _: integrate_irradiance(direction))
    write_dds(ASSETS / "ibl_irradiance.dds", 16, 16, 1, irradiance)

    prefiltered = generate_cube(64, 5, prefilter_environment)
    write_dds(ASSETS / "ibl_prefiltered.dds", 64, 64, 5, prefiltered)

    size = 128
    pixels = bytearray()
    for y in range(size):
        roughness = (y + 0.5) / size
        for x in range(size):
            n_dot_v = (x + 0.5) / size
            scale, bias = integrate_brdf(n_dot_v, roughness)
            pixels += rgba8((scale, bias, 0.0))
    write_dds(ASSETS / "ibl_brdf_lut.dds", size, size, 1, [[bytes(pixels)]])


if __name__ == "__main__":
    main()
