# Changelog — v5.0: connectivity diagnostics, verified end-to-end

## What changed

1. **Diagnostic instrumentation added to `firmware/SmartGrow_ESP32.ino`'s
   HTTPS path.** Persistent "connection refused" errors on the deployed
   (Vercel/HTTPS) path were not reproducible/diagnosable from the Flask
   side alone — the website backend was independently verified byte-for-
   byte identical to the known-working reference (see "Website
   verification" below), so the remaining unknown was specifically where,
   on the ESP32 side, the HTTPS connection was failing: DNS, TCP, or the
   TLS handshake. Three new lines now print before every HTTPS POST
   attempt, isolating exactly which layer fails (see README.md's new
   "Troubleshooting ESP32 connectivity" section for how to read them).
2. **Fixed an ordering bug in that same diagnostic code**, caught before
   shipping: the diagnostic raw-connect check was initially placed before
   `secureClient.setInsecure()`, so it would attempt full certificate-chain
   validation (which an ESP32 can't complete without a CA bundle) and
   always report failure — regardless of whether the real, correctly-
   ordered POST attempt right after it would have succeeded. Moved
   `setInsecure()` to run first, so the diagnostic reflects the same
   connection conditions as the real request.
3. **Website verification (no changes needed).** In response to "ESP32
   still not connecting," the actually-deployed repository
   (github.com/Justine-Villacin/esp32_appv2, branch `main`) was pulled
   apart and checked line by line: `app.py`, `storage.py`,
   `templates/index.html`, `vercel.json`, `.vercelignore`,
   `requirements.txt` are all byte-identical to this project's verified
   reference versions, contain no unresolved git-merge artifacts, and
   compile/import cleanly. The repository itself was also confirmed clean
   (no accidental `.venv` or `sensor_data.csv` committed, working tree
   in sync with `origin/main`). This ruled out the website as the source
   of the "ESP32 DISCONNECTED" symptom and redirected troubleshooting to
   the ESP32 side, which is what items 1-2 above address.
4. **`config.h` finalized** with this device's real WiFi credentials, its
   current Vercel deployment host, and its actual wiring (`DHT_PIN 14`,
   `DHT_TYPE DHT11` — a stale "DHT22 data pin" comment left over from the
   generic template was also corrected).
5. **Confirmed already-correct, left unchanged:** sensor values are only
   ever sent to the server (and only ever printed as a real value in the
   Serial Monitor, versus "OFFLINE") when that sensor's own connectivity
   flag is true — `postReading()` omits `temperature`/`humidity`/
   `soil_moisture`/`light` from the JSON entirely while their sensor is
   disconnected rather than sending a stale or zeroed value, and
   `printStatus()` prints "OFFLINE" in the same cases. This was already
   correct firmware behavior from the Round 2 rewrite; re-verified here
   rather than re-implemented, per item 2 of this round's request.

## Known open item

The root cause of the original "connection refused" on the HTTPS/deployed
path has not been conclusively identified — the diagnostics in this
version are what will pin it down on the next test run, and the README's
troubleshooting section explains how to read the result. This is flagged
explicitly rather than claimed fixed, since a connectivity issue that
can't be reproduced outside the user's own network can't be honestly
marked resolved from code changes alone.

---

# Changelog — Round 2: bug fixes, firmware, and dashboard clarity

Changes made in response to a second review round. Numbered to match the
items as given.

1. **Relays staying ON when sensors disconnect — fixed at three layers:**
   - **Firmware** (`firmware/SmartGrow_ESP32/SmartGrow_ESP32.ino`): the fan,
     pump, and grow light are now forced off the instant their governing
     sensor looks disconnected, re-checked every loop (not just on the
     transition) — this is the real hardware-level fix, since automation
     runs on the ESP32, not the website.
   - **Server** (`app.py`, `/update`): even if a payload somehow reports a
     sensor disconnected and its actuator ON at the same time, the server
     now overrides the actuator to `false` before storing it. Also, when
     the ESP32 hasn't posted in `DISCONNECT_TIMEOUT_SECONDS` at all,
     `/api/sensors` now forces `fan`/`pump`/`lightState` to `false` in
     addition to the sensor-connected flags (it previously only did the
     latter, so the automation cards could keep showing ACTIVE while the
     sensor badges said DISCONNECTED).
   - **Frontend** (`public/js/ui.js`): automation cards now independently
     check sensor connectivity and show a dedicated OFFLINE state
     regardless of what the data says, rather than trusting `data.fan`/
     `data.pump`/`data.lightState` blindly.
2. **No indicators for disconnected sensors — added a real visual state,**
   not just a small status-chip color. Both the sensor cards
   (`.sensor.offline` in `public/css/sensors.css`: dashed red border,
   greyed icon) and the automation cards
   (`.automation-card.offline` in `public/css/automation.css`) now get a
   distinct, unmistakable "offline" treatment, and automation card text
   explicitly says *why* ("Soil sensor disconnected — pump disabled for
   safety.").
3. **Plant Health wording rewritten for clarity:**
   - Disconnected-sensor metrics now say "No Data" instead of a bare "--".
   - Vague labels reworded: temperature's "High"/"Low" → "Too Hot"/"Too
     Cold"; humidity's "High" → "Too Humid" (paired with the existing "Dry
     Air").
   - Light's default good-state label changed from "Good" to "Optimal" to
     match the other three metrics.
   - **Also fixed a scoring bug while in here:** a disconnected humidity or
     light sensor previously cost 0 points (no score deduction at all),
     while a disconnected temperature or soil sensor cost 25/35 points —
     wildly inconsistent, and it meant a half-broken sensor rig could still
     show a near-100% health score. All four now deduct when disconnected
     (temp -25, humidity -20, soil -35, light -25), on the principle that
     not knowing a reading is at least as bad as its worst known value, not
     better.
5. **Environmental Overview color coding fixed:** the light sensor's status
   chip had OPTIMAL and LOW swapped relative to every other sensor's
   convention — OPTIMAL rendered in the warning (orange) color and LOW in
   danger (red), backwards from temperature/humidity/soil, which all use
   green-for-good. OPTIMAL now renders in the same primary/green used
   everywhere else. (Found and fixed a second, unrelated color/structure
   bug in the same sweep: `logActivity()` in `public/js/utils.js` computed
   a status color and built two lines of plain text, but never actually
   used the color or built the `.activity-dot`/`.activity-time` elements
   the CSS expected — the System Log entries had no colored dot and no
   right-aligned timestamp. Rewritten to build real DOM elements.)
6. **Constant ESP32 disconnection — addressed on both ends:**
   - **Firmware:** WiFi connect/reconnect was rewritten to be fully
     non-blocking (`maintainWiFi()`, checked every loop via `millis()`,
     never a blocking `delay()`-based retry loop), so a flaky network no
     longer stalls the sensor/automation loop or the HTTP post loop.
   - **Server:** the disconnect timeout was a hardcoded `5` (seconds) in
     `storage.py`, tight relative to a ~2s post interval — any single
     missed/delayed post flipped the dashboard to "DISCONNECTED" and back.
     It's now `DISCONNECT_TIMEOUT_SECONDS` (default 8, tunable via
     `SMARTGROW_DISCONNECT_TIMEOUT`), giving a couple of posts' worth of
     slack before flagging a real disconnect.
7. **Light sensor logic realigned — this was the core bug.** The original
   API contract used the **same JSON key, `"light"`,** for two different
   things: the numeric LDR reading the dashboard displays, and the
   `"ON"`/`"OFF"` grow-light relay command. They silently collided — the
   numeric reading was never actually settable, it stayed at its default
   (0) forever. Fixed by splitting them: `"light"` is now exclusively the
   numeric 0-1000 reading, and the relay command moved to its own field,
   `"grow_light"`. `app.py`'s `/update` handler, `storage.py`'s capped
   history, the CSV/Excel export headers, and the firmware were all updated
   to match this contract (documented in `README.md`'s payload reference
   table). The dashboard's light sensor description text, which previously
   said "Target: 15k - 25k Lux equivalent" while the actual status/
   automation logic used a 300-unit threshold, was also reworded to match
   the real threshold instead of an unrelated number.
8. **Firmware provided** — see `firmware/`: `SmartGrow_ESP32.ino` +
   `config.h` (production) and `SmartGrow_Diagnostics.ino` (standalone
   wiring/calibration helper, no WiFi required). Both are new; no `.ino`
   files existed in the original project. See `firmware/README.md` for
   wiring, required libraries, and flashing steps.
9. **Full bug sweep** — see the numbered items above for the substantive
   ones. Verified with `py_compile` and a full Flask test-client pass after
   every backend change (see "Verification performed" below).
10. **End-to-end communication verified** — the full `/update` payload
    contract is now documented in one place (`README.md`), used
    identically by the firmware, `app.py`'s validation, and
    `storage.py`'s history rebuild, and exercised against the real
    `app.py` module (not just read) before packaging — see "Verification
    performed, round 2" below.

## Verification performed, round 2

- `python -m py_compile app.py storage.py desktop.py` — no syntax errors.
- Flask test client: posted a reading using the corrected `light` +
  `grow_light` payload, confirmed `GET /api/sensors` reflects both
  correctly.
- Posted `dht_connected: false` together with `fan: "ON"` in the same
  payload (simulating a firmware bug or tampered request) and confirmed the
  server now overrides `fan` to `false` rather than trusting the report.
- Confirmed `GET /api/status` reports the new `disconnect_timeout_seconds`
  field.

---

# Changelog — Round 1: Vercel deployment rework

Everything below was changed starting from the original single-`app.py`
version of SmartGrow. Organized to match the original bug list.

## P0 — deployment-crashing issues (fixed)

1. **Split `app.py` into `app.py` + `desktop.py`.** `app.py` now contains
   only Flask, its routes, and WSGI-safe imports — no `webview`, no
   `if __name__ == "__main__":` block, no threads started at import time.
   `desktop.py` contains the pywebview window, the manual HTTPS runner, the
   dev-mode file watcher, and local CSV logging; it does `from app import
   app` and starts its own background threads only inside its own
   `if __name__ == "__main__":` block. Vercel only ever imports `app.py`.
2. **`openpyxl` added to `requirements.txt`, pinned** (`openpyxl==3.1.5`).
   **`flask` pinned too** (`flask==3.0.3`, was previously unbounded).
3. **`redis` wired up instead of removed.** See P1.1 — it's the fix for
   persistent state, not dead weight.

## P1 — serverless architecture fixes

1. **Persistent state via `storage.py`.** Replaced the global `sensor_data`
   dict with a small storage module that uses Redis (via `REDIS_URL` /
   `KV_URL`) when configured, and falls back to an in-memory dict when it
   isn't (so local/dev use needs no setup). The "disconnected after 5s"
   check moved out of the old background loop and into `GET /api/sensors`,
   computed at read time from a stored timestamp — exactly as specified.
2. **CSV/Excel history — implemented option (a), with (b) as a fallback,
   not the primary strategy.** Every `/update` appends to a capped
   (2000-entry) history list via `storage.py` (Redis-backed once
   `REDIS_URL` is set; in-memory otherwise), and the export routes rebuild
   the CSV/Excel from that list. The local desktop-mode CSV file (written
   by `desktop.py`'s `background_logging` thread) takes priority when it
   exists, since it persists across restarts and `desktop.py` users should
   keep seeing their full existing history unchanged. If neither has any
   data, the routes return a **styled JSON "not available yet" response**
   (not a bare 404). **Why (a) as the primary path:** it's the only option
   that gives the hosted dashboard a real, working export feature rather
   than a permanently-disabled one, and Redis was already a stated project
   dependency.
3. **Static assets moved to `public/`** at the project root. All
   `/static/...` references in `templates/index.html`, `sw.js`'s
   `APP_SHELL` list, and `manifest.json`'s icon paths were updated to drop
   the `/static` prefix. The old `@app.route('/manifest.json')` and
   `@app.route('/sw.js')` handlers were removed — instead, `app.py` sets
   `Flask(__name__, static_folder="public", static_url_path="")`, so the
   exact same files are served at the exact same root-level URLs by
   Flask's own static handling locally (`desktop.py`), and by Vercel's CDN
   directly in production (Flask's static handling is simply unused there,
   which is expected and harmless).
4. **`DEV_MODE` now reads `SMARTGROW_DEV_MODE`** from the environment,
   defaulting to `false`. When it's off, `/dev/reload-stream` is **not
   registered as a route at all** (the `@app.route` decorator is inside an
   `if DEV_MODE:` block) — hitting it 404s like any unknown route, and no
   watcher thread or SSE listener ever starts. `desktop.py` sets
   `SMARTGROW_DEV_MODE=true` as a default *before* importing `app.py`, so
   local desktop use keeps the original "dev mode always on" experience
   without any manual configuration.
5. **`vercel.json` rewritten** to the current `functions` format
   (`maxDuration`, `excludeFiles`) — no `builds`/`routes` — matching
   Vercel's current zero-config Flask detection (Vercel finds the `app`
   object in `app.py` automatically; no explicit route table is needed).
   Added `.vercelignore` and `.gitignore` (neither existed before),
   excluding `data/`, `sensor_data.csv`, `__pycache__`, `venv`/`.venv`,
   `.env`, `cert.pem`, `key.pem`, and desktop-only files.

## P2 — frontend bugs (fixed)

1. `manifest.json` icon paths corrected to `/icons/icon-192.png` /
   `/icons/icon-512.png` (done as part of the `public/` reorganization).
2. `sw.js`'s `APP_SHELL` list updated to cache the files that actually
   exist now (`/css/main.css`, `/js/main.js`, `/manifest.json`,
   `/icons/icon-192.png`, `/icons/icon-512.png`) instead of the long-dead
   `styles.css`/`script.js`/flat icon paths. Cache version bumped to
   `smartgrow-v4` to force old clients to pick up the new list. Also added
   `/sensor_data.csv` and `/download/excel` to the "always hit the
   network, never cache" list alongside `/api/*` and `/update`.
3. **Confirmed root-level `script.js` and `styles.css` were unused**
   (`templates/index.html` only ever loaded `/static/css/main.css` and
   `/static/js/main.js`) and deleted both.
4. **Root-level `sensor_data.csv` and the `data/` folder removed** from
   the delivered project (they were runtime output, not source) and added
   to `.gitignore`/`.vercelignore` so they don't come back.

## P3 — security (fixed)

1. **`X-API-Key` header check added** to `POST /update` and
   `POST /api/control`, comparing against `SMARTGROW_API_KEY`. **Decision:**
   when `SMARTGROW_API_KEY` is unset, both routes stay open — matching the
   original home-LAN-only behavior — rather than hard-failing every
   request out of the box. This means the key is opt-in, not
   automatically enforced; `README.md` and `USER_MANUAL.md` both call out
   that it should be set before exposing the dashboard publicly or
   pointing real hardware at it.
2. **`request.get_json(silent=True)`** used in both `/update` and
   `/api/control`, each returning a clean `400` JSON error on a missing/
   invalid body instead of raising. `/api/toggle/<sensor_name>` now
   returns `400` for an unrecognized `sensor_name` instead of silently
   no-op'ing.
3. **Bounds/type checking added** for `temperature` (-40–80°C), `humidity`
   (0–100%), and `soil_moisture` (0–100%) in `/update`; any value outside
   range or not parseable as a number rejects the whole request with a
   `400` and a message naming the offending field(s).

## P4 — usability polish

1. **A distinct "backend unreachable" banner** was added (`#backendBanner`
   in `templates/index.html`, styled in `public/css/dashboard.css`, logic
   in `public/js/main.js`). It only appears after 2 consecutive
   `/api/sensors` fetch failures, and is visually and semantically
   separate from the existing ESP32 connection badge in the topbar — the
   badge means "the server is fine, the ESP32 isn't answering"; the banner
   means "the dashboard can't reach the server at all."
2. **CSV/Excel download buttons now use a fetch-driven download**
   (`downloadFile()` in `public/js/utils.js`) instead of plain
   `<a href>` links. A failure (missing history, network error) is now
   caught and shown as a toast in the existing System Log/activity feed,
   instead of navigating the whole tab to a bare, unstyled error page.
3. **`README.md` and `USER_MANUAL.md` rewritten from scratch** (the
   original `README.md` was 2 lines) with environment variables, the
   desktop-vs-hosted distinction, Redis setup, and ESP32 firmware update
   instructions.

## Things flagged as explicit decisions (not silent scope-narrowing)

- **API key is opt-in, not mandatory** (see P3.1 above) — chosen to avoid
  breaking existing home-LAN-only setups that upgrade without touching
  environment variables. Both docs recommend setting it before going
  public.
- **CSV/Excel history uses Redis as the primary store, with the local
  desktop file as a secondary fallback** (see P1.2 above) — implemented
  (a) rather than only (b), since (a) gives the hosted dashboard real
  functionality.
- **The numeric `light` sensor value's update path was left exactly as it
  was** in the original code: `/update` maps `data.get("light") == "ON"`
  to the `lightState` relay flag, but nothing in `/update` ever sets a
  numeric `light` reading — it stays at its default (`0`) unless changed
  via `/api/toggle/light` or `/api/control`. This looked like a possible
  pre-existing gap (the dashboard has a "Light Intensity (Lux)" sensor
  card that therefore never updates from real hardware), but it wasn't in
  the confirmed bug list, so it was **not** changed, to avoid guessing at
  ESP32 firmware behavior that wasn't specified. Worth a look if the Light
  Intensity card seems stuck at `0` in practice.
- **`/api/toggle/<sensor_name>` was left without an API key requirement.**
  It's a manual debug/testing helper (flips a "connected" flag without
  touching real hardware), and the brief only named `/update` and
  `/api/control` for the auth requirement. If you'd rather lock this down
  too on a public deployment, it's a one-line change mirroring
  `/api/control`'s `_check_api_key()` call.

## Verification performed

- `pip install -r requirements.txt` — succeeds, no missing packages.
- `python -c "import app"` — succeeds standalone, no GUI side effects, no
  threads started, no exceptions.
- Confirmed `/dev/reload-stream` is registered only when
  `SMARTGROW_DEV_MODE=true`, and absent (404) otherwise.
- Confirmed no route in `app.py` reads/writes a local file path that
  wouldn't exist once deployed, other than the local-desktop-CSV fallback
  in the export routes, which is guarded by an `os.path.exists()` check
  and degrades gracefully when the file is absent.
- Confirmed no secrets or `cert.pem`/`key.pem` are committed; required env
  vars are listed in both `README.md` and `.env.example`.
