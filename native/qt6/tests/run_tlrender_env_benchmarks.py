#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path


DEFAULT_VIDEO_DIR = Path("/mnt/ssd2/Tests/Videos")
DEFAULT_HARNESS = Path(
    "build-linux-recovery-native-qt6/tests/test_tlrender_playback_harness"
)
FPS_TOLERANCE = 0.6

METRICS_RE = re.compile(
    r"Playback metrics for (?P<path>.+?): elapsed=(?P<elapsed>[0-9.]+) s, "
    r"renderedSignals=(?P<rendered_signals>\d+) \((?P<rendered_fps>[0-9.]+) fps\), "
    r"distinctRasterFrames=(?P<distinct_frames>\d+) \((?P<distinct_fps>[0-9.]+) fps\), "
    r"distinctPlayerFrames=(?P<player_frames>\d+) \((?P<player_fps>[0-9.]+) fps\), "
    r"playerFrameSignals=(?P<player_signals>\d+), "
    r"frameRenderedIntervalMs\(avg/min/max\)=(?P<avg_ms>[0-9.]+)/(?P<min_ms>[0-9.]+)/(?P<max_ms>[0-9.]+)"
)


@dataclass
class BenchmarkRow:
    file_path: str
    expected_fps: float
    measured_rendered_fps: float
    passed: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run test_tlrender_playback_harness benchmarkEnvFile for each video in a directory."
    )
    parser.add_argument(
        "--video-dir",
        default=str(DEFAULT_VIDEO_DIR),
        help=f"Directory of videos to benchmark (default: {DEFAULT_VIDEO_DIR})",
    )
    parser.add_argument(
        "--harness",
        default=str(DEFAULT_HARNESS),
        help=f"Path to test_tlrender_playback_harness (default: {DEFAULT_HARNESS})",
    )
    parser.add_argument(
        "--tolerance",
        type=float,
        default=FPS_TOLERANCE,
        help=f"Allowed absolute fps delta for pass/fail (default: {FPS_TOLERANCE})",
    )
    return parser.parse_args()


def probe_expected_fps(file_path: Path) -> float:
    command = [
        "ffprobe",
        "-v",
        "error",
        "-select_streams",
        "v:0",
        "-show_entries",
        "stream=avg_frame_rate,r_frame_rate",
        "-of",
        "default=noprint_wrappers=1:nokey=1",
        str(file_path),
    ]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    for value in lines:
        fps = parse_fractional_rate(value)
        if fps > 0:
            return fps
    raise RuntimeError(f"Could not determine fps for {file_path}")


def parse_fractional_rate(value: str) -> float:
    try:
        return float(Fraction(value))
    except (ValueError, ZeroDivisionError):
        return 0.0


def run_harness(harness_path: Path, file_path: Path) -> float:
    env = os.environ.copy()
    env["KASSETMANAGER_TLRENDER_BENCHMARK_FILE"] = str(file_path)
    result = subprocess.run(
        [str(harness_path), "benchmarkEnvFile", "-o", "-"],
        check=False,
        capture_output=True,
        text=True,
        env=env,
    )

    output = result.stdout + result.stderr
    match = METRICS_RE.search(output)
    if result.returncode != 0:
        raise RuntimeError(
            f"Harness failed for {file_path} with exit code {result.returncode}\n{output}"
        )
    if not match:
        raise RuntimeError(
            f"Harness output did not include benchmark metrics for {file_path}\n{output}"
        )
    return float(match.group("rendered_fps"))


def list_video_files(video_dir: Path) -> list[Path]:
    files = [path for path in sorted(video_dir.iterdir()) if path.is_file()]
    if not files:
        raise RuntimeError(f"No files found in {video_dir}")
    return files


def format_table(rows: list[BenchmarkRow], tolerance: float) -> str:
    headers = (
        "file path",
        "expected fps",
        "measured rendered fps",
        f"pass/fail (tol {tolerance:.2f})",
    )
    body = [
        (
            row.file_path,
            f"{row.expected_fps:.3f}",
            f"{row.measured_rendered_fps:.2f}",
            "PASS" if row.passed else "FAIL",
        )
        for row in rows
    ]
    widths = [len(header) for header in headers]
    for record in body:
        for index, value in enumerate(record):
            widths[index] = max(widths[index], len(value))

    def format_row(values: tuple[str, str, str, str]) -> str:
        return " | ".join(
            value.ljust(widths[index]) for index, value in enumerate(values)
        )

    separator = "-+-".join("-" * width for width in widths)
    lines = [format_row(headers), separator]
    lines.extend(format_row(record) for record in body)
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    video_dir = Path(args.video_dir).resolve()
    harness_path = Path(args.harness).resolve()

    if not video_dir.is_dir():
        raise RuntimeError(f"Video directory does not exist: {video_dir}")
    if not harness_path.is_file():
        raise RuntimeError(f"Harness executable does not exist: {harness_path}")

    rows: list[BenchmarkRow] = []
    for file_path in list_video_files(video_dir):
        expected_fps = probe_expected_fps(file_path)
        measured_rendered_fps = run_harness(harness_path, file_path)
        passed = abs(measured_rendered_fps - expected_fps) <= args.tolerance
        rows.append(
            BenchmarkRow(
                file_path=str(file_path),
                expected_fps=expected_fps,
                measured_rendered_fps=measured_rendered_fps,
                passed=passed,
            )
        )

    print(format_table(rows, args.tolerance))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
