# SmartGrow — ESP32 Greenhouse Monitor

An automated greenhouse dashboard for an ESP32-based Pechay grow setup:
live temperature/humidity/soil/light readings, pump/light/fan automation
status, a plant-health score, and CSV/Excel export.

This project has **two ways to run it**:

| Mode | File | Where it runs | Use for |
|---|---|---|---|
| **Desktop app** | `desktop.py` | Your PC, on your home Wi-Fi | Talking to the physical ESP32, day-to-day monitoring |
| **Hosted web app** | `app.py` | Vercel (`*.vercel.app`) | A public URL you (or anyone) can check from anywhere |

Both share the exact same Flask routes, templates, and static files — only
the "how it's launched" part differs.

There's also **`firmware/`** — the ESP32 Arduino sketches. Automation
(pump/grow-light/fan) runs on the ESP32 itself, not on the website; the
dashboard only ever displays what the firmware reports. See
`firmware/README.md` for wiring, library installation, and flashing
instructions, and run `firmware/SmartGrow_Diagnostics` first to confirm
your wiring and find your calibration numbers.

---

## 1. Local desktop mode (talks to the real ESP32)

```bash
python -m venv .venv
source .venv/bin/activate        # Windows: .venv\Scripts\activate
pip install -r requirements-desktop.txt
python desktop.py
```

This opens a native window pointed at `http://127.0.0.1:8000`, starts a
background CSV logger (`sensor_data.csv`, next to `desktop.py`), and — if
you drop a `cert.pem`/`key.pem` pair next to `desktop.py` — a second HTTPS
server on port 8443 so a phone on the same Wi-Fi can install it as a PWA.

Live-reload (auto-restart on `.py` changes, auto-refresh on template/CSS/JS
changes) is **on by default** in this mode. Point your ESP32 firmware's
`http://` target at your PC's LAN IP, port 8000, and it just works — no
environment variables required for local-only use.

## 2. Hosted web app (Vercel)

The version of this app that Vercel actually runs is `app.py` alone — it
never sees `desktop.py`, `webview`, or your local CSV file.

### Deploy

```bash
npm i -g vercel      # once
vercel link          # once, from the project folder
vercel env pull      # syncs env vars for local `vercel dev` testing
vercel                # preview deploy
vercel --prod         # production deploy
```

Or connect the repo at [vercel.com/new](https://vercel.com/new) for
Git-based deploys — either way, Vercel auto-detects the Flask `app` object
in `app.py` with zero build configuration.

### Required environment variables

Set these in **Project Settings → Environment Variables** (see
`.env.example` for the same list with comments):

| Variable | Required? | Purpose |
|---|---|---|
| `SMARTGROW_API_KEY` | Strongly recommended | Shared secret checked on `POST /update` and `POST /api/control`. Without it, those routes accept requests from anyone on the internet. |
| `REDIS_URL` | Recommended | Connection string for a Redis-compatible store (see below). Without it, sensor data resets every cold start. |
| `SMARTGROW_DEV_MODE` | No | Leave unset/`false` on Vercel. |

### Persistent storage: why you need Redis on Vercel

The original app kept sensor readings in a plain Python dict in memory.
That works fine on a PC that stays running, but Vercel Functions are
serverless: each invocation can land on a fresh instance with no memory of
the last one; the ESP32's `POST /update` and the dashboard's
`GET /api/sensors` might not even hit the same instance. Without external
storage, the dashboard would look "disconnected" almost all the time.

`storage.py` fixes this by reading/writing through Redis when `REDIS_URL`
is set, using it as:

- one JSON blob for the latest reading + a timestamp
- a capped list (2000 entries) of historical readings, used to rebuild the
  CSV/Excel exports

**To set it up:** open your Vercel project → **Storage** tab → add a Redis
integration from the Marketplace (e.g. Upstash Redis — Vercel KV was
retired and existing KV stores were auto-migrated to Upstash Redis in
December 2024). Vercel injects the connection string as an environment
variable automatically; copy its value into `REDIS_URL` in your project's
env vars if it isn't named that exactly (some integrations use `KV_URL` —
`storage.py` accepts either).

