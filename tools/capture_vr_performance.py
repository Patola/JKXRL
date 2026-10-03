#!/usr/bin/env python3
"""Capture a JKA baseline with timestamped engine, CPU and GPU logs.

Uses the user's running OpenXR runtime. Does not start/reconfigure WiVRn,
change installed files, or terminate processes it did not launch.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA = "/games/SteamLibrary/steamapps/common/Jedi Academy/GameData"


def runtime_processes():
    found = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            name = (proc / "comm").read_text().strip()
            if name.startswith(("wivrn", "wayvr", "openjk_sp", "openjo_sp")):
                try:
                    executable = os.readlink(proc / "exe")
                except OSError:
                    executable = None
                found.append({"pid": int(proc.name), "name": name,
                              "exe": executable})
        except (OSError, ValueError):
            continue
    return found


def metadata(command, env, note):
    settings = {}
    config = Path(env.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "openjk/base/openjk_sp.cfg"
    if config.is_file():
        for line in config.read_text(errors="replace").splitlines():
            try:
                words = shlex.split(line)
            except ValueError:
                continue
            if len(words) >= 3 and words[0] == "seta" and words[1].startswith(("r_", "com_maxfps")):
                settings[words[1]] = words[2]
    hashes = {}
    for name in ("openjk_sp.x86_64", "rdsp-vulkan_x86_64.so", "base/jagamex86_64.so"):
        path = Path("/usr/lib/jkxr") / name
        if path.is_file():
            with path.open("rb") as source:
                digest = hashlib.sha256()
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(block)
            hashes[str(path)] = digest.hexdigest()
    return {
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "kernel": platform.release(), "command": command, "note": note,
        "runtime_processes": runtime_processes(), "installed_sha256": hashes,
        "archived_renderer_settings_before_launch": settings,
        "environment": {k: env[k] for k in (
            "XR_RUNTIME_JSON", "VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE",
            "VK_DRIVER_FILES", "SDL_VIDEODRIVER", "JKXR_JKA_GAMEDATA") if k in env},
    }


def stop_process(process):
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def capture(directory, commands, env, stop):
    processes = {}
    threads = []
    failures = []

    def consume(name, process):
        try:
            with (directory / f"{name}.log").open("w", buffering=1) as raw, \
                    (directory / f"{name}.jsonl").open("w", buffering=1) as events:
                for line in process.stdout:
                    raw.write(line)
                    events.write(json.dumps({"unix_s": time.time(), "monotonic_s": time.monotonic(),
                                             "line": line.rstrip("\n")}) + "\n")
                    if name == "game":
                        sys.stdout.write(line)
                        sys.stdout.flush()
        except Exception as error:
            failures.append(f"{name}: {error}")
            stop.set()
        finally:
            process.stdout.close()

    try:
        # Start collectors before the game, so initial loads are captured too.
        for name, command in commands.items():
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, errors="replace", bufsize=1, env=env,
                                       start_new_session=True)
            processes[name] = process
            thread = threading.Thread(target=consume, args=(name, process))
            thread.start()
            threads.append(thread)
        reported = set()
        while processes["game"].poll() is None and not stop.wait(0.2):
            for name, process in processes.items():
                if name != "game" and process.poll() is not None and name not in reported:
                    failures.append(f"{name} stopped early (exit {process.returncode}); inspect its log")
                    print(f"WARNING: {failures[-1]}", file=sys.stderr)
                    reported.add(name)
    finally:
        for process in reversed(list(processes.values())):
            stop_process(process)
        for thread in threads:
            thread.join(timeout=10)
        summary = {"exit_codes": {name: proc.returncode for name, proc in processes.items()},
                   "pids": {name: proc.pid for name, proc in processes.items()},
                   "interrupted": stop.is_set(), "collector_errors": failures}
        (directory / "completion.json").write_text(json.dumps(summary, indent=2) + "\n")
    return processes["game"].returncode or (1 if failures else 0)


def self_test():
    for exit_code, interrupted in ((0, False), (7, False), (0, True)):
        with tempfile.TemporaryDirectory(prefix="jkxr-perf-test-") as folder:
            directory = Path(folder)
            stop = threading.Event()
            commands = {
                "collector": [sys.executable, "-u", "-c", "import time; print('sample'); time.sleep(60)"],
                "game": [sys.executable, "-u", "-c",
                         f"import time; print('PERF_OUTDOOR'); time.sleep({60 if interrupted else 0.4}); raise SystemExit({exit_code})"],
            }
            timer = threading.Timer(0.5, stop.set) if interrupted else None
            if timer:
                timer.start()
            try:
                result = capture(directory, commands, dict(os.environ), stop)
            finally:
                if timer:
                    timer.cancel()
                    timer.join()
            assert result == (-signal.SIGTERM if interrupted else exit_code)
            events = [json.loads(line) for line in (directory / "game.jsonl").read_text().splitlines()]
            assert events[0]["line"] == "PERF_OUTDOOR"
            assert events[0]["monotonic_s"] > 0
            completion = json.loads((directory / "completion.json").read_text())
            for pid in completion["pids"].values():
                assert not Path(f"/proc/{pid}").exists(), f"Collector/child left running: {pid}"
    with tempfile.TemporaryDirectory(prefix="jkxr-perf-failure-") as folder:
        try:
            capture(Path(folder), {"collector": [sys.executable, "-c", "import time; time.sleep(60)"],
                                   "game": [str(Path(folder) / "missing-executable")]},
                    dict(os.environ), threading.Event())
            raise AssertionError("Missing executable was not rejected")
        except FileNotFoundError:
            completion = json.loads((Path(folder) / "completion.json").read_text())
            assert all(not Path(f"/proc/{pid}").exists() for pid in completion["pids"].values())
    print("PASS: timestamps, normal exit, failure exit, interruption and failed-start cleanup")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, default=Path("/tmp/jkxrl-perf"))
    parser.add_argument("--note", default="", help="Optional headset settings or run notes")
    parser.add_argument("--self-test", action="store_true", help="Test cleanup without launching the game or WiVRn")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    for command in ("jkxr-jka", "pidstat", "amdgpu_top"):
        if not shutil.which(command):
            parser.error(f"Required command not found: {command}")
    if any(p["name"].startswith(("openjk_sp", "openjo_sp")) for p in runtime_processes()):
        parser.error("Exit the running game first; this starts a separate measured run.")
    env = dict(os.environ, LC_ALL="C")
    env.setdefault("JKXR_JKA_GAMEDATA", DEFAULT_DATA)
    if not (Path(env["JKXR_JKA_GAMEDATA"]) / "base/assets0.pk3").is_file():
        parser.error("Set JKXR_JKA_GAMEDATA to the installed Jedi Academy GameData directory.")
    game = ["jkxr-jka", "+set", "rd_vulkanDiagnosticWorld", "0", "+set", "cg_thirdPerson", "0",
            "+set", "r_vulkanTiming", "1", "+set", "r_vulkanQuestColorProfile", "0",
            "+set", "r_vulkanBloom", "0"]
    for cvar in ("r_vulkanShadows", "r_vulkanShadowFilter", "r_vulkanComputeSkinning",
                 "r_picmip", "r_dynamiclight", "com_maxfps",
                 "com_maxfpsUnfocused", "com_maxfpsMinimized"):
        game.append("+" + cvar)
    commands = {
        "pidstat": ["pidstat", "-h", "-H", "-u", "-r", "-w", "-t", "-C", "openjk_sp|wivrn|wayvr", "1"],
        "amdgpu_top": ["amdgpu_top", "-J", "-s", "1000"],
        "game": game,
    }
    args.output_root.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix=time.strftime("jka-wedge-%Y%m%d-%H%M%S-"), dir=args.output_root))
    info = metadata(game, env, args.note)
    info["collectors"] = commands
    (directory / "metadata.json").write_text(json.dumps(info, indent=2) + "\n")
    stop = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda _sig, _frame: stop.set())
    print(f"Capture: {directory}\nCommand: {shlex.join(game)}\nCollectors stop automatically on game exit.", flush=True)
    try:
        return capture(directory, commands, env, stop)
    finally:
        print(f"Capture finished: {directory}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
