#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${TRAASH_BIN:-$ROOT/build/traash}"
RUNTIME="${XDG_RUNTIME_DIR:-/tmp}/traash-agent-test-$$"
mkdir -p "$RUNTIME"
export XDG_RUNTIME_DIR="$RUNTIME"

cleanup() {
  if [[ -n "${SERVER_PID:-}" ]]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -rf "$RUNTIME"
}
trap cleanup EXIT

"$BIN" --server --create agent_it &
SERVER_PID=$!
sleep 0.5

OUT="$("$BIN" agent state agent_it 2>/dev/null)"
echo "$OUT" | grep -q '"session":"agent_it"'
echo "$OUT" | grep -q '"panes"'

echo "agent integration ok"