**If you skip this:** the app still runs, using an in-memory fallback. It
works for a quick test, but readings will appear to reset randomly as
Vercel spins up new instances, and CSV/Excel export will report no data
available (see below).

### CSV / Excel history: which option was implemented

The brief offered two options for history since local file writes don't
persist on Vercel. **This app implements (a):** when `REDIS_URL` is
configured, every `/update` from the ESP32 also appends to a capped
history list in Redis, and `/sensor_data.csv` / `/download/excel` rebuild
the export from that list on demand.

As a fallback (not the primary strategy), when Redis isn't configured —
running `desktop.py` locally, or a fresh Vercel deploy with no Redis
attached yet — the export routes look for the local
`sensor_data.csv` file next to `app.py` (only ever present in desktop
mode), and if neither exists, return a **styled JSON response** the
frontend shows as a toast, instead of a bare 404 page. See `CHANGELOG.md`
for the reasoning.

### Check what's actually happening: `/api/status`

A small diagnostic endpoint was added at `GET /api/status`. It reports
which storage backend is active (`redis` or `memory`), whether dev mode is
on, whether an API key is required, and whether the ESP32 currently looks
connected — useful right after a deploy to confirm Redis is wired up
correctly.

### Updating your ESP32 firmware

Once `SMARTGROW_API_KEY` is set, the ESP32's `POST /update` and any manual
`POST /api/control` calls must include the header:

```
X-API-Key: <the same value as SMARTGROW_API_KEY>
```

Requests without a matching header get a `401 Unauthorized` JSON response
instead of being silently accepted. `firmware/SmartGrow_ESP32/config.h` has
an `API_KEY` field for exactly this.

### `POST /update` payload reference

This is the JSON the ESP32 firmware sends on every report; `app.py` and
`firmware/SmartGrow_ESP32.ino` are both written against this exact contract
— if you write your own firmware, match this shape:

| Field | Type | Notes |
|---|---|---|
| `temperature` | number | °C, -40 to 80. Omit entirely if the DHT read failed. |
| `humidity` | number | %, 0-100. Omit if the DHT read failed. |
| `soil_moisture` | number | %, 0-100. Omit if the soil sensor looks disconnected. |
| `light` | number | 0-1000 (calibrated LDR scale — **not** a relay command). Omit if the LDR looks disconnected. |
| `dht_connected` | bool | Defaults to `true` if omitted (older firmware). |
| `soil_connected` | bool | Defaults to `true` if omitted. |
| `light_connected` | bool | Defaults to `true` if omitted. |
| `fan` | `"ON"` / `"OFF"` | Exhaust fan relay state. |
| `water_pump` | `"ON"` / `"OFF"` | Pump relay state. |
| `grow_light` | `"ON"` / `"OFF"` | Grow light relay state. **Deliberately a different field from `light`** above — see `CHANGELOG.md` for why an earlier version of this project used the same key for both and silently broke the numeric light reading. |

The server additionally enforces, regardless of what's reported: if
`dht_connected` is `false`, `fan` is forced `false`; if `soil_connected` is
`false`, `water_pump`/pump is forced `false`; if `light_connected` is
`false`, `grow_light`/light is forced `false`. This is defense-in-depth on
top of the firmware's own fail-safes — see `CHANGELOG.md`.

### Tuning the "ESP32 disconnected" timeout

The dashboard treats the ESP32 as disconnected after `DISCONNECT_TIMEOUT_SECONDS`
(default 8s, in `storage.py`) since the last successful `/update`. The
firmware posts every 2 seconds by default (`SERVER_POST_INTERVAL_MS` in
`firmware/SmartGrow_ESP32/config.h`), so 8s tolerates a couple of missed/
delayed posts from normal WiFi jitter before flagging a real disconnect. If
your network is noisier, raise it with the `SMARTGROW_DISCONNECT_TIMEOUT`
environment variable rather than editing the code.

