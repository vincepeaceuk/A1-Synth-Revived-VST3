"""A staged source edit must not bypass the translator's pinned-source gate."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("a1_runtime_builder", ROOT / "tools/prepare-x87-runtime.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class SourcePinTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="A1 source pin fixture ")
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        subprocess.run(["git", "clone", "--no-hardlinks", str(BUILDER.SOURCE), str(self.source)],
                       check=True, capture_output=True)
        subprocess.run(["git", "apply", str(BUILDER.PATCH)], cwd=self.source,
                       check=True, capture_output=True)

    def tearDown(self):
        self.temporary.cleanup()

    def validate(self):
        with patch.object(BUILDER, "SOURCE", self.source), patch.object(
                sys, "argv", [str(SPEC.origin), "--build-dir", str(self.root / "never-built")]):
            BUILDER.main()

    def test_staged_source_change_is_rejected(self):
        source = self.source / "rosetta_core/src/Translator.cpp"
        source.write_text(source.read_text() + "\n// Unreviewed staged fixture change.\n")
        subprocess.run(["git", "add", "rosetta_core/src/Translator.cpp"], cwd=self.source, check=True)
        with self.assertRaisesRegex(RuntimeError, "Unexpected translator source changes"):
            self.validate()
        self.assertFalse((self.root / "never-built").exists())

    def test_untracked_source_is_rejected(self):
        (self.source / "unexpected-security-fixture.c").write_text("/* unreviewed fixture */\n")
        with self.assertRaisesRegex(RuntimeError, "Unexpected files"):
            self.validate()
        self.assertFalse((self.root / "never-built").exists())


if __name__ == "__main__":
    unittest.main()
