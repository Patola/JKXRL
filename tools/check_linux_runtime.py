#!/usr/bin/env python3
"""Check Linux install/launcher contracts and optional built ELF dependencies."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
GAMES = (
    ("jka", "openjk", "rdsp", "code/game/jagamex86_64.so"),
    ("jko", "openjo", "rdjosp", "codeJK2/game/jospgamex86_64.so"),
)


def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs)


def check_installers():
    with tempfile.TemporaryDirectory(prefix="jkxr-install-test-") as temp:
        work = Path(temp)
        assets = work / "assets"
        (assets / "weapons").mkdir(parents=True)
        (assets / "z_vr_assets_base.pk3").write_bytes(b"base-vr")
        shutil.copy2(ROOT / "install_linux.sh", work)
        build = work / "build"
        for game, home, renderer, module in GAMES:
            engine = f"{home}_sp.x86_64"
            renderer_path = f"code/rd-vulkan/{renderer}-vulkan_x86_64.so"
            for name in (engine, renderer_path, module):
                dest = build / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text("#!/bin/sh\nprintf '%s\\n' \"$SDL_VIDEODRIVER\" \"$@\"\n")
                dest.chmod(0o755)
            game_pk3 = f"z_vr_assets_{game}.pk3"
            weapon_pk3 = f"z_vr_weapons_{game}_Crusty_and_Elin.pk3"
            (assets / game_pk3).write_bytes(b"game-vr")
            (assets / "weapons" / weapon_pk3).write_bytes(b"weapons")
            data = work / f"{game} GameData"
            (data / "base").mkdir(parents=True)
            (data / "base/assets0.pk3").touch()
            user = work / "user"
            user_base = user / home / "base"
            user_base.mkdir(parents=True)
            game_module = Path(module).name
            (user_base / game_module).write_bytes(b"stale-module")
            (user_base / "user.cfg").write_bytes(b"preserve-user-settings")
            env = dict(os.environ, XDG_DATA_HOME=str(user), BUILD_DIR=str(build))
            run(["sh", str(work / "install_linux.sh"), game, str(data)], env=env)
            assert (data / Path(renderer_path).name).is_file()
            assert not (data / game_module).exists()
            assert (data / "base" / game_module).read_bytes() == (build / module).read_bytes()
            assert (user_base / game_module).read_bytes() == (build / module).read_bytes()
            assert (user_base / "user.cfg").read_bytes() == b"preserve-user-settings"

            # Relocate only the package roots for the fixture, not launcher logic.
            lib = work / f"lib-{game}"
            (lib / "base").mkdir(parents=True)
            shutil.copy2(build / engine, lib / engine)
            shutil.copy2(build / module, lib / "base" / game_module)
            share = work / f"share-{game}"
            share.mkdir()
            for src in (assets / "z_vr_assets_base.pk3", assets / game_pk3,
                        assets / "weapons" / weapon_pk3):
                shutil.copy2(src, share / src.name)
            launcher = (ROOT / "packaging/arch" / f"jkxr-{game}").read_text()
            launcher = launcher.replace("LIBDIR=/usr/lib/jkxr", f'LIBDIR="{lib}"')
            launcher = launcher.replace(f"SHAREDIR=/usr/share/jkxr/{game}", f'SHAREDIR="{share}"')
            script = work / f"jkxr-{game}"
            script.write_text(launcher)
            env[f"JKXR_{game.upper()}_GAMEDATA"] = str(data)
            env.pop("SDL_VIDEODRIVER", None)
            for driver in ("x11", "wayland"):
                if driver == "wayland":
                    env["SDL_VIDEODRIVER"] = driver
                (user_base / game_module).write_bytes(b"stale-again")
                (user_base / game_pk3).write_bytes(b"stale-pack")
                # Newer timestamps must not prevent a content-based update.
                os.utime(user_base / game_module, (2_000_000_000, 2_000_000_000))
                result = run(["sh", str(script), "+set", "r_vulkanBloom", "0"], env=env)
                assert result.stdout.splitlines() == [
                    driver, "+set", "fs_basepath", str(data),
                    "+set", "r_vulkanBloom", "0",
                ], result.stdout
                assert (user_base / game_module).read_bytes() == (build / module).read_bytes()
                assert (user_base / game_pk3).read_bytes() == (assets / game_pk3).read_bytes()
            env[f"JKXR_{game.upper()}_GAMEDATA"] = str(work / "missing")
            rejected = subprocess.run(["sh", str(script)], env=env, capture_output=True)
            assert rejected.returncode != 0
    print("PASS: both installers/launchers, module location, stale-file sync, driver override, arguments")


def check_elf(build):
    for game, home, renderer, module in GAMES:
        files = (f"{home}_sp.x86_64",
                 f"code/rd-vulkan/{renderer}-vulkan_x86_64.so", module)
        for name in files:
            binary = build / name
            dynamic = run(["readelf", "-d", str(binary)]).stdout
            symbols = run(["nm", "-D", "--undefined-only", str(binary)]).stdout
            for forbidden in ("libGL.", "libGLX.", "libOpenGL.", "libGLU.", "libGLES", "libSDL2"):
                assert forbidden not in dynamic, (name, forbidden)
            assert "SDL_GL_" not in symbols and "glX" not in symbols, name
            if name.endswith(".x86_64"):
                assert "libSDL3.so" in dynamic
            if "rd-vulkan" in name:
                assert "libvulkan.so" in dynamic and "libopenxr_loader.so" in dynamic
    print("PASS: six runtime ELFs have no legacy graphics dependencies/symbols")


if __name__ == "__main__":
    check_installers()
    if len(sys.argv) > 1:
        check_elf(Path(sys.argv[1]).resolve())
