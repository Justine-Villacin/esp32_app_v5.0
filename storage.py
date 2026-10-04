"""
storage.py — shared state store for SmartGrow.

Why this exists
----------------
On Vercel, app.py runs as a serverless function: every invocation can land on
a fresh instance with no memory of the last one, so a plain Python dict
(as the original app used) silently "forgets" every ESP32 reading between
requests. This module gives app.py a single get/set surface that:

  * Uses Redis when a connection string is configured (REDIS_URL, or KV_URL
    for older Vercel KV-style setups) — this is the real fix for Vercel.
  * Falls back to an in-memory dict when no connection string is set — this
    keeps local desktop mode (desktop.py) and `python app.py`-style testing
    working with zero setup, exactly like the original app did.

The in-memory fallback does NOT persist across separate serverless
invocations, so it is not a substitute for Redis in production — it exists
purely so the app still runs without extra setup while developing locally.
"""

import json
import os
import threading
import time
from collections import deque

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REDIS_URL = os.environ.get("REDIS_URL") or os.environ.get("KV_URL")

KEY_LATEST = "smartgrow:latest"
KEY_HISTORY = "smartgrow:history"
MAX_HISTORY = 2000  # capped list length, used to rebuild CSV/Excel exports

# How long since the ESP32's last successful /update before the dashboard
# treats it as disconnected. The ESP32 firmware posts every ~2 seconds
# (SERVER_POST_INTERVAL_MS in firmware/config.h); 8 seconds gives room for a
# couple of missed/delayed posts from normal WiFi jitter without flapping
# the "ESP32 DISCONNECTED" state on every brief hiccup. Tune with the
# SMARTGROW_DISCONNECT_TIMEOUT env var if your network needs more slack.
DISCONNECT_TIMEOUT_SECONDS = float(os.environ.get("SMARTGROW_DISCONNECT_TIMEOUT", "8"))

DEFAULT_STATE = {
    "esp32Connected": False,
    "dhtConnected": False,
    "soilConnected": False,
    "lightConnected": False,
    "temp": 0.0,
    "hum": 0.0,
    "soil": 0,
    "light": 0,
    "pump": False,
    "lightState": False,
    "fan": False,
    # Internal bookkeeping field (stripped before it reaches the frontend).
    "_last_update_time": 0,
}

# ---------------------------------------------------------------------------
# Redis client (lazy, cached, never raises on import)
# ---------------------------------------------------------------------------

_redis_client = None          # cached client instance once a connection succeeded
_redis_retry_after = 0.0      # after a failed connect, wait a few seconds then try again
_redis_last_error = None      # last connection error text (shown by /api/status)
_redis_client_lock = threading.Lock()


def _get_redis_client():
    """Returns a connected redis client, or None if Redis isn't configured
    or isn't reachable right now. Never raises. A failed connect is NOT
    cached forever (it used to be): it is retried after a short cooldown, so
    one slow cold start can't silently pin an instance to in-memory storage."""
    global _redis_client, _redis_retry_after, _redis_last_error

    if not REDIS_URL:
        return None
    if _redis_client is not None:
        return _redis_client
    if time.time() < _redis_retry_after:
        return None

    with _redis_client_lock:
        if _redis_client is not None:
            return _redis_client
        try:
            import redis  # lazy import so a missing package never crashes app.py at import time
            client = redis.from_url(
                REDIS_URL,
                decode_responses=True,
                socket_connect_timeout=3,
                socket_timeout=3,
            )
            client.ping()
            _redis_client = client
            _redis_last_error = None
            return _redis_client
        except Exception as exc:  # noqa: BLE001
            _redis_last_error = f"{type(exc).__name__}: {exc}"
            print(f"[storage] Redis configured but unreachable, using memory for now: {_redis_last_error}")
            _redis_retry_after = time.time() + 3
            return None


def last_redis_error():
    """Last Redis connection error (or None). Exposed via /api/status."""
    return _redis_last_error


# ---------------------------------------------------------------------------
# In-memory fallback
# ---------------------------------------------------------------------------

_mem_lock = threading.Lock()
_mem_latest = dict(DEFAULT_STATE)
_mem_history = deque(maxlen=MAX_HISTORY)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def backend_name():
    """'redis' if a working Redis connection is available, else 'memory'."""
    return "redis" if _get_redis_client() is not None else "memory"


def is_remote_configured():
    """True if a Redis connection string is set (regardless of reachability).
    Used to decide export-route behavior/messaging."""
    return bool(REDIS_URL)


def get_latest():
    """Returns the latest known sensor/automation state as a plain dict."""
    client = _get_redis_client()
    if client is not None:
        try:
            raw = client.get(KEY_LATEST)
            if raw:
                state = dict(DEFAULT_STATE)
                state.update(json.loads(raw))
                return state
            return dict(DEFAULT_STATE)
        except Exception as exc:  # noqa: BLE001
            print(f"[storage] Redis read failed, falling back to memory: {exc}")

    with _mem_lock:
        return dict(_mem_latest)


def set_latest(state):
    """Persists the full state dict (as returned by get_latest(), mutated)."""
    client = _get_redis_client()
    if client is not None:
        try:
            client.set(KEY_LATEST, json.dumps(state))
            return
        except Exception as exc:  # noqa: BLE001
            print(f"[storage] Redis write failed, falling back to memory: {exc}")

    with _mem_lock:
        _mem_latest.clear()
        _mem_latest.update(state)


def append_history(row):
    """Appends one reading (a dict) to the capped history list used to
    rebuild the CSV/Excel exports. Newest entries are pushed to the front."""
    client = _get_redis_client()
    payload = json.dumps(row)
    if client is not None:
        try:
            pipe = client.pipeline()
            pipe.lpush(KEY_HISTORY, payload)
            pipe.ltrim(KEY_HISTORY, 0, MAX_HISTORY - 1)
            pipe.execute()
            return
        except Exception as exc:  # noqa: BLE001
            print(f"[storage] Redis history write failed, falling back to memory: {exc}")

    with _mem_lock:
        _mem_history.appendleft(row)


def get_history():
    """Returns stored readings, oldest first, as a list of dicts."""
    client = _get_redis_client()
    if client is not None:
        try:
            raw_rows = client.lrange(KEY_HISTORY, 0, MAX_HISTORY - 1)
            rows = [json.loads(r) for r in raw_rows]
            rows.reverse()  # oldest first, matching how a CSV reads top-to-bottom
            return rows
        except Exception as exc:  # noqa: BLE001
            print(f"[storage] Redis history read failed, falling back to memory: {exc}")

    with _mem_lock:
        return list(reversed(_mem_history))


def touch(now=None):
    """Convenience: returns (state, connected) where `connected` reflects the
    ESP32 timeout (DISCONNECT_TIMEOUT_SECONDS) computed at READ time from
    `_last_update_time`."""
    now = now if now is not None else time.time()
    state = get_latest()
    last_update = state.get("_last_update_time", 0)
    connected = bool(last_update) and (now - last_update) <= DISCONNECT_TIMEOUT_SECONDS
    return state, connected