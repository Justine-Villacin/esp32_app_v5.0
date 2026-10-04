# SmartGrow User Manual

A plain-language walkthrough for getting SmartGrow running, whether you
just want it on your PC talking to your ESP32, or you want a public link
you can check from your phone anywhere.

You do **not** need to read `README.md` to follow this — this manual
stands on its own. `README.md` has more technical detail if you want it
later.

---

## Part 0 — Flashing your ESP32 (do this first)

Before either part below, your ESP32 needs the SmartGrow firmware on it.

1. Install the **Arduino IDE** ([arduino.cc/en/software](https://www.arduino.cc/en/software)) if you don't have it.
2. In the IDE, go to **Tools → Board → Boards Manager**, search "esp32", and install the one by **Espressif Systems**.
3. Go to **Tools → Manage Libraries** and install:
   - **DHT sensor library** (by Adafruit) — say yes when it offers to also install "Adafruit Unified Sensor"
   - **ArduinoJson** (by Benoit Blanchon) — pick **version 7.x**
4. Wire up your ESP32, DHT22, soil sensor, LDR, and relay module following the pin table in `firmware/README.md`.
5. Open `firmware/SmartGrow_Diagnostics/SmartGrow_Diagnostics.ino`, select your board/port under **Tools**, and click **Upload**.
6. Open **Tools → Serial Monitor**, set the speed to **115200**. You should see temperature/humidity readings, and two raw numbers for the soil sensor and the light sensor updating every 2 seconds.
   - Note the soil sensor's number when dry, and again with the probe in a cup of water.
   - Note the LDR's number when covered, and again under a bright light (a phone flashlight works).
   - Type `p1`, `l1`, `f1` into the Serial Monitor one at a time and confirm each relay clicks and controls the right device; `p0`/`l0`/`f0` turn them back off.
7. Open `firmware/SmartGrow_ESP32/SmartGrow_ESP32.ino` — `config.h` will open alongside it as a second tab. Fill in:
   - Your WiFi name and password
   - The 4 numbers you just noted (`SOIL_RAW_DRY`, `SOIL_RAW_WET`, `LDR_RAW_DARK`, `LDR_RAW_BRIGHT`)
   - The server address — see Part A or Part B below for what to put here
8. Click **Upload**. Reopen the Serial Monitor to confirm it connects to WiFi and starts posting readings.

With that done, continue to Part A (PC) or Part B (public link) below.

---

## Part A — Running it on your PC (talks to your real ESP32)

This is the "desktop app" mode: a window opens on your computer showing
the dashboard, and it talks directly to your ESP32 over your home Wi-Fi.

### What you need first

- Python 3.10 or newer installed on your PC
  ([python.org/downloads](https://www.python.org/downloads/))
- The project folder (this one) saved somewhere on your PC
- Your ESP32 flashed and connected to the same Wi-Fi network as your PC

### Steps

1. **Open a terminal / command prompt** in this project folder.

2. **Create a virtual environment** (keeps this project's Python packages
   separate from everything else on your PC):

   ```bash
   python -m venv .venv
   ```

3. **Activate it:**

   - Windows: `.venv\Scripts\activate`
   - Mac/Linux: `source .venv/bin/activate`

   You'll know it worked if your terminal prompt now starts with `(.venv)`.

4. **Install the required packages:**

   ```bash
   pip install -r requirements-desktop.txt
   ```

5. **Run it:**

   ```bash
   python desktop.py
   ```

   A window titled "Automated ESP32 Greenhouse for Pechay" should open.
   That's the dashboard. It also starts logging readings to a file called
   `sensor_data.csv` in this same folder every 2 seconds.

6. **Point your ESP32 at your PC.** In `firmware/SmartGrow_ESP32/config.h`,
   set:

   ```cpp
   #define SERVER_USE_HTTPS   false
   #define SERVER_HOST        "192.168.1.100"   // your PC's IP — see below
   #define SERVER_PORT        8000
   ```

   To find your PC's IP address:

   - Windows: open Command Prompt, run `ipconfig`, look for "IPv4 Address".
   - Mac: System Settings → Wi-Fi → Details → look for the IP address.
   - Linux: run `hostname -I`.

   Re-upload the firmware with the updated address, and readings should
   start appearing on the dashboard within a couple of seconds.

### Optional: install it on your phone as an app

By default, phones can only be offered the "Add to Home Screen" install
prompt over a secure (HTTPS) connection. To enable that:

1. Get a `cert.pem` and `key.pem` file (a free option is
   [mkcert](https://github.com/FiloSottile/mkcert) — search "mkcert
   quickstart" for a copy-pasteable set of commands for your OS).
2. Put both files directly in this project folder, next to `desktop.py`.
3. Restart `python desktop.py`. You should see a message in the terminal
   confirming HTTPS is enabled on port 8443.
4. On your phone (same Wi-Fi network), open
   `https://<your-PC's-IP>:8443` in Chrome or Safari.
5. Tap the **Install App** button in the dashboard, or (on iPhone) use the
   Share icon → "Add to Home Screen".

If you skip this, everything else still works — you just won't get the
one-tap install prompt on phones.

### Downloading your sensor history

Open the **Export** tab (bottom navigation) and tap **CSV** or **EXCEL** to
download the full history collected so far. If nothing's been logged yet,
you'll see a message in the System Log panel instead of a broken page.

---

## Part B — Putting it on the internet (a public link)

This is for when you want to check your greenhouse from anywhere, not just
your home Wi-Fi — for example, from work or while traveling. This part
uses [Vercel](https://vercel.com), a free hosting service, and does not
require your PC to stay on.

### What you need first

- A free [Vercel account](https://vercel.com/signup)
- A free [GitHub account](https://github.com/signup) (or GitLab/Bitbucket)
  to hold a copy of this project

### Steps

1. **Put this project on GitHub.** If you're not sure how, GitHub's
   [quickstart guide](https://docs.github.com/en/get-started/quickstart)
   walks through creating a repository and uploading files from the
   website — you don't need to use the command line for this part if you
   don't want to.

2. **Import it into Vercel:**
   - Go to [vercel.com/new](https://vercel.com/new)
   - Choose "Import Git Repository" and select the repo you just created
   - Vercel will detect it's a Flask app automatically — you don't need to
     change any build settings
   - Click **Deploy**

3. **Add a Redis storage add-on** (recommended — without this, your
   readings will keep "resetting" and history exports won't work):
   - In your new Vercel project, open the **Storage** tab
   - Click **Browse Marketplace**, search for **Redis** (Upstash Redis is
     the common option), and add it to your project
   - Vercel will automatically add a connection-string environment
     variable to your project (usually called `REDIS_URL` or `KV_URL` —
     either name works with this app)

4. **Set a security key** (recommended before sharing the link with
   anyone, or pointing real hardware at it):
   - In your project, go to **Settings → Environment Variables**
   - Add a new variable named `SMARTGROW_API_KEY`
   - For the value, use any long random password-like string (a password
     manager's "generate password" feature works well for this)
   - Save it, then in `firmware/SmartGrow_ESP32/config.h` set
     `#define API_KEY "<the same value you just set>"`, and set
     `SERVER_USE_HTTPS` to `true` and `SERVER_HOST` to your
     `your-project-name.vercel.app` domain (no `https://` prefix, no
     trailing slash). Re-upload the firmware.

5. **Redeploy** (Vercel → Deployments → click the "..." menu on the latest
   deployment → **Redeploy**) so the new environment variables take
   effect.

6. **Visit your link.** Vercel gives you a URL like
   `https://your-project-name.vercel.app` — that's your public dashboard.

### Checking that everything's wired up correctly

Visit `https://your-project-name.vercel.app/api/status` in a browser. It
shows a small JSON summary:

```json
{
  "storage_backend": "redis",
  "dev_mode": false,
  "api_key_required": true,
  "esp32_connected": false
}
```

- `storage_backend` should say `"redis"` (not `"memory"`) if you set up
  the Redis add-on correctly.
- `api_key_required` should say `true` if you set `SMARTGROW_API_KEY`.
- `esp32_connected` will say `true` once your ESP32 has sent a reading in
  the last 5 seconds.

### Troubleshooting

| Problem | Likely cause |
|---|---|
| Dashboard shows "ESP32 DISCONNECTED" all the time | Your ESP32 hasn't been pointed at the new public URL, or it's missing the `X-API-Key` header — check the Serial Monitor for `401 Unauthorized` |
| A sensor card shows a dashed red "OFFLINE" state | That specific sensor's reading is failing (DHT read error, or an analog sensor stuck at 0 or max) — check its wiring, or run `SmartGrow_Diagnostics.ino` again |
| A pump/light/fan card shows "OFFLINE" instead of Standby/Active | Its sensor is disconnected (see above) — the relay is deliberately forced off until the sensor comes back |
| A red banner says "Backend unreachable" | The dashboard itself can't reach the server — check your own internet connection first, then check Vercel's status page |
| `/api/status` shows `"storage_backend": "memory"` | The Redis add-on isn't connected — double-check the env var name/value and redeploy |
| CSV/Excel export says "no data available yet" | No readings have come in since Redis was connected — this is expected right after setup |
| ESP32 keeps disconnecting/reconnecting | Check Serial Monitor for WiFi signal issues; try moving the ESP32 closer to your router, or raise `SMARTGROW_DISCONNECT_TIMEOUT` on the server if it's just borderline-slow WiFi (see `README.md`) |

---

Questions or something not covered here? Check `README.md` for the more
technical version of this same setup, or `CHANGELOG.md` for a full list of
everything that changed from the original single-file version.
