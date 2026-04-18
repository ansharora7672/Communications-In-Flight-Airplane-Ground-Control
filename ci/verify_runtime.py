#!/usr/bin/env python3

from __future__ import annotations

import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = Path(os.environ.get("CI_BUILD_DIR", str(ROOT / "build"))).expanduser()
RUNTIME_DIR = ROOT / "runtime"
HOST = "127.0.0.1"
STARTUP_TIMEOUT_SECONDS = 20
STARTUP_GRACE_SECONDS = 1.5
CLIENT_TIMEOUT_SECONDS = 40
SERVER_SHUTDOWN_TIMEOUT_SECONDS = 10
BUILD_CONFIG = os.environ.get("CI_BUILD_CONFIG", "").strip()
PREFER_GUI = os.environ.get("CI_PREFER_GUI", "1").strip() != "0"
USE_XVFB = os.environ.get("CI_USE_XVFB", "").strip() == "1"


class OutputCollector:
    def __init__(self, process: subprocess.Popen[str]) -> None:
        if process.stdout is None:
            raise RuntimeError("Process does not expose stdout for capture.")
        self._process = process
        self._stdout = process.stdout
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._reader.start()

    def _read_stdout(self) -> None:
        for line in self._stdout:
            with self._lock:
                self._lines.append(line)

    def text(self) -> str:
        with self._lock:
            return "".join(self._lines)

    def wait_for_markers(self, markers: tuple[str, ...], timeout_seconds: float, label: str) -> str:
        deadline = time.time() + timeout_seconds
        while time.time() < deadline:
            text = self.text()
            if any(marker in text for marker in markers):
                return text
            if self._process.poll() is not None:
                break
            time.sleep(0.1)

        text = self.text()
        if self._process.poll() is not None:
            raise AssertionError(
                f"{label} did not appear before the client exited.\nClient output:\n{text}"
            )
        raise TimeoutError(f"Timed out waiting for {label}.\nClient output:\n{text}")

    def wait_for_exit(self, timeout_seconds: float) -> str:
        self._process.wait(timeout=timeout_seconds)
        self._reader.join(timeout=1)
        return self.text()


def close_process_stdin(process: subprocess.Popen[str]) -> None:
    if process.stdin is not None and not process.stdin.closed:
        process.stdin.close()


def candidate_executable_paths(name: str) -> list[Path]:
    suffix = ".exe" if os.name == "nt" else ""
    candidates: list[Path] = []

    if BUILD_CONFIG:
        candidates.append(BUILD_DIR / BUILD_CONFIG / f"{name}{suffix}")
        candidates.append(BUILD_DIR / "bin" / BUILD_CONFIG / f"{name}{suffix}")

    candidates.extend(
        [
            BUILD_DIR / f"{name}{suffix}",
            BUILD_DIR / "bin" / f"{name}{suffix}",
            BUILD_DIR / "Release" / f"{name}{suffix}",
            BUILD_DIR / "bin" / "Release" / f"{name}{suffix}",
            BUILD_DIR / "Debug" / f"{name}{suffix}",
            BUILD_DIR / "bin" / "Debug" / f"{name}{suffix}",
            BUILD_DIR / "RelWithDebInfo" / f"{name}{suffix}",
            BUILD_DIR / "bin" / "RelWithDebInfo" / f"{name}{suffix}",
            BUILD_DIR / "MinSizeRel" / f"{name}{suffix}",
            BUILD_DIR / "bin" / "MinSizeRel" / f"{name}{suffix}",
        ]
    )
    return candidates


def find_executable(name: str) -> Path:
    for candidate in candidate_executable_paths(name):
        if candidate.exists():
            return candidate
    searched = "\n".join(f"- {path}" for path in candidate_executable_paths(name))
    raise FileNotFoundError(f"Unable to locate {name}. Searched:\n{searched}")


def choose_port() -> int:
    configured_port = os.environ.get("CI_TEST_PORT", "").strip()
    if configured_port:
        return int(configured_port)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((HOST, 0))
        return int(probe.getsockname()[1])


def wait_for_server(server_process: subprocess.Popen[str]) -> None:
    deadline = time.time() + STARTUP_TIMEOUT_SECONDS
    grace_deadline = time.time() + STARTUP_GRACE_SECONDS
    while time.time() < deadline:
        if server_process.poll() is not None:
            output, _ = server_process.communicate(timeout=1)
            raise RuntimeError(f"Server exited before becoming reachable.\n{output}")
        time.sleep(0.2)
        if time.time() >= grace_deadline:
            return

    raise TimeoutError("Timed out waiting for the headless server to stay alive during startup.")


