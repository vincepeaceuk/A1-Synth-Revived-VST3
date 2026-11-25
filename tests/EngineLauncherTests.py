"""Exercise only owned native/Wine fixtures; no original A1 DLL is loaded."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

parser = argparse.ArgumentParser()
for name in ("launcher", "shim", "sidecar", "wine_loader", "fixture", "slow_shim", "work_dir"):
    parser.add_argument("--" + name.replace("_", "-"), required=True, type=Path)
ARGS, remaining = parser.parse_known_args()
PATTERN = bytes(range(256)) * 64


def windows(path):
    return "Z:" + str(path).replace("/", "\\")


class LauncherTests(unittest.TestCase):
    def setUp(self):
        ARGS.work_dir.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="A1 launch 'fixture' ", dir=ARGS.work_dir))
        self.children = []
        self.sockets = []
        self.logs = []
        for source, name in ((ARGS.launcher, "a1-engine-launcher"), (ARGS.shim, "a1-coop.dylib"),
                             (ARGS.sidecar, "x87sidecar"), (ARGS.wine_loader, "a1-wineloader"),
                             (ARGS.fixture, "a1-helper.exe")):
            shutil.copy2(source, self.root / name)
        (self.root / "a1.dll").write_bytes(b"unit fixture only; never an original DLL")

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=3)
        for connection in self.sockets:
            connection.close()
        for log in self.logs:
            log.close()
        # The sidecar is detached by upstream. Match only this unique test path.
        for pid in self.sidecars():
            os.kill(pid, 15)

    def sidecars(self):
        result = subprocess.run(["/bin/ps", "-axo", "pid=,command="], capture_output=True,
                                text=True, check=True)
        prefix = str(self.root / "x87sidecar") + " "
        return [int(line.split(None, 1)[0]) for line in result.stdout.splitlines()
                if len(line.split(None, 1)) == 2 and line.split(None, 1)[1].startswith(prefix)]

    def launch(self, stage=False, env_extra=None, invalid=False):
        parent, child_socket = socket.socketpair()
        # Match the existing helper's cold-start budget, not its render budget.
        parent.settimeout(15)
        self.sockets.append(parent)
        log_path = self.root / ("launch-%d.log" % len(self.children))
        log = log_path.open("wb")
        self.logs.append(log)
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(("X87_", "ROSETTA_", "A1_X87_", "DYLD_"))}
        environment.update(WINEPREFIX=str(Path.home() / "Library/Application Support/"
                                         "A1RevivalPreview/bottles/A1RevivalPreview"),
                           WINEDLLPATH="/Applications/CrossOver.app/Contents/SharedSupport/"
                                       "CrossOver/lib/wine",
                           WINESERVER="/Applications/CrossOver.app/Contents/SharedSupport/"
                                      "CrossOver/bin/wineserver", WINEDEBUG="-all,+msgbox")
        environment.update(env_extra or {})
        args = [str(self.root / "a1-engine-launcher")]
        if stage:
            args.append("--run")
        args += [windows(self.root / ("wrong.exe" if invalid else "a1-helper.exe")),
                 windows(self.root / "a1.dll")]
        child = subprocess.Popen(args, stdin=child_socket, stdout=child_socket, stderr=log,
                                 env=environment)
        child_socket.close()
        self.children.append(child)
        return child, parent, log_path

    def receive(self, connection, size):
        result = b""
        while len(result) < size:
            part = connection.recv(size - len(result))
            self.assertTrue(part, "fixture connection closed before its complete payload")
            result += part
        return result

    def finish(self, child, connection):
        self.assertEqual(self.receive(connection, len(PATTERN)), PATTERN)
        connection.sendall(b"\x7f")
        self.assertEqual(child.wait(timeout=3), 0)
        self.assertEqual(connection.recv(1), b"")

    def test_missing_bootstrap_refuses_engine(self):
        child, connection, _ = self.launch(stage=True, env_extra={"A1_X87_READY": "1"})
        self.assertEqual(child.wait(timeout=3), 78)
        self.assertEqual(connection.recv(1), b"")

    def test_arguments_are_bound_to_resources(self):
        child, connection, _ = self.launch(invalid=True)
        self.assertEqual(child.wait(timeout=3), 64)
        self.assertEqual(connection.recv(1), b"")

    def test_missing_runtime_refuses_engine(self):
        (self.root / "a1-coop.dylib").unlink()
        child, connection, _ = self.launch()
        self.assertEqual(child.wait(timeout=3), 78)
        self.assertEqual(connection.recv(1), b"")

    def test_binary_socket_and_controlled_environment(self):
        profile = self.root / "unexpected-profile"
        child, connection, log = self.launch(env_extra={"X87_DISABLE_HOOK": "1",
                                                      "X87_PROFILE": str(profile),
                                                      "A1_X87_READY": "forged"})
        self.finish(child, connection)
        self.assertIn("A1 acceleration: cooperative x87 engine ready", log.read_text())
        self.assertFalse(list(self.root.glob("unexpected-profile*")))

    def test_instances_have_independent_lifetimes(self):
        first, first_socket, _ = self.launch()
        second, second_socket, _ = self.launch()
        self.finish(first, first_socket)
        self.assertIsNone(second.poll())
        self.finish(second, second_socket)

    def test_unhooked_fallback_refuses_engine(self):
        stub = self.root / "x87sidecar"
        stub.write_text('#!/bin/sh\nshift\nexec "$@"\n')
        stub.chmod(0o755)
        child, connection, _ = self.launch()
        self.assertEqual(child.wait(timeout=3), 78)
        self.assertEqual(connection.recv(1), b"")

    def test_close_during_bootstrap_reaps_sidecar(self):
        shutil.copy2(ARGS.slow_shim, self.root / "a1-coop.dylib")
        child, _, _ = self.launch()
        deadline = time.monotonic() + 2
        while not self.sidecars() and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertTrue(self.sidecars(), "fixture must reach cooperative setup")
        child.terminate()
        child.wait(timeout=2)
        deadline = time.monotonic() + 1.5
        while self.sidecars() and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertEqual(self.sidecars(), [])


if __name__ == "__main__":
    unittest.main(argv=[__file__, *remaining])
