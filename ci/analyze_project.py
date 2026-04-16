#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
from collections import Counter
from pathlib import Path

from analysis_common import (
    REPORTS_ROOT,
    ROOT,
    RUNTIME_CRITICAL_PATHS,
    collect_environment_metadata,
    copy_runtime_snapshot,
    ensure_directory,
    find_tool,
    is_production_path,
    markdown_table,
    module_for,
    production_files,
    repo_relative,
    run_command,
    utc_timestamp,
    write_csv,
    write_json,
    write_text,
)


PRODUCTION_HEADERS = [
    "path",
    "module",
    "line_percent",
    "function_percent",
    "line_covered",
    "line_count",
    "function_covered",
    "function_count",
]


def candidate_executable_paths(build_dir: Path, name: str) -> list[Path]:
    suffix = ".exe" if os.name == "nt" else ""
    return [
        build_dir / f"{name}{suffix}",
        build_dir / "Debug" / f"{name}{suffix}",
        build_dir / "Release" / f"{name}{suffix}",
        build_dir / "RelWithDebInfo" / f"{name}{suffix}",
        build_dir / "MinSizeRel" / f"{name}{suffix}",
    ]


def find_executable(build_dir: Path, name: str) -> Path:
    for candidate in candidate_executable_paths(build_dir, name):
        if candidate.exists():
            return candidate
    searched = "\n".join(str(path) for path in candidate_executable_paths(build_dir, name))
    raise FileNotFoundError(f"Unable to locate {name} in analysis build.\nSearched:\n{searched}")


def session_root_for(stamp: str) -> Path:
    return REPORTS_ROOT / stamp


def latest_session_root() -> Path:
    if not REPORTS_ROOT.exists():
        raise FileNotFoundError("No reports/analysis output exists yet.")
    candidates = sorted(path for path in REPORTS_ROOT.iterdir() if path.is_dir())
    if not candidates:
        raise FileNotFoundError("No reports/analysis session directories were found.")
    return candidates[-1]


def prepare_session_root(stamp: str) -> Path:
    session_root = session_root_for(stamp)
    ensure_directory(session_root)
    metadata_path = session_root / "metadata.json"
    metadata = {
        "stamp": stamp,
        "environment": collect_environment_metadata(),
        "production_file_count": len(production_files()),
        "production_scope": ["client/", "common/", "server/"],
        "excluded_scope": ["third_party/", "tests/", "build/", "runtime/", "untracked_files/"],
        "compliance_position": (
            "This project does not claim formal Canadian aviation or MISRA certification. "
            "Generated evidence is intended for course documentation and classroom-style compliance discussion."
        ),
    }
    write_json(metadata_path, metadata)
    return session_root


def reset_workflow_dir(workflow_dir: Path) -> tuple[Path, Path]:
    if workflow_dir.exists():
        shutil.rmtree(workflow_dir)
    logs_dir = ensure_directory(workflow_dir / "logs")
    build_dir = workflow_dir / "build"
    return logs_dir, build_dir


def configure_build(build_dir: Path, logs_dir: Path, extra_args: list[str]) -> None:
    ensure_directory(build_dir.parent)
    run_command(
        [
            "cmake",
            "-S",
            str(ROOT),
            "-B",
            str(build_dir),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            *extra_args,
        ],
        log_path=logs_dir / "configure.txt",
    )


def build_targets(build_dir: Path, logs_dir: Path, targets: list[str], *, log_name: str = "build.txt") -> None:
    run_command(
        ["cmake", "--build", str(build_dir), "--target", *targets],
        log_path=logs_dir / log_name,
    )


def parse_coverage_export(data: dict[str, object]) -> dict[str, dict[str, object]]:
    aggregated: dict[str, dict[str, object]] = {}
    for dataset in data.get("data", []):
        for file_entry in dataset.get("files", []):
            filename = repo_relative(file_entry.get("filename", ""))
            if not is_production_path(filename):
                continue
            summary = file_entry.get("summary", {})
            lines = summary.get("lines", {})
            functions = summary.get("functions", {})
            record = aggregated.setdefault(
                filename,
                {
                    "path": filename,
                    "module": module_for(filename),
                    "line_count": 0,
                    "line_covered": 0,
                    "function_count": 0,
                    "function_covered": 0,
                },
            )
            record["line_count"] += int(lines.get("count", 0))
            record["line_covered"] += int(lines.get("covered", 0))
            record["function_count"] += int(functions.get("count", 0))
            record["function_covered"] += int(functions.get("covered", 0))

    for record in aggregated.values():
        line_count = int(record["line_count"])
        function_count = int(record["function_count"])
        record["line_percent"] = round((100.0 * int(record["line_covered"]) / line_count), 2) if line_count else 100.0
        record["function_percent"] = (
            round((100.0 * int(record["function_covered"]) / function_count), 2) if function_count else 100.0
        )
    return aggregated


