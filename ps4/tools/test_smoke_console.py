"""Observable smoke-runner regressions from ps4/CONSOLE-QA.md (AI-assisted)."""

from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

TOOLS = Path(__file__).resolve().parent
BUILD = "caliza-123456789abc"


def probe(frames, *, build=BUILD, session="session-1", sequence=1):
    return dict(build=build, session=session, game_sequence=sequence, mode="game", frames=frames, shelf_frames=0, vm_running=True)


class SmokeConsoleTests(unittest.TestCase):
    def run_console(self, samples, *, report=True, wait=1, phase="game"):
        remaining = iter(samples)
        current = samples[0]

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                nonlocal current
                if self.path == "/api/probe":
                    current = next(remaining, current)
                    data = json.dumps(current).encode()
                elif self.path == "/api/report" and report:
                    data = b"console diagnostics"
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        for port in range(8844, 8852):
            try:
                server = HTTPServer(("127.0.0.1", port), Handler)
                break
            except OSError:
                continue
        else:
            self.fail("could not bind a test port")
        with server, tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            # Stand in for only the FTP transport. The real collector chooses the URL
            # and writes diagnostics through the same command it uses on a PS4.
            curl = root / "curl"
            curl.write_text("""#!/bin/sh
for arg; do
  case "$arg" in
    ftp://*/data/caliza/logs/boot.log) found=1 ;;
    ftp://*/data/PCSX2/*) exit 88 ;;
  esac
  destination=$arg
done
[ "${found:-}" = 1 ] || exit 78
printf '[dbg] memmap: data=1\\n' > "$destination"
""")
            curl.chmod(0o700)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                result = subprocess.run(
                    [sys.executable, str(TOOLS / "smoke-console.py"), "127.0.0.1",
                     "--port", str(port), "--expect", phase, "--build", BUILD,
                     "--wait", str(wait), "--out", temp],
                    capture_output=True, text=True, timeout=10,
                    env={**os.environ, "PATH": f"{temp}{os.pathsep}{os.environ['PATH']}"},
                )
            finally:
                server.shutdown()
                thread.join()
            self.assertTrue(result.stdout, result.stderr)
            summary = json.loads(result.stdout)
            run = Path(summary["run"])
            return result.returncode, summary["verdict"], (
                (run / "report.txt").is_file(), (run / "ftp" / "boot.log").is_file()
            )

    def test_only_live_game_progress_passes(self):
        for label, samples, expected in (
            ("advancing", [probe(60), probe(120)], (0, "pass", (True, False))),
            ("frozen", [probe(60), probe(60)], (1, "inconclusive", (True, False))),
            ("restarted", [probe(60), probe(120, session="session-2")], (1, "inconclusive", (True, False))),
            ("changed game", [probe(60), probe(120, sequence=2)], (1, "inconclusive", (True, False))),
            ("wrong build", [probe(60, build="ps4-poc")], (1, "wrong_build", (True, False))),
        ):
            with self.subTest(label=label):
                self.assertEqual(self.run_console(samples), expected)

    def test_menu_requires_presentation_progress(self):
        idle = dict(probe(0), mode="menu", vm_running=False)
        live = dict(idle, shelf_frames=10)
        self.assertEqual(self.run_console([idle], phase="menu", wait=0), (1, "inconclusive", (True, False)))
        self.assertEqual(self.run_console([idle, live], phase="menu"), (0, "pass", (True, False)))

    def test_downloads_caliza_logs_when_http_report_fails(self):
        self.assertEqual(self.run_console([probe(0)], report=False, wait=0), (1, "inconclusive", (False, True)))


if __name__ == "__main__":
    unittest.main()