---

## Project layout

```
app.py                 Flask app — routes, templates, static config. This is
                        the ONLY file Vercel imports.
firmware/               ESP32 Arduino sketches (production + diagnostics).
                        See firmware/README.md. Not deployed — flashed to
                        the ESP32 itself.
desktop.py              pywebview window, manual HTTPS runner, dev-mode file
                        watcher, local CSV logging. Local use only.
storage.py               Redis-backed (or in-memory fallback) state store
                        shared by app.py and desktop.py.
templates/index.html    Dashboard page.
public/                 CSS, JS, icons, manifest.json, sw.js — served at the
                        site root (no "/static" prefix) by Vercel's CDN, and
                        by Flask's own static handling locally.
requirements.txt        What Vercel installs (Flask, openpyxl, redis client).
requirements-desktop.txt  Adds pywebview, for `python desktop.py` only.
vercel.json             Modern `functions` config (maxDuration, excludeFiles).
.vercelignore / .gitignore  Keep secrets, local data, and dev-only files out
                        of deployments and version control.
```

## Verifying a deploy yourself

```bash
pip install -r requirements.txt   # should succeed with no missing packages
python -c "import app"            # should succeed with zero GUI side effects
vercel dev                        # serves /, /api/sensors, /manifest.json,
                                   # /sw.js, and the export routes locally
```

See `CHANGELOG.md` for the full list of changes made from the original
single-file version, and `USER_MANUAL.md` for a plain-language walkthrough
if you're setting this up for the first time.

## Troubleshooting ESP32 connectivity (offline vs. deployed)

The firmware supports two server targets via `SERVER_USE_HTTPS` in
`firmware/SmartGrow_ESP32/config.h` — switch between them by commenting/
uncommenting the matching block:

- **Offline / local mode** (`SERVER_USE_HTTPS false`, plain HTTP to your
  PC's LAN IP, port 8000, talking to `python desktop.py`): no TLS, no
  internet dependency, by far the simplest path to debug. **Always test
  this path first** when something doesn't connect — if local mode works
  but the deployed one doesn't, the problem is specifically in the
  HTTPS/internet path, not your sensors, wiring, or WiFi credentials.
- **Deployed mode** (`SERVER_USE_HTTPS true`, HTTPS to your Vercel domain):
  adds DNS resolution and a TLS handshake to the picture, both of which can
  fail independently of WiFi itself being connected.

If the deployed path shows `POST failed: connection refused` in the Serial
Monitor, the firmware now prints three extra diagnostic lines right before
each attempt that pinpoint exactly which layer is failing:

```
[SmartGrow][diag] DNS OK: your-project.vercel.app -> <some IP>
[SmartGrow][diag] Raw TLS connect() to your-project.vercel.app:443 -> SUCCESS/FAILED
[SmartGrow][diag] mbedTLS error detail: <text, only printed on failure>
```

- **DNS FAILED** — your router/network's DNS isn't resolving the hostname.
  Try switching the ESP32 to a phone hotspot temporarily to see if it's
  specific to your home router/ISP (some ISP routers filter or rate-limit
  DNS for IoT-looking traffic).
- **DNS OK, but raw TLS connect FAILED** — something between the ESP32 and
  Vercel's edge is blocking or resetting the HTTPS connection specifically
  (a firewall, parental-control/content filtering on the router, or deep
  packet inspection some ISP routers do). The mbedTLS error line usually
  names the specific failure. Testing from a phone hotspot again isolates
  router-specific blocking from a genuine code/device problem.
- **Both OK, but the real POST still fails** — note the HTTP status code
  printed; a `401` means the API key doesn't match, anything else likely
  means a server-side issue worth checking against `/api/status`.

## License

Apache License 2.0 — see `LICENSE`.
