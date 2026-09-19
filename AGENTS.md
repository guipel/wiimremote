# WiiM Remote Project Directives

These directives are binding and must be strictly followed on all tasks in this repository.

## 1. Mandatory Implementation Plan Before File Modifications
- **Never modify, create, or delete code or configuration files without presenting an implementation plan first.**
- Before executing any code changes, draft a detailed implementation plan outlining the proposed modifications, affected files, and verification steps.
- **Wait for explicit user review and approval** before modifying workspace files.

## 2. Simplicity & Clean Design (Zero Overengineering)
- Prioritize clean, readable, minimal, and straightforward solutions.
- Avoid speculative generality, deep inheritance trees, unnecessary abstractions, or bloated dependencies when a simple, direct approach is possible.
- Write code tailored specifically to the ESP32-S3 hardware constraints (memory, CPU, FreeRTOS dual cores) without excess ceremony.

## 3. Grounded in Documentation (No Guessing)
- **Do not guess or assume** hardware registers, peripheral pins, LVGL APIs, ESP-IDF/Arduino function signatures, or LinkPlay/WiiM HTTP/UPnP API endpoints.
- Always check verified project headers, official documentation, or local codebase references before implementing or proposing changes.
- If an API or behavioral aspect is undocumented or ambiguous, investigate and verify before writing code.

## 4. Transparent Debugging Protocol
- When diagnosing bugs, regressions, build failures, or runtime crashes:
  1. **Declare the objective**: State clearly what symptom or issue is being investigated.
  2. **State working hypotheses**: Explain what might be causing the issue.
  3. **Detail the investigation plan**: Explain *how* it is being investigated (which logs, metrics, code sections, or tests will be inspected) before executing diagnostic steps.

## 5. Strict Changelog Discipline (Main Branch Only)
- Every time code or configuration changes are committed to or merged into `main`, they **must** be documented in `CHANGELOG.md`.
- **No changelog modifications on feature branches**: Feature/topic branches must only contain the relevant code, config, or assets.
- **Consolidation upon merge**: When merging a branch into `main`, all changes introduced by that branch must be consolidated and recorded in `CHANGELOG.md` adhering to the Keep a Changelog format.

