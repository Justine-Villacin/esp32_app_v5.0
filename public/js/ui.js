// Automation cards show one of three states, not two:
//   ACTIVE   - the actuator is genuinely running
//   STANDBY  - its sensor is fine, conditions just don't call for it
//   OFFLINE  - the sensor it depends on is disconnected, so the relay is
//              forced off for safety and the card says so explicitly.
// Previously there was no OFFLINE state at all, which is why the pump/
// grow-light/fan cards could keep showing ACTIVE (or at least never say
// anything was wrong) even while the sensor driving that decision was
// disconnected.
export function updateAutomationUI(data) {
    // --- Pump (depends on the soil sensor) ---
    const pumpCard = document.getElementById("autoWateringCard");
    pumpCard.classList.remove("active-pump", "offline");
    if (!data.soilConnected) {
        pumpCard.classList.add("offline");
        document.getElementById("pumpTitle").textContent = "Auto-Watering: OFFLINE";
        document.getElementById("pumpDesc").textContent = "Soil sensor disconnected — pump disabled for safety.";
    } else if (data.pump) {
        pumpCard.classList.add("active-pump");
        document.getElementById("pumpTitle").textContent = "Auto-Watering: ACTIVE";
        document.getElementById("pumpDesc").textContent = "Soil is dry (<=40%). Pumping water.";
    } else {
        document.getElementById("pumpTitle").textContent = "Auto-Watering: Standby";
        document.getElementById("pumpDesc").textContent = "Soil moisture is optimal.";
    }

    // --- Grow light (depends on the light sensor) ---
    const lightCard = document.getElementById("autoLightCard");
    lightCard.classList.remove("active-light", "offline");
    if (!data.lightConnected) {
        lightCard.classList.add("offline");
        document.getElementById("lightTitle").textContent = "Grow Light: OFFLINE";
        document.getElementById("lightDesc").textContent = "Light sensor disconnected — grow light disabled for safety.";
    } else if (data.lightState) {
        lightCard.classList.add("active-light");
        document.getElementById("lightTitle").textContent = "Grow Light: ACTIVE";
        document.getElementById("lightDesc").textContent = "Low light detected. Supplementing.";
    } else {
        document.getElementById("lightTitle").textContent = "Grow Light: Standby";
        document.getElementById("lightDesc").textContent = "Natural light is sufficient.";
    }

    // --- Exhaust fan (depends on the DHT temperature sensor) ---
    const fanCard = document.getElementById("autoFanCard");
    fanCard.classList.remove("active-fan", "offline");
    if (!data.dhtConnected) {
        fanCard.classList.add("offline");
        document.getElementById("fanTitle").textContent = "Exhaust Fan: OFFLINE";
        document.getElementById("fanDesc").textContent = "Temperature sensor disconnected — fan disabled for safety.";
    } else if (data.fan) {
        fanCard.classList.add("active-fan");
        document.getElementById("fanTitle").textContent = "Exhaust Fan: ACTIVE";
        document.getElementById("fanDesc").textContent = "High temp (>=30°C). Cooling down.";
    } else {
        document.getElementById("fanTitle").textContent = "Exhaust Fan: Standby";
        document.getElementById("fanDesc").textContent = "Temperature is optimal.";
    }
}

// Toggles a visible "offline" treatment on a sensor card (dashed border,
// dimmed icon/value — see .sensor.offline in public/css/sensors.css) in
// addition to the small status chip text. The chip alone was easy to miss;
// this makes a disconnected sensor unmistakable at a glance.
function setSensorOffline(cardSelector, isOffline) {
    const card = document.querySelector(cardSelector);
    if (card) card.classList.toggle("offline", isOffline);
}

