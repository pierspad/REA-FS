# New-Sensor Investigation Template

Copy this template into your private session directory (`sessions/<id>/`); never publish raw biometric data or credentials.

## 1. Identity & Provenance

- **Bus & device ID** (`VID:PID` or ACPI/HID, `bcdDevice`, internal ROM/module ID):
- **System environment** (distro, kernel, `libfprint`/`fprintd` versions, desktop session):
- **Repository revisions & local state**:
- **OEM package provenance** (source URL, version, SHA-256, matched `.inf` entries):

## 2. Preferences & Data Policy

- **Authorization mode & scope** (`allowed_actions` / `excluded_actions`):
- **Execution channel** (GUI panel `plan.json` or headless fallback; stop procedure):
- **Privacy & storage** (private session directory `0700`, `external_transfer: false`):

## 3. Experimental Question (`STATE 3`)

- **Verified prerequisite fact**:
- **Hypothesis & plausible alternative**:
- **Why previous runs do not answer this question**:
- **Exact command / `plan.json` entry & inspected side effects**:
- **Wall-clock timeout, output bounds, and cleanup**:
- **Observable success & failure criteria**:
- **Physical action required from user (if any)**:

## 4. Observed Outcome (`STATE 4`)

- **Timestamps, exit code, vendor status codes, and cleanup confirmation**:
- **Private artifact paths & SHA-256 hashes (no raw secrets in public docs)**:
- **Facts observed vs. interpretation & confidence**:
- **Next reproducible step**:

## 5. Biometric Evaluation (When Reached)

- **Enrollment stages** (distinct touches separated by verified lift):
- **Held-out verification set** (excluded from enrollment and threshold selection):
- **Matcher, pipeline revision, template count, and fixed threshold**:
- **Genuine vs. impostor attempts, retries, and timeouts**:
- **Latency breakdown** (open/calibration, touch-to-result, matching, close):
- **Recovery checks** (cancellation/reopen, reboot, suspend/resume):
