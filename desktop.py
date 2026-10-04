"""
desktop.py — SmartGrow desktop app (local use only, NOT deployed to Vercel).

Run this on your PC to get the pywebview window talking to the physical
ESP32 over your home Wi-Fi:

    python desktop.py

This file owns every piece of the original app that can't run in a
serverless environment: the pywebview window, the manual HTTPS server (for
the installable-PWA prompt on phones), the dev-mode file watcher / live
reload, and local CSV logging. Flask itself — routes, templates, static
files — lives in app.py, which this file imports and reuses unchanged.

Install extra local-only dependencies first:
    pip install -r requirements-desktop.txt
"""

import csv
import os
import sys
import threading
import time
from datetime import datetime

# Local desktop mode keeps the old "dev mode always on" experience by
# default. Set SMARTGROW_DEV_MODE=false in your environment before running
# this if you want live-reload off locally too. This MUST happen before
# `import app`, since app.py reads the env var once at import time.
os.environ.setdefault("SMARTGROW_DEV_MODE", "true")

import webview  # not in requirements.txt on purpose — see requirements-desktop.txt

from app import app, DEV_MODE, broadcast_reload, BASE_DIR

# ============================================================
# DEV MODE — live reload (no manual refresh, no manual re-run)
# ============================================================
# While DEV_MODE is on, a background thread watches app.py, desktop.py,
# templates/, and public/ once a second. When something changes:
#   - a .py file changed        -> the whole app restarts itself
#                                   (webview window closes and reopens)
#   - a template/static file
#     changed (html/css/js/json) -> every open browser tab (phone or the
#                                   desktop window) is told to reload itself
PUBLIC_DIR = os.path.join(BASE_DIR, "public")
TEMPLATES_DIR = os.path.join(BASE_DIR, "templates")
WATCH_PATHS = [
    __file__,
    os.path.join(BASE_DIR, "app.py"),
    os.path.join(BASE_DIR, "storage.py"),
    TEMPLATES_DIR,
    PUBLIC_DIR,
]


def _collect_watched_files():
    files = []
    for path in WATCH_PATHS:
        if os.path.isfile(path):
            files.append(path)
        elif os.path.isdir(path):
            for root, _, names in os.walk(path):
                for name in names:
                    files.append(os.path.join(root, name))
    return files


def _snapshot_mtimes():
    snapshot = {}
    for f in _collect_watched_files():
        try:
            snapshot[f] = os.path.getmtime(f)
        except OSError:
            pass
    return snapshot


def watch_for_changes():
    """Polls source files once a second. A Python change restarts the whole
    process; a template/static change just pings connected browsers."""
    last_snapshot = _snapshot_mtimes()
    while True:
        time.sleep(1)
        current = _snapshot_mtimes()
        changed_paths = {
            p for p in set(current) | set(last_snapshot)
            if current.get(p) != last_snapshot.get(p)
        }

        if changed_paths:
            if any(p.endswith(".py") for p in changed_paths):
                print(f"[DevReload] Python change detected: {changed_paths} — restarting SmartGrow...")
                os.execv(sys.executable, [sys.executable] + sys.argv)
            else:
                print(f"[DevReload] Frontend change detected: {changed_paths} — reloading connected browsers...")
                broadcast_reload()

        last_snapshot = current


# --- Optional HTTPS support (needed for the automatic PWA install prompt) ---
# Chrome/Edge/Samsung Internet on Android will ONLY offer to install this
# dashboard (and fire the automatic "Add to Home screen" prompt) when it is
# served over HTTPS. Plain HTTP is fine for the ESP32 and for this desktop
# window (which talks to 127.0.0.1 == "localhost", always treated as
# secure), but a phone visiting your PC's LAN IP over HTTP will never see
# the real install prompt.
#
# Drop a cert.pem + key.pem next to this script and a second HTTPS server
# will automatically start alongside the normal HTTP one. If the files
# aren't there, HTTPS is simply skipped and everything else behaves exactly
# as before.
CERT_FILE = os.path.join(BASE_DIR, "cert.pem")
KEY_FILE = os.path.join(BASE_DIR, "key.pem")
HTTPS_PORT = 8443