export function updateSensorUI(data) {
    const tempEl = document.getElementById("temperature");
    const tempStatus = document.getElementById("temperatureStatus");
    const humEl = document.getElementById("humidity");
    const humStatus = document.getElementById("humidityStatus");

    setSensorOffline(".sensor.temperature", !data.dhtConnected);
    setSensorOffline(".sensor.humidity", !data.dhtConnected);

    if (tempEl && humEl) {
        if (!data.dhtConnected) {
            tempEl.textContent = "--";
            tempStatus.textContent = "OFFLINE";
            tempStatus.style.color = "var(--danger)";
            humEl.textContent = "--";
            humStatus.textContent = "OFFLINE";
            humStatus.style.color = "var(--danger)";
        } else {
            tempEl.textContent = Number(data.temp).toFixed(1);
            tempStatus.textContent = data.temp >= 30 ? "WARNING" : "NORMAL";
            tempStatus.style.color = data.temp >= 30 ? "var(--danger)" : "var(--primary)";
            humEl.textContent = Math.round(data.hum);
            humStatus.textContent = data.hum >= 60 && data.hum <= 80 ? "NORMAL" : "WARNING";
            humStatus.style.color = data.hum >= 60 && data.hum <= 80 ? "var(--primary)" : "var(--warning)";
        }
    }

    const soilEl = document.getElementById("soil");
    const soilStatus = document.getElementById("soilStatus");
    setSensorOffline(".sensor.soil", !data.soilConnected);
    if (soilEl) {
        if (!data.soilConnected) {
            soilEl.textContent = "--";
            soilStatus.textContent = "OFFLINE";
            soilStatus.style.color = "var(--danger)";
        } else {
            soilEl.textContent = Math.round(data.soil);
            soilStatus.textContent = data.soil <= 40 ? "LOW" : "GOOD";
            soilStatus.style.color = data.soil <= 40 ? "var(--danger)" : "var(--primary)";
        }
    }

    const lightEl = document.getElementById("light");
    const lightStatus = document.getElementById("lightStatus");
    setSensorOffline(".sensor.light", !data.lightConnected);
    if (lightEl) {
        if (!data.lightConnected) {
            lightEl.textContent = "--";
            lightStatus.textContent = "OFFLINE";
            lightStatus.style.color = "var(--danger)";
        } else {
            lightEl.textContent = Math.round(data.light);
            lightStatus.textContent = data.light >= 300 ? "OPTIMAL" : "LOW";
            // Was previously inverted: OPTIMAL rendered in the warning
            // (orange) color and LOW in danger (red) — the opposite of
            // every other sensor card's good/bad color convention.
            lightStatus.style.color = data.light >= 300 ? "var(--primary)" : "var(--danger)";
        }
    }

    // ==========================================
    // DYNAMIC PLANT HEALTH SCORING
    // ==========================================
    // A disconnected sensor is scored as being AT LEAST as bad as its worst
    // known reading, not as a free pass (0 deduction). We don't know what's
    // really happening without the sensor, so the score should reflect
    // that uncertainty rather than silently looking healthier than it is.
    let score = 100;
    let tempStatusText = "Optimal";
    let humStatusText = "Optimal";
    let soilStatusText = "Moist";
    let lightStatusText = "Optimal";

    // 1. Temperature Check (Target: < 30C)
    if (data.dhtConnected) {
        if (data.temp >= 30) {
            score -= 25;
            tempStatusText = "Too Hot";
        } else if (data.temp < 15) {
            score -= 15;
            tempStatusText = "Too Cold";
        }
    } else {
        score -= 25;
        tempStatusText = "No Data";
    }

    // 2. Humidity Check (Target: 60% - 80%)
    if (data.dhtConnected) {
        if (data.hum > 85) {
            score -= 15;
            humStatusText = "Too Humid";
        } else if (data.hum < 50) {
            score -= 20;
            humStatusText = "Dry Air";
        }
    } else {
        score -= 20;
        humStatusText = "No Data";
    }

    // 3. Soil Moisture Check (Target: > 40%)
    if (data.soilConnected) {
        if (data.soil <= 40) {
            score -= 35;
            soilStatusText = "Dry";
        } else if (data.soil > 85) {
            score -= 10;
            soilStatusText = "Too Wet";
        }
    } else {
        score -= 35;
        soilStatusText = "No Data";
    }

    // 4. Light Check (Target: >= 300, matches the automation threshold)
    if (data.lightConnected) {
        if (data.light < 300) {
            score -= 25;
            lightStatusText = "Low";
        }
    } else {
        score -= 25;
        lightStatusText = "No Data";
    }

    score = Math.max(0, score);

    // Update UI Elements
    const healthScoreEl = document.getElementById("healthScore");
    const healthProgressEl = document.getElementById("healthProgress");

    if (healthScoreEl) {
        healthScoreEl.textContent = `${score}%`;
        if (score >= 80) healthScoreEl.style.color = "var(--primary)";
        else if (score >= 50) healthScoreEl.style.color = "var(--warning)";
        else healthScoreEl.style.color = "var(--danger)";
    }

    if (healthProgressEl) {
        healthProgressEl.style.width = `${score}%`;

        if (score >= 80) {
            healthProgressEl.style.background = "var(--primary)";
            healthProgressEl.style.boxShadow = "0 0 10px var(--primary-glow)";
        } else if (score >= 50) {
            healthProgressEl.style.background = "var(--warning)";
            healthProgressEl.style.boxShadow = "0 0 10px var(--warning-glow)";
        } else {
            healthProgressEl.style.background = "var(--danger)";
            healthProgressEl.style.boxShadow = "0 0 10px var(--danger-glow)";
        }
    }

    // Helper to update specific plant health metrics
    function setMetricUI(id, text) {
        const el = document.getElementById(id);
        if (!el) return;
        el.textContent = text;

        el.className = "metric-value";
        if (text === "Optimal" || text === "Moist") {
            el.classList.add("good");
        } else if (text === "Low" || text === "Too Wet" || text === "Dry Air" || text === "Too Cold") {
            el.classList.add("warning");
        } else if (text === "Too Hot" || text === "Dry" || text === "Too Humid" || text === "No Data") {
            el.classList.add("danger");
        }
    }

    setMetricUI("tempHealth", tempStatusText);
    setMetricUI("humidityHealth", humStatusText);
    setMetricUI("soilHealth", soilStatusText);
    setMetricUI("lightHealth", lightStatusText);
}
