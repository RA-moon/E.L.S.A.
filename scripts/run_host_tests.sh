#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${ROOT_DIR}/scripts/run_no_beat_policy_test.sh"
"${ROOT_DIR}/scripts/run_beat_event_flow_test.sh"
"${ROOT_DIR}/scripts/run_beat_scheduler_integration_test.sh"
