#!/usr/bin/env python3
"""Observe a Caliza session and archive its diagnostics on the Mac."""

import argparse
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from urllib.error import HTTPError
from urllib.request import HTTPRedirectHandler, ProxyHandler, build_opener

TOOLS = Path(__file__).resolve().parent


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


# Keep console requests on the specified LAN address, even on a host with a proxy configured.
HTTP = build_opener(ProxyHandler({}), NoRedirect())


def read(url, cap, timeout):
    with HTTP.open(url, timeout=timeout) as response:
        data = response.read(cap + 1)
    if len(data) > cap:
        raise ValueError("response exceeds size limit")
    return data


def sample(url):
    value = json.loads(read(url + "/api/probe", 4096, 2))
    if (
        not isinstance(value, dict)
        or not isinstance(value.get("build"), str)
        or not isinstance(value.get("session"), str)
        or not value["session"]
        or value.get("mode") not in ("menu", "game")
        or type(value.get("vm_running")) is not bool
        or any(type(value.get(key)) is not int or value[key] < 0 for key in ("game_sequence", "frames", "shelf_frames"))
    ):
        raise ValueError("invalid Caliza probe response")
    return value


def observe(url, args, samples):
    deadline = time.monotonic() + args.wait
    previous = None
    counter = "frames" if args.expect == "game" else "shelf_frames"
    reason = "probe did not respond"
    while True:
        try:
            current = sample(url)
            samples.append(current)
        except HTTPError as error:
            reason = f"probe returned HTTP {error.code}"
            previous = None
        except (OSError, ValueError) as error:
            reason = f"probe unavailable: {error}"
            previous = None
        else:
            if current["build"] != args.build:
                return "wrong_build", f"expected {args.build}, got {current['build']}"
            if current["mode"] != args.expect or current["vm_running"] != (args.expect == "game"):
                reason = f"waiting for {args.expect}"
                previous = None
            else:
                if previous and all(current[key] == previous[key] for key in ("session", "game_sequence")):
                    if current[counter] > previous[counter]:
                        return "pass", f"{args.expect} frame counter advanced in the same session"
                    if current[counter] < previous[counter]:
                        return "invalid_probe", "frame counter regressed within the same session"
                previous = current
                reason = f"{args.expect} reported but no frame progress observed"
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return "inconclusive", reason
        time.sleep(min(1, remaining))


def collect_ftp(host, directory):
    destination = directory / "ftp"
    with (directory / "ftp-collection.txt").open("w") as log:
        try:
            result = subprocess.run(
                ["bash", str(TOOLS / "collect-console-logs.sh"), str(destination)],
                env={**os.environ, "PS4_HOST": host, "PS4_LOG_ROOT": "caliza", "PS4_LOG_ROTATIONS": "0"},
                stdout=log, stderr=subprocess.STDOUT, timeout=45, check=False,
            )
            collection = f"collector exit {result.returncode}"
        except subprocess.TimeoutExpired:
            collection = "collector timed out"
    logs = [destination / name for name in ("boot-trace.log", "boot.log", "emulog.txt", "stderr.log", "crash.log")]
    logs = [str(log) for log in logs if log.is_file() and log.stat().st_size]
    if logs:
        with (directory / "analysis.txt").open("w") as output:
            subprocess.run(
                [sys.executable, str(TOOLS / "analyze_console_logs.py"), *logs],
                stdout=output, stderr=subprocess.STDOUT, check=False,
            )
    return collection


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", type=ipaddress.IPv4Address, help="console's IPv4 address")
    parser.add_argument("--port", type=int, choices=range(8844, 8852), default=8844)
    parser.add_argument("--expect", choices=("menu", "game"), required=True)
    parser.add_argument("--build", required=True, help="caliza- followed by the 12-character build commit")
    parser.add_argument("--wait", type=int, default=20, metavar="SECONDS")
    parser.add_argument("--out", type=Path, required=True, help="parent directory for a new run folder")
    parser.add_argument("--ftp", action="store_true", help="also collect raw files when the HTTP report succeeds")
    args = parser.parse_args()
    if not re.fullmatch(r"caliza-[0-9a-f]{12}", args.build) or not 0 <= args.wait <= 300:
        parser.error("use a clean Caliza build tag and a wait between 0 and 300 seconds")

    args.out.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="caliza-", dir=args.out))
    url = f"http://{args.host}:{args.port}"
    samples = []
    verdict, detail = observe(url, args, samples)
    summary = {"verdict": verdict, "detail": detail, "run": str(directory), "samples": samples}
    try:
        report = read(url + "/api/report", 12 * 1024 * 1024, 5)
        (directory / "report.txt").write_bytes(report)
        summary["report"] = "saved"
    except (OSError, ValueError) as error:
        summary["report"] = str(error)
    if args.ftp or summary["report"] != "saved":
        summary["ftp"] = collect_ftp(str(args.host), directory)
    rendered = json.dumps(summary, indent=2)
    (directory / "result.json").write_text(rendered + "\n")
    print(rendered)
    return 0 if verdict == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
