#!/usr/bin/env python3
"""Read-only Wedge pulse/color audit. Requires ffmpeg, NumPy and Pillow.

The fixed ROI fits the two user recordings, not arbitrary gameplay video.
Palette distances ignore phase and overlap count; they are diagnostic evidence,
not a pixel-perfect reconstruction of the headset compositor or capture encoder.
"""

import argparse
import io
import json
import subprocess
import zipfile

import numpy as np
from PIL import Image


def video_colors(path):
    raw = subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", path, "-vf", "fps=10,scale=400:400",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
    ])
    frames = np.frombuffer(raw, np.uint8).reshape(-1, 400, 400, 3)
    colors = []
    for frame in frames:
        roi = frame[115:232, 80:330].reshape(-1, 3).astype(int)
        mask = (roi[:, 2] > roi[:, 0] + 35) & (roi[:, 2] > roi[:, 1] + 30) & (roi[:, 2] > 90)
        pixels = roi[mask]
        if not len(pixels):
            raise ValueError("No projection found in fixed ROI")
        bins = pixels // 8
        keys = bins[:, 0] * 1024 + bins[:, 1] * 32 + bins[:, 2]
        dominant = np.bincount(keys).argmax()
        colors.append(np.median(pixels[keys == dominant], axis=0))
    return np.array(colors) / 255.0


def linearize(rgb):
    return np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)


def encode(rgb):
    rgb = np.clip(rgb, 0, 1)
    return np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * rgb ** (1 / 2.4) - 0.055)


def rec2020_to_srgb(rgb):
    # Linear-light D65 primary conversion (W3C CSS Color 4 conversion matrices).
    # OpenXR's sRGB swapchain transfer is separate from XR_FB_color_space primaries.
    matrix = np.array([
        [1.660491, -0.587641, -0.072850],
        [-0.124550, 1.132900, -0.008349],
        [-0.018151, -0.100579, 1.118730],
    ])
    return encode(linearize(rgb) @ matrix.T)


def sample(image, uv):
    height, width, _ = image.shape
    xy = (uv % 1) * [width, height] - 0.5
    ix, iy = np.floor(xy).astype(int).T
    fx, fy = (xy - np.floor(xy)).T
    return ((1 - fy[:, None]) * ((1 - fx[:, None]) * image[iy % height, ix % width] +
                                fx[:, None] * image[iy % height, (ix + 1) % width]) +
            fy[:, None] * ((1 - fx[:, None]) * image[(iy + 1) % height, ix % width] +
                           fx[:, None] * image[(iy + 1) % height, (ix + 1) % width]))


def palette(energy, tint, legacy_upload=False, wide_gamut=False):
    if legacy_upload:
        # Quest r_picmip=1: reduce first, then R_LightScaleTexture before upload.
        h, w, _ = energy.shape
        energy = np.floor(energy.reshape(h // 2, 2, w // 2, 2, 3).mean(axis=(1, 3)))
        def lookup(pixels):
            intensity = np.minimum(255, np.floor(pixels * 1.07))
            return np.floor(255 * (intensity / 255) ** (1 / 1.15) + 0.5) / 255
        energy, tint = lookup(energy), lookup(tint)
    else:
        energy, tint = energy / 255, tint / 255
    seconds = np.arange(0, 5, 0.005)
    first = sample(energy, np.column_stack((seconds * 0, seconds * 0.2)))
    second = sample(energy, np.column_stack((seconds * 0.2, seconds * -0.2)))
    colors = []
    for layers in range(1, 6):
        # Stage-major rasterization, with UNORM clamping after each operation.
        rgb = np.broadcast_to(0.3 * tint ** layers, first.shape).copy()
        rgb = np.clip(rgb + first * layers, 0, 1)
        for _ in range(layers):
            rgb = np.clip(second * (1 + rgb), 0, 1)
        colors.append(rec2020_to_srgb(rgb) if wide_gamut else rgb)
    return np.concatenate(colors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    parser.add_argument("quest_video")
    parser.add_argument("vulkan_video")
    parser.add_argument("--profile-videos", nargs="*", default=[],
                        help="Compare recorded profile palettes directly to the Quest video")
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as archive:
        def texture(name):
            return np.asarray(Image.open(io.BytesIO(archive.read(
                "models/map_objects/wedge/" + name + ".jpg"))).convert("RGB"), dtype=float)
        energy, tint = texture("power38"), texture("blue")[0, 0]
    candidates = {
        "raw-upload-srgb": palette(energy, tint),
        "legacy-upload-srgb": palette(energy, tint, legacy_upload=True),
        "raw-upload-rec2020": palette(energy, tint, wide_gamut=True),
        "legacy-upload-rec2020": palette(energy, tint, legacy_upload=True, wide_gamut=True),
    }
    result = {}
    for label, path in [("quest", args.quest_video), ("vulkan", args.vulkan_video)]:
        colors = video_colors(path)
        distances = {}
        for name, predicted in candidates.items():
            per_frame = np.sqrt(np.min(np.mean(
                (colors[:, None, :] - predicted[None, :, :]) ** 2, axis=2), axis=1)) * 255
            distances[name] = {
                "mean": float(per_frame.mean()), "p90": float(np.percentile(per_frame, 90)),
            }
        result[label] = {
            "frames": len(colors), "sample_fps": 10,
            "dominant_rgb_min": (colors.min(axis=0) * 255).tolist(),
            "dominant_rgb_max": (colors.max(axis=0) * 255).tolist(),
            "palette_distance_rgb_8bit": distances,
            "colors_by_time": (colors * 255).tolist(),
        }
    if args.profile_videos:
        reference = np.array(result["quest"]["colors_by_time"])
        profiles = {}
        for path in args.profile_videos:
            colors = video_colors(path) * 255
            distances = np.sqrt(np.mean(
                (colors[:, None, :] - reference[None, :, :]) ** 2, axis=2))
            profiles[path] = {
                "frames": len(colors),
                "nearest_reference_rgb_error_mean": float(distances.min(axis=1).mean()),
                "symmetric_palette_error_mean": float(
                    (distances.min(axis=1).mean() + distances.min(axis=0).mean()) / 2),
                "rgb_min": colors.min(axis=0).tolist(),
                "rgb_max": colors.max(axis=0).tolist(),
            }
        result["recorded_profiles"] = profiles
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