def check_pwa_files():
    """Startup diagnostic: prints exactly which required PWA files are
    missing, so a broken install prompt is obvious immediately instead of
    requiring DevTools guesswork."""
    required = [
        "manifest.json", "sw.js", "css/main.css", "js/main.js",
        "icons/icon-192.png", "icons/icon-512.png",
    ]
    missing = [f for f in required if not os.path.exists(os.path.join(PUBLIC_DIR, f))]
    if missing:
        print(f"[PWA CHECK] WARNING — missing from {PUBLIC_DIR}: {', '.join(missing)}")
        print("[PWA CHECK] The app will still run, but Chrome will refuse to")
        print("[PWA CHECK] show the install prompt until these files exist there.")
    else:
        print(f"[PWA CHECK] OK — all required PWA files found in {PUBLIC_DIR}")


def log_to_csv(temp, hum, soil, light, pump, light_state, fan):
    """Logs real-time data or 'ERR' to sensor_data.csv every 2 seconds.
    Local-desktop-only: this file never exists on Vercel."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    row = [timestamp, temp, hum, soil, light, pump, light_state, fan]
    csv_path = os.path.join(BASE_DIR, "sensor_data.csv")

    with open(csv_path, mode="a", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        if csv_file.tell() == 0:
            writer.writerow([
                "Timestamp", "Temperature_C", "Humidity_pct",
                "Soil_Moisture_pct", "Light_Lux", "Pump_Active",
                "Grow_Light_Active", "Exhaust_Fan_Active",
            ])
        writer.writerow(row)


def background_logging():
    """Background loop exclusively for CSV logging and timeout detection.
    (The dashboard's own /api/sensors route computes the same 5-second
    timeout independently, at read time, so this thread not running — e.g.
    on Vercel — never breaks the live dashboard, only local history logging.)"""
    import storage  # local import: avoids desktop-only code touching app.py's import graph

    while True:
        time.sleep(2)
        state, connected = storage.touch()

        temp = state.get("temp", 0.0) if connected and state.get("dhtConnected") else "ERR"
        hum = state.get("hum", 0.0) if connected and state.get("dhtConnected") else "ERR"
        soil = state.get("soil", 0) if connected and state.get("soilConnected") else "ERR"
        light = state.get("light", 0) if connected and state.get("lightConnected") else "ERR"

        log_to_csv(
            temp, hum, soil, light,
            state.get("pump", False),
            state.get("lightState", False),
            state.get("fan", False),
        )


def run_flask_http():
    """Plain HTTP — used by the ESP32 over Wi-Fi and by this pywebview
    window (which loads 127.0.0.1, i.e. localhost, always treated as secure)."""
    app.run(host="0.0.0.0", port=8000, debug=False, use_reloader=False, threaded=True)


def run_flask_https():
    """HTTPS — only for phones that want the real installable-PWA experience.
    Starts automatically if cert.pem/key.pem exist next to this file;
    otherwise it's skipped and nothing else changes."""
    if os.path.exists(CERT_FILE) and os.path.exists(KEY_FILE):
        print(f"[SmartGrow] HTTPS enabled — open https://<your-PC-LAN-IP>:{HTTPS_PORT} on your phone to install it.")
        app.run(
            host="0.0.0.0",
            port=HTTPS_PORT,
            debug=False,
            use_reloader=False,
            threaded=True,
            ssl_context=(CERT_FILE, KEY_FILE),
        )
    else:
        print("[SmartGrow] cert.pem/key.pem not found next to desktop.py — HTTPS server not started.")
        print("[SmartGrow] The automatic install prompt will NOT appear on phones until this is set up.")


if __name__ == "__main__":
    check_pwa_files()

    flask_thread = threading.Thread(target=run_flask_http, daemon=True)
    flask_thread.start()

    https_thread = threading.Thread(target=run_flask_https, daemon=True)
    https_thread.start()

    log_thread = threading.Thread(target=background_logging, daemon=True)
    log_thread.start()

    if DEV_MODE:
        watch_thread = threading.Thread(target=watch_for_changes, daemon=True)
        watch_thread.start()
        print("[DevReload] Watching app.py, desktop.py, templates/, and public/ for changes...")

    webview.create_window(
        "Automated ESP32 Greenhouse for Pechay",
        "http://127.0.0.1:8000",
        width=1400,
        height=850,
        background_color="#030a07",
    )
    webview.start()