def latest_file(directory: Path, pattern: str) -> Path:
    matches = sorted(directory.glob(pattern))
    if not matches:
        raise FileNotFoundError(f"No files matching {pattern!r} found in {directory}")
    return matches[-1]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require_nonempty_file(path: Path, label: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{label} does not exist: {path}")
    if path.stat().st_size == 0:
        raise AssertionError(f"{label} is empty: {path}")


def send_line(process: subprocess.Popen[str], line: str) -> None:
    if process.stdin is None:
        raise RuntimeError("Client process does not expose stdin for scripted input.")
    process.stdin.write(line)
    process.stdin.flush()


def wait_for_client_connected(output: OutputCollector, label: str) -> str:
    return output.wait_for_markers(
        ("State: CONNECTED", "State: TELEMETRY", "[W] Request Weather Map"),
        timeout_seconds=15,
        label=label,
    )


def wait_for_telemetry_packets(output: OutputCollector, minimum_count: int, label: str) -> str:
    deadline = time.time() + 15
    while time.time() < deadline:
        text = output.text()
        if text.count("[TX] SEQ=") >= minimum_count:
            return text
        if output._process.poll() is not None:
            break
        time.sleep(0.1)

    text = output.text()
    if output._process.poll() is not None:
        raise AssertionError(
            f"{label} did not appear before the client exited.\nClient output:\n{text}"
        )
    raise TimeoutError(f"Timed out waiting for {label}.\nClient output:\n{text}")


def wait_for_weather_map_saved(output: OutputCollector) -> str:
    return output.wait_for_markers(
        ("Weather map saved",),
        timeout_seconds=30,
        label="weather-map transfer completion",
    )


def wait_for_client_disconnected(output: OutputCollector, label: str) -> str:
    return output.wait_for_markers(
        ("State: DISCONNECTED", "Disconnected from ground control", "Server disconnected the session"),
        timeout_seconds=15,
        label=label,
    )


def extract_paths(pattern: str, text: str, label: str) -> list[Path]:
    matches = re.findall(pattern, text)
    if not matches:
        raise AssertionError(f"Unable to locate {label} in verification output.")
    return [ROOT / match for match in matches]


def telemetry_sequences(server_log_text: str, aircraft_id: str) -> list[int]:
    pattern = re.compile(
        rf"\[RX\] \[TELEMETRY\] Aircraft={re.escape(aircraft_id)} Seq=(\d+) PayloadSize=20"
    )
    return [int(match) for match in pattern.findall(server_log_text)]


def parse_server_tx_packets(server_log_text: str, aircraft_id: str) -> list[tuple[int, str]]:
    pattern = re.compile(
        rf"\[.*?\] \[TX\] \[(.*?)\] Aircraft={re.escape(aircraft_id)} Seq=(\d+) PayloadSize=(\d+)"
    )
    return [(index, packet_type) for index, (packet_type, _, _) in enumerate(pattern.findall(server_log_text))]


def verify_nominal_artifacts(aircraft_id: str, client_output: str) -> dict[str, Path]:
    aircraft_log_path = latest_file(RUNTIME_DIR / "logs" / "aircraft_comms", "*.log")
    server_log_path = latest_file(RUNTIME_DIR / "logs" / "groundctrl_comms", "*.log")
    blackbox_log_path = latest_file(RUNTIME_DIR / "logs" / "blackbox", "*.log")

    require_nonempty_file(aircraft_log_path, "Aircraft communication log")
    require_nonempty_file(server_log_path, "Ground server communication log")
    if not blackbox_log_path.exists():
        raise FileNotFoundError(f"Black-box log is missing: {blackbox_log_path}")
    if blackbox_log_path.stat().st_size != 0:
        blackbox_log = read_text(blackbox_log_path).strip()
        raise AssertionError(
            "Nominal scenario black-box log should be empty: "
            f"{blackbox_log_path}\nBlack-box contents:\n{blackbox_log}"
        )

    aircraft_log = read_text(aircraft_log_path)
    server_log = read_text(server_log_path)

    if "[FAULT]" in aircraft_log or "[FAULT]" in server_log:
        raise AssertionError("Nominal scenario should not contain fault entries in communication logs.")

    if "HANDSHAKE_ACK" not in aircraft_log:
        raise AssertionError("Aircraft communication log does not contain a handshake acknowledgement.")
    if "HANDSHAKE_REQUEST" not in server_log or "HANDSHAKE_ACK" not in server_log:
        raise AssertionError("Server communication log does not contain the expected handshake flow.")
    if "[TX] [LARGE_FILE]" not in server_log or "[RX] [LARGE_FILE]" not in aircraft_log:
        raise AssertionError("Nominal scenario does not contain the expected large-file transfer logs.")
    if "[RX] [DISCONNECT]" not in server_log and "[TX] [DISCONNECT]" not in aircraft_log:
        raise AssertionError("Nominal scenario does not contain the expected disconnect logs.")

    sequences = telemetry_sequences(server_log, aircraft_id)
    if len(sequences) < 3:
        raise AssertionError("Nominal scenario should record at least three telemetry packets.")
    if sequences != sorted(sequences):
        raise AssertionError(f"Telemetry packets were not received in-order over TCP: {sequences}")

    tx_packets = parse_server_tx_packets(server_log, aircraft_id)
    ack_index = next((index for index, packet_type in tx_packets if packet_type == "HANDSHAKE_ACK"), None)
    if ack_index is None:
        raise AssertionError("Server log is missing the handshake acknowledgement packet.")
    for index, packet_type in tx_packets:
        if packet_type in {"LARGE_FILE", "DISCONNECT"} and index <= ack_index:
            raise AssertionError(
                f"Server command packet {packet_type} appeared before handshake verification was complete."
            )

    generated_paths = extract_paths(
        r"Generated weather map: (runtime/bitmaps/generated/[^\s]+\.bmp)",
        server_log,
        "generated weather-map path",
    )
    received_paths = extract_paths(
        r"Weather map saved: (runtime/bitmaps/received/[^\s]+\.bmp)",
        aircraft_log,
        "received weather-map path",
    )
    generated_path = generated_paths[-1]
    received_path = received_paths[-1]
    require_nonempty_file(generated_path, "Generated weather-map BMP")
    require_nonempty_file(received_path, "Received weather-map BMP")
    if generated_path.stat().st_size != received_path.stat().st_size:
        raise AssertionError(
            f"Generated and received weather-map sizes differ: {generated_path.stat().st_size} vs {received_path.stat().st_size}"
        )
    if generated_path.stat().st_size < 1024 * 1024:
        raise AssertionError("Weather-map transfer is smaller than the 1 MB requirement.")

    if all(
        marker not in client_output
        for marker in ("Handshake verified", "State: CONNECTED", "State: TELEMETRY", "Weather map saved")
    ):
        raise AssertionError(
            "Client output does not show a verified connected session with large-file evidence.\n"
            f"Client output:\n{client_output}"
        )

    return {
        "aircraft_log": aircraft_log_path,
        "server_log": server_log_path,
        "blackbox_log": blackbox_log_path,
        "generated_bmp": generated_path,
        "received_bmp": received_path,
    }


def verify_fault_artifacts(aircraft_id: str, recovery_aircraft_id: str) -> dict[str, Path]:
    server_log_path = latest_file(RUNTIME_DIR / "logs" / "groundctrl_comms", "*.log")
    blackbox_log_path = latest_file(RUNTIME_DIR / "logs" / "blackbox", "*.log")

    require_nonempty_file(server_log_path, "Fault scenario ground server communication log")
    require_nonempty_file(blackbox_log_path, "Fault scenario black-box log")

    server_log = read_text(server_log_path)
    blackbox_log = read_text(blackbox_log_path)

    sequences = telemetry_sequences(server_log, aircraft_id)
    if len(sequences) < 3:
        raise AssertionError("Fault scenario should record telemetry before the forced TCP loss.")
    if "[FAULT]" not in server_log:
        raise AssertionError("Fault scenario server log is missing a fault entry.")
    if "[FAULT]" not in blackbox_log:
        raise AssertionError("Fault scenario black-box log is missing a fault entry.")
    if "Cause=ReceiveFailure" not in server_log or "Cause=ReceiveFailure" not in blackbox_log:
        raise AssertionError("Fault scenario did not record the expected receive failure cause.")
    if "State=STATE_TELEMETRY" not in server_log or "State=STATE_TELEMETRY" not in blackbox_log:
        raise AssertionError("Fault scenario did not record the expected active telemetry state.")
    if f"[RX] [HANDSHAKE_REQUEST] Aircraft={recovery_aircraft_id}" not in server_log:
        raise AssertionError("Fault scenario did not record a post-fault recovery handshake request.")
    if f"[TX] [HANDSHAKE_ACK] Aircraft={recovery_aircraft_id}" not in server_log:
        raise AssertionError("Fault scenario did not record a post-fault recovery handshake acknowledgement.")

    return {
        "server_log": server_log_path,
        "blackbox_log": blackbox_log_path,
    }


def server_command(server_executable: Path, port: int, mode: str) -> list[str]:
    command: list[str] = []
    if mode == "gui" and USE_XVFB:
        command.extend(["xvfb-run", "-a"])

    command.append(str(server_executable))
    if mode == "headless":
        command.append("--headless")
    command.append(str(port))
    return command


def terminate_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is None:
        if os.name == "nt":
            process.terminate()
        else:
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.communicate(timeout=SERVER_SHUTDOWN_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                process.kill()
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.communicate(timeout=SERVER_SHUTDOWN_TIMEOUT_SECONDS)


def kill_process_immediately(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        process.kill()
    else:
        os.killpg(process.pid, signal.SIGKILL)
    process.communicate(timeout=SERVER_SHUTDOWN_TIMEOUT_SECONDS)


def run_nominal_scenario(server_executable: Path, client_executable: Path, mode: str) -> dict[str, Path]:
    port = choose_port()

    server_process = subprocess.Popen(
        server_command(server_executable, port, mode),
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        start_new_session=os.name != "nt",
        text=True,
    )

    try:
        wait_for_server(server_process)

        client_process = subprocess.Popen(
            [str(client_executable), HOST, str(port), "AC-101"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            stdin=subprocess.PIPE,
            start_new_session=os.name != "nt",
            text=True,
        )
        client_output = OutputCollector(client_process)

        send_line(client_process, "1\n")
        wait_for_client_connected(client_output, "client connection")
        wait_for_telemetry_packets(client_output, minimum_count=3, label="three telemetry packets")
        send_line(client_process, "W\n")
        wait_for_weather_map_saved(client_output)
        send_line(client_process, "D\n")
        wait_for_client_disconnected(client_output, "client disconnect")
        send_line(client_process, "Q\n")
        close_process_stdin(client_process)
        final_output = client_output.wait_for_exit(timeout_seconds=CLIENT_TIMEOUT_SECONDS)

        if client_process.returncode != 0:
            raise RuntimeError(
                f"Client exited with code {client_process.returncode}.\n"
                f"Client output:\n{final_output}"
            )

        return verify_nominal_artifacts("AC-101", final_output)
    finally:
        terminate_process(server_process)


def run_fault_scenario(server_executable: Path, client_executable: Path, mode: str) -> dict[str, Path]:
    port = choose_port()

    server_process = subprocess.Popen(
        server_command(server_executable, port, mode),
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        start_new_session=os.name != "nt",
        text=True,
    )

    try:
        wait_for_server(server_process)

        client_process = subprocess.Popen(
            [str(client_executable), HOST, str(port), "AC-102"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            stdin=subprocess.PIPE,
            start_new_session=os.name != "nt",
            text=True,
        )

        send_line(client_process, "1\n")
        time.sleep(5.0)
        kill_process_immediately(client_process)
        time.sleep(2.5)

        recovery_client = subprocess.Popen(
            [str(client_executable), HOST, str(port), "AC-103"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            stdin=subprocess.PIPE,
            start_new_session=os.name != "nt",
            text=True,
        )
        recovery_output = OutputCollector(recovery_client)
        send_line(recovery_client, "1\n")
        wait_for_client_connected(recovery_output, "recovery client reconnection")
        send_line(recovery_client, "D\n")
        wait_for_client_disconnected(recovery_output, "recovery client disconnect")
        send_line(recovery_client, "Q\n")
        close_process_stdin(recovery_client)
        final_recovery_output = recovery_output.wait_for_exit(timeout_seconds=CLIENT_TIMEOUT_SECONDS)
        if recovery_client.returncode != 0:
            raise RuntimeError(
                f"Recovery client exited with code {recovery_client.returncode}.\n"
                f"Client output:\n{final_recovery_output}"
            )
        if all(
            marker not in final_recovery_output
            for marker in ("State: CONNECTED", "State: TELEMETRY", "[W] Request Weather Map")
        ):
            raise AssertionError(
                "Recovery client did not reconnect successfully after the fault.\n"
                f"Client output:\n{final_recovery_output}"
            )

        return verify_fault_artifacts("AC-102", "AC-103")
    finally:
        terminate_process(server_process)


def run_verification_mode(server_executable: Path, client_executable: Path, mode: str) -> None:
    shutil.rmtree(RUNTIME_DIR, ignore_errors=True)
    nominal_artifacts = run_nominal_scenario(server_executable, client_executable, mode)
    fault_artifacts = run_fault_scenario(server_executable, client_executable, mode)

    print("Nominal artifacts:")
    for key, path in nominal_artifacts.items():
        print(f"  {key}: {path}")
    print("Fault artifacts:")
    for key, path in fault_artifacts.items():
        print(f"  {key}: {path}")


def main() -> int:
    server_executable = find_executable("ground_server")
    client_executable = find_executable("aircraft_client")
    if PREFER_GUI and os.environ.get("GITHUB_ACTIONS") != "true":
        modes = ["gui", "headless"]
    else:
        modes = ["headless"]
    failures: list[str] = []

    for mode in modes:
        try:
            run_verification_mode(server_executable, client_executable, mode)
            print(f"Verification passed using {mode} mode.")
            print(f"Server executable: {server_executable}")
            print(f"Client executable: {client_executable}")
            return 0
        except Exception as exc:
            failures.append(f"{mode}: {exc}")

    raise RuntimeError("All verification modes failed.\n" + "\n\n".join(failures))


if __name__ == "__main__":
    sys.exit(main())
