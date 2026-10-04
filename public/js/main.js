import { updateDateTime } from './utils.js';
import { updateSensorUI, updateAutomationUI } from './ui.js';
import './pwa.js'; // Imports and runs PWA logic automatically
import './navigation.js'; // Imports and runs Navigation logic automatically

// Initialize date and time
updateDateTime();
setInterval(updateDateTime, 60000);

// Consecutive /api/sensors failures before we show the "backend unreachable"
// banner. This is a different failure mode from "ESP32 disconnected" (which
// the topbar badge already shows) — this means the dashboard itself can't
// reach the Flask backend at all (server down, network issue, etc).
const BACKEND_FAILURE_THRESHOLD = 2;
let consecutiveFailures = 0;

function showBackendBanner(message) {
    const banner = document.getElementById("backendBanner");
    const text = document.getElementById("backendBannerText");
    if (!banner) return;
    if (text) text.textContent = message;
    banner.style.display = "flex";
}

function hideBackendBanner() {
    const banner = document.getElementById("backendBanner");
    if (banner) banner.style.display = "none";
}

function fetchESP32Data() {
    fetch('/api/sensors')
        .then(response => {
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            return response.json();
        })
        .then(data => {
            consecutiveFailures = 0;
            hideBackendBanner();

            // Update the UI components
            updateSensorUI(data);
            updateAutomationUI(data);

            const badge = document.getElementById("espBadge");
            const dot = document.getElementById("espDot");
            const text = document.getElementById("espText");

            // Handle ESP32 Connection Status
            if (data.esp32Connected) {
                text.textContent = "ESP32 CONNECTED";
                badge.style.color = "var(--primary)";
                dot.style.background = "var(--primary)";
                dot.style.boxShadow = "0 0 10px var(--primary)";
                dot.style.animation = "pulse 2s infinite";
            } else {
                text.textContent = "ESP32 DISCONNECTED";
                badge.style.color = "var(--danger)";
                dot.style.background = "var(--danger)";
                dot.style.boxShadow = "none";
                dot.style.animation = "none";
            }

            // Update footer timestamp
            const time = new Date().toLocaleTimeString("en-US", { hour: "numeric", minute: "2-digit" });
            document.getElementById("footerStatus").textContent = `System online • Last sync at ${time}`;

            // NOTE: The daily averages live calculation block was removed
            // to match the updated HTML structure.
        })
        .catch(err => {
            console.error("Connection error:", err);
            consecutiveFailures += 1;

            const footerStatus = document.getElementById("footerStatus");
            if (footerStatus) {
                footerStatus.textContent = "Connection lost";
            }

            if (consecutiveFailures >= BACKEND_FAILURE_THRESHOLD) {
                showBackendBanner("Backend unreachable — the dashboard can't reach the server. Retrying...");
            }
        });
}

// Initial fetch and start polling every 2 seconds
fetchESP32Data();
setInterval(fetchESP32Data, 2000);
