#!/usr/bin/env python3

from __future__ import annotations

import csv
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = ROOT / "reports" / "analysis"
PRODUCTION_PREFIXES = ("client/", "common/", "server/")
RUNTIME_CRITICAL_PATHS = {
    "client/aircraft_client.cpp",
    "client/client_app.cpp",
    "server/ground_server.cpp",
    "server/imgui_dashboard.cpp",
    "server/main.cpp",
    "client/main.cpp",
}


@dataclass
class CommandResult:
    command: list[str]
    returncode: int
    stdout: str


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")


def ensure_directory(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def repo_relative(path: str | Path) -> str:
    candidate = Path(path)
    try:
        return candidate.resolve().relative_to(ROOT).as_posix()
    except Exception:
        text = candidate.as_posix()
        if text.startswith(str(ROOT)):
            return Path(os.path.relpath(text, ROOT)).as_posix()
        return text


def is_production_path(path: str | Path) -> bool:
    relative = repo_relative(path)
    return relative.endswith((".cpp", ".h")) and relative.startswith(PRODUCTION_PREFIXES)


def module_for(path: str | Path) -> str:
    relative = repo_relative(path)
    return relative.split("/", 1)[0] if "/" in relative else relative


def production_files() -> list[Path]:
    files: list[Path] = []
    for prefix in PRODUCTION_PREFIXES:
        files.extend(sorted((ROOT / prefix.rstrip("/")).rglob("*.cpp")))
        files.extend(sorted((ROOT / prefix.rstrip("/")).rglob("*.h")))
    return files


def write_text(path: Path, content: str) -> None:
    ensure_directory(path.parent)
    path.write_text(content, encoding="utf-8")


def write_json(path: Path, data: object) -> None:
    write_text(path, json.dumps(data, indent=2, sort_keys=True) + "\n")


def write_csv(path: Path, headers: list[str], rows: Iterable[dict[str, object]]) -> None:
    ensure_directory(path.parent)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def markdown_table(headers: list[str], rows: Iterable[Iterable[object]]) -> str:
    rows = list(rows)
    if not rows:
        return "_No rows._\n"
    header_line = "| " + " | ".join(headers) + " |"
    separator_line = "| " + " | ".join(["---"] * len(headers)) + " |"
    body_lines = ["| " + " | ".join(str(value) for value in row) + " |" for row in rows]
    return "\n".join([header_line, separator_line, *body_lines]) + "\n"


def run_command(
    command: list[str],
    *,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
    log_path: Path | None = None,
    allow_failure: bool = False,
    timeout_seconds: int | None = None,
) -> CommandResult:
    command_text = shlex.join(command)
    try:
        completed = subprocess.run(
            command,
            cwd=str(cwd),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
            timeout=timeout_seconds,
        )
        stdout = completed.stdout
        returncode = completed.returncode
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        returncode = 124
        if exc.stderr:
            stdout += exc.stderr

    log_content = f"$ {command_text}\n\n{stdout}"
    if timeout_seconds is not None and returncode == 124:
        log_content += f"\n\n[analysis] Command timed out after {timeout_seconds} seconds.\n"
    if log_path is not None:
        write_text(log_path, log_content)
    if returncode != 0 and not allow_failure:
        raise RuntimeError(f"Command failed ({returncode}): {command_text}\n{stdout}")
    return CommandResult(command=command, returncode=returncode, stdout=stdout)


def find_tool(name: str) -> str | None:
    return shutil.which(name)


def find_xcrun_tool(name: str) -> str | None:
    try:
        result = run_command(["xcrun", "--find", name], allow_failure=True)
    except RuntimeError:
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip() or None


def relative_command_path(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def copy_runtime_snapshot(destination: Path) -> None:
    runtime_root = ROOT / "runtime"
    if not runtime_root.exists():
        return
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(runtime_root, destination)


def collect_environment_metadata() -> dict[str, str]:
    metadata = {
        "platform": platform.platform(),
        "python": sys.version.split()[0],
    }
    for label, command in (
        ("cmake", ["cmake", "--version"]),
        ("clang", ["clang", "--version"]),
        ("brew", ["brew", "--version"]),
    ):
        result = run_command(command, allow_failure=True)
        metadata[label] = result.stdout.splitlines()[0] if result.stdout else "unavailable"
    return metadata
