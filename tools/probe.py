#!/usr/bin/env python3
"""Build and run the original A1 in a dedicated, bounded CrossOver probe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / "local"
CX = Path("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin")
ZIG = LOCAL / "tools/zig-aarch64-macos-0.15.2/zig"
BOTTLE = "A1Research"
EXPECTED = {
    "a1.dll": "073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4",
    "A1_Default_Bank2.fxb": "7d27216672c0de3f365b96e617358f7bf3942e2004d765ef55dbb019828dec7a",
    "A1.vstxml": "6a8684222caa43c54cce97c3655a133ff293283dd69de9a91541bf10ed0994a7",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=int, default=12, help="seconds, 1–60")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 60:
        parser.error("timeout must be between 1 and 60 seconds")
    for name, digest in EXPECTED.items():
        p = LOCAL / "original" / name
        if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            raise SystemExit(f"Missing or changed original: {p}")
    if not ZIG.is_file():
        raise SystemExit(f"Expected the pinned Zig 0.15.2 toolchain at {ZIG}")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = str(LOCAL / "zig-cache")
    subprocess.run(
        [str(ZIG), "cc", "-target", "x86-windows-gnu", "-std=c11", "-O2",
         "-Wall", "-Wextra", "-Werror", str(ROOT / "tools/engine_probe.c"),
         "-o", str(build / "engine_probe.exe")], env=env, check=True,
    )
    print("Build passed (Windows x86; warnings treated as errors).", flush=True)
    if args.build_only:
        return 0
    if not (CX / "wine").is_file():
        raise SystemExit("CrossOver is required for this investigation probe.")
    bottles = LOCAL / "bottles"
    bottles.mkdir(exist_ok=True)
    env["CX_BOTTLE_PATH"] = str(bottles)
    if not (bottles / BOTTLE / "cxbottle.conf").is_file():
        subprocess.run(
            [str(CX / "cxbottle"), "--bottle", BOTTLE, "--create",
             "--template", "win10_64"], env=env, check=True, timeout=45,
        )
    evidence = LOCAL / "evidence"
    evidence.mkdir(exist_ok=True)
    stamp = time.time_ns()
    log_path = evidence / f"probe-{stamp}.log"
    wave_path = evidence / f"probe-{stamp}.wav"
    # CrossOver converts existing input paths. A new output needs a Windows path.
    output = "Z:" + str(wave_path).replace("/", "\\")
    command = [str(CX / "wine"), "--bottle", BOTTLE, "--no-gui", "--debugmsg",
               "-all,+msgbox", str(build / "engine_probe.exe"),
               str(LOCAL / "original/a1.dll"),
               str(LOCAL / "original/A1_Default_Bank2.fxb"), "1", output]
    timed_out = False
    started = time.monotonic()
    with log_path.open("x") as log:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            code = 124
        finally:
            stop_env = env.copy()
            stop_env["WINEPREFIX"] = str(bottles / BOTTLE)
            try:
                subprocess.run([str(CX / "wineserver"), "-k"], env=stop_env,
                               capture_output=True, check=True, timeout=10)
            finally:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    output_log = log_path.read_text(errors="replace")
    host_rejected = "A1 Analog Synth Unit doesn't work with this host!" in output_log
    status = "blocked_by_original_host_restriction" if host_rejected else (
        "rendered" if code == 0 and wave_path.is_file() else "failed")
    result = {
        "status": status, "process_exit": code, "timed_out": timed_out,
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "log": str(log_path), "audio": str(wave_path) if wave_path.is_file() else None,
        "native_macos_plugin_built": False,
    }
    (evidence / "probe-result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 12 if host_rejected else (0 if status == "rendered" else 1)


if __name__ == "__main__":
    sys.exit(main())
