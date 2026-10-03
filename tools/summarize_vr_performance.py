#!/usr/bin/env python3
"""Summarize fully contained 120-frame blocks after settled PERF_* markers."""
import argparse
import json
from pathlib import Path
import re
import statistics


def events(path):
    with path.open() as source:
        for line in source:
            yield json.loads(line)


def average(values):
    return round(statistics.mean(values), 4) if values else None


def summarize(folder, settle=15, duration=30, combat_duration=60):
    game = list(events(folder / "game.jsonl"))
    origin = game[0]["monotonic_s"]
    wall_offset = game[0]["unix_s"] - origin
    markers, interruptions, blocks = [], [], []
    block = None
    for event in game:
        line, now = event["line"], event["monotonic_s"]
        if re.fullmatch(r"PERF_(OUTDOOR|HOLOGRAM|AWAY|COMBAT|END)", line.strip()):
            markers.append((line.strip(), now))
        if "rd-vulkan-console: captured" in line or line.startswith("Server:"):
            interruptions.append(now)
        if line.startswith("rd-vulkan-timing: frames="):
            block = {"start": blocks[-1]["end"] if blocks else now, "end": now, "metrics": {}}
            blocks.append(block)
        if block is not None and line.startswith(("rd-vulkan-timing:", "rd-vulkan-phases:",
                "rd-vulkan-model-phases:", "rd-vulkan-shadow-timing:", "rd-vulkan-compute-skin:")):
            prefix = line.split(":", 1)[0]
            for key, value in re.findall(r"([a-z][\w-]*)=([-+\d.]+(?:/[-+\d.]+)*)", line):
                for index, number in enumerate(value.split("/")):
                    suffix = f"/{index}" if "/" in value else ""
                    block["metrics"][f"{prefix}.{key}{suffix}"] = float(number)
    completion = json.loads((folder / "completion.json").read_text())
    game_pid = str(completion["pids"]["game"])
    cpu = []
    columns = []
    for line in (folder / "pidstat.log").read_text().splitlines():
        if line.startswith("# Time"):
            columns = line[1:].split()
        elif columns and line and line[0].isdigit():
            values = line.split(maxsplit=len(columns) - 1)
            if len(values) == len(columns):
                cpu.append(dict(zip(columns, values)))
    gpu = []
    for event in events(folder / "amdgpu_top.jsonl"):
        try:
            data = json.loads(event["line"])
        except json.JSONDecodeError:
            continue
        if data.get("devices"):
            gpu.append((event["monotonic_s"], data["devices"][0]))
    results = []
    for index, (label, marker) in enumerate(markers):
        if label == "PERF_END":
            continue
        begin = marker + settle
        finish = begin + (combat_duration if label == "PERF_COMBAT" else duration)
        if index + 1 < len(markers):
            finish = min(finish, markers[index + 1][1])
        finish = min([finish] + [t for t in interruptions if begin < t < finish])
        samples = [b for b in blocks if begin <= b["start"] < b["end"] <= finish]
        if not samples:
            results.append({"label": label, "marker_s": marker - origin, "blocks": 0})
            continue
        # Match host samples to the complete renderer blocks, not the console.
        low, high = samples[0]["start"], samples[-1]["end"]
        total_seconds = sum(b["end"] - b["start"] for b in samples)
        metrics = {key: average([b["metrics"][key] for b in samples if key in b["metrics"]])
                   for key in samples[0]["metrics"]}
        host = [row for row in cpu if low + wall_offset <= float(row["Time"]) <= high + wall_offset]
        process = [float(row["%CPU"]) for row in host if row["TGID"] == game_pid and row["TID"] == "-"]
        main = [float(row["%CPU"]) for row in host if row["TID"] == game_pid and row["TGID"] == "-"]
        devices = [device for now, device in gpu if low <= now <= high]
        def values(section, key):
            return [d[section][key]["value"] for d in devices
                    if d.get(section, {}).get(key) is not None]
        results.append({
            "label": label, "marker_s": round(marker - origin, 3),
            "start_s": round(low - origin, 3), "end_s": round(high - origin, 3),
            "blocks": len(samples), "effective_game_submissions_per_second": round(
                sum(b["metrics"]["rd-vulkan-timing.frames"] for b in samples) / total_seconds, 3),
            "metrics": metrics, "host": {
                "game_cpu_percent": average(process), "game_main_thread_cpu_percent": average(main),
                "gpu_samples": len(devices),
                "gpu_name": devices[0]["Info"]["DeviceName"] if devices else None,
                "gfx_activity_percent": average(values("gpu_activity", "GFX")),
                "media_activity_percent": average(values("gpu_activity", "MediaEngine")),
                "junction_max_c": max(values("Sensors", "Junction Temperature"), default=None),
            }})
    return {"capture": str(folder), "settle_seconds": settle, "stationary_seconds": duration,
            "combat_seconds": combat_duration, "completion": completion, "samples": results,
            "caveats": ["Game submission cadence is not delivered headset FPS.",
                        "CPU wait includes GPU queue completion: do not sum it with GPU time.",
                        "Only successful echo output is used as a phase marker.",
                        "An unknown query for a renderer cvar at startup does not change that cvar."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--settle", type=float, default=15)
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--combat-duration", type=float, default=60)
    args = parser.parse_args()
    print(json.dumps(summarize(args.capture, args.settle, args.duration, args.combat_duration), indent=2))
