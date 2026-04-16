# Aviation Ground Control Communication System

CSCN74000 Group 4 sprint implementation in C++17 using a CLI aircraft client and a Dear ImGui ground-control server.

## Build

```bash
cmake -B build
cmake --build build
```

## Test

```bash
cmake -B build
cmake --build build --target unit_tests
ctest --test-dir build --output-on-failure
```

The automated Google Test suite currently contains 58 unit tests covering client/server utilities, option parsing, session state, packet handling, payload ownership, weather-map helpers, state-machine transitions, and verified-session command gating.

For a headless end-to-end verification run that generates fresh runtime evidence, use:

```bash
CI_PREFER_GUI=0 python3 ci/verify_runtime.py
```

This verifier exercises two scenarios:

- a nominal session with handshake, telemetry, weather-map transfer, and clean disconnect
- a fault session with abrupt client loss during telemetry, which must produce a server fault log and black-box entry

## Run

```bash
./build/ground_server
./build/aircraft_client
```

Optional server port:

```bash
./build/ground_server 5050
```

Optional client arguments:

```bash
./build/aircraft_client 127.0.0.1 5000 AC-001
```

If you omit the aircraft ID, the client now auto-assigns one and will retry with a different ID if the server rejects it as already in use:

```bash
./build/aircraft_client 127.0.0.1 5000
```

To connect to a custom server port, pass the same port to both programs:

```bash
./build/ground_server 5050
./build/aircraft_client 127.0.0.1 5050 AC-001
```

Headless mode is also available for environments that cannot open a GLFW window:

```bash
./build/ground_server --headless 5050
./build/aircraft_client 127.0.0.1 5050 AC-001
```

To simulate multiple aircraft, start one server and then launch multiple clients with distinct aircraft IDs:

```bash
./build/ground_server 5050
./build/aircraft_client 127.0.0.1 5050 AC-001
./build/aircraft_client 127.0.0.1 5050 AC-002
```

Each client must use a unique aircraft ID in the format `AC-001`. The server dashboard tracks aircraft independently, and the weather-map and disconnect controls apply to the currently selected aircraft only. The GUI also includes `Previous Flight` and `Next Flight` buttons so you can cycle through connected aircraft without clicking the list manually.

## Runtime Output Layout

- `runtime/logs/aircraft_comms/`: aircraft client communication logs.
- `runtime/logs/groundctrl_comms/`: ground server communication logs.
- `runtime/logs/blackbox/`: server fault-only black box logs.
- `runtime/bitmaps/generated/`: timestamped server-generated weather map BMPs, one file per send.
- `runtime/bitmaps/received/`: timestamped client-downloaded weather map BMPs, one file per receive.
- `runtime/ui/`: Dear ImGui runtime window-layout state such as `imgui.ini`.

## Repository Layout

- `common/`: shared packet definitions, socket helpers, and timestamped logging.
- `client/`: modular CLI aircraft client split into option parsing, flight simulation, network/session handling, and console app orchestration.
- `server/`: modular ground server split into option parsing, per-client session state, socket lifecycle, weather-map generation, state-machine handling, and Dear ImGui dashboard rendering.
- `tests/`: modular Google Test files grouped by client helpers/options, server helpers/options/sessions/weather maps, packet/checksum/payload behavior, telemetry payloads, and state-machine rules.
- `third_party/imgui/`: bundled Dear ImGui sources and OpenGL/GLFW backends.

Logs are written under `runtime/logs/` with UTC timestamped filenames.

Each weather-map send writes a new timestamped BMP in `runtime/bitmaps/generated/`, and each receive writes a new timestamped BMP in `runtime/bitmaps/received/`. The filenames include the aircraft ID and transfer sequence so you can trace which generated file matches which received file.

## Testing And Evidence

The submission evidence is intended to be reproducible from the repository:

- `ctest --test-dir build --output-on-failure` covers the fast unit/regression suite.
- `python3 ci/verify_runtime.py` validates the runtime communication flow and produces fresh log/BMP artifacts.
- `runtime/logs/aircraft_comms/` and `runtime/logs/groundctrl_comms/` provide packet-level TX/RX traces with UTC timestamps, packet type, aircraft ID, sequence number, and payload size.
- `runtime/logs/blackbox/` captures server-side fault entries with cause, state, and sequence number when an active TCP session fails unexpectedly.
- `runtime/bitmaps/generated/` and `runtime/bitmaps/received/` provide matching evidence for the 1 MB+ weather-map transfer requirement.

The runtime verifier checks that telemetry packets arrive in-order on the server log, that server-originated command packets appear only after handshake verification, that the nominal run leaves the black-box log empty, and that the forced-fault run records a matching receive-failure entry in both server and black-box logs.

## Traceability And Defect Handling

The filled project test log is the primary traceability artifact for manual, system, and integration testing. Failed tests are documented there using local defect-and-retest notes backed by concrete runtime evidence such as:

- exact command/setup used
- aircraft ID and port under test
- communication-log and black-box filenames
- generated and received bitmap filenames when large-file transfer is involved
- retest notes showing the post-fix verification evidence

The workbook does not use fabricated GitHub issue numbers. When no real issue exists, the defect column is left blank and the note field carries the local defect trace and retest evidence.

## Compliance Framing

This course project does not claim certified avionics compliance. The testing and logging workflow is instead framed around official Canadian guidance on record keeping, corrective action, and evidence retention:

- CAR 605.93 technical records and electronic-record corrections: <https://laws-lois.justice.gc.ca/eng/regulations/SOR-96-433/section-605.93.html>
- Transport Canada Advisory Circular AC QUA-001, Quality Assurance Programs: <https://tc.canada.ca/en/aviation/reference-centre/advisory-circulars/advisory-circular-ac-qua-001>
- Transport Canada Advisory Circular AC SUR-002, Root Cause Analysis and Corrective Action for TCCA Findings: <https://tc.canada.ca/en/aviation/reference-centre/advisory-circulars/advisory-circular-ac-no-002>

In practice, that framing means the repository emphasizes requirement-to-test traceability, UTC-stamped communication records, preservation of fault evidence, and explicit retest notes after corrective changes.
