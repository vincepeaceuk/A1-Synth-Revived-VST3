"""Validate release identity, supported OS, untouched assets and signed runtimes."""
import os
from pathlib import Path
import hashlib
import json
import plistlib
import re
import subprocess
import sys
import unittest

if len(sys.argv) != 2:
    raise SystemExit("usage: PackageContractTests.py BUNDLE.vst3")
BUNDLE = Path(sys.argv.pop()).resolve()
RESOURCES = BUNDLE / "Contents/Resources"


class PackageContracts(unittest.TestCase):
    def test_release_identity_keeps_existing_projects_compatible(self):
        info = plistlib.loads((BUNDLE / "Contents/Info.plist").read_bytes())
        self.assertEqual(info["CFBundleIdentifier"], "uk.richardpeace.a1revival")
        self.assertEqual(info["CFBundleDisplayName"], "A1 Revival")
        # The SDK emits optional trailing commas in moduleinfo.json.
        module = json.loads(re.sub(r",(\s*[\]}])", r"\1", (RESOURCES / "moduleinfo.json").read_text()))
        self.assertEqual({item["CID"] for item in module["Classes"]}, {
            "ABCDEF019182FAEB5270527641315276",
            "ABCDEF011234ABCD5270527641315276",
        })
        self.assertEqual(module["Version"], info["CFBundleShortVersionString"])

    def test_declared_minimum_matches_the_runtime(self):
        info = plistlib.loads((BUNDLE / "Contents/Info.plist").read_bytes())
        self.assertGreaterEqual(tuple(map(int, info.get("LSMinimumSystemVersion", "0.0").split(".")))[:2], (14, 0))
        native = BUNDLE / "Contents/MacOS" / info["CFBundleExecutable"]
        build = subprocess.check_output(["vtool", "-show-build", str(native)], text=True)
        versions = re.findall(r"\bminos\s+(\d+)\.(\d+)", build)
        self.assertTrue(versions)
        self.assertTrue(all((int(major), int(minor)) >= (14, 0) for major, minor in versions))
        self.assertIn("arm64", subprocess.check_output(["lipo", "-archs", str(native)], text=True).split())

    def test_original_assets_are_unchanged(self):
        expected = {
            "a1.dll": "073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4",
            "A1_Default_Bank1.fxb": "62c7c9b98780e48b0a2209865a6bcfe19ebbc76ee8b5c3e7b3a150be81f644a0",
            "A1_Default_Bank2.fxb": "7d27216672c0de3f365b96e617358f7bf3942e2004d765ef55dbb019828dec7a",
            "A1.vstxml": "6a8684222caa43c54cce97c3655a133ff293283dd69de9a91541bf10ed0994a7",
        }
        for name, digest in expected.items():
            self.assertEqual(hashlib.sha256((RESOURCES / name).read_bytes()).hexdigest(), digest, name)
        self.assertEqual(len(list((RESOURCES / "OriginalUI").glob("BITMAP_*.png"))), 21)
        self.assertTrue((RESOURCES / "ThirdParty/x87sidecar-LICENSE.txt").is_file())

    def test_bundle_and_runtime_signatures(self):
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(BUNDLE)], check=True, capture_output=True)
        for name in ("a1-engine-launcher", "a1-coop.dylib", "a1-wineloader", "x87sidecar"):
            path = RESOURCES / name
            self.assertTrue(os.access(path, os.X_OK), name)
            subprocess.run(["codesign", "--verify", "--strict", str(path)], check=True, capture_output=True)


unittest.main()
