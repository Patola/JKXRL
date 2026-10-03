#!/usr/bin/env python3
"""Read-only MD3 coverage audit; no game assets or runtime settings are modified."""

import argparse
import io
import json
import struct
import zipfile

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    parser.add_argument("--frame", type=int, default=0)
    parser.add_argument("--yaw", type=float, default=35)
    parser.add_argument("--seconds", type=float, default=1)
    parser.add_argument("--output-prefix")
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as archive:
        data = archive.read("models/map_objects/wedge/holo_map.md3")
        assert data[:4] == b"IDP3"
        header = struct.unpack_from("<9i", data, 72)
        assert header[3] == 1
        offset = header[7]
        surface = struct.unpack_from("<10i", data, offset + 68)
        _, frames, _, vertices, triangles, ofs_tri, _, ofs_uv, ofs_xyz, _ = surface
        frame = args.frame % frames
        points = np.frombuffer(data, dtype="<i2", count=vertices * 4,
                               offset=offset + ofs_xyz + frame * vertices * 8)
        points = points.reshape(-1, 4)[:, :3].astype(float) / 64
        indices = np.frombuffer(data, dtype="<i4", count=triangles * 3,
                                offset=offset + ofs_tri).reshape(-1, 3)
        uv = np.frombuffer(data, dtype="<f4", count=vertices * 2, offset=offset + ofs_uv)
        assert np.all(uv == 0), "Audit assumes the shipped hologram's zero UVs"
        energy = np.asarray(Image.open(io.BytesIO(archive.read(
            "models/map_objects/wedge/power38.jpg"))).convert("RGB")) / 255
        tint = np.asarray(Image.open(io.BytesIO(archive.read(
            "models/map_objects/wedge/blue.jpg"))).convert("RGB"))[0, 0] / 255

    yaw = np.radians(args.yaw)
    eye = np.array([np.cos(yaw), np.sin(yaw), 0.2])
    eye /= np.linalg.norm(eye)
    right = np.cross([0, 0, 1], eye)
    right /= np.linalg.norm(right)
    up = np.cross(eye, right)
    projected = np.column_stack((points @ right, points @ up))
    lo, hi = projected.min(axis=0), projected.max(axis=0)
    projected = (projected - (lo + hi) / 2) * (230 / max(hi - lo)) + 128
    def cross2(a, b):
        return a[..., 0] * b[..., 1] - a[..., 1] * b[..., 0]

    counts = np.zeros((2, 256, 256), dtype=int)
    for triangle in indices:
        p = projected[triangle]
        area = cross2(p[1] - p[0], p[2] - p[0])
        if abs(area) < 1e-6:
            continue
        x0, y0 = np.maximum(0, np.floor(p.min(axis=0)).astype(int))
        x1, y1 = np.minimum(255, np.ceil(p.max(axis=0)).astype(int))
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        samples = np.stack((xx + 0.37, yy + 0.61), axis=-1)
        weights = [cross2(p[(i + 1) % 3] - p[i], samples - p[i]) / area for i in range(3)]
        inside = np.logical_and.reduce([w >= 0 for w in weights])
        counts[int(area > 0), y0:y1 + 1, x0:x1 + 1] += inside

    def sample(u, v):
        h, w, _ = energy.shape
        x, y = (u % 1) * w - 0.5, (v % 1) * h - 0.5
        ix, iy = int(np.floor(x)), int(np.floor(y))
        fx, fy = x - ix, y - iy
        return ((1 - fy) * ((1 - fx) * energy[iy % h, ix % w] + fx * energy[iy % h, (ix + 1) % w]) +
                fy * ((1 - fx) * energy[(iy + 1) % h, ix % w] + fx * energy[(iy + 1) % h, (ix + 1) % w]))

    sources = [sample(0, args.seconds * 0.2), sample(args.seconds * 0.2, -args.seconds * 0.2)]
    stats = {}
    for label, count in [("two-sided", counts.sum(axis=0)), ("side0", counts[0]), ("side1", counts[1])]:
        rgb = np.broadcast_to(np.array([0.3, 0.3, 0.3]), (256, 256, 3)).copy()
        rgb *= tint ** count[:, :, None]
        rgb = np.minimum(1, rgb + sources[0] * count[:, :, None])
        for n in range(int(count.max())):
            rgb = np.where((count > n)[:, :, None], np.minimum(1, sources[1] * (1 + rgb)), rgb)
        covered = count > 0
        stats[label] = {"pixels": int(covered.sum()), "max_layers": int(count.max()),
                        "mean_layers": float(count[covered].mean()),
                        "mean_rgb": rgb[covered].mean(axis=0).tolist()}
        if args.output_prefix:
            Image.fromarray(np.uint8(np.clip(rgb[::-1] * 255, 0, 255))).save(
                args.output_prefix + "-" + label + ".png")
    print(json.dumps({"frame": frame, "energy": [s.tolist() for s in sources], "coverage": stats}, indent=2))


if __name__ == "__main__":
    main()
