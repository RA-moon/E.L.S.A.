#!/usr/bin/env bash
set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found. Install it and retry."
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
find "$ROOT_DIR/include" "$ROOT_DIR/src" \( -name "*.h" -o -name "*.cpp" \) -print0 \
  | xargs -0 clang-format -i
