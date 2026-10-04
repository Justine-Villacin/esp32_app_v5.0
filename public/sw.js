// SmartGrow Service Worker
//
// A registered, working service worker + a valid manifest.json served over
// HTTPS are the two things Chrome/Edge/Samsung Internet require before they
// will treat this site as an installable PWA and fire the automatic
// "Add to Home screen" prompt.

const CACHE_NAME = 'smartgrow-v4';

// Files that make up the app "shell" — cached so the dashboard still opens
// even with a flaky Wi-Fi link. Paths match the public/ layout served at
// the site root (no "/static" prefix — see README.md for why).
const APP_SHELL = [
    '/',
    '/css/main.css',
    '/js/main.js',
    '/manifest.json',
    '/icons/icon-192.png',
    '/icons/icon-512.png'
];

self.addEventListener('install', (event) => {
    console.log('[Service Worker] Installing...');
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => {
            // cache.add() one-by-one (not cache.addAll) so ONE missing file
            // can't abort the whole install and silently break installability.
            return Promise.all(
                APP_SHELL.map((url) =>
                    cache.add(url).catch((err) => console.warn('[Service Worker] Could not cache', url, err))
                )
            );
        }).then(() => self.skipWaiting())
    );
});

self.addEventListener('activate', (event) => {
    console.log('[Service Worker] Activated');
    event.waitUntil(
        caches.keys()
            .then((keys) => Promise.all(
                keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))
            ))
            .then(() => clients.claim())
    );
});

self.addEventListener('fetch', (event) => {
    const url = new URL(event.request.url);

    // Dev-only live-reload stream: never intercept this. Not calling
    // respondWith() means the browser handles the request natively, which is
    // required for a long-lived Server-Sent-Events connection to work.
    if (url.pathname === '/dev/reload-stream') {
        return;
    }

    // Live sensor data, control, and export endpoints must always hit the
    // network — never serve stale sensor readings or exports from cache,
    // and never crash if offline (the old handler's catch() returned
    // nothing, which throws).
    if (url.pathname.startsWith('/api/') || url.pathname === '/update'
        || url.pathname === '/sensor_data.csv' || url.pathname === '/download/excel') {
        event.respondWith(
            fetch(event.request).catch(() =>
                new Response(JSON.stringify({ status: 'error', message: 'offline' }), {
                    status: 503,
                    headers: { 'Content-Type': 'application/json' }
                })
            )
        );
        return;
    }

    // App shell / static assets: network-first (so you get the latest
    // dashboard while on Wi-Fi), falling back to cache when offline.
    event.respondWith(
        fetch(event.request)
            .then((response) => {
                const responseClone = response.clone();
                caches.open(CACHE_NAME).then((cache) => cache.put(event.request, responseClone));
                return response;
            })
            .catch(() => caches.match(event.request))
    );
});
