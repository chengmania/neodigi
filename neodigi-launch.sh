#!/bin/bash
# neodigi-launch.sh — Start neodigi (fldigi + Qt UI)
# Author: chengmania KC3SMW

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NEODIGI_BIN="$SCRIPT_DIR/app/build/neodigi"
FLDIGI_CONFIG="$HOME/.neodigi-fldigi"
FLDIGI_PATH_CONF="$FLDIGI_CONFIG/fldigi_path.conf"
XMLRPC_PORT=7362
XMLRPC_HOST="localhost"
FLDIGI_PID=""

# ── Ensure config directory exists ─────────────────────────────────────────────

mkdir -p "$FLDIGI_CONFIG"

# ── Resolve fldigi binary path ─────────────────────────────────────────────────
# Priority: saved config → which fldigi → user prompt

resolve_fldigi() {
    local path=""

    # 1) Saved config
    if [ -f "$FLDIGI_PATH_CONF" ]; then
        path="$(head -1 "$FLDIGI_PATH_CONF" | tr -d '[:space:]')"
        if [ -n "$path" ] && [ -x "$path" ]; then
            echo "$path"
            return 0
        fi
        echo "neodigi: Saved fldigi path '$path' is invalid — re-detecting." >&2
    fi

    # 2) PATH lookup
    if command -v fldigi &>/dev/null; then
        path="$(command -v fldigi)"
        echo "$path" > "$FLDIGI_PATH_CONF"
        echo "$path"
        return 0
    fi

    # 3) Prompt user
    echo "neodigi: fldigi binary not found." >&2
    local prompt="Please enter the full path to the fldigi executable:"
    if command -v zenity &>/dev/null; then
        path="$(zenity --file-selection --title="Select fldigi executable" --filename="/usr/bin/fldigi" 2>/dev/null || true)"
    elif command -v kdialog &>/dev/null; then
        path="$(kdialog --getopenfilename "/usr/bin" "fldigi" 2>/dev/null || true)"
    else
        read -r -p "$prompt " path </dev/tty || true
    fi

    if [ -z "$path" ] || [ ! -x "$path" ]; then
        echo "neodigi: ERROR — no valid fldigi path provided." >&2
        echo "neodigi: Run this script again or create $FLDIGI_PATH_CONF with the full path." >&2
        exit 1
    fi

    echo "$path" > "$FLDIGI_PATH_CONF"
    echo "$path"
    return 0
}

FLDIGI_BIN="$(resolve_fldigi)"
echo "neodigi: Using fldigi at $FLDIGI_BIN"

# ── Cleanup on exit ────────────────────────────────────────────────────────────

cleanup() {
    if [ -n "$FLDIGI_PID" ] && kill -0 "$FLDIGI_PID" 2>/dev/null; then
        echo "neodigi: Stopping fldigi (PID $FLDIGI_PID)..."
        kill "$FLDIGI_PID" 2>/dev/null
        wait "$FLDIGI_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ── First-run detection ────────────────────────────────────────────────────────

if [ ! -f "$FLDIGI_CONFIG/fldigi.prefs" ]; then
    echo "neodigi: First run — launching fldigi setup wizard."
    echo "neodigi: Complete configuration, then close fldigi and run this script again."
    "$FLDIGI_BIN" --config-dir "$FLDIGI_CONFIG"
    echo "neodigi: Setup complete. Run neodigi-launch.sh to start."
    exit 0
fi

# ── Launch fldigi ──────────────────────────────────────────────────────────────
# NOTE: --wfall-only causes fldigi 4.2.11 to crash (SIGSEGV) on this system.
# -iconic (FLTK single-dash flag) starts fldigi iconified/minimized to the taskbar.

FLDIGI_LOG="$FLDIGI_CONFIG/fldigi-crash.log"
echo "neodigi: Starting fldigi modem engine (stderr → $FLDIGI_LOG)..."
"$FLDIGI_BIN" \
    -iconic \
    --config-dir "$FLDIGI_CONFIG" \
    2>"$FLDIGI_LOG" &
FLDIGI_PID=$!
echo "neodigi: fldigi PID = $FLDIGI_PID"

# ── Wait for fldigi to be fully ready ─────────────────────────────────────────
# Two-phase wait:
#   Phase 1: port 7362 opens  (XML-RPC thread started — but fldigi still init-ing)
#   Phase 2: modem.get_name returns a value (modem fully initialized)
# Without phase 2, neodigi connects while fldigi is mid-init → SIGSEGV in fldigi.

echo "neodigi: Waiting for fldigi to start..."
WAITED=0
MAX_WAIT=30
while true; do
    if nc -z "$XMLRPC_HOST" "$XMLRPC_PORT" 2>/dev/null; then
        echo "neodigi: XML-RPC port open (${WAITED}s) — waiting for modem init..."
        break
    fi
    if ! kill -0 "$FLDIGI_PID" 2>/dev/null; then
        echo "neodigi: ERROR — fldigi exited unexpectedly. Check $FLDIGI_LOG"
        exit 1
    fi
    if [ "$WAITED" -ge "$MAX_WAIT" ]; then
        echo "neodigi: WARNING — fldigi not responding after ${MAX_WAIT}s."
        break
    fi
    sleep 1
    WAITED=$((WAITED + 1))
done

# Phase 2: poll modem.get_name until it returns a non-empty, non-fault response.
XMLRPC_CALL='<?xml version="1.0"?><methodCall><methodName>modem.get_name</methodName><params></params></methodCall>'
PHASE2_WAIT=0
PHASE2_MAX=20
while true; do
    if ! kill -0 "$FLDIGI_PID" 2>/dev/null; then
        echo "neodigi: ERROR — fldigi died during modem init. Check $FLDIGI_LOG"
        exit 1
    fi
    RESPONSE=$(curl -s --max-time 2 -X POST \
        "http://$XMLRPC_HOST:$XMLRPC_PORT/RPC2" \
        -H "Content-Type: text/xml" \
        -H "Connection: close" \
        -d "$XMLRPC_CALL" 2>/dev/null || true)
    if echo "$RESPONSE" | grep -q "<value>" && ! echo "$RESPONSE" | grep -q "<fault>"; then
        echo "neodigi: fldigi modem ready (${PHASE2_WAIT}s after port open)."
        break
    fi
    if [ "$PHASE2_WAIT" -ge "$PHASE2_MAX" ]; then
        echo "neodigi: WARNING — modem not responding after ${PHASE2_MAX}s, launching anyway."
        break
    fi
    sleep 1
    PHASE2_WAIT=$((PHASE2_WAIT + 1))
done

# ── Ensure fldigi window is minimized ─────────────────────────────────────────
# Belt-and-suspenders: -iconic should handle this, but some WMs ignore it.
# Only minimize (iconify) — do NOT use add,hidden which makes the window
# unraiseable by wmctrl/xdotool later.

if command -v xdotool &>/dev/null; then
    xdotool search --class Fldigi windowminimize 2>/dev/null || true
elif command -v wmctrl &>/dev/null; then
    wmctrl -r fldigi -b add,shaded 2>/dev/null || true
fi

# ── Launch neodigi Qt UI ───────────────────────────────────────────────────────

echo "neodigi: Launching UI..."
"$NEODIGI_BIN"

echo "neodigi: UI closed."
