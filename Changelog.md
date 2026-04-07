# Changelog

This file is updated after implementation commits with timestamps, commit hashes, files changed, and short summaries.

## 2026-04-07 07:42:48 UTC

- Commit: `7246873`
- Files changed: `.gitignore`, `AGENT_BUILD_SPEC.md`, `AGENT_CHANGELOG.md`, `CMakeLists.txt`, `README.md`, `common/*`, `server/*`, `third_party/imgui/*`
- Summary: Initialized the repository, added the CMake build, implemented shared protocol/logging pieces, built the ground server and dashboard, and vendored Dear ImGui sources.

## 2026-04-07 07:43:07 UTC

- Commit: `b460d0c`
- Files changed: `client/main.cpp`, `AGENT_CHANGELOG.md`
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
