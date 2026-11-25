"""Build a pinned, reviewed translator; never modifies installed runtimes."""
import argparse
from pathlib import Path
import subprocess

PIN = "052944fc613e187f5e238bfa362b411787045d6b"
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "local/tools/x87sidecar-a1"
PATCH = ROOT / "patches/x87sidecar-a1.patch"


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    if not SOURCE.exists():
        run(["git", "clone", "--no-checkout", "https://github.com/athei/x87sidecar.git", str(SOURCE)])
        run(["git", "checkout", "--detach", PIN], cwd=SOURCE)
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    if head != PIN:
        raise RuntimeError("Unexpected x87sidecar checkout; retain it and prepare the pinned source separately")
    diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=SOURCE)
    expected = PATCH.read_bytes()
    if not diff:
        run(["git", "apply", "--check", str(PATCH)], cwd=SOURCE)
        run(["git", "apply", str(PATCH)], cwd=SOURCE)
        diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=SOURCE)
    if diff != expected:
        raise RuntimeError("Unexpected translator source changes; refusing to build")
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard"], cwd=SOURCE)
    if untracked.strip():
        raise RuntimeError("Unexpected files in translator source; refusing to build")
    build = args.build_dir.resolve()
    run(["cmake", "-S", str(SOURCE), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64"])
    run(["cmake", "--build", str(build), "--target", "x87sidecar", "-j6"])
    run([str(build / "bin/x87sidecar"), "--probe"])


if __name__ == "__main__":
    main()