def coverage_totals(rows: list[dict[str, object]]) -> dict[str, float]:
    line_count = sum(int(row["line_count"]) for row in rows)
    line_covered = sum(int(row["line_covered"]) for row in rows)
    function_count = sum(int(row["function_count"]) for row in rows)
    function_covered = sum(int(row["function_covered"]) for row in rows)
    return {
        "line_percent": round((100.0 * line_covered / line_count), 2) if line_count else 100.0,
        "function_percent": round((100.0 * function_covered / function_count), 2) if function_count else 100.0,
        "line_count": line_count,
        "line_covered": line_covered,
        "function_count": function_count,
        "function_covered": function_covered,
    }


def coverage_reason(path: str, unit_row: dict[str, object], combined_row: dict[str, object]) -> str:
    unit_line = float(unit_row.get("line_percent", 0.0))
    combined_line = float(combined_row.get("line_percent", 0.0))
    if path in RUNTIME_CRITICAL_PATHS and combined_line > unit_line:
        return "Runtime-heavy executable flow; unit tests do not compile or exercise this file directly."
    if path.endswith("main.cpp"):
        return "Entry-point logic is primarily exercised through full executable runs."
    if combined_line < 75.0:
        return "Still lightly exercised after automation; add targeted tests for error and state branches."
    if combined_line - unit_line >= 20.0:
        return "Coverage depends heavily on the end-to-end verifier rather than isolated unit tests."
    return "Operationally important file with room for deeper scenario coverage."


def build_coverage_high_risk_rows(
    unit_rows: dict[str, dict[str, object]],
    combined_rows: dict[str, dict[str, object]],
) -> list[dict[str, object]]:
    results: list[dict[str, object]] = []
    for path, combined_row in combined_rows.items():
        unit_row = unit_rows.get(path, {"line_percent": 0.0, "function_percent": 0.0})
        unit_line = float(unit_row.get("line_percent", 0.0))
        combined_line = float(combined_row.get("line_percent", 0.0))
        combined_function = float(combined_row.get("function_percent", 0.0))
        if path not in RUNTIME_CRITICAL_PATHS and combined_line >= 85.0 and combined_function >= 85.0:
            continue
        results.append(
            {
                "path": path,
                "module": combined_row["module"],
                "unit_line_percent": round(unit_line, 2),
                "combined_line_percent": round(combined_line, 2),
                "combined_function_percent": round(combined_function, 2),
                "reason": coverage_reason(path, unit_row, combined_row),
            }
        )
    return sorted(results, key=lambda row: (float(row["combined_line_percent"]), row["path"]))


def export_coverage(
    build_dir: Path,
    profdata_path: Path,
    logs_dir: Path,
    output_prefix: Path,
) -> dict[str, dict[str, object]]:
    unit_tests = find_executable(build_dir, "unit_tests")
    client = find_executable(build_dir, "aircraft_client")
    server = find_executable(build_dir, "ground_server")

    report_command = [
        "xcrun",
        "llvm-cov",
        "report",
        str(unit_tests),
        "-object",
        str(client),
        "-object",
        str(server),
        "-instr-profile",
        str(profdata_path),
        "-ignore-filename-regex",
        r"(^|/)(third_party|tests|runtime|untracked_files|reports/analysis)/",
    ]
    report_log_name = f"{output_prefix.name}_report.txt"
    run_command(report_command, log_path=logs_dir / report_log_name)

    export_result = run_command(
        [
            "xcrun",
            "llvm-cov",
            "export",
            str(unit_tests),
            "-object",
            str(client),
            "-object",
            str(server),
            "-instr-profile",
            str(profdata_path),
            "-format=text",
        ],
        log_path=logs_dir / f"{output_prefix.name}_export.txt",
    )
    json_start = export_result.stdout.find("{")
    if json_start == -1:
        raise RuntimeError(f"llvm-cov export did not produce JSON output.\n{export_result.stdout}")
    export_data = json.loads(export_result.stdout[json_start:])
    rows_map = parse_coverage_export(export_data)
    rows = sorted(rows_map.values(), key=lambda row: row["path"])
    write_json(output_prefix.with_suffix(".json"), rows)
    write_csv(output_prefix.with_suffix(".csv"), PRODUCTION_HEADERS, rows)
    write_text(
        output_prefix.with_suffix(".md"),
        markdown_table(
            ["Path", "Module", "Line %", "Function %", "Lines Covered/Total"],
            [
                (
                    row["path"],
                    row["module"],
                    f'{row["line_percent"]:.2f}',
                    f'{row["function_percent"]:.2f}',
                    f'{row["line_covered"]}/{row["line_count"]}',
                )
                for row in rows
            ],
        ),
    )
    return rows_map


