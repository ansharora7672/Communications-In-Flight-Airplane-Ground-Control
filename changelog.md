# changelog

This file is updated after implementation commits with timestamps, commit hashes, files changed, and short summaries.

## 2026-04-07 07:42:48 UTC

- Commit: `7246873`
- Files changed: `.gitignore`, `changelog.md`, `CMakeLists.txt`, `README.md`, `common/*`, `server/*`, `third_party/imgui/*`
- Summary: Initialized the repository, added the CMake build, implemented shared protocol/logging pieces, built the ground server and dashboard, and vendored Dear ImGui sources.

## 2026-04-07 07:43:07 UTC

- Commit: `b460d0c`
- Files changed: `client/main.cpp`, `changelog.md`
- Summary: Implemented the aircraft CLI client with handshake validation, telemetry transmission, weather map download handling, and structured status output.

## 2026-04-07 07:56:52 UTC

- Commit: `6001c2b`
- Files changed: `README.md`, `server/main.cpp`
- Summary: Made the ground server port configurable from the command line and documented the custom-port run flow.

## 2026-04-07 07:56:52 UTC

- Commit: `fb8a455`
- Files changed: `README.md`, `server/main.cpp`
- Summary: Added an optional `--headless` server mode for single-session protocol testing in environments that cannot open the GLFW window.

## 2026-04-07 08:01:25 UTC

- Commit: `2940251`
- Files changed: `.gitignore`, `README.md`, `client/main.cpp`, `common/logger.cpp`, `server/main.cpp`
- Summary: Organized runtime output into dedicated log and bitmap folders, keeping aircraft logs, ground-control logs, black box logs, generated BMPs, and received BMPs separate.

## 2026-04-07 04:04:00 UTC

- Commit: `c3ae0c1`
- Files changed: `changelog.md`
- Summary: Renamed the changelog file and updated its heading to the new title.

## 2026-04-07 08:16:11 UTC

- Commit: `7ea5165`
- Files changed: `AGENT_BUILD_SPEC.md`, `README.md`, `server/main.cpp`, `changelog.md`
- Summary: Removed the old build spec from the repo, normalized the changelog filename to lowercase, moved ImGui runtime settings into `runtime/ui/`, and kept the macOS GLFW startup fix in the server.

## 2026-04-07 08:35:42 UTC

- Commit: `5dc8d96`
- Files changed: `README.md`, `client/main.cpp`, `common/logger.cpp`, `server/imgui_dashboard.cpp`, `server/imgui_dashboard.h`, `server/main.cpp`
- Summary: Reworked the ground server to track multiple aircraft concurrently, scoped dashboard controls to a selected aircraft, made aircraft IDs configurable on the client, cleaned up intentional client disconnect handling, and separated concurrent client log files and received weather-map files more cleanly.

## 2026-04-07 08:44:22 UTC

- Commit: `1d04d59`
- Files changed: `README.md`, `server/imgui_dashboard.cpp`, `server/main.cpp`
- Summary: Changed weather-map generation to create a distinct generated BMP per aircraft based on aircraft identity and telemetry, and added `Previous Flight` and `Next Flight` dashboard controls for cycling between connected aircraft.