def merge_profdata(profiles: list[Path], output_path: Path, logs_dir: Path, log_name: str) -> None:
    if not profiles:
        raise RuntimeError(f"No .profraw files were generated for {output_path.name}.")
    run_command(
        ["xcrun", "llvm-profdata", "merge", "-sparse", *[str(path) for path in profiles], "-o", str(output_path)],
        log_path=logs_dir / log_name,
    )


def run_coverage(session_root: Path) -> bool:
    workflow_dir = session_root / "coverage"
    logs_dir, build_dir = reset_workflow_dir(workflow_dir)
    profiles_root = ensure_directory(workflow_dir / "profiles")
    unit_profiles_dir = ensure_directory(profiles_root / "unit")
    runtime_profiles_dir = ensure_directory(profiles_root / "runtime")

    configure_build(build_dir, logs_dir, ["-DAVIATION_COMMS_ENABLE_COVERAGE=ON"])
    build_targets(build_dir, logs_dir, ["unit_tests", "aircraft_client", "ground_server"])

    unit_env = os.environ.copy()
    unit_env["LLVM_PROFILE_FILE"] = str(unit_profiles_dir / "unit-%m-%p.profraw")
    unit_result = run_command(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
        env=unit_env,
        log_path=logs_dir / "ctest.txt",
    )

    runtime_env = os.environ.copy()
    runtime_env["CI_PREFER_GUI"] = "0"
    runtime_env["CI_BUILD_DIR"] = str(build_dir)
    runtime_env["LLVM_PROFILE_FILE"] = str(runtime_profiles_dir / "runtime-%m-%p.profraw")
    runtime_result = run_command(
        ["python3", "ci/verify_runtime.py"],
        env=runtime_env,
        log_path=logs_dir / "runtime_verifier.txt",
        timeout_seconds=180,
    )
    copy_runtime_snapshot(workflow_dir / "runtime_snapshot")

    unit_profdata = profiles_root / "unit.profdata"
    combined_profdata = profiles_root / "combined.profdata"
    unit_profiles = sorted(unit_profiles_dir.glob("*.profraw"))
    runtime_profiles = sorted(runtime_profiles_dir.glob("*.profraw"))
    merge_profdata(unit_profiles, unit_profdata, logs_dir, "unit_profdata_merge.txt")
    merge_profdata(unit_profiles + runtime_profiles, combined_profdata, logs_dir, "combined_profdata_merge.txt")

    unit_rows = export_coverage(build_dir, unit_profdata, logs_dir, workflow_dir / "unit_coverage")
    combined_rows = export_coverage(build_dir, combined_profdata, logs_dir, workflow_dir / "combined_coverage")
    unit_totals = coverage_totals(list(unit_rows.values()))
    combined_totals = coverage_totals(list(combined_rows.values()))
    high_risk_rows = build_coverage_high_risk_rows(unit_rows, combined_rows)
    write_csv(
        workflow_dir / "high_risk_coverage_gaps.csv",
        ["path", "module", "unit_line_percent", "combined_line_percent", "combined_function_percent", "reason"],
        high_risk_rows,
    )
    write_text(
        workflow_dir / "high_risk_coverage_gaps.md",
        markdown_table(
            ["Path", "Module", "Unit Line %", "Combined Line %", "Combined Function %", "Reason"],
            [
                (
                    row["path"],
                    row["module"],
                    f'{float(row["unit_line_percent"]):.2f}',
                    f'{float(row["combined_line_percent"]):.2f}',
                    f'{float(row["combined_function_percent"]):.2f}',
                    row["reason"],
                )
                for row in high_risk_rows
            ],
        ),
    )

    summary = {
        "workflow": "coverage",
        "unit_totals": unit_totals,
        "combined_totals": combined_totals,
        "unit_profile_count": len(unit_profiles),
        "runtime_profile_count": len(runtime_profiles),
        "unit_test_command_succeeded": unit_result.returncode == 0,
        "runtime_verifier_succeeded": runtime_result.returncode == 0,
        "runtime_artifact_log": repo_relative(logs_dir / "runtime_verifier.txt"),
        "runtime_snapshot": repo_relative(workflow_dir / "runtime_snapshot"),
        "high_risk_file_count": len(high_risk_rows),
    }
    write_json(workflow_dir / "summary.json", summary)
    write_text(
        workflow_dir / "summary.md",
        "\n".join(
            [
                "# Coverage Summary",
                "",
                f"- Unit-only line coverage: {unit_totals['line_percent']:.2f}%",
                f"- Combined unit + runtime line coverage: {combined_totals['line_percent']:.2f}%",
                f"- Unit-only function coverage: {unit_totals['function_percent']:.2f}%",
                f"- Combined unit + runtime function coverage: {combined_totals['function_percent']:.2f}%",
                f"- High-risk / low-coverage files flagged: {len(high_risk_rows)}",
                "",
                "## High-Risk Coverage Gaps",
                "",
                markdown_table(
                    ["Path", "Unit Line %", "Combined Line %", "Reason"],
                    [
                        (
                            row["path"],
                            f'{float(row["unit_line_percent"]):.2f}',
                            f'{float(row["combined_line_percent"]):.2f}',
                            row["reason"],
                        )
                        for row in high_risk_rows[:10]
                    ],
                ).strip(),
                "",
            ]
        )
        + "\n",
    )
    return True


def ensure_cppcheck(logs_dir: Path, *, skip_install: bool) -> str:
    existing = find_tool("cppcheck")
    if existing:
        return existing
    if skip_install:
        raise RuntimeError("cppcheck is not installed. Re-run without --skip-install to allow Homebrew installation.")
    run_command(["brew", "install", "cppcheck"], log_path=logs_dir / "brew_install_cppcheck.txt")
    installed = find_tool("cppcheck")
    if not installed:
        raise RuntimeError("cppcheck is still unavailable after brew install.")
    return installed


def find_misra_addon(cppcheck_path: str) -> Path | None:
    brew_prefix = run_command(["brew", "--prefix", "cppcheck"], allow_failure=True)
    candidates: list[Path] = []
    if brew_prefix.returncode == 0 and brew_prefix.stdout.strip():
        prefix = Path(brew_prefix.stdout.strip())
        candidates.extend(
            [
                prefix / "share" / "cppcheck" / "addons" / "misra.py",
                prefix / "share" / "cppcheck" / "addons" / "misra.json",
            ]
        )
    cppcheck_root = Path(cppcheck_path).resolve().parents[1]
    candidates.extend(
        [
            cppcheck_root / "share" / "cppcheck" / "addons" / "misra.py",
            cppcheck_root / "share" / "cppcheck" / "addons" / "misra.json",
        ]
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def parse_cppcheck_template_output(output: str) -> list[dict[str, object]]:
    findings: list[dict[str, object]] = []
    pattern = re.compile(r"^(.*?):(\d+):([A-Za-z]+):([^:]+):(.*)$")
    for line in output.splitlines():
        match = pattern.match(line.strip())
        if not match:
            continue
        file_name, line_no, severity, finding_id, message = match.groups()
        if not is_production_path(file_name):
            continue
        findings.append(
            {
                "path": repo_relative(file_name),
                "module": module_for(file_name),
                "line": int(line_no),
                "severity": severity.lower(),
                "id": finding_id.strip(),
                "message": message.strip(),
            }
        )
    return findings


def status_for_finding(severity: str) -> str:
    if severity in {"error", "internalerror", "syntaxerror"}:
        return "must-fix"
    if severity in {"warning", "portability", "performance"}:
        return "explain"
    return "acceptable deviation"


def finalize_findings(findings: list[dict[str, object]]) -> list[dict[str, object]]:
    finalized: list[dict[str, object]] = []
    for finding in findings:
        finalized.append(
            {
                **finding,
                "plain_language_interpretation": finding["message"],
                "status": status_for_finding(str(finding["severity"])),
            }
        )
    return finalized


def summarize_findings(findings: list[dict[str, object]]) -> dict[str, object]:
    severity_counts = Counter(str(finding["severity"]) for finding in findings)
    id_counts = Counter(str(finding["id"]) for finding in findings)
    file_counts = Counter(str(finding["path"]) for finding in findings)
    return {
        "count": len(findings),
        "severity_counts": dict(severity_counts),
        "top_ids": id_counts.most_common(10),
        "top_files": file_counts.most_common(10),
    }


def write_findings_bundle(
    workflow_dir: Path,
    stem: str,
    findings: list[dict[str, object]],
    *,
    heading: str,
) -> None:
    headers = [
        "path",
        "module",
        "line",
        "severity",
        "id",
        "message",
        "plain_language_interpretation",
        "status",
    ]
    sorted_findings = sorted(findings, key=lambda row: (row["path"], int(row["line"]), row["id"]))
    write_json(workflow_dir / f"{stem}.json", sorted_findings)
    write_csv(workflow_dir / f"{stem}.csv", headers, sorted_findings)
    write_text(
        workflow_dir / f"{stem}.md",
        "\n".join(
            [
                f"# {heading}",
                "",
                markdown_table(
                    ["Path", "Line", "Severity", "ID", "Status", "Interpretation"],
                    [
                        (
                            row["path"],
                            row["line"],
                            row["severity"],
                            row["id"],
                            row["status"],
                            row["plain_language_interpretation"],
                        )
                        for row in sorted_findings[:50]
                    ],
                ).strip(),
                "",
            ]
        )
        + "\n",
    )


def run_static_analysis(session_root: Path, *, skip_install: bool) -> bool:
    workflow_dir = session_root / "static_analysis"
    logs_dir, build_dir = reset_workflow_dir(workflow_dir)

    configure_build(build_dir, logs_dir, [])
    cppcheck_path = ensure_cppcheck(logs_dir, skip_install=skip_install)

    base_args = [
        cppcheck_path,
        "--std=c++17",
        "--language=c++",
        "--template={file}:{line}:{severity}:{id}:{message}",
        "--inline-suppr",
        "--quiet",
        "-Iclient",
        "-Icommon",
        "-Iserver",
        "-Ithird_party/imgui",
        "client",
        "common",
        "server",
    ]
    general_result = run_command(
        [*base_args, "--enable=warning,style,performance,portability,information"],
        log_path=logs_dir / "cppcheck_general.txt",
        allow_failure=True,
    )
    general_findings = finalize_findings(parse_cppcheck_template_output(general_result.stdout))
    write_findings_bundle(workflow_dir, "general_findings", general_findings, heading="Cppcheck General Findings")

    misra_findings: list[dict[str, object]] = []
    misra_status = "addon-not-found"
    addon_path = find_misra_addon(cppcheck_path)
    if addon_path is not None:
        misra_result = run_command(
            [*base_args, f"--addon={addon_path}"],
            log_path=logs_dir / "cppcheck_misra.txt",
            allow_failure=True,
        )
        misra_findings = finalize_findings(
            [
                finding
                for finding in parse_cppcheck_template_output(misra_result.stdout)
                if "misra" in str(finding["id"]).lower() or "misra" in str(finding["message"]).lower()
            ]
        )
        misra_status = "available"
        write_findings_bundle(
            workflow_dir,
            "misra_findings",
            misra_findings,
            heading="Cppcheck MISRA-Style Findings",
        )
    else:
        write_text(
            logs_dir / "cppcheck_misra.txt",
            "MISRA addon was not found in the installed cppcheck package.\n",
        )
        write_text(
            workflow_dir / "misra_findings.md",
            "# Cppcheck MISRA-Style Findings\n\n_MISRA addon was not available in the installed cppcheck package._\n",
        )
        write_json(workflow_dir / "misra_findings.json", [])
        write_csv(
            workflow_dir / "misra_findings.csv",
            ["path", "module", "line", "severity", "id", "message", "plain_language_interpretation", "status"],
            [],
        )

    summary = {
        "workflow": "static-analysis",
        "general": summarize_findings(general_findings),
        "misra": summarize_findings(misra_findings),
        "misra_status": misra_status,
        "compile_commands": repo_relative(build_dir / "compile_commands.json"),
    }
    write_json(workflow_dir / "summary.json", summary)
    write_text(
        workflow_dir / "summary.md",
        "\n".join(
            [
                "# Static Analysis Summary",
                "",
                f"- General cppcheck findings: {summary['general']['count']}",
                f"- MISRA-style findings: {summary['misra']['count']}",
                f"- MISRA addon status: {misra_status}",
                "",
                "## Top General Findings",
                "",
                markdown_table(
                    ["ID", "Count"],
                    [(finding_id, count) for finding_id, count in summary["general"]["top_ids"]],
                ).strip(),
                "",
                "## Top MISRA-Style Findings",
                "",
                markdown_table(
                    ["ID", "Count"],
                    [(finding_id, count) for finding_id, count in summary["misra"]["top_ids"]],
                ).strip(),
                "",
            ]
        )
        + "\n",
    )
    return True


def parse_warning_lines(output: str) -> list[dict[str, object]]:
    findings: list[dict[str, object]] = []
    seen: set[tuple[str, int, int, str, str]] = set()
    pattern = re.compile(r"^(.*?):(\d+):(\d+): warning: (.*?)(?: \[(-W[^\]]+)\])?$")
    for line in output.splitlines():
        match = pattern.match(line.strip())
        if not match:
            continue
        file_name, line_no, column_no, message, category = match.groups()
        if not is_production_path(file_name):
            continue
        normalized = (
            repo_relative(file_name),
            int(line_no),
            int(column_no),
            category or "uncategorized",
            message.strip(),
        )
        if normalized in seen:
            continue
        seen.add(normalized)
        findings.append(
            {
                "path": normalized[0],
                "module": module_for(file_name),
                "line": normalized[1],
                "column": normalized[2],
                "category": normalized[3],
                "message": normalized[4],
            }
        )
    return findings


def run_warnings(session_root: Path) -> bool:
    workflow_dir = session_root / "warnings"
    logs_dir, build_dir = reset_workflow_dir(workflow_dir)

    configure_build(build_dir, logs_dir, ["-DAVIATION_COMMS_ENABLE_STRICT_WARNINGS=ON"])
    build_targets(build_dir, logs_dir, ["aircraft_client", "ground_server"])

    build_output = (logs_dir / "build.txt").read_text(encoding="utf-8", errors="replace")
    warnings = parse_warning_lines(build_output)
    category_counts = Counter(str(finding["category"]) for finding in warnings)
    file_counts = Counter(str(finding["path"]) for finding in warnings)

    write_json(workflow_dir / "warnings.json", warnings)
    write_csv(
        workflow_dir / "warnings.csv",
        ["path", "module", "line", "column", "category", "message"],
        warnings,
    )
    write_text(
        workflow_dir / "warnings.md",
        "\n".join(
            [
                "# Strict Warning Summary",
                "",
                f"- Build clean: {'yes' if not warnings else 'no'}",
                f"- Warning count: {len(warnings)}",
                "",
                "## Warning Categories",
                "",
                markdown_table(["Category", "Count"], category_counts.items()).strip(),
                "",
                "## Representative Warnings",
                "",
                markdown_table(
                    ["Path", "Line", "Category", "Message"],
                    [(row["path"], row["line"], row["category"], row["message"]) for row in warnings[:25]],
                ).strip(),
                "",
            ]
        )
        + "\n",
    )

    summary = {
        "workflow": "warnings",
        "warning_count": len(warnings),
        "category_counts": dict(category_counts),
        "top_files": file_counts.most_common(10),
        "warning_clean": len(warnings) == 0,
    }
    write_json(workflow_dir / "summary.json", summary)
    return True


def parse_sanitizer_findings(output: str) -> list[str]:
    findings: list[str] = []
    patterns = [
        re.compile(r".*AddressSanitizer.*"),
        re.compile(r".*UndefinedBehaviorSanitizer.*"),
        re.compile(r".*runtime error:.*"),
        re.compile(r".*SUMMARY: .*Sanitizer.*"),
    ]
    for line in output.splitlines():
        stripped = line.strip()
        for pattern in patterns:
            if pattern.match(stripped):
                findings.append(stripped)
                break
    return findings


def run_sanitizers(session_root: Path) -> bool:
    workflow_dir = session_root / "sanitizers"
    logs_dir, build_dir = reset_workflow_dir(workflow_dir)

    configure_build(build_dir, logs_dir, ["-DAVIATION_COMMS_ENABLE_SANITIZERS=ON"])
    build_targets(build_dir, logs_dir, ["unit_tests", "aircraft_client", "ground_server"])

    sanitizer_env = os.environ.copy()
    sanitizer_env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
    sanitizer_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    ctest_result = run_command(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
        env=sanitizer_env,
        log_path=logs_dir / "ctest.txt",
        allow_failure=True,
    )
    runtime_env = sanitizer_env.copy()
    runtime_env["CI_PREFER_GUI"] = "0"
    runtime_env["CI_BUILD_DIR"] = str(build_dir)
    runtime_result = run_command(
        ["python3", "ci/verify_runtime.py"],
        env=runtime_env,
        log_path=logs_dir / "runtime_verifier.txt",
        allow_failure=True,
        timeout_seconds=180,
    )
    copy_runtime_snapshot(workflow_dir / "runtime_snapshot")

    findings = parse_sanitizer_findings(ctest_result.stdout) + parse_sanitizer_findings(runtime_result.stdout)
    unique_findings = list(dict.fromkeys(findings))
    summary = {
        "workflow": "sanitizers",
        "ctest_passed": ctest_result.returncode == 0,
        "runtime_verifier_passed": runtime_result.returncode == 0,
        "sanitizer_finding_count": len(unique_findings),
        "sanitizer_clean": not unique_findings and ctest_result.returncode == 0 and runtime_result.returncode == 0,
        "runtime_snapshot": repo_relative(workflow_dir / "runtime_snapshot"),
    }
    write_json(workflow_dir / "summary.json", summary)
    write_text(
        workflow_dir / "findings.md",
        "\n".join(
            [
                "# Sanitizer Summary",
                "",
                f"- Unit tests passed under sanitizers: {'yes' if summary['ctest_passed'] else 'no'}",
                f"- Runtime verifier passed under sanitizers: {'yes' if summary['runtime_verifier_passed'] else 'no'}",
                f"- Sanitizer diagnostics found: {len(unique_findings)}",
                "",
                "## Diagnostics",
                "",
                markdown_table(["Diagnostic"], [(finding,) for finding in unique_findings[:25]]).strip(),
                "",
            ]
        )
        + "\n",
    )
    return True


def load_summary(path: Path) -> dict[str, object] | None:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def generate_traceability_matrix(session_root: Path, summaries: dict[str, dict[str, object] | None]) -> str:
    rows = [
        (
            "Unit/regression behavior",
            "Automated",
            "coverage/logs/ctest.txt",
            "58 Google Test cases contribute unit-only and combined coverage evidence.",
        ),
        (
            "Handshake, telemetry, large-file flow",
            "Automated",
            "coverage/logs/runtime_verifier.txt",
            "Nominal verifier run captures packet logs and BMP transfer artifacts.",
        ),
        (
            "Fault logging and recovery",
            "Automated",
            "sanitizers/logs/runtime_verifier.txt or coverage/logs/runtime_verifier.txt",
            "Fault scenario verifies black-box logging and reconnect behavior.",
        ),
        (
            "Static-analysis / MISRA-style evidence",
            "Automated",
            "static_analysis/general_findings.md and static_analysis/misra_findings.md",
            "Findings are scoped to client/, common/, and server/ only.",
        ),
        (
            "Usability evidence",
            "Manual",
            "untracked_files/Project_Test_Log_FILLED.xlsx",
            "Usability tests remain manual/documented and should be discussed as such in the report.",
        ),
    ]
    return (
        "# Traceability Matrix\n\n"
        + markdown_table(["Requirement Area", "Automation", "Evidence Source", "Notes"], rows)
    )


def generate_report_outline(session_root: Path, summaries: dict[str, dict[str, object] | None]) -> str:
    coverage = summaries.get("coverage") or {}
    static_analysis = summaries.get("static-analysis") or {}
    warnings = summaries.get("warnings") or {}
    sanitizers = summaries.get("sanitizers") or {}
    return "\n".join(
        [
            "# Analysis Report Outline",
            "",
            "## 1. Objective And Scope",
            "- Evaluate test depth, runtime behavior, code quality, and standards-oriented practices for the course project.",
            "- Scope the evidence to project-owned code in `client/`, `common/`, and `server/`.",
            "- Exclude `third_party/`, `tests/`, build outputs, runtime outputs, and workbook files from compliance metrics.",
            "",
            "## 2. Test Environment And Method",
            "- Document the compiler, CMake version, Python version, and any static-analysis tools used.",
            "- State that reproducibility requires network access because CMake fetches dependencies during configure.",
            "",
            "## 3. Coverage Findings",
            f"- Unit-only line coverage: {coverage.get('unit_totals', {}).get('line_percent', 'n/a')}%",
            f"- Combined unit + runtime line coverage: {coverage.get('combined_totals', {}).get('line_percent', 'n/a')}%",
            "- Use `coverage/unit_coverage.md`, `coverage/combined_coverage.md`, and `coverage/high_risk_coverage_gaps.md` as source tables.",
            "",
            "## 4. MISRA-Style And Static-Analysis Findings",
            f"- General cppcheck findings: {static_analysis.get('general', {}).get('count', 'n/a')}",
            f"- MISRA-style findings: {static_analysis.get('misra', {}).get('count', 'n/a')}",
            "- Draw examples from `static_analysis/general_findings.md` and `static_analysis/misra_findings.md`.",
            "",
            "## 5. Reliability Findings",
            f"- Strict-warning build clean: {warnings.get('warning_clean', 'n/a')}",
            f"- Sanitizer clean: {sanitizers.get('sanitizer_clean', 'n/a')}",
            "- Cite the runtime verifier logs and runtime snapshots for handshake gating, logging, black-box behavior, and recovery behavior.",
            "",
            "## 6. Gaps And Limitations",
            "- Call out manual-only usability evidence from the workbook.",
            "- Highlight any files that remain lightly covered after combined automation.",
            "- State explicitly that the project does not claim formal Canadian aviation or MISRA certification.",
            "",
            "## 7. Conclusion",
            "- End with a balanced summary of what is strongly supported by automated evidence versus what remains manual or course-scoped.",
            "",
        ]
    )


def generate_analysis_overview(session_root: Path, summaries: dict[str, dict[str, object] | None]) -> str:
    coverage = summaries.get("coverage") or {}
    static_analysis = summaries.get("static-analysis") or {}
    warnings = summaries.get("warnings") or {}
    sanitizers = summaries.get("sanitizers") or {}
    return "\n".join(
        [
            "# Analysis Overview",
            "",
            "- This artifact set supports a documentation-heavy course report; it is not a formal certification package.",
            f"- Coverage combined line percentage: {coverage.get('combined_totals', {}).get('line_percent', 'n/a')}%",
            f"- General static-analysis findings: {static_analysis.get('general', {}).get('count', 'n/a')}",
            f"- MISRA-style findings: {static_analysis.get('misra', {}).get('count', 'n/a')}",
            f"- Strict-warning build clean: {warnings.get('warning_clean', 'n/a')}",
            f"- Sanitizer clean: {sanitizers.get('sanitizer_clean', 'n/a')}",
            "",
        ]
    )


def run_summary(session_root: Path) -> bool:
    report_dir = ensure_directory(session_root / "report")
    summaries = {
        "coverage": load_summary(session_root / "coverage" / "summary.json"),
        "static-analysis": load_summary(session_root / "static_analysis" / "summary.json"),
        "warnings": load_summary(session_root / "warnings" / "summary.json"),
        "sanitizers": load_summary(session_root / "sanitizers" / "summary.json"),
    }
    write_text(report_dir / "traceability_matrix.md", generate_traceability_matrix(session_root, summaries))
    write_text(report_dir / "report_outline.md", generate_report_outline(session_root, summaries))
    write_text(report_dir / "analysis_overview.md", generate_analysis_overview(session_root, summaries))
    write_json(report_dir / "analysis_overview.json", summaries)
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate evidence-driven testing and compliance artifacts.")
    parser.add_argument(
        "command",
        choices=["coverage", "static-analysis", "warnings", "sanitizers", "summary", "all"],
        help="Workflow to execute.",
    )
    parser.add_argument("--stamp", help="Reuse an existing reports/analysis/<stamp> directory.")
    parser.add_argument(
        "--skip-install",
        action="store_true",
        help="Do not auto-install missing tools such as cppcheck.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    session_root = prepare_session_root(args.stamp or utc_timestamp()) if args.command != "summary" else (
        session_root_for(args.stamp) if args.stamp else latest_session_root()
    )

    command_succeeded = True
    if args.command == "coverage":
        command_succeeded = run_coverage(session_root)
    elif args.command == "static-analysis":
        command_succeeded = run_static_analysis(session_root, skip_install=args.skip_install)
    elif args.command == "warnings":
        command_succeeded = run_warnings(session_root)
    elif args.command == "sanitizers":
        command_succeeded = run_sanitizers(session_root)
    elif args.command == "summary":
        command_succeeded = run_summary(session_root)
    elif args.command == "all":
        coverage_ok = run_coverage(session_root)
        static_ok = run_static_analysis(session_root, skip_install=args.skip_install)
        warnings_ok = run_warnings(session_root)
        sanitizers_ok = run_sanitizers(session_root)
        summary_ok = run_summary(session_root)
        command_succeeded = coverage_ok and static_ok and warnings_ok and sanitizers_ok and summary_ok

    print(f"Analysis artifacts written to: {session_root}")
    return 0 if command_succeeded else 1


if __name__ == "__main__":
    raise SystemExit(main())
