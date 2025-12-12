const API_BASE = '';
// DOM helpers
const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelector(sel);
const doc = document;
// Translations
let currentLang = 'en';
let currentTheme = 'dark';

const EVENT_SAVE_DEBOUNCE_MS = 400;
const DEFAULT_EFFECT_DEBOUNCE_MS = 400;
const AUDIO_SAVE_DEBOUNCE_MS = 400;
const HARDWARE_SAVE_DEBOUNCE_MS = 400;

const OTA_STATE_KEYS = {
    0: 'idle',
    1: 'receiving',
    2: 'writing',
    3: 'success',
    4: 'error'
};
function getOtaStateKey(state) {
    if (state === undefined || state === null) {
        return 'idle';
    }
    const numericState = Number(state);
    return OTA_STATE_KEYS.hasOwnProperty(numericState) ? OTA_STATE_KEYS[numericState] : 'idle';
}
const BLE_CONFIG = {
    serviceUuid: '4fafc201-1fb5-459e-8fcc-c5c9c331914b',
    commandCharacteristicUuid: 'beb5483e-36e1-4688-b7f5-ea07361b26a8',
    responseCharacteristicUuid: '64a0990c-52eb-4c1b-aa30-ea826f4ba9dc',
    maxChunkSize: 180,
    responseTimeoutMs: 8000,
    deviceName: 'CarLightSync'
};
const usingFileProtocol = window.location.protocol === 'file:';
const usingCapacitor = window.Capacitor !== undefined;
const FALLBACK_ORIGIN = (!usingFileProtocol && window.location.origin && window.location.origin !== 'null')
    ? window.location.origin
    : 'http://localhost';
let bleTransportInstance = null;
let bleAutoConnectInProgress = false;
let bleAutoConnectGestureCaptured = false;
let bleAutoConnectGestureHandlerRegistered = false;
let bleAutoConnectAwaitingGesture = false;
let wifiOnline = !usingFileProtocol && !usingCapacitor && navigator.onLine;
let apiConnectionReady = wifiOnline;
let apiConnectionResolvers = [];
let initialDataLoaded = false;
let initialDataLoadPromise = null;
let activeTabName = 'vehicle';
let statusIntervalHandle = null;
let isApMode = false; // Mode AP de l'ESP32 (plus lent)
let maxLeds = 300; // Par défaut, sera mis à jour depuis /api/config
function isApiConnectionReady() {
    return wifiOnline || (bleTransportInstance && bleTransportInstance.isConnected());
}
function updateApiConnectionState() {
    const ready = isApiConnectionReady();
    if (ready && !apiConnectionReady) {
        apiConnectionReady = true;
        const resolvers = [...apiConnectionResolvers];
        apiConnectionResolvers = [];
        resolvers.forEach(resolve => resolve());
        if (!initialDataLoaded) {
            scheduleInitialDataLoad();
        }
    } else if (!ready) {
        apiConnectionReady = false;
    }
    if (!ready) {
        maybeAutoConnectBle();
    }
}
function waitForApiConnection() {
    if (isApiConnectionReady()) {
        apiConnectionReady = true;
        return Promise.resolve();
    }
    return new Promise(resolve => {
        apiConnectionResolvers.push(resolve);
    });
}
function registerBleAutoConnectGestureHandler() {
    if (bleAutoConnectGestureHandlerRegistered) {
        return;
    }
    bleAutoConnectGestureHandlerRegistered = true;
    // Mettre à jour l'overlay de chargement pour afficher le message BLE et le bouton
    updateLoadingProgress(0, t('ble.tapToAuthorize'));
    updateLoadingBleButton();

    const unlock = () => {
        doc.removeEventListener('pointerdown', unlock);
        doc.removeEventListener('keydown', unlock);
        bleAutoConnectGestureHandlerRegistered = false;
        bleAutoConnectGestureCaptured = true;
        bleAutoConnectAwaitingGesture = false;

        // Feedback immédiat : changer le message et masquer le bouton
        updateLoadingProgress(0, t('ble.connecting'));
        const bleButton = $('loading-ble-button');
        if (bleButton) {
            bleButton.style.display = 'none';
        }

        if (!wifiOnline) {
            maybeAutoConnectBle(true);
        }
    };
    doc.addEventListener('pointerdown', unlock, { once: true });
    doc.addEventListener('keydown', unlock, { once: true });
}
function maybeAutoConnectBle(fromGesture = false) {
    if (!bleTransport.isSupported()) {
        bleAutoConnectAwaitingGesture = false;
        return;
    }
    if (wifiOnline) {
        bleAutoConnectAwaitingGesture = false;
        return;
    }
    const status = bleTransport.getStatus();
    if (status === 'connected' || status === 'connecting') {
        bleAutoConnectAwaitingGesture = false;
        return;
    }
    if (!fromGesture && !bleAutoConnectGestureCaptured) {
        bleAutoConnectAwaitingGesture = true;
        registerBleAutoConnectGestureHandler();
        return;
    }
    if (bleAutoConnectInProgress) {
        return;
    }
    bleAutoConnectAwaitingGesture = false;
    bleAutoConnectInProgress = true;

    // Feedback immédiat : la connexion démarre
    updateLoadingProgress(0, t('ble.connecting'));
    const bleButton = $('loading-ble-button');
    if (bleButton) {
        bleButton.style.display = 'none';
    }

    bleTransport.connect().catch(error => {
        if (error && (error.n === 'SecurityError' || /SecurityError/i.test(error.message || ''))) {
            console.warn('BLE auto-connect blocked by browser security, waiting for interaction');
            bleAutoConnectGestureCaptured = false;
            bleAutoConnectAwaitingGesture = true;
            registerBleAutoConnectGestureHandler();
        } else if (error && (error.name === 'NotFoundError' || /User cancelled/i.test(error.message || ''))) {
            console.warn('BLE connection cancelled by user');
            // L'utilisateur a annulé - afficher le bouton pour lui permettre de réessayer
            bleAutoConnectGestureCaptured = true;
            updateLoadingProgress(0, t('loading.connecting'));
            updateLoadingBleButton();
        } else {
            console.warn('BLE auto-connect failed', error);
            // Autres erreurs - afficher aussi le bouton
            updateLoadingBleButton();
        }
    }).finally(() => {
        bleAutoConnectInProgress = false;
        updateLoadingBleButton();
    });
}
function scheduleInitialDataLoad() {
    if (initialDataLoaded || initialDataLoadPromise) {
        return;
    }
    initialDataLoadPromise = (async () => {
        try {
            await waitForApiConnection();
            await loadInitialData();
            initialDataLoaded = true;
        } catch (error) {
            console.error('Initial data load failed:', error);
            initialDataLoadPromise = null;
        }
    })();
}
window.addEventListener('online', () => {
    wifiOnline = !usingFileProtocol && !usingCapacitor && navigator.onLine;
    updateApiConnectionState();
});
window.addEventListener('offline', () => {
    wifiOnline = false;
    updateApiConnectionState();
});
const bleTextEncoder = new TextEncoder();
const bleTextDecoder = new TextDecoder();
const BLE_BUTTON_ICONS = {
    connect: '🔗',
    disconnect: '⛓️‍💥',
    connecting: '⏳',
    unsupported: '⚠️'
};
function normalizeUrl(url) {
    if (url instanceof URL) {
        return url;
    }
    try {
        if (typeof url === 'string' && !url.match(/^[a-zA-Z][a-zA-Z0-9+.-]*:/)) {
            const trimmed = url.trim();
            const withoutScheme = trimmed.replace(/^[a-zA-Z]+:\/*/i, '');
            const withoutDrive = withoutScheme.replace(/^([a-zA-Z]:)/, '');
            const normalized = withoutDrive.replace(/\\/g, '/');
            const ensured = normalized.startsWith('/') ? normalized : '/' + normalized;
            const finalUrl = new URL(FALLBACK_ORIGIN + ensured);
            return finalUrl;
        }
        const parsed = new URL(url);
        const sanitizedPath = parsed.pathname.replace(/^\/?[a-zA-Z]:/, '').replace(/\\/g, '/');
        parsed.pathname = sanitizedPath.startsWith('/') ? sanitizedPath : '/' + sanitizedPath;
        const fallbackUrl = new URL(parsed.pathname + parsed.search, FALLBACK_ORIGIN);
        return fallbackUrl;
    } catch (e) {
        console.warn('[BLE] normalizeUrl failed for', url, e);
        return null;
    }
}
function pathLooksLikeApi(pathname) {
    if (!pathname) return false;
    const normalized = pathname.replace(/\\/g, '/');
    if (normalized === '/api' || normalized === 'api') return true;
    if (normalized.startsWith('/api/') || normalized.startsWith('api/')) return true;
    return normalized.includes('/api/');
}
function isApiRequestFromInput(input) {
    if (typeof input === 'string') {
        const normalized = normalizeUrl(input);
        return normalized ? pathLooksLikeApi(normalized.pathname) : false;
    } else if (input && typeof Request !== 'undefined' && input instanceof Request) {
        const normalized = normalizeUrl(input.url);
        return normalized ? pathLooksLikeApi(normalized.pathname) : false;
    }
    return false;
}
function isApiRequest(url, originalInput) {
    if (typeof originalInput !== 'undefined' && isApiRequestFromInput(originalInput)) {
        return true;
    }
    return isApiRequestFromInput(url);
}
class BleTransport {
    constructor() {
        this.device = null;
        this.server = null;
        this.commandCharacteristic = null;
        this.responseCharacteristic = null;
        this.responseBuffer = '';
        this.pending = null;
        this.listeners = new Set();
        this.status = this.isSupported() ? t('ble.disconnected') : t('ble.unsupported');
        this.boundDeviceDisconnect = this.handleDeviceDisconnected.bind(this);
        this.boundNotificationHandler = this.handleNotification.bind(this);
        this.requestQueue = Promise.resolve();

        if(this.isSupported()){
          $('wifi-status-item').style.display = 'none';
        } else {
          $('ble-status-item').style.display = 'none';
        }
    }
    async requestDevice(forceNew = false) {
        if (!forceNew && this.device) {
            return this.device;
        }
        const filters = [{ services: [BLE_CONFIG.serviceUuid] }];
        if (BLE_CONFIG.deviceName) {
            filters.push({ name: BLE_CONFIG.deviceName });
            filters.push({ namePrefix: BLE_CONFIG.deviceName });
        }
        const device = await navigator.bluetooth.requestDevice({
            filters,
            optionalServices: [BLE_CONFIG.serviceUuid]
        });
        if (this.device && this.device !== device) {
            try {
                this.device.removeEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
            } catch (e) {}
        }
        this.device = device;
        this.device.addEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
        return device;
    }
    async ensureGattConnection(device, forceReconnect = false) {
        if (!device || !device.gatt) {
            throw new Error('GATT server unavailable');
        }
        let server = device.gatt;
        if (forceReconnect && typeof server.disconnect === 'function' && server.connected) {
            try {
                server.disconnect();
            } catch (e) {}
        }
        if (!server.connected) {
            server = await server.connect();
        }
        this.server = server;
        return server;
    }
    isSupported() {
        return !!(navigator.bluetooth && navigator.bluetooth.requestDevice);
    }
    getStatus() {
        return this.status;
    }
    setStatus(status) {
        if (this.status === status) {
            return;
        }
        this.status = status;
        this.listeners.forEach(listener => {
            try {
                listener(status);
            } catch (e) {
                console.warn('BLE status listener error', e);
            }
        });
    }
    onStatusChange(cb) {
        if (typeof cb === 'function') {
            this.listeners.add(cb);
        }
    }
    offStatusChange(cb) {
        if (cb && this.listeners.has(cb)) {
            this.listeners.delete(cb);
        }
    }
    isConnected() {
        return this.status === 'connected';
    }
    shouldUseBle() {
        return this.isConnected();
    }
    async connect() {
        if (!this.isSupported()) {
            throw new Error(t('ble.notSupported'));
        }
        if (this.isConnected() || this.status === 'connecting') {
            return;
        }
        this.setStatus('connecting');
        try {
            const device = await this.requestDevice();
            let server = await this.ensureGattConnection(device);
            let service;
            try {
                service = await server.getPrimaryService(BLE_CONFIG.serviceUuid);
            } catch (error) {
                if (error && error.n === 'NetworkError') {
                    server = await this.ensureGattConnection(device, true);
                    service = await server.getPrimaryService(BLE_CONFIG.serviceUuid);
                } else {
                    throw error;
                }
            }
            this.commandCharacteristic = await service.getCharacteristic(BLE_CONFIG.commandCharacteristicUuid);
            this.responseCharacteristic = await service.getCharacteristic(BLE_CONFIG.responseCharacteristicUuid);
            await this.responseCharacteristic.startNotifications();
            this.responseCharacteristic.addEventListener('characteristicvaluechanged', this.boundNotificationHandler);
            this.setStatus('connected');
        } catch (error) {
            console.error('BLE connect error', error);
            this.teardown();
            this.setStatus(this.isSupported() ? 'disconnected' : 'unsupported');
            throw error;
        }
    }
    async disconnect() {
        if (this.device) {
            try {
                this.device.removeEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
            } catch (e) {}
        }
        if (this.device && this.device.gatt && this.device.gatt.connected) {
            try {
                this.device.gatt.disconnect();
            } catch (e) {}
        }
        this.teardown();
        this.setStatus(this.isSupported() ? 'disconnected' : 'unsupported');
    }
    handleDeviceDisconnected() {
        this.teardown();
        this.setStatus(this.isSupported() ? 'disconnected' : 'unsupported');
    }
    teardown() {
        if (this.responseCharacteristic) {
            try {
                this.responseCharacteristic.removeEventListener('characteristicvaluechanged', this.boundNotificationHandler);
            } catch (e) {}
        }
        if (this.device) {
            try {
                this.device.removeEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
            } catch (e) {}
        }
        this.server = null;
        this.commandCharacteristic = null;
        this.responseCharacteristic = null;
        this.responseBuffer = '';
        this.device = null;
        if (this.pending && this.pending.reject) {
            this.pending.reject(new Error(t('ble.disconnected')));
        }
        this.pending = null;
    }
    async sendRequest(message) {
        if (!this.commandCharacteristic) {
            throw new Error(t('ble.disconnected'));
        }
        if (this.pending) {
            throw new Error(t('ble.requestRejected'));
        }
        const framed = JSON.stringify(message) + '\n';
        const encoded = bleTextEncoder.encode(framed);
        return new Promise((resolve, reject) => {
            const timeoutId = setTimeout(() => {
                if (this.pending && this.pending.reject) {
                    this.pending.reject(new Error(t('ble.timeout')));
                }
            }, BLE_CONFIG.responseTimeoutMs);
            this.pending = {
                resolve: (response) => {
                    clearTimeout(timeoutId);
                    this.pending = null;
                    resolve(response);
                },
                reject: (err) => {
                    clearTimeout(timeoutId);
                    this.pending = null;
                    reject(err);
                }
            };
            this.writeChunks(encoded).catch(error => {
                if (this.pending && this.pending.reject) {
                    this.pending.reject(error);
                } else {
                    reject(error);
                }
            });
        });
    }
    async writeChunks(encoded) {
        const chunkSize = BLE_CONFIG.maxChunkSize;
        for (let offset = 0; offset < encoded.length; offset += chunkSize) {
            const chunk = encoded.slice(offset, Math.min(offset + chunkSize, encoded.length));
            await this.commandCharacteristic.writeValueWithResponse(chunk);
        }
    }
    handleNotification(event) {
        const value = event.target.value;
        this.responseBuffer += bleTextDecoder.decode(value);
        let newlineIndex;
        while ((newlineIndex = this.responseBuffer.indexOf('\n')) !== -1) {
            const message = this.responseBuffer.slice(0, newlineIndex).trim();
            this.responseBuffer = this.responseBuffer.slice(newlineIndex + 1);
            if (!message) {
                continue;
            }
            let parsed;
            try {
                parsed = JSON.parse(message);
            } catch (e) {
                console.warn('Invalid BLE payload', message);
                continue;
            }
            if (this.pending && this.pending.resolve) {
                this.pending.resolve(parsed);
            }
        }
    }
    enqueueRequest(taskFn) {
        const next = this.requestQueue.then(() => taskFn()).then(async (result) => {
            await new Promise(resolve => setTimeout(resolve, 20));
            return result;
        });
        this.requestQueue = next.catch(() => {});
        return next;
    }
    clearQueue() {
        // Réinitialise la queue pour éviter l'accumulation
        console.log('[BLE] Queue vidée pour éviter l\'accumulation');

        // Annuler la requête en attente si elle existe
        if (this.pending && this.pending.reject) {
            this.pending.reject(new Error('Queue cleared'));
            this.pending = null;
        }

        this.requestQueue = Promise.resolve();
    }
    async waitForQueue() {
        // Attend que la queue soit vide sans annuler les requêtes en cours
        console.log('[BLE] Attente de la fin de la queue...');
        await this.requestQueue.catch(() => {});
        console.log('[BLE] Queue vide, prêt pour la prochaine requête');
    }
}
const bleTransport = new BleTransport();
bleTransportInstance = bleTransport;
if (!wifiOnline) {
    maybeAutoConnectBle();
}
// Mettre à jour la visibilité de l'onglet Logs après init BLE
updateLogsTabVisibility();
const nativeFetch = window.fetch.bind(window);
function shouldUseBleForRequest(url, isApiCallOverride) {
    if (!bleTransport.shouldUseBle()) {
        return false;
    }
    if (typeof isApiCallOverride === 'boolean') {
        return isApiCallOverride;
    }
    return isApiRequest(url);
}
window.fetch = async function(input, init = {}) {
    const request = new Request(input, init);
    const isApiCall = isApiRequest(request.url, input);

    if (isApiCall) {
        await waitForApiConnection();
    }
    if (!shouldUseBleForRequest(request.url, isApiCall)) {
        return nativeFetch(request);
    }
    const headers = {};
    request.headers.forEach((value, key) => {
        headers[key] = value;
    });
    let bodyText = undefined;
    const method = (request.method || 'GET').toUpperCase();
    if (method !== 'GET' && method !== 'HEAD') {
        try {
            bodyText = await request.clone().text();
        } catch (e) {
            bodyText = undefined;
        }
    }
    const urlObj = normalizeUrl(request.url);
    let requestPath;
    if (urlObj) {
        const normalizedPath = urlObj.pathname.replace(/\\/g, '/');
        requestPath = normalizedPath + urlObj.search;
    } else if (typeof request.url === 'string') {
        const trimmed = request.url.trim();
        const withoutScheme = trimmed.replace(/^[a-zA-Z]+:\/*/i, '');
        const withoutDrive = withoutScheme.replace(/^([a-zA-Z]:)/, '');
        const sanitized = withoutDrive.replace(/\\/g, '/');
        requestPath = sanitized.startsWith('/') ? sanitized : '/' + sanitized;
    } else {
        requestPath = '/';
    }
    try {
        const bleResponse = await bleTransport.enqueueRequest(() => bleTransport.sendRequest({
            method,
            path: requestPath,
            headers,
            body: bodyText
        }));
        const responseHeaders = new Headers(bleResponse.headers || { 'Content-Type': 'application/json' });
        return new Response(bleResponse.body || '', {
            status: bleResponse.status || 200,
            statusText: bleResponse.statusText || 'OK',
            headers: responseHeaders
        });
    } catch (error) {
        console.error('BLE fetch error', error);
        throw error;
    }
};
let lastBleStatus = bleTransport.getStatus();
bleTransport.onStatusChange((status) => {
    updateBleUiState();
    if (status === 'connected') {
        showNotification('ble-notification', t('ble.toastConnected'), 'success');
        updateApiConnectionState();
    } else if (status === 'disconnected' && lastBleStatus === 'connected') {
        showNotification('ble-notification', t('ble.toastDisconnected'), 'info');
        // Réafficher l'overlay de chargement lors de la déconnexion BLE
        if (!wifiOnline) {
            initialDataLoaded = false;
            initialDataLoadPromise = null;
            showLoadingOverlay();
        }
        updateApiConnectionState();
    } else if (status === 'connecting') {
        updateApiConnectionState();
    }
    lastBleStatus = status;
    // Mettre à jour le bouton BLE sur l'overlay de chargement
    updateLoadingBleButton();
});
const simulationSections = [
    {
        titleKey: 'simulation.turnSignals',
        events: [
            { id: 'TURN_LEFT', labelKey: 'simulation.left' },
            { id: 'TURN_RIGHT', labelKey: 'simulation.right' },
            { id: 'TURN_HAZARD', labelKey: 'simulation.hazard' }
        ]
    },
    {
        titleKey: 'simulation.charging',
        events: [
            { id: 'CHARGING', labelKey: 'simulation.chargingNow' },
            { id: 'CHARGE_COMPLETE', labelKey: 'simulation.chargeComplete' },
            { id: 'CHARGING_STARTED', labelKey: 'simulation.chargingStarted' },
            { id: 'CHARGING_STOPPED', labelKey: 'simulation.chargingStopped' }
        ]
    },
    {
        titleKey: 'simulation.chargingHardware',
        events: [
            { id: 'CHARGING_CABLE_CONNECTED', labelKey: 'simulation.cableConnected' },
            { id: 'CHARGING_CABLE_DISCONNECTED', labelKey: 'simulation.cableDisconnected' },
            { id: 'CHARGING_PORT_OPENED', labelKey: 'simulation.portOpened' }
        ]
    },
    {
        titleKey: 'simulation.doors',
        events: [
            { id: 'DOOR_OPEN', labelKey: 'simulation.doorOpen' },
            { id: 'DOOR_CLOSE', labelKey: 'simulation.doorClose' }
        ]
    },
    {
        titleKey: 'simulation.lock',
        events: [
            { id: 'LOCKED', labelKey: 'simulation.locked' },
            { id: 'UNLOCKED', labelKey: 'simulation.unlocked' }
        ]
    },
    {
        titleKey: 'simulation.driving',
        events: [
            { id: 'BRAKE_ON', labelKey: 'simulation.brakeOn' },
            { id: 'SPEED_THRESHOLD', labelKey: 'simulation.speedThreshold' }
        ]
    },
    {
        titleKey: 'simulation.autopilot',
        events: [
            { id: 'AUTOPILOT_ENGAGED', labelKey: 'simulation.autopilotEngaged' },
            { id: 'AUTOPILOT_DISENGAGED', labelKey: 'simulation.autopilotDisengaged' },
            { id: 'AUTOPILOT_ABORTING', labelKey: 'simulation.autopilotAborting' }
        ]
    },
    {
        titleKey: 'simulation.gear',
        events: [
            { id: 'GEAR_DRIVE', labelKey: 'simulation.gearDrive' },
            { id: 'GEAR_REVERSE', labelKey: 'simulation.gearReverse' },
            { id: 'GEAR_PARK', labelKey: 'simulation.gearPark' }
        ]
    },
    {
        titleKey: 'simulation.blindspot',
        events: [
            { id: 'BLINDSPOT_LEFT', labelKey: 'simulation.blindspotLeft' },
            { id: 'BLINDSPOT_RIGHT', labelKey: 'simulation.blindspotRight' }
        ]
    },
    {
        titleKey: 'simulation.sideCollision',
        events: [
            { id: 'SIDE_COLLISION_LEFT', labelKey: 'simulation.sideCollisionLeft' },
            { id: 'SIDE_COLLISION_RIGHT', labelKey: 'simulation.sideCollisionRight' }
        ]
    },
    {
        titleKey: 'simulation.laneDeparture',
        events: [
            { id: 'LANE_DEPARTURE_LEFT_LV1', labelKey: 'simulation.laneDepartureLeftLv1' },
            { id: 'LANE_DEPARTURE_LEFT_LV2', labelKey: 'simulation.laneDepartureLeftLv2' },
            { id: 'LANE_DEPARTURE_RIGHT_LV1', labelKey: 'simulation.laneDepartureRightLv1' },
            { id: 'LANE_DEPARTURE_RIGHT_LV2', labelKey: 'simulation.laneDepartureRightLv2' }
        ]
    },
    {
        titleKey: 'simulation.sentry',
        events: [
            { id: 'SENTRY_MODE_ON', labelKey: 'simulation.sentryOn' },
            { id: 'SENTRY_MODE_OFF', labelKey: 'simulation.sentryOff' },
            { id: 'SENTRY_ALERT', labelKey: 'simulation.sentryAlert' }
        ]
    }
];
// Language & theme management
currentLang = localStorage.getItem('language') || currentLang;
currentTheme = localStorage.getItem('theme') || currentTheme;
function updateBleUiState() {
    const button = $('ble-connect-button');
    const statusValue = $('ble-status-text');
    const statusItem = $('ble-status-item');
    if (!button || !statusValue) {
        return;
    }
    const supported = bleTransport.isSupported();
    if (statusItem) {
        statusItem.hidden = !supported;
    }
    if (!supported) {
        button.disabled = true;
        button.textContent = BLE_BUTTON_ICONS.unsupported;
        button.title = t('ble.notSupported');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value status-offline';
        statusValue.dataset.i18n = 'ble.statusUnsupported';
        statusValue.textContent = t('ble.statusUnsupported');
        return;
    }
    const status = bleTransport.getStatus();
    if (status === 'connected') {
        button.disabled = false;
        button.textContent = BLE_BUTTON_ICONS.disconnect;
        button.title = t('ble.disconnect');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value status-online';
        statusValue.dataset.i18n = 'ble.connected';
        statusValue.textContent = t('ble.connected');
    } else if (status === 'connecting') {
        button.disabled = true;
        button.textContent = BLE_BUTTON_ICONS.connecting;
        button.title = t('ble.connecting');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value';
        statusValue.dataset.i18n = 'ble.connecting';
        statusValue.textContent = t('ble.connecting');
    } else {
        button.disabled = false;
        button.textContent = BLE_BUTTON_ICONS.connect;
        button.title = t('ble.connect');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value status-offline';
        statusValue.dataset.i18n = 'ble.disconnected';
        statusValue.textContent = t('ble.disconnected');
    }
    updateOtaBleNote();
}
function updateOtaBleNote() {
    const note = $('ota-ble-note');
    if (!note) {
        return;
    }
    const useBle = bleTransport && typeof bleTransport.shouldUseBle === 'function'
        ? bleTransport.shouldUseBle()
        : false;
    note.style.display = useBle ? 'block' : 'none';
    const fileInput = $('ota-progress-title');
    const uploadBtn = $('ota-upload-btn');
    if (fileInput) {
        fileInput.style.display = useBle ? 'none' : 'block';
        fileInput.disabled = useBle;
    }
    if (uploadBtn) {
        uploadBtn.style.display = useBle ? 'none' : 'inline-block';
        uploadBtn.disabled = useBle;
    }
}
function updateLogsTabVisibility() {
    const logsTabButton = $('logs-tab-button');
    if (!logsTabButton) {
        return;
    }
    const useBle = bleTransport && typeof bleTransport.shouldUseBle === 'function'
        ? bleTransport.shouldUseBle()
        : false;
    // Afficher l'onglet Logs uniquement si on n'utilise PAS BLE (donc WiFi)
    logsTabButton.style.display = useBle ? 'none' : 'block';
}
async function toggleBleConnection() {
    if (!bleTransport.isSupported()) {
        showNotification('ble-notification', t('ble.notSupported'), 'error');
        return;
    }
    const status = bleTransport.getStatus();
    if (status === 'connecting') {
        return;
    }
    try {
        if (bleTransport.isConnected()) {
            await bleTransport.disconnect();
        } else {
            await bleTransport.connect();
        }
    } catch (error) {
        console.error('BLE toggle error', error);
        const message = t('ble.toastError') + (error && error.message ? ' - ' + error.message : '');
        showNotification('ble-notification', message, 'error');
    } finally {
        updateBleUiState();
    }
}
function setLanguage(lang) {
    if (!lang || lang === currentLang) {
        updateLanguageSelector();
        updateLanguageIcon();
        return;
    }
    currentLang = lang;
    localStorage.setItem('language', currentLang);
    applyTranslations();
    renderEventsTable();
    updateLanguageIcon();
}
function updateLanguageSelector() {
    const select = $('language-select');
    if (select) {
        select.value = currentLang;
    }
}
function applyTheme() {
    doc.body.classList.toggle('light-theme', currentTheme === 'light');
    const select = $('theme-select');
    if (select) {
        select.value = currentTheme;
    }
}
function setTheme(theme) {
    if (!theme) return;
    currentTheme = theme;
    localStorage.setItem('theme', currentTheme);
    applyTheme();
    updateThemeIcon();
}

function toggleTheme() {
    currentTheme = currentTheme === 'dark' ? 'light' : 'dark';
    setTheme(currentTheme);
}

function updateThemeIcon() {
    const icon = $('theme-icon');
    if (icon) {
        icon.textContent = currentTheme === 'dark' ? '☀️' : '🌙';
    }
}

function toggleLanguage() {
    const newLang = currentLang === 'fr' ? 'en' : 'fr';
    setLanguage(newLang);
}

function updateLanguageIcon() {
    const icon = $('language-icon');
    if (icon) {
        icon.textContent = currentLang === 'fr' ? 'FR' : 'EN';
    }
}
function applyTranslations() {
    doc.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        const keys = key.split('.');
        let value = translations[currentLang];
        for (let k of keys) {
            value = value[k];
        }
        if (value) {
            el.textContent = value;
        }
    });
    doc.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
        const key = el.getAttribute('data-i18n-placeholder');
        const keys = key.split('.');
        let value = translations[currentLang];
        for (let k of keys) {
            value = value[k];
        }
        if (value) {
            el.placeholder = value;
        }
    });
    // Update select options
    updateSelectOptions();
    refreshEffectOptionLabels();
    updateLanguageIcon();
    updateThemeIcon();
    updateBleUiState();
}
function updateSelectOptions() {
    const effectSelect = $('effect-select');
    if (effectSelect) {
        Array.from(effectSelect.options).forEach(opt => {
            const key = opt.getAttribute('data-i18n');
            if (key) {
                const keys = key.split('.');
                let value = translations[currentLang];
                for (let k of keys) {
                    value = value[k];
                }
                if (value) opt.textContent = value;
            }
        });
    }
}
function refreshEffectOptionLabels() {
    const selects = doc.querySelectorAll('select[data-effect-options]');
    selects.forEach(select => {
        Array.from(select.options).forEach(opt => {
            const effectId = opt.value;
            if (!effectId) return;
            const translated = translateEffectId(effectId);
            if (translated) {
                opt.textContent = translated;
            } else {
                const fallback = opt.getAttribute('data-effect-name');
                if (fallback) {
                    opt.textContent = fallback;
                }
            }
        });
    });
}
function t(key, ...params) {
    const keys = key.split('.');
    let value = translations[currentLang];
    for (let k of keys) {
        value = value[k];
    }
    let result = value || key;
    // Replace {0}, {1}, etc. with parameters
    params.forEach((param, index) => {
        result = result.replace(`{${index}}`, param);
    });
    return result;
}
// Gestion des tabs
// État des toggles de simulation (persiste entre les changements d'onglets)
let simulationTogglesState = {};
let simulationAutoStopTimers = {};
function isSimulationEventEnabled(eventType) {
    const config = getSimulationEventConfig(eventType);
    if (!config) {
        return true;
    }
    return config.en !== false;
}
function switchTab(tabName, evt) {
    const tabs = doc.querySelectorAll('.tabs .tab');
    tabs.forEach(tab => tab.classList.remove('active'));
    doc.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));
    const button = (evt && evt.currentTarget) || $$(`.tabs .tab[data-tab="${tabName}"]`);
    if (button) {
        button.classList.add('active');
    }
    const target = $(tabName + '-tab');
    if (target) {
        target.classList.add('active');
    }
    activeTabName = tabName;

    // Gérer le polling audio et FFT selon l'onglet actif
    if (tabName === 'config') {
      loadFFTStatus();
      startAudioDataPolling(); // Démarrera seulement si audioEnabled est true
    } else {
      stopAudioDataPolling(); // Arrêter le polling si on quitte l'onglet config
    }

    // Load data for specific tabs
    if (tabName === 'events-config') {
        loadEventsConfig();
    } else if (tabName === 'config') {
        loadHardwareConfig();
    } else if (tabName === 'simulation') {
        // Restaurer l'état des toggles de simulation
        restoreSimulationToggles();
    } else if (tabName === 'profiles') {
        loadProfiles();
    } else if (tabName === 'diagnostic') {
        updateGvretTcpStatus();
        updateCanserverStatus();
        updateSlcanTcpStatus();
    } else if (tabName === 'logs') {
        // Onglet Logs: rien à charger au démarrage
    }
}
function restoreSimulationToggles() {
    // Restaurer l'état de tous les toggles
    Object.keys(simulationTogglesState).forEach(eventId => {
        const checkbox = $('toggle-' + eventId);
        if (checkbox) {
            checkbox.checked = simulationTogglesState[eventId];
        }
    });
}
function getSimulationEventConfig(eventType) {
    if (!eventsConfigData || eventsConfigData.length === 0) {
        return null;
    }
    const normalized = typeof eventType === 'string' ? eventType : String(eventType);
    return eventsConfigData.find(evt => evt.ev === normalized) || null;
}
function cancelSimulationAutoStop(eventType) {
    if (simulationAutoStopTimers[eventType]) {
        clearTimeout(simulationAutoStopTimers[eventType]);
        delete simulationAutoStopTimers[eventType];
    }
}
function scheduleSimulationAutoStop(eventType, durationMs) {
    if (!durationMs || durationMs <= 0) {
        return;
    }
    cancelSimulationAutoStop(eventType);
    simulationAutoStopTimers[eventType] = setTimeout(() => {
        autoStopSimulationEvent(eventType);
    }, durationMs);
}
async function autoStopSimulationEvent(eventType) {
    cancelSimulationAutoStop(eventType);
    const checkbox = $('toggle-' + eventType);
    if (checkbox) {
        checkbox.checked = false;
    }
    simulationTogglesState[eventType] = false;
    // Call stop event API directly
    try {
        await fetch(API_BASE + '/api/stop/event', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ event: eventType })
        });
    } catch (e) {
        console.error('Error auto-stopping event:', e);
    }
}
async function getSimulationEventDuration(eventType) {
    try {
        await ensureEventsConfigData();
    } catch (error) {
        return 0;
    }
    const config = getSimulationEventConfig(eventType);
    return config && typeof config.dur === 'number' ? config.dur : 0;
}
// renderSimulationSections removed - simulation is now integrated into events config tab
// Conversion pourcentage <-> 0-255
function percentTo255(percent) {
    return Math.round((percent * 255) / 100);
}
function to255ToPercent(value) {
    return Math.round((value * 100) / 255);
}
const dynamicBrightnessEnabled = $('dynamic-brightness-enabled');
if (dynamicBrightnessEnabled) {
    dynamicBrightnessEnabled.addEventListener('change', scheduleDefaultEffectSave);
}
// Mise à jour des sliders avec pourcentage (seulement ceux qui existent)
const dynamicBrightnessRateSlider = $('dynamic-brightness-rate');
if (dynamicBrightnessRateSlider) {
    dynamicBrightnessRateSlider.oninput = function() {
        $('dynamic-brightness-rate-value').textContent = this.value + '%';
        scheduleDefaultEffectSave();
    };
}
const defaultBrightnessSlider = $('default-brightness-slider');
if (defaultBrightnessSlider) {
    defaultBrightnessSlider.oninput = function() {
        $('default-brightness-value').textContent = this.value + '%';
        scheduleDefaultEffectSave();
    };
}
const defaultSpeedSlider = $('default-speed-slider');
if (defaultSpeedSlider) {
    defaultSpeedSlider.oninput = function() {
        $('default-speed-value').textContent = this.value + '%';
        scheduleDefaultEffectSave();
    };
}
const defaultColorInput = $('default-color1');
if (defaultColorInput) {
    defaultColorInput.addEventListener('change', scheduleDefaultEffectSave);
}
const defaultEffectSelect = $('default-effect-select');
if (defaultEffectSelect) {
    defaultEffectSelect.addEventListener('change', scheduleDefaultEffectSave);
}
const defaultAudioReactive = $('default-audio-reactive');
if (defaultAudioReactive) {
    defaultAudioReactive.addEventListener('change', scheduleDefaultEffectSave);
}
const defaultReverse = $('default-reverse');
if (defaultReverse) {
    defaultReverse.addEventListener('change', scheduleDefaultEffectSave);
}
const defaultAccelPedalEnabled = $('default-accel-pedal-enabled');
if (defaultAccelPedalEnabled) {
    defaultAccelPedalEnabled.addEventListener('change', scheduleDefaultEffectSave);
}
const defaultAccelPedalOffsetSlider = $('default-accel-pedal-offset-slider');
if (defaultAccelPedalOffsetSlider) {
    defaultAccelPedalOffsetSlider.oninput = function() {
        $('default-accel-pedal-offset-value').textContent = this.value + '%';
        scheduleDefaultEffectSave();
    };
}
const SEGMENT_MIN_LENGTH = 10;

// Normalise une plage de segment (min/max) pour garantir une longueur minimale et borne max
function normalizeSegmentRange(minSlider, maxSlider, minLength = SEGMENT_MIN_LENGTH, maxValue = maxLeds) {
    let min = parseInt(minSlider.value);
    let max = parseInt(maxSlider.value);

    if (max - min < minLength) {
        if (minSlider === document.activeElement) {
            max = Math.min(min + minLength, maxValue);
            if (max - min < minLength) {
                min = Math.max(0, max - minLength);
            }
            maxSlider.value = max;
        } else {
            min = Math.max(0, max - minLength);
            if (max - min < minLength) {
                max = Math.min(maxValue, min + minLength);
            }
            minSlider.value = min;
        }
    }

    return { start: min, end: max, length: max - min };
}

// Met à jour l'affichage visuel d'un segment (slider + valeurs)
function renderSegmentRange(minSlider, maxSlider, selectedEl, startEl, lengthEl, maxValue = maxLeds) {
    const { start, end, length } = normalizeSegmentRange(minSlider, maxSlider, SEGMENT_MIN_LENGTH, maxValue);

    if (selectedEl) {
        const percent1 = (start / maxValue) * 100;
        const percent2 = (end / maxValue) * 100;
        selectedEl.style.left = percent1 + '%';
        selectedEl.style.width = (percent2 - percent1) + '%';
    }
    if (startEl) {
        startEl.textContent = start;
    }
    if (lengthEl) {
        lengthEl.textContent = length;
    }

    return { start, length };
}

// Helpers pour les sliders de segment par défaut
function updateDefaultSegmentRange(triggerSave = true) {
    const minSlider = $('default-segment-range-min');
    const maxSlider = $('default-segment-range-max');
    const selected = $('default-segment-range-selected');
    const startValue = $('default-segment-value-start');
    const lengthValue = $('default-segment-value-length');

    if (!minSlider || !maxSlider || !selected || !startValue || !lengthValue) {
        return;
    }

    renderSegmentRange(minSlider, maxSlider, selected, startValue, lengthValue);

    if (triggerSave) {
        scheduleDefaultEffectSave();
    }
}

function getDefaultSegmentRange() {
    const minSlider = $('default-segment-range-min');
    const maxSlider = $('default-segment-range-max');
    if (!minSlider || !maxSlider) {
        return null;
    }
    const start = parseInt(minSlider.value);
    const length = parseInt(maxSlider.value) - start;
    // 0 signifie pleine longueur
    return { start, length: length === maxLeds ? 0 : length };
}

function setDefaultSegmentRange(start, length) {
    const minSlider = $('default-segment-range-min');
    const maxSlider = $('default-segment-range-max');
    if (!minSlider || !maxSlider) {
        return;
    }
    const resolvedStart = start ?? 0;
    const resolvedLength = (length !== undefined && length !== 0) ? length : maxLeds;
    minSlider.max = maxLeds;
    maxSlider.max = maxLeds;
    minSlider.value = resolvedStart;
    maxSlider.value = resolvedStart + resolvedLength;
    // Mise à jour visuelle sans déclencher de sauvegarde
    updateDefaultSegmentRange(false);
}
// Notification helper
function showNotification(elementId, message, type, timeout = 2000) {
    const notification = $(elementId);
    if (!notification) {
        console.warn('showNotification: element not found:', elementId);
        return;
    }
    notification.textContent = message;
    notification.className = 'notification ' + type + ' show';
    setTimeout(() => {
        notification.classList.remove('show');
    }, timeout);
}
async function parseApiResponse(response) {
    const rawText = await response.text();
    let data = null;
    if (rawText) {
        try {
            data = JSON.parse(rawText);
        } catch (e) {
            data = null;
        }
    }
    const status = data && typeof data.st === 'string' ? data.st : null;
    const success = response.ok && (status === null || status === 'ok');
    return { success, data, raw: rawText };
}
// Events Configuration
let eventsConfigData = [];
const eventAutoSaveTimers = new Map();
let defaultEffectSaveTimer = null;
let eventsConfigLoadingPromise = null;
function getDefaultEffectId() {
    if (effectsList.length === 0) {
        return 'OFF';
    }
    const offEffect = effectsList.find(effect => effect.id === 'OFF');
    if (offEffect) {
        return offEffect.id;
    }
    const nonCan = effectsList.find(effect => !effect.cr);
    if (nonCan) {
        return nonCan.id;
    }
    return effectsList[0].id;
}
async function ensureEventsConfigData(forceRefresh = false) {
    if (!forceRefresh && eventsConfigData.length > 0) {
        return eventsConfigData;
    }
    if (!forceRefresh && eventsConfigLoadingPromise) {
        return eventsConfigLoadingPromise;
    }
    const fetchPromise = (async () => {
        try {
            const response = await fetch(API_BASE + '/api/events');
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            const data = await response.json();
            eventsConfigData = data.events || [];
            return eventsConfigData;
        } catch (error) {
            console.error('Failed to load events config:', error);
            throw error;
        }
    })();
    if (!forceRefresh) {
        eventsConfigLoadingPromise = fetchPromise.finally(() => {
            eventsConfigLoadingPromise = null;
        });
        return eventsConfigLoadingPromise;
    }
    return fetchPromise;
}
async function loadEventsConfig() {
    const loading = $('events-loading');
    const content = $('events-content');
    loading.style.display = 'block';
    content.style.display = 'none';
    try {
        await ensureEventsConfigData(true);
        renderEventsTable();
        loading.style.display = 'none';
        content.style.display = 'block';
    } catch (e) {
        console.error('Error:', e);
        showNotification('events-notification', t('eventsConfig.loadError'), 'error');
        loading.style.display = 'none';
    }
}
function renderEventsTable() {
    const container = $('events-accordion-container');
    if (!container) {
        console.error('events-accordion-container not found in DOM');
        return;
    }

    // Sauvegarder l'état des accordéons ouverts avant de recréer
    const openAccordions = new Set();
    container.querySelectorAll('.event-accordion-header.active').forEach(header => {
        // Trouver l'index réel de l'accordéon
        const contentId = header.nextElementSibling?.id;
        if (contentId) {
            const match = contentId.match(/event-accordion-content-(\d+)/);
            if (match) {
                openAccordions.add(parseInt(match[1]));
            }
        }
    });

    container.innerHTML = '';
    // If no data from API, create default rows for all events
    if (eventsConfigData.length === 0) {
        const defaultEffectId =
            effectsList.find(effect => !effect.cr)?.id ||
            effectsList[0]?.id ||
            'OFF';
        if (eventTypesList.length > 0) {
            eventTypesList
                .filter(evt => evt.id !== 'NONE')
                .forEach(evt => {
                    eventsConfigData.push({
                        ev: evt.id,
                        fx: defaultEffectId,
                        br: 128,
                        sp: 128,
                        c1: 0xFF0000,
                        dur: 0,
                        pri: 100,
                        en: true,
                        at: 0,
                        pid: -1,
                        csp: false,
                        st: 0,
                        ln: 0
                    });
                });
        }
    }
    eventsConfigData.forEach((event, index) => {
        // Skip CAN_EVENT_NONE only
        if (event.ev === 'NONE') {
            return;
        }
        const eventName = getEventName(event.ev);
        const actionType = event.at !== undefined ? event.at : 0;
        const canSwitchProfile = event.csp || false;
        const profileId = event.pid !== undefined ? event.pid : -1;

        // Générer les options d'action
        let actionOptions = '';
        actionOptions += `<option value="0" ${actionType === 0 ? 'selected' : ''}>${t('eventsConfig.applyEffect')}</option>`;
        if (canSwitchProfile) {
            actionOptions += `<option value="1" ${actionType === 1 ? 'selected' : ''}>${t('eventsConfig.switchProfile')}</option>`;
        }

        // Générer les options de profil
        const profileSelect = $('profile-select');
        let profileOptions = '<option value="-1">--</option>';
        if (profileSelect) {
            for (let i = 0; i < profileSelect.options.length; i++) {
                const opt = profileSelect.options[i];
                profileOptions += `<option value="${opt.value}" ${profileId == opt.value ? 'selected' : ''}>${opt.text}</option>`;
            }
        }

        // Générer le résumé pour le mode comprimé (activé en premier)
        let summaryHTML = `
            <div class="event-accordion-summary-item enabled-item">
                <span class="event-accordion-summary-label">${t('eventsConfig.enabled')}:</span>
                <span class="event-accordion-summary-value">${event.en ? '✓' : '✗'}</span>
            </div>
        `;

        if (actionType === 0) {
            // Apply Effect
            const effectName = getEffectName(event.fx);
            const colorHex = '#' + event.c1.toString(16).padStart(6, '0');
            const displayLength = (event.ln ?? 0) === 0 ? maxLeds : event.ln;
            const segmentInfo = `[${event.st ?? 0}→${(event.st ?? 0) + displayLength}]`;
            summaryHTML += `
                <div class="event-accordion-summary-item effect-item">
                    <span class="event-accordion-summary-label">${t('eventsConfig.effect')}:</span>
                    <span class="event-accordion-summary-value">${effectName} ${segmentInfo}</span>
                </div>
                <div class="event-accordion-summary-item color-item">
                    <span class="event-accordion-summary-label">${t('eventsConfig.color')}:</span>
                    <span class="event-accordion-color-preview" style="background-color: ${colorHex};"></span>
                </div>
            `;
        } else {
            // Switch Profile
            const profileName = profileSelect && profileId >= 0 ?
                profileSelect.options[Array.from(profileSelect.options).findIndex(opt => opt.value == profileId)]?.text || '--' :
                '--';
            summaryHTML += `
                <div class="event-accordion-summary-item effect-item">
                    <span class="event-accordion-summary-label">${t('eventsConfig.profile')}:</span>
                    <span class="event-accordion-summary-value">${profileName}</span>
                </div>
            `;
        }

        // Créer l'élément accordéon
        const accordionItem = doc.createElement('div');
        accordionItem.className = 'event-accordion-item ' + (event.en ? 'enabled' : 'disabled');
        accordionItem.id = 'event-accordion-item-' + index;
        accordionItem.innerHTML = `
            <div class="event-accordion-header">
                <div class="event-accordion-left" onclick="toggleEventAccordion(${index})">
                    <div class="event-accordion-title">${eventName}</div>
                    <div class="event-accordion-summary">
                        ${summaryHTML}
                    </div>
                    <div class="event-accordion-toggle">▼</div>
                </div>
                <div class="event-accordion-simulate">
                    <label class="toggle-switch" onclick="event.stopPropagation()">
                        <input type="checkbox" id="toggle-${event.ev}" onchange="toggleEventTest('${event.ev}', this.checked)" ${!event.en ? 'disabled' : ''}>
                        <span class="toggle-slider"></span>
                    </label>
                </div>
            </div>
            <div class="event-accordion-content" id="event-accordion-content-${index}">
                <div class="event-accordion-form" id="event-accordion-form-${index}">
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.enabled">${t('eventsConfig.enabled')}</label>
                        <input type="checkbox" id="event-enabled-${index}" ${event.en ? 'checked' : ''}
                            onchange="updateEventConfig(${index}, 'en', this.checked); toggleEventFormFields(${index});">
                    </div>
                    ${canSwitchProfile ? `
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.action">${t('eventsConfig.action')}</label>
                        <select id="event-action-${index}" data-event-index="${index}" ${!event.en ? 'disabled' : ''}>
                            ${actionOptions}
                        </select>
                    </div>
                    ` : ''}
                    ${actionType === 1 && canSwitchProfile ? `
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.profile">${t('eventsConfig.profile')}</label>
                        <select onchange="updateEventConfig(${index}, 'pid', parseInt(this.value))" ${!event.en ? 'disabled' : ''}>
                            ${profileOptions}
                        </select>
                    </div>
                    ` : ''}
                    ${actionType === 0 ? `
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.effect">${t('eventsConfig.effect')}</label>
                        <select data-effect-options="true" onchange="updateEventConfig(${index}, 'fx', this.value)" ${!event.en ? 'disabled' : ''}>
                            ${effectsList
                                .filter(effect => !effect.ae)
                                .map(effect =>
                                    `<option value="${effect.id}" data-effect-name="${effect.n}" ${event.fx == effect.id ? 'selected' : ''}>${getEffectName(effect.id)}</option>`
                                ).join('')}
                        </select>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.brightness">${t('eventsConfig.brightness')}</label>
                        <div class="slider-container">
                            <input type="range" id="event-brightness-${index}" min="1" max="100" value="${to255ToPercent(event.br)}"
                                oninput="$('event-brightness-value-${index}').textContent = this.value + '%'"
                                onchange="updateEventConfig(${index}, 'br', percentTo255(parseInt(this.value)))" ${!event.en ? 'disabled' : ''}>
                            <span class="slider-value" id="event-brightness-value-${index}">${to255ToPercent(event.br)}%</span>
                        </div>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.speed">${t('eventsConfig.speed')}</label>
                        <div class="slider-container">
                            <input type="range" id="event-speed-${index}" min="1" max="100" value="${to255ToPercent(event.sp)}"
                                oninput="$('event-speed-value-${index}').textContent = this.value + '%'"
                                onchange="updateEventConfig(${index}, 'sp', percentTo255(parseInt(this.value)))" ${!event.en ? 'disabled' : ''}>
                            <span class="slider-value" id="event-speed-value-${index}">${to255ToPercent(event.sp)}%</span>
                        </div>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.color">${t('eventsConfig.color')}</label>
                        <input type="color" value="#${event.c1.toString(16).padStart(6, '0')}"
                            onchange="updateEventConfig(${index}, 'c1', parseInt(this.value.substring(1), 16))" ${!event.en ? 'disabled' : ''}>
                    </div>
                    <div class="event-form-field checkbox-field">
                        <label class="event-form-label" data-i18n="eventsConfig.reverse">${t('eventsConfig.reverse')}</label>
                        <input type="checkbox" ${event.rv ? 'checked' : ''} ${!event.en ? 'disabled' : ''}
                            onchange="updateEventConfig(${index}, 'rv', this.checked)">
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.duration">${t('eventsConfig.duration')}</label>
                        <input type="number" min="0" max="60000" step="100" value="${event.dur}"
                            onchange="updateEventConfig(${index}, 'dur', parseInt(this.value))" ${!event.en ? 'disabled' : ''}>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.priority">${t('eventsConfig.priority')}</label>
                        <div class="slider-container">
                            <input type="range" id="event-priority-${index}" min="1" max="100" value="${to255ToPercent(event.pri)}"
                                oninput="$('event-priority-value-${index}').textContent = this.value"
                                onchange="updateEventConfig(${index}, 'pri', percentTo255(parseInt(this.value)))" ${!event.en ? 'disabled' : ''}>
                            <span class="slider-value" id="event-priority-value-${index}">${to255ToPercent(event.pri)}</span>
                        </div>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.segmentRange">${t('eventsConfig.segmentRange')}</label>
                        <div class="segment-range-container">
                            <div class="segment-range-slider" id="segment-range-${index}">
                                <div class="segment-range-track"></div>
                                <div class="segment-range-selected" id="segment-range-selected-${index}"></div>
                                <input type="range" min="0" max="${maxLeds}" step="1" value="${event.st ?? 0}"
                                    class="segment-range-min" id="segment-range-min-${index}"
                                    oninput="updateSegmentRange(${index})" ${!event.en ? 'disabled' : ''}>
                                <input type="range" min="0" max="${maxLeds}" step="1" value="${(event.st ?? 0) + ((event.ln ?? 0) === 0 ? maxLeds : event.ln)}"
                                    class="segment-range-max" id="segment-range-max-${index}"
                                    oninput="updateSegmentRange(${index})" ${!event.en ? 'disabled' : ''}>
                            </div>
                            <div class="segment-range-values">
                                <span data-i18n="eventsConfig.start">${t('eventsConfig.start')}</span>: <span id="segment-value-start-${index}">${event.st ?? 0}</span> |
                                <span data-i18n="eventsConfig.length">${t('eventsConfig.length')}</span>: <span id="segment-value-length-${index}">${(event.ln ?? 0) === 0 ? maxLeds : event.ln}</span>
                            </div>
                        </div>
                    </div>
                    ` : ''}
                </div>
            </div>
        `;
        container.appendChild(accordionItem);
    });

    // Event delegation pour les selects d'action (évite les fuites mémoire)
    container.querySelectorAll('[id^="event-action-"]').forEach(select => {
        select.addEventListener('change', function() {
            const idx = parseInt(this.dataset.eventIndex);
            updateEventConfig(idx, 'at', parseInt(this.value));
            renderEventsTable();
        });
    });

    // Restaurer l'état des accordéons qui étaient ouverts
    openAccordions.forEach(index => {
        const header = container.querySelector(`#event-accordion-item-${index} .event-accordion-header`);
        const content = $(`event-accordion-content-${index}`);
        if (header && content) {
            header.classList.add('active');
            content.classList.add('active');
        }
    });
}
function updateSegmentRange(index) {
    const minSlider = $('segment-range-min-' + index);
    const maxSlider = $('segment-range-max-' + index);
    const selected = $('segment-range-selected-' + index);
    const startValue = $('segment-value-start-' + index);
    const lengthValue = $('segment-value-length-' + index);

    if (!minSlider || !maxSlider) {
        return;
    }

    const { start, length } = renderSegmentRange(minSlider, maxSlider, selected, startValue, lengthValue);
    const lengthToSave = (length === maxLeds) ? 0 : length;

    // Mettre à jour les données
    updateEventConfig(index, 'st', start);
    updateEventConfig(index, 'ln', lengthToSave);
}

function updateEventConfig(index, field, value) {
    eventsConfigData[index][field] = value;
    if (field === 'action_type') {
        const event = eventsConfigData[index];
        if (value === 0) {
            event.pid = -1;
        } else if (value === 1) {
            event.fx = getDefaultEffectId();
        } else if (value === 2) {
            if (event.pid === undefined || event.pid === null) {
                event.pid = -1;
            }
            if (!event.fx) {
                event.fx = getDefaultEffectId();
            }
        }
    }

    // Mettre à jour le résumé dans le header
    updateEventSummary(index);

    scheduleEventAutoSave(index);
}
function toggleEventAccordion(index) {
    const headers = doc.querySelectorAll('.event-accordion-header');
    const header = headers[index];
    const content = $('event-accordion-content-' + index);

    if (!header || !content) {
        return;
    }

    const isActive = header.classList.contains('active');

    if (isActive) {
        // Fermer l'accordéon
        header.classList.remove('active');
        content.classList.remove('active');
    } else {
        // Fermer tous les autres accordéons
        headers.forEach((h, i) => {
            if (i !== index) {
                h.classList.remove('active');
                const otherContent = $('event-accordion-content-' + i);
                if (otherContent) {
                    otherContent.classList.remove('active');
                }
            }
        });

        // Ouvrir l'accordéon sélectionné
        header.classList.add('active');
        content.classList.add('active');

        // Initialiser le segment range slider visuellement
        setTimeout(() => updateSegmentRange(index), 10);
    }
}
function toggleEventFormFields(index) {
    const enabled = $('event-enabled-' + index)?.checked;
    const form = $('event-accordion-form-' + index);
    const item = $('event-accordion-item-' + index);

    if (!form) {
        return;
    }

    // Désactiver/activer tous les champs sauf la checkbox "enabled"
    const inputs = form.querySelectorAll('input:not(#event-enabled-' + index + '), select');
    inputs.forEach(input => {
        input.disabled = !enabled;
    });

    // Mettre à jour la classe CSS de l'item pour le style visuel
    if (item) {
        if (enabled) {
            item.classList.remove('disabled');
            item.classList.add('enabled');
        } else {
            item.classList.remove('enabled');
            item.classList.add('disabled');
        }
    }

    // Mettre à jour le toggle de test
    const eventId = eventsConfigData[index]?.ev;
    if (eventId) {
        const testToggle = $('toggle-' + eventId);
        if (testToggle) {
            testToggle.disabled = !enabled;
            if (!enabled && testToggle.checked) {
                testToggle.checked = false;
                simulationTogglesState[eventId] = false;
            }
        }
    }

    // Mettre à jour le résumé dans le header
    updateEventSummary(index);
}
function updateEventSummary(index) {
    const event = eventsConfigData[index];
    if (!event) {
        return;
    }

    const headers = doc.querySelectorAll('.event-accordion-header');
    const header = headers[index];
    if (!header) {
        return;
    }

    const summary = header.querySelector('.event-accordion-summary');
    if (!summary) {
        return;
    }

    const actionType = event.at !== undefined ? event.at : 0;
    const profileId = event.pid !== undefined ? event.pid : -1;
    const profileSelect = $('profile-select');

    let summaryHTML = `
        <div class="event-accordion-summary-item enabled-item">
            <span class="event-accordion-summary-label">${t('eventsConfig.enabled')}:</span>
            <span class="event-accordion-summary-value">${event.en ? '✓' : '✗'}</span>
        </div>
    `;

    if (actionType === 0) {
        const effectName = getEffectName(event.fx);
        const colorHex = '#' + event.c1.toString(16).padStart(6, '0');
        const displayLength = (event.ln ?? 0) === 0 ? maxLeds : event.ln;
        const segmentInfo = `[${event.st ?? 0}→${(event.st ?? 0) + displayLength}]`;
        summaryHTML += `
            <div class="event-accordion-summary-item effect-item">
                <span class="event-accordion-summary-label">${t('eventsConfig.effect')}:</span>
                <span class="event-accordion-summary-value">${effectName} ${segmentInfo}</span>
            </div>
            <div class="event-accordion-summary-item color-item">
                <span class="event-accordion-summary-label">${t('eventsConfig.color')}:</span>
                <span class="event-accordion-color-preview" style="background-color: ${colorHex};"></span>
            </div>
        `;
    } else {
        const profileName = profileSelect && profileId >= 0 ?
            profileSelect.options[Array.from(profileSelect.options).findIndex(opt => opt.value == profileId)]?.text || '--' :
            '--';
        summaryHTML += `
            <div class="event-accordion-summary-item effect-item">
                <span class="event-accordion-summary-label">${t('eventsConfig.profile')}:</span>
                <span class="event-accordion-summary-value">${profileName}</span>
            </div>
        `;
    }

    summary.innerHTML = summaryHTML;
}
function scheduleEventAutoSave(index) {
    if (!eventsConfigData[index]) {
        return;
    }
    if (eventAutoSaveTimers.has(index)) {
        clearTimeout(eventAutoSaveTimers.get(index));
    }
    const timer = setTimeout(() => {
        eventAutoSaveTimers.delete(index);
        autoSaveEvent(index);
    }, EVENT_SAVE_DEBOUNCE_MS);
    eventAutoSaveTimers.set(index, timer);
}
function buildEventPayload(event) {
    if (!event) {
        return null;
    }
    const allowedKeys = [
        'ev', 'fx', 'br', 'sp', 'c1', 'rv',
        'dur', 'pri', 'en', 'at', 'pid', 'csp',
        'st', 'ln'
    ];
    const payload = {};
    allowedKeys.forEach(key => {
        if (event[key] !== undefined) {
            payload[key] = event[key];
        }
    });
    return payload;
}
async function autoSaveEvent(index) {
    const payload = buildEventPayload(eventsConfigData[index]);
    if (!payload) {
        return;
    }
    try {
        const response = await fetch(API_BASE + '/api/events/update', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ event: payload })
        });
        if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        }
    } catch (error) {
        console.error('Event auto-save failed', error);
        showNotification('events-notification', t('eventsConfig.saveError'), 'error');
    }
}
function scheduleDefaultEffectSave() {
    if (defaultEffectSaveTimer) {
        clearTimeout(defaultEffectSaveTimer);
    }
    defaultEffectSaveTimer = setTimeout(() => {
        defaultEffectSaveTimer = null;
        saveProfile(true);
    }, DEFAULT_EFFECT_DEBOUNCE_MS);
}
// Hardware Configuration
async function loadHardwareConfig() {
    try {
        const response = await fetch(API_BASE + '/api/config');
        const config = await response.json();
        if (config.lc !== undefined) {
            $('led-count').value = config.lc;
        }
        if (config.wheel_ctl !== undefined) {
            $('wheel-control-toggle').checked = !!config.wheel_ctl;
        }
        if (config.wheel_spd !== undefined) {
            $('wheel-speed-limit').value = config.wheel_spd;
        }
    } catch (e) {
        console.error('Error:', e);
        showNotification('config-notification', t('config.loadError'), 'error');
    }
}

// Debounce timer pour saveHardwareConfig
let saveHardwareConfigTimeout = null;

// Sauvegarder la configuration matérielle avec debounce
async function saveHardwareConfig() {
    // Annuler le timer précédent
    if (saveHardwareConfigTimeout) {
        clearTimeout(saveHardwareConfigTimeout);
    }

    // Créer un nouveau timer
    saveHardwareConfigTimeout = setTimeout(async () => {
        await saveHardwareConfigImmediate();
    }, HARDWARE_SAVE_DEBOUNCE_MS);
}

// Fonction interne pour sauvegarder immédiatement
async function saveHardwareConfigImmediate() {
    const config = {
        lc: parseInt($('led-count').value),
        wheel_ctl: $('wheel-control-toggle').checked,
        wheel_spd: parseInt($('wheel-speed-limit').value, 10) || 0
    };
    try {
        const response = await fetch(API_BASE + '/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        if (response.ok) {
            showNotification('config-notification', t('config.saveSuccess'), 'success');
        } else {
            showNotification('config-notification', t('config.saveError'), 'error');
        }
    } catch (e) {
        console.error('Error:', e);
        showNotification('config-notification', t('config.saveError'), 'error');
    }
}
// Factory Reset
function confirmFactoryReset() {
    if (confirm(t('config.factoryResetConfirm'))) {
        performFactoryReset();
    }
}
async function performFactoryReset() {
    try {
        showNotification('config-notification', t('config.factoryResetInProgress'), 'info');
        const response = await fetch(API_BASE + '/api/factory-reset', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });
        const data = await response.json();
        if (response.ok && data.st === 'ok') {
            showNotification('config-notification', t('config.factoryResetSuccess'), 'success');
            // L'ESP32 va redémarrer, afficher un message
            setTimeout(() => {
                alert(t('config.deviceRestarting'));
                location.reload();
            }, 2000);
        } else {
            showNotification('config-notification', data.msg || t('config.factoryResetError'), 'error');
        }
    } catch (e) {
        console.error('Error:', e);
        showNotification('config-notification', t('config.factoryResetError'), 'error');
    }
}
// Gestion des profils
async function loadProfiles() {
    try {
        const response = await fetch(API_BASE + '/api/profiles');
        const data = await response.json();
        const select = $('profile-select');
        select.innerHTML = '';
        data.profiles.forEach(profile => {
            const option = doc.createElement('option');
            option.value = profile.id;
            option.dataset.name = profile.n;
            const activeSuffix = profile.ac ? ' ' + t('profiles.activeSuffix') : '';
            option.textContent = profile.n + activeSuffix;
            if (profile.ac) option.selected = true;
            select.appendChild(option);
        });
        $('profile-status').textContent = data.an;

        // Afficher les statistiques de stockage
        if (data.storage) {
            const storageInfo = $('storage-info');
            const storageText = $('storage-text');
            const storageBar = $('storage-bar');

            const usagePct = data.storage.usage_pct || 0;
            const used = data.storage.used || 0;
            const total = data.storage.total || 0;
            const free = data.storage.free || 0;

            storageText.textContent = t('profiles.storageUsage', usagePct, used, total);
            storageBar.style.width = usagePct + '%';

            // Changer la couleur selon l'utilisation
            if (usagePct < 50) {
                storageBar.style.background = '#4CAF50';
            } else if (usagePct < 80) {
                storageBar.style.background = '#FFC107';
            } else {
                storageBar.style.background = '#F44336';
            }

            // Désactiver les boutons de création si espace insuffisant (< 200 entries libres)
            const canCreate = free >= 200;
            const newButton = $('profile-new-button');
            const importButton = $('profile-import-button');
            if (newButton) {
                newButton.disabled = !canCreate;
                newButton.title = canCreate ? '' : t('profiles.storageInsufficient');
            }
            if (importButton) {
                importButton.disabled = !canCreate;
                importButton.title = canCreate ? '' : t('profiles.storageInsufficient');
            }

            storageInfo.style.display = 'block';
        }
    } catch (e) {
        console.error('Erreur:', e);
    }
}
async function activateProfile() {
    const profileId = parseInt($('profile-select').value);
    try {
        await fetch(API_BASE + '/api/profile/activate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ pid: profileId })
        });
        loadProfiles();
        loadConfig();
    } catch (e) {
        console.error('Erreur:', e);
    }
}
function showNewProfileDialog() {
    $('newProfileModal').classList.add('active');
}
function hideNewProfileDialog() {
    $('newProfileModal').classList.remove('active');
}
async function createProfile() {
    const name = $('new-profile-name').value;
    if (!name) return;
    try {
        const response = await fetch(API_BASE + '/api/profile/create', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name: name })
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            hideNewProfileDialog();
            $('new-profile-name').value = '';
            await loadProfiles();
            await activateProfile();
            showNotification('profiles-notification', t('profiles.create') + ' ' + t('config.saveSuccess'), 'success');
        } else {
            const message = apiResult.data?.msg || apiResult.raw || t('config.saveError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('profiles-notification', e.message || t('config.saveError'), 'error');
    }
}
async function deleteProfile() {
    const profileId = parseInt($('profile-select').value);
    if (!confirm(t('profiles.deleteConfirm'))) return;
    try {
        const response = await fetch(API_BASE + '/api/profile/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ pid: profileId })
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            await loadProfiles();
            await activateProfile();
            showNotification('profiles-notification', t('profiles.delete') + ' ' + t('config.saveSuccess'), 'success');
        } else {
            const message = apiResult.data?.msg || apiResult.raw || t('config.saveError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('profiles-notification', e.message || t('config.saveError'), 'error');
    }
}
function showRenameProfileDialog() {
    const select = $('profile-select');
    if (!select || select.options.length === 0) {
        showNotification('profiles-notification', t('profiles.selectProfile'), 'error');
        return;
    }
    const selectedOption = select.options[select.selectedIndex];
    const currentName = (selectedOption && selectedOption.dataset && selectedOption.dataset.name) ? selectedOption.dataset.name : (selectedOption ? selectedOption.textContent : '');
    $('rename-profile-name').value = currentName;
    $('renameProfileModal').classList.add('active');
}
function hideRenameProfileDialog() {
    $('renameProfileModal').classList.remove('active');
}
async function renameProfile() {
    const profileId = parseInt($('profile-select').value);
    const newName = $('rename-profile-name').value.trim();
    if (!newName) {
        showNotification('profiles-notification', t('profiles.nameRequired') || t('profiles.profileName'), 'error');
        return;
    }
    try {
        const response = await fetch(API_BASE + '/api/profile/rename', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ pid: profileId, name: newName })
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            hideRenameProfileDialog();
            await loadProfiles();
            const select = $('profile-select');
            if (select) {
                select.value = profileId;
            }
            showNotification('profiles-notification', t('profiles.renameSuccess'), 'success');
        } else {
            const message = apiResult.data?.msg || apiResult.raw || t('profiles.renameError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('profiles-notification', e.message || t('profiles.renameError'), 'error');
    }
}
async function exportProfile() {
    const profileId = parseInt($('profile-select').value);
    if (profileId < 0) {
        showNotification('profiles-notification', t('profiles.selectProfile'), 'error');
        return;
    }
    try {
        const response = await fetch(API_BASE + '/api/profile/export?profile_id=' + profileId);
        if (response.ok) {
            const blob = await response.blob();
            const url = window.URL.createObjectURL(blob);
            const a = doc.createElement('a');
            a.style.display = 'none';
            a.href = url;
            a.download = 'profile_' + profileId + '.json';
            doc.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            doc.body.removeChild(a);
            showNotification('profiles-notification', t('profiles.exportSuccess'), 'success');
        } else {
            showNotification('profiles-notification', t('profiles.exportError'), 'error');
        }
    } catch (e) {
        console.error('Erreur export:', e);
        showNotification('profiles-notification', t('profiles.exportError'), 'error');
    }
}
function showImportDialog() {
    const input = doc.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.multiple = true; // Permettre la sélection multiple
    input.onchange = async (e) => {
        const files = e.target.files;
        if (!files || files.length === 0) return;

        showNotification('profiles-notification', t('profiles.importQueue', files.length), 'info');

        // Créer une file d'attente pour uploader les fichiers un par un
        for (let i = 0; i < files.length; i++) {
            const file = files[i];
            try {
                // Afficher une notification pour le fichier en cours
                showNotification('profiles-notification', t('profiles.importing', file.name, i + 1, files.length), 'info');

                const profileData = await new Promise((resolve, reject) => {
                    const reader = new FileReader();
                    reader.onload = (event) => resolve(JSON.parse(event.target.result));
                    reader.onerror = (error) => reject(error);
                    reader.readAsText(file);
                });

                const response = await fetch(API_BASE + '/api/profile/import', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        profile_data: profileData
                    })
                });

                const result = await response.json();

                if (result.st === 'ok') {
                    // Recharger les profils après chaque import réussi pour mettre la liste à jour
                    await loadProfiles(); 
                    if (typeof result.pid === 'number') {
                        const select = $('profile-select');
                        if (select) {
                            select.value = result.pid;
                        }
                    }
                } else {
                    const message = result.msg || t('profiles.importErrorFile', file.name);
                    showNotification('profiles-notification', message, 'error');
                    // Arrêter la file d'attente en cas d'erreur
                    break; 
                }
            } catch (err) {
                console.error('Erreur import:', err);
                showNotification('profiles-notification', t('profiles.importErrorFile', file.name), 'error');
                // Arrêter la file d'attente en cas d'erreur
                break;
            }
        }

        // Notification finale
        showNotification('profiles-notification', t('profiles.importComplete'), 'success');
        // Recharger la configuration complète du dernier profil importé (ou du profil actif)
        await loadConfig();
    };
    input.click();
}
// Fonction unifiée pour sauvegarder le profil (settings + effet par défaut)
async function saveProfile(params = {}) {
    if (bleTransportInstance && bleTransportInstance.waitForQueue) {
        await bleTransportInstance.waitForQueue();
    }

    const profileId = parseInt($('profile-select').value);
    const payload = { pid: profileId };

    // Paramètres profil (luminosité dynamique)
    if (params.settings !== false) {
        payload.dbe = $('dynamic-brightness-enabled').checked;
        payload.dbr = parseInt($('dynamic-brightness-rate').value);
    }

    // Effet par défaut
    if (params.defaultEffect !== false) {
        payload.fx = effectIdToEnum($('default-effect-select').value);
        payload.br = percentTo255(parseInt($('default-brightness-slider').value));
        payload.sp = percentTo255(parseInt($('default-speed-slider').value));
        payload.c1 = parseInt($('default-color1').value.substring(1), 16);
        const audioReactiveCheckbox = $('default-audio-reactive');
        payload.ar = audioReactiveCheckbox ? audioReactiveCheckbox.checked : false;

        // Reverse
        const reverseCheckbox = $('default-reverse');
        payload.rv = reverseCheckbox ? reverseCheckbox.checked : false;

        // Segment range
        const segment = getDefaultSegmentRange();
        if (segment) {
            payload.st = segment.start;
            payload.ln = segment.length;
            console.log(`[Profile] Saving default effect segment: st=${segment.start}, ln=${segment.length}`);
        }

        // Accel pedal modulation
        const accelPedalEnabledCheckbox = $('default-accel-pedal-enabled');
        payload.ape = accelPedalEnabledCheckbox ? accelPedalEnabledCheckbox.checked : false;
        const accelPedalOffsetSlider = $('default-accel-pedal-offset-slider');
        payload.apo = accelPedalOffsetSlider ? parseInt(accelPedalOffsetSlider.value) : 0;
    }

    try {
        const response = await fetch(API_BASE + '/api/profile/update', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            if (!params.silent) {
                const msg = params.defaultEffect !== false
                    ? t('profiles.saveDefault') + ' ' + t('config.saveSuccess')
                    : t('config.saveSuccess');
                showNotification('profiles-notification', msg, 'success');
            }
        } else {
            const message = apiResult.data?.msg || apiResult.raw || t('config.saveError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Error saving profile:', e);
        showNotification('profiles-notification', e.message || t('config.saveError'), 'error');
    }
}
// Appliquer l'effet
// Mise à jour du statut
async function updateStatus() {
    try {
        const response = await fetch(API_BASE + '/api/status');
        const data = await response.json();

        // Détecter le mode AP (pas de connexion WiFi station)
        isApMode = !data.wc;

        $('wifi-status').textContent = data.wc ? t('status.connected') : t('status.ap');
        $('wifi-status').className = 'status-value status-online';

        // CPU usage
        if (data.cpu !== undefined) {
            $('cpu-status').textContent = data.cpu + '%';
            // Couleur selon la charge: vert <50%, orange 50-80%, rouge >80%
            const cpuClass = data.cpu < 50 ? 'status-online' : (data.cpu < 80 ? 'status-warning' : 'status-offline');
            $('cpu-status').className = 'status-value ' + cpuClass;
        } else {
            $('cpu-status').textContent = '...';
            $('cpu-status').className = 'status-value';
        }

        // CAN Body status
        if (data.cbb) {
            const bodyStatus = data.cbb.r ?
                `${t('status.connected')}` : // (RX:${data.cbb.rx}, TX:${data.cbb.tx})` :
                t('status.disconnected');
            $('can-body-status').textContent = bodyStatus;
            $('can-body-status').className = 'status-value ' + (data.cbb.r ? 'status-online' : 'status-offline');
        }        
        // CAN Chassis status
        if (data.cbc) {
            const chassisStatus = data.cbc.r ?
                `${t('status.connected')}` : // (RX:${data.cbc.rx}, TX:${data.cbc.tx})` :
                t('status.disconnected');
            $('can-chassis-status').textContent = chassisStatus;
            $('can-chassis-status').className = 'status-value ' + (data.cbc.r ? 'status-online' : 'status-offline');
        }

        $('vehicle-status').textContent = data.va ? t('status.active') : t('status.inactive');
        $('vehicle-status').className = 'status-value ' + (data.va ? 'status-online' : 'status-offline');
        if (data.pn) {
            $('profile-status').textContent = data.pn;
        }
        // Données véhicule complètes
        if (data.vehicle && data.va) {
            const v = data.vehicle;
            // État général
            $('v-speed').textContent = v.s.toFixed(1) + ' km/h';
            $('v-gear').textContent = ['--', 'P', 'R', 'N', 'D'][v.g] || '--';
            $('v-brake').textContent = v.bp ? t('status.active') : t('status.inactive');
            $('v-accel-pedal').textContent = (v.ap !== undefined ? v.ap : 0) + '%';
            $('v-locked').textContent = v.lk ? t('vehicle.locked') : t('vehicle.unlocked');
            // Portes
            if (v.doors) {
                $('v-door-fl').textContent = v.doors.fl ? t('vehicle.open') : t('vehicle.closed');
                $('v-door-fr').textContent = v.doors.fr ? t('vehicle.open') : t('vehicle.closed');
                $('v-door-rl').textContent = v.doors.rl ? t('vehicle.open') : t('vehicle.closed');
                $('v-door-rr').textContent = v.doors.rr ? t('vehicle.open') : t('vehicle.closed');
                $('v-trunk').textContent = v.doors.t ? t('vehicle.open') : t('vehicle.closed');
                $('v-frunk').textContent = v.doors.f ? t('vehicle.open') : t('vehicle.closed');
            }
            // Charge
            if (v.charge) {
                $('v-charging').textContent = v.charge.ch ? t('status.active') : t('status.inactive');
                $('v-charge').textContent = v.charge.pct?.toFixed(1) + '%';
                $('v-charge-power').textContent = v.charge.pw?.toFixed(1) + ' kW';
            }
            // Lumières
            if (v.lights) {
                $('v-headlights').textContent = v.lights.h ? t('status.active') : t('status.inactive');
                $('v-high-beams').textContent = v.lights.hb ? t('status.active') : t('status.inactive');
                $('v-fog-lights').textContent = v.lights.fg ? t('status.active') : t('status.inactive');
                if(v.lights.hz) {
                  $('v-turn-signal').textContent = t('vehicle.hazard');
                } else {
                  $('v-turn-signal').textContent = v.lights.tl ? t('vehicle.left'): v.lights.tr ? t('vehicle.right'): t('vehicle.off')
                }
            }
            // Batterie et autres
            $('v-battery-lv').textContent = v.blv?.toFixed(2) + ' V';
            $('v-battery-hv').textContent = v.bhv?.toFixed(2) + ' V';
            $('v-odometer').textContent = v.odo.toLocaleString() + ' km';
            // Sécurité
            if (v.safety) {
                $('v-night').textContent = v.safety.nm ? t('status.active') : t('status.inactive');
                $('v-brightness').textContent = v.safety.bri + '%';
                $('v-blindspot-left').textContent = v.safety.bsl ? t('status.active') : t('status.inactive');
                $('v-blindspot-right').textContent = v.safety.bsr ? t('status.active') : t('status.inactive');
                $('v-side-collision-left').textContent = v.safety.scl ? t('status.active') : t('status.inactive');
                $('v-side-collision-right').textContent = v.safety.scr ? t('status.active') : t('status.inactive');

                $('v-lane-departure-left-lv1').textContent = v.safety.ldl1 ? t('status.active') : t('status.inactive');
                $('v-lane-departure-left-lv2').textContent = v.safety.ldl2 ? t('status.active') : t('status.inactive');
                $('v-lane-departure-right-lv1').textContent = v.safety.ldr1 ? t('status.active') : t('status.inactive');
                $('v-lane-departure-right-lv2').textContent = v.safety.ldr2 ? t('status.active') : t('status.inactive');

                const sentryModeEl = $('v-sentry-mode');
                if (sentryModeEl) {
                    if (typeof v.safety.sm === 'boolean') {
                        sentryModeEl.textContent = v.safety.sm ? t('vehicle.sentryOn') : t('vehicle.sentryOff');
                    } else {
                        sentryModeEl.textContent = t('vehicle.none');
                    }
                }

                const autopilotEl = $('v-autopilot');
                if (autopilotEl) {
                    const requestMap = {
                      0: t('vehicle.DISABLED'),
                      1: t('vehicle.UNAVAILABLE'),
                      3: t('vehicle.ACTIVE_NOMINAL'),
                      4: t('vehicle.ACTIVE_RESTRICTED'),
                      5: t('vehicle.ACTIVE_NAV'),
                      2: t('vehicle.AVAILABLE'),
                      8: t('vehicle.ABORTING'),
                      9: t('vehicle.ABORTED'),
                      14: t('vehicle.FAULT')
                    };
                    const autopilotState = v.safety.ap;
                    autopilotEl.textContent = autopilotState ? (requestMap[autopilotState] || autopilotState) : t('vehicle.none');
                }

                const sentryAlertEl = $('v-sentry-alert');
                if (sentryAlertEl) {
                    if (typeof v.safety.sa === 'boolean') {
                        sentryAlertEl.textContent = v.safety.sa ? t('vehicle.sentryAlertActive') : t('vehicle.none');
                    } else {
                        sentryAlertEl.textContent = t('vehicle.none');
                    }
                }
            }
        } else {
            // Afficher des tirets quand il n'y a pas de données
            const fields = [
                'v-speed', 'v-gear', 'v-brake', 'v-locked',
                'v-door-fl', 'v-door-fr', 'v-door-rl', 'v-door-rr', 'v-trunk', 'v-frunk',
                'v-charging', 'v-charge', 'v-charge-power',
                'v-headlights', 'v-high-beams', 'v-fog-lights', 'v-turn-signal',
                'v-battery-lv', 'v-battery-hv', 'v-odometer', 'v-night', 'v-brightness',
                'v-blindspot-left', 'v-blindspot-right',
                'v-side-collision-left', 'v-side-collision-right',
                'v-lane-departure-left-lv1', 'v-lane-departure-right-lv1',
                'v-lane-departure-left-lv2', 'v-lane-departure-right-lv2',
                'v-sentry-mode', 'v-autopilot', 'v-sentry-alert'
            ];
            fields.forEach(id => {
                const element = $(id);
                if (element) element.textContent = '--';
            });
        }
    } catch (e) {
        console.error('Erreur:', e);
    } finally {
        // Planifier le prochain appel après avoir terminé (évite les bouchons)
        if (statusIntervalHandle !== null && statusIntervalHandle !== 'stopped') {
            // Annuler le précédent timeout s'il existe
            if (typeof statusIntervalHandle === 'number') {
                clearTimeout(statusIntervalHandle);
            }
            statusIntervalHandle = setTimeout(updateStatus, 2000);
        }
    }
}
// Chargement de la config
async function loadConfig() {
    try {
        const response = await fetch(API_BASE + '/api/config');
        const config = await response.json();

        // Mettre à jour le nombre max de LEDs
        if (config.lc && config.lc > 0) {
            maxLeds = config.lc;
        }

        // Charger l'effet par défaut et les paramètres du profil actif
        loadActiveProfileDefaultEffect();
    } catch (e) {
        console.error('Erreur:', e);
    }
}
// Charger l'effet par défaut du profil actif
async function loadActiveProfileDefaultEffect() {
    try {
        const response = await fetch(API_BASE + '/api/profiles');
        const data = await response.json();
        const activeProfile = data.profiles.find(p => p.ac);
        if (activeProfile && activeProfile.default_effect) {
            const defaultEffect = activeProfile.default_effect;
            // Convert numeric enum to string ID for the dropdown
            const effectId = effectEnumToId(defaultEffect.fx);
            $('default-effect-select').value = effectId;
            const defBrightnessPercent = to255ToPercent(defaultEffect.br);
            const defSpeedPercent = to255ToPercent(defaultEffect.sp);
            $('default-brightness-slider').value = defBrightnessPercent;
            $('default-brightness-value').textContent = defBrightnessPercent + '%';
            $('default-speed-slider').value = defSpeedPercent;
            $('default-speed-value').textContent = defSpeedPercent + '%';
            $('default-color1').value = '#' + defaultEffect.c1.toString(16).padStart(6, '0');

            // Audio reactive
            const audioReactiveCheckbox = $('default-audio-reactive');
            if (audioReactiveCheckbox && defaultEffect.ar !== undefined) {
                audioReactiveCheckbox.checked = defaultEffect.ar;
            }

            // Reverse
            const reverseCheckbox = $('default-reverse');
            if (reverseCheckbox && defaultEffect.rv !== undefined) {
                reverseCheckbox.checked = defaultEffect.rv;
            }

            // Segment range
            const start = defaultEffect.st !== undefined ? defaultEffect.st : 0;
            const length = (defaultEffect.ln !== undefined && defaultEffect.ln !== 0) ? defaultEffect.ln : maxLeds;
            console.log(`[Profile] Loading default effect segment: st=${start}, ln=${defaultEffect.ln}, calculated length=${length}, maxLeds=${maxLeds}`);
            setDefaultSegmentRange(start, length);

            // Accel pedal modulation
            const accelPedalEnabledCheckbox = $('default-accel-pedal-enabled');
            if (accelPedalEnabledCheckbox && defaultEffect.ape !== undefined) {
                accelPedalEnabledCheckbox.checked = defaultEffect.ape;
            }
            const accelPedalOffsetSlider = $('default-accel-pedal-offset-slider');
            const accelPedalOffsetValue = $('default-accel-pedal-offset-value');
            if (accelPedalOffsetSlider && defaultEffect.apo !== undefined) {
                accelPedalOffsetSlider.value = defaultEffect.apo;
                if (accelPedalOffsetValue) {
                    accelPedalOffsetValue.textContent = defaultEffect.apo + '%';
                }
            }

            // Luminosité dynamique
            const dynBrightEnabled = $('dynamic-brightness-enabled');
            if (dynBrightEnabled && activeProfile.dbe !== undefined) {
                dynBrightEnabled.checked = activeProfile.dbe;
            }
            const dynBrightRate = $('dynamic-brightness-rate');
            const dynBrightRateValue = $('dynamic-brightness-rate-value');
            if (dynBrightRate && activeProfile.dbr !== undefined) {
                dynBrightRate.value = activeProfile.dbr;
                if (dynBrightRateValue) {
                    dynBrightRateValue.textContent = activeProfile.dbr + '%';
                }
            }
        }
    } catch (e) {
        console.error('Error loading default effect:', e);
    }
}

// ============================================================================
// AUDIO FUNCTIONS
// ============================================================================

let audioEnabled = false;
let audioUpdateInterval = null;
function applyAudioEffectAvailability() {
    const selects = doc.querySelectorAll('[data-effect-options="true"]');
    selects.forEach(select => {
        Array.from(select.options).forEach(opt => {
            if (opt.getAttribute('data-audio-effect') === 'true') {
                opt.disabled = !audioEnabled;
                if (!audioEnabled) {
                    opt.classList.add('audio-disabled');
                } else {
                    opt.classList.remove('audio-disabled');
                }
            }
        });
    });
}

function applyAudioEnabledState(enabled) {
    audioEnabled = enabled;
    const checkbox = $('audio-enable');
    if (checkbox) {
        checkbox.checked = enabled;
    }

    const statusEl = $('audio-status');
    if (statusEl) {
        statusEl.textContent = t(`audio.${enabled ? 'enabled' : 'disabled'}`);
        statusEl.style.color = enabled ? '#10B981' : 'var(--color-muted)';
    }

    const calibrationBtn = $('audio-calibrate-btn');
    if (calibrationBtn) {
        calibrationBtn.disabled = !enabled;
    }

    applyAudioEffectAvailability();

    const settingsEl = $('audio-settings');
    if (settingsEl) {
        settingsEl.style.display = enabled ? 'block' : 'none';
    }
}

function applyAudioCalibrationState(calibration) {
    const statusEl = $('audio-calibration-status');
    const noiseEl = $('audio-calibration-noise');
    const peakEl = $('audio-calibration-peak');

    if (!statusEl) {
        return;
    }

    if (calibration && calibration.av) {
        statusEl.textContent = t('audio.calibrated');
        statusEl.style.color = '#10B981';
        if (noiseEl) {
            noiseEl.textContent = `${Math.round((calibration.nf || 0) * 100)}%`;
        }
        if (peakEl) {
            peakEl.textContent = `${Math.round((calibration.pk || 1) * 100)}%`;
        }
    } else {
        statusEl.textContent = t('audio.notCalibrated');
        statusEl.style.color = 'var(--color-muted)';
        if (noiseEl) {
            noiseEl.textContent = '-';
        }
        if (peakEl) {
            peakEl.textContent = '-';
        }
    }
}

// Charger le statut audio
async function loadAudioStatus() {
    try {
        const response = await fetch(API_BASE + '/api/audio/status');
        const data = await response.json();

        applyAudioEnabledState(data.en);
        $('audio-sensitivity').value = data.sen;
        $('audio-sensitivity-value').textContent = data.sen;
        $('audio-gain').value = data.gn;
        $('audio-gain-value').textContent = data.gn;
        $('audio-auto-gain').checked = data.ag;
        applyAudioCalibrationState(data.cal);

        if (audioEnabled) {
            startAudioDataPolling();
        }
    } catch (error) {
        console.error('Failed to load audio status:', error);
    }
}

// Lancer une calibration ponctuelle du micro
async function startAudioCalibration() {
    if (!audioEnabled) {
        showNotification('config-notification', t('audio.calibrationNeedsMic'), 'warning');
        return;
    }

    const btn = $('audio-calibrate-btn');
    const originalText = btn ? btn.textContent : '';

    if (btn) {
        btn.disabled = true;
        btn.textContent = t('audio.calibrating');
    }

    // Feedback utilisateur sur les phases (silence puis musique)
    showNotification('config-notification', t('audio.calibPhaseSilence'), 'info', 3000);
    const phaseTimers = [];
    phaseTimers.push(setTimeout(() => {
        showNotification('config-notification', t('audio.calibPhaseMusic'), 'info', 3000);
    }, 3200)); // après ~3s passer à la musique

    try {
        const response = await fetch(API_BASE + '/api/audio/calibrate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ dur: 6000 })
        });

        const data = await response.json();

        if (response.ok && data.ok) {
            applyAudioCalibrationState(data.cal);
            showNotification('config-notification', t('audio.calibrationSaved'), 'success');
        } else {
            showNotification('config-notification', t('audio.calibrationFailed'), 'error');
        }
    } catch (error) {
        console.error('Failed to calibrate microphone:', error);
        showNotification('config-notification', t('audio.calibrationFailed'), 'error');
    } finally {
        phaseTimers.forEach(clearTimeout);
        if (btn) {
            btn.disabled = !audioEnabled;
            btn.textContent = originalText || t('audio.calibrate');
        }
    }
}

// Activer/désactiver le micro
async function toggleAudio(enabled) {
    try {
        const response = await fetch(API_BASE + '/api/audio/enable', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ enabled })
        });

        if (response.ok) {
            applyAudioEnabledState(enabled);

            if (enabled) {
                loadFFTStatus();
                startAudioDataPolling();
            } else {
                stopAudioDataPolling();
            }

            showNotification('config-notification', enabled ? 'Audio enabled' : 'Audio disabled', 'success');
        }
    } catch (error) {
        console.error('Failed to toggle audio:', error);
        showNotification('config-notification', 'Failed to toggle audio', 'error');
    }
}

// Debounce timer pour saveAudioConfig
let saveAudioConfigTimeout = null;

// Mettre à jour les valeurs des sliders
function updateAudioValue(param, value) {
    $(`audio-${param}-value`).textContent = value;
    saveAudioConfig();
}

// Sauvegarder la configuration audio avec debounce
async function saveAudioConfig() {
    // Annuler le timer précédent
    if (saveAudioConfigTimeout) {
        clearTimeout(saveAudioConfigTimeout);
    }

    // Créer un nouveau timer
    saveAudioConfigTimeout = setTimeout(async () => {
        await saveAudioConfigImmediate();
    }, AUDIO_SAVE_DEBOUNCE_MS);
}

// Fonction interne pour sauvegarder immédiatement
async function saveAudioConfigImmediate() {
    const config = {
        sensitivity: parseInt($('audio-sensitivity').value),
        gain: parseInt($('audio-gain').value),
        autoGain: $('audio-auto-gain').checked
        // fftEnabled est géré automatiquement selon l'effet sélectionné
    };

    try {
        const response = await fetch(API_BASE + '/api/audio/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        if (response.ok) {
            showNotification('config-notification', 'Audio configuration saved', 'success');
        }
    } catch (error) {
        console.error('Failed to save audio config:', error);
        showNotification('config-notification', 'Failed to save audio config', 'error');
    }
}

// Récupérer les données audio et FFT en temps réel (un seul appel)
async function updateAudioData() {
    try {
        // Un seul appel qui retourne audio + FFT
        const response = await fetch(API_BASE + '/api/audio/data');
        const data = await response.json();

        // Mise à jour des données audio de base
        if (data.av !== false) {
            $('audio-amplitude-value').textContent =
                (data.amp * 100).toFixed(0) + '%';
            if ($('audio-raw-value') && data.ram !== undefined) {
                $('audio-raw-value').textContent = (data.ram * 100).toFixed(0) + '%';
            }
            if ($('audio-calibrated-value') && data.cam !== undefined) {
                $('audio-calibrated-value').textContent = (data.cam * 100).toFixed(0) + '%';
            }
            $('audio-bpm-value').textContent =
                data.bpm > 0 ? data.bpm.toFixed(1) : '-';
            $('audio-bass-value').textContent =
                (data.ba * 100).toFixed(0) + '%';
            $('audio-mid-value').textContent =
                (data.md * 100).toFixed(0) + '%';
            $('audio-treble-value').textContent =
                (data.tr * 100).toFixed(0) + '%';
        }

        // Mise à jour des données FFT (si disponibles dans la réponse)
        if (data.fft && data.fft.av) {
            // Update frequency info
            $('fftPeakFreq').textContent = Math.round(data.fft.pf);
            $('fftCentroid').textContent = Math.round(data.fft.sc);

            // Update detections with icons
            const t = translations[currentLang];
            $('fftKick').innerHTML = data.fft.kd ?
                `<span style="color: #4CAF50;">🥁 ${t.fft.detected}</span>` : '-';
            $('fftSnare').innerHTML = data.fft.sd ?
                `<span style="color: #FF9800;">🎵 ${t.fft.detected}</span>` : '-';
            $('fftVocal').innerHTML = data.fft.vd ?
                `<span style="color: #2196F3;">🎤 ${t.fft.detected}</span>` : '-';

            // Draw spectrum
            drawFFTSpectrum(data.fft.bands);
        }
    } catch (error) {
        console.error('Failed to update audio data:', error);
    } finally {
        // Planifier le prochain appel seulement après avoir terminé celui-ci
        // Évite les bouchons si une requête prend du temps
        if (audioUpdateInterval !== null && audioUpdateInterval !== 'stopped') {
            // Annuler le précédent timeout s'il existe
            if (typeof audioUpdateInterval === 'number') {
                clearTimeout(audioUpdateInterval);
            }
            audioUpdateInterval = setTimeout(updateAudioData, 500);
        }
    }
}

// ============================================================================
// FFT Functions
// ============================================================================

let audioFFTEnabled = false;

// Draw FFT spectrum on canvas
function drawFFTSpectrum(bands) {
    // Affiche une bande sur deux (0,2,4...) et limite aux 8 premières
    const displayBands = bands
        ? bands.filter((_, idx) => idx % 2 === 0).slice(0, 8)
        : [];
    const fftBox = $('fftStatusBox');
    if (fftBox && audioFFTEnabled) {
        fftBox.style.display = 'block';
    } else if( fftBox) {
        fftBox.style.display = 'none';
    } else {
      return;
    }
    const canvas = $('fftSpectrumCanvas');
    if (!canvas) {
        console.warn('Canvas fftSpectrumCanvas not found');
        return;
    }

    if (!displayBands || displayBands.length === 0) {
        console.warn('No FFT bands data to draw');
        return;
    }

    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    // Clear canvas
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, width, height);

    const barWidth = width / displayBands.length;

    // Draw bars
    for (let i = 0; i < displayBands.length; i++) {
        const barHeight = displayBands[i] * height;

        // Calculate rainbow color (bass = red, treble = blue)
        const hue = (i / displayBands.length) * 255;
        const r = Math.floor(255 * Math.sin((hue / 255) * Math.PI));
        const g = Math.floor(255 * Math.sin(((hue / 255) + 0.33) * Math.PI));
        const b = Math.floor(255 * Math.sin(((hue / 255) + 0.66) * Math.PI));

        ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
        ctx.fillRect(i * barWidth, height - barHeight, barWidth - 2, barHeight);
    }
}

// Initialise FFT status from backend (called on page load)
async function loadFFTStatus() {
    try {
        // Charger l'état FFT du backend
        const audioResponse = await fetch(API_BASE + '/api/audio/status');
        const audioData = await audioResponse.json();
        audioFFTEnabled = audioData.ffe || false;

        // Le backend décide si le FFT doit être activé selon l'effet
        // Afficher la section FFT uniquement si le FFT est activé
        const fftBox = $('fftStatusBox');
        if (fftBox) {
            fftBox.style.display = audioFFTEnabled ? 'block' : 'none';
        }
    } catch (error) {
        console.error('Failed to load FFT status:', error);
    }
}

function startAudioDataPolling() {
    // Ne démarrer le polling que si on est sur l'onglet config ET que le micro est activé
    if (!audioEnabled || activeTabName !== 'config') {
        return;
    }

    // En mode BLE, désactiver complètement le polling audio pour économiser la bande passante
    if (bleTransportInstance && bleTransportInstance.shouldUseBle()) {
        const audioDataBox = $('audio-data');
        const audioFFTDataBox = $('audio-fft-data');
        if (audioDataBox) {
          audioDataBox.style.display = 'none';
        }
        if (audioFFTDataBox) {
            audioFFTDataBox.style.display = 'none';
        }

        console.log('[Audio] Polling désactivé en mode BLE (économie bande passante)');
        return;
    }

    if (audioUpdateInterval === null) {
        // Utiliser un flag non-null pour indiquer que le polling est actif
        audioUpdateInterval = 'active';
        // Démarrer immédiatement, puis utiliser setTimeout dans la fonction
        updateAudioData();
        console.log('[Audio] Polling démarré à ~2Hz (WiFi, auto-régulé)');
    }
}

function stopAudioDataPolling() {
    if (audioUpdateInterval !== null) {
        console.log('[Audio] Polling arrêté');
        // Arrêter le setTimeout planifié (si audioUpdateInterval est un timeout ID)
        if (typeof audioUpdateInterval === 'number') {
            clearTimeout(audioUpdateInterval);
        }
        audioUpdateInterval = null;
        // Réinitialiser l'affichage
        $('audio-amplitude-value').textContent = '-';
        $('audio-bpm-value').textContent = '-';
        $('audio-bass-value').textContent = '-';
        $('audio-mid-value').textContent = '-';
        $('audio-treble-value').textContent = '-';
    }
}

// OTA Functions
let otaReloadScheduled = false;
let otaManualUploadRunning = false;
let otaPollingInterval = null;
function scheduleOtaReload(delayMs) {
    if (otaReloadScheduled) {
        return;
    }
    otaReloadScheduled = true;
    setTimeout(() => window.location.reload(), delayMs);
}
async function loadOTAInfo() {
    try {
        const response = await fetch(API_BASE + '/api/ota/info');
        const data = await response.json();
        $('ota-version').textContent = 'v' + data.v;
        const progressContainer = $('ota-progress-container');
        const progressBar = $('ota-progress-bar');
        const progressPercent = $('ota-progress-percent');
        const statusMessage = $('ota-status-message');
        const uploadBtn = $('ota-upload-btn');
        const restartBtn = $('ota-restart-btn');
        const fileInputEl = $('ota-progress-title');
        const backendStateKey = getOtaStateKey(data.st);
        if (otaManualUploadRunning && backendStateKey !== 'idle') {
            otaManualUploadRunning = false;
        }
        const stateKey = otaManualUploadRunning ? 'receiving' : backendStateKey;
        const showProgress = otaManualUploadRunning || (typeof data.st === 'number' && data.st !== 0);
        if (progressContainer && progressBar && progressPercent && statusMessage) {
            const progressValue = Math.max(0, Math.min(100, Number(data.pg) || 0));
            if (showProgress || stateKey === 'success' || stateKey === 'error') {
                progressContainer.style.display = 'block';
                // Ne mettre à jour le progress que si on n'est pas en train d'uploader (XHR gère le progress)
                if (!otaManualUploadRunning) {
                    progressBar.style.width = progressValue + '%';
                    progressPercent.textContent = progressValue + '%';
                }
            } else {
                progressContainer.style.display = 'none';
                progressBar.style.width = '0%';
                progressPercent.textContent = '0%';
            }
            let statusText = t('ota.states.' + stateKey);
            if (stateKey === 'error' && data.err) {
                statusText += ' - ' + data.err;
            }
            statusMessage.textContent = statusText;
            if (stateKey === 'success') {
                statusMessage.style.color = '#10b981';
            } else if (stateKey === 'error') {
                statusMessage.style.color = '#f87171';
            } else {
                statusMessage.style.color = 'var(--color-muted)';
            }
        }
        const busy = stateKey === 'receiving' || stateKey === 'writing';
        if (uploadBtn) {
            const effectiveBusy = busy || otaManualUploadRunning;
            if (effectiveBusy) {
                uploadBtn.disabled = true;
                uploadBtn.style.display = 'none';
            } else if (!bleTransport.shouldUseBle()) {
                uploadBtn.disabled = false;
                uploadBtn.style.display = 'inline-block';
            }
        }
        if (fileInputEl) {
            const effectiveBusy = busy || otaManualUploadRunning;
            if (effectiveBusy) {
                fileInputEl.disabled = true;
                fileInputEl.style.display = 'none';
            } else if (!bleTransport.shouldUseBle()) {
                fileInputEl.disabled = false;
                fileInputEl.style.display = 'block';
            }
        }
        if (restartBtn) {
            restartBtn.style.display = stateKey === 'success' ? 'inline-block' : 'none';
        }
        const rebootCountdownEl = $('ota-reboot-countdown');
        if (rebootCountdownEl) {
            if (typeof data.rc === 'number' && data.rc >= 0) {
                if (progressContainer) {
                    progressContainer.style.display = 'block';
                }
                rebootCountdownEl.style.display = 'block';
                if (data.rc === 0) {
                    rebootCountdownEl.textContent = t('ota.restarting');
                    scheduleOtaReload(5000);
                } else {
                    rebootCountdownEl.textContent = t('ota.autoRestartIn') + ' ' + data.rc + 's';
                    if (data.rc <= 5) {
                        scheduleOtaReload((data.rc + 2) * 1000);
                    }
                }
            } else {
                rebootCountdownEl.style.display = 'none';
            }
        }
    } catch (e) {
        console.error('Erreur:', e);
        $('ota-version').innerHTML = '<span data-i18n="ota.loading">' + t('ota.loading') + '</span>';
        const rebootCountdownEl = $('ota-reboot-countdown');
        if (rebootCountdownEl) {
            rebootCountdownEl.style.display = 'none';
        }
    } finally {
        // Toujours mettre à jour la note BLE après le chargement des infos OTA
        updateOtaBleNote();
    }
}
async function uploadFirmware() {
    const fileInputEl = $('firmware-file');
    const file = fileInputEl.files[0];
    if (!file) {
        showNotification('ota-notification', t('ota.selectFile'), 'error');
        return;
    }
    if (!file.name.endsWith('.bin')) {
        showNotification('ota-notification', t('ota.wrongExtension'), 'error');
        return;
    }
    if (!confirm(t('ota.confirmUpdate'))) {
        return;
    }
    const progressTitle = $('ota-progress-title');
    const progressContainer = $('ota-progress-container');
    const progressBar = $('ota-progress-bar');
    const progressPercent = $('ota-progress-percent');
    const statusMessage = $('ota-status-message');
    const uploadBtn = $('ota-upload-btn');
    const restartBtn = $('ota-restart-btn');
    otaManualUploadRunning = true;
    progressTitle.style.display = 'block';
    progressContainer.style.display = 'block';
    uploadBtn.disabled = true;
    uploadBtn.style.display = 'none';
    if (fileInputEl) {
        fileInputEl.disabled = true;
        fileInputEl.style.display = 'none';
    }
    statusMessage.textContent = t('ota.states.receiving');
    statusMessage.style.color = 'var(--color-muted)';
    // Nettoyer l'ancien intervalle s'il existe
    if (otaPollingInterval) {
        clearInterval(otaPollingInterval);
    }
    // Démarrer le polling OTA pendant l'upload (moins fréquent pour ne pas surcharger l'ESP32)
    otaPollingInterval = setInterval(loadOTAInfo, 3000);
    try {
        await waitForApiConnection();
        const xhr = new XMLHttpRequest();
        xhr.upload.addEventListener('progress', (e) => {
            if (e.lengthComputable) {
                const percent = Math.round((e.loaded / e.total) * 100);
                progressBar.style.width = percent + '%';
                progressPercent.textContent = percent + '%';
                statusMessage.textContent = t('ota.states.receiving') + ' (' + percent + '%)';
            }
        });
        xhr.addEventListener('load', () => {
            if (xhr.status === 200) {
                progressBar.style.width = '100%';
                progressPercent.textContent = '100%';
                statusMessage.textContent = t('ota.states.writing');
                statusMessage.style.color = 'var(--color-muted)';
                restartBtn.style.display = 'inline-block';
                uploadBtn.style.display = 'none';
                // L'intervalle continue pour suivre l'écriture en flash
            } else {
                statusMessage.textContent = t('ota.states.err');
                statusMessage.style.color = '#f87171';
                uploadBtn.disabled = false;
                uploadBtn.style.display = 'inline-block';
                if (fileInputEl) {
                    fileInputEl.disabled = false;
                    fileInputEl.style.display = 'block';
                }
                // Arrêter le polling OTA en cas d'erreur
                if (otaPollingInterval) {
                    clearInterval(otaPollingInterval);
                    otaPollingInterval = null;
                }
            }
        });
        xhr.addEventListener('error', (e) => {
            statusMessage.textContent = t('ota.states.err') + (e.message ? ': ' + e.message : '');
            statusMessage.style.color = '#f87171';
            uploadBtn.disabled = false;
            uploadBtn.style.display = 'inline-block';
            if (fileInputEl) {
                fileInputEl.disabled = false;
                fileInputEl.style.display = 'block';
            }
            // Arrêter le polling OTA en cas d'erreur réseau
            if (otaPollingInterval) {
                clearInterval(otaPollingInterval);
                otaPollingInterval = null;
            }
        });
        xhr.open('POST', API_BASE + '/api/ota/upload');
        xhr.send(file);
    } catch (e) {
        console.error('Erreur:', e);
        statusMessage.textContent = t('ota.err') + ': ' + e.message;
        statusMessage.style.color = '#E82127';
        uploadBtn.disabled = false;
        uploadBtn.style.display = 'inline-block';
        if (fileInputEl) {
            fileInputEl.disabled = false;
            fileInputEl.style.display = 'block';
        }
        // Arrêter le polling OTA en cas d'exception
        if (otaPollingInterval) {
            clearInterval(otaPollingInterval);
            otaPollingInterval = null;
        }
    }
}
async function restartDevice() {
    if (!confirm(t('ota.confirmRestart'))) {
        return;
    }
    try {
        await fetch(API_BASE + '/api/ota/restart', { method: 'POST' });
        const restartMessage = t('ota.restarting');
        const otaStatusEl = $('ota-status-message');
        if (otaStatusEl) {
            otaStatusEl.textContent = restartMessage;
        }
        const configNotificationEl = $('config-notification');
        if (configNotificationEl) {
            showNotification('config-notification', restartMessage, 'info', 10000);
        }
        setTimeout(() => {
            location.reload();
        }, 10000);
    } catch (e) {
        console.error('Erreur:', e);
    }
}
// Night mode has been removed and replaced by dynamic brightness

// Toggle event test from events config tab
async function toggleEventTest(eventId, isEnabled) {
    try {
        if (isEnabled) {
            // Vérifier que l'événement est activé dans la config
            try {
                await ensureEventsConfigData();
            } catch (error) {
                console.error('Failed to refresh events config for test:', error);
            }
            if (!isSimulationEventEnabled(eventId)) {
                const checkbox = $('toggle-' + eventId);
                if (checkbox) {
                    checkbox.checked = false;
                }
                simulationTogglesState[eventId] = false;
                showNotification('events-notification', t('test.disabledEvent'), 'error');
                return;
            }
            cancelSimulationAutoStop(eventId);
            showNotification('events-notification', t('test.sending') + ': ' + getEventName(eventId) + '...', 'info');
            const response = await fetch(API_BASE + '/api/simulate/event', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ event: eventId })
            });
            const result = await response.json();
            if (result.st === 'ok') {
                showNotification('events-notification', t('test.eventActive', getEventName(eventId)), 'success');
                simulationTogglesState[eventId] = true;
                const durationMs = await getSimulationEventDuration(eventId);
                if (durationMs > 0) {
                    scheduleSimulationAutoStop(eventId, durationMs);
                }
            } else {
                const checkbox = $('toggle-' + eventId);
                if (checkbox) checkbox.checked = false;
                showNotification('events-notification', t('test.testError', t('test.error')), 'error');
                simulationTogglesState[eventId] = false;
            }
        } else {
            cancelSimulationAutoStop(eventId);
            showNotification('events-notification', t('test.stoppingEvent', getEventName(eventId)), 'info');
            const response = await fetch(API_BASE + '/api/stop/event', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ event: eventId })
            });
            const result = await response.json();
            if (result.st === 'ok') {
                showNotification('events-notification', t('test.eventStopped', getEventName(eventId)), 'success');
                simulationTogglesState[eventId] = false;
            } else {
                const checkbox = $('toggle-' + eventId);
                if (checkbox) checkbox.checked = true;
                showNotification('events-notification', t('test.testError', t('test.error')), 'error');
                simulationTogglesState[eventId] = true;
            }
        }
    } catch (e) {
        console.error('Erreur test:', e);
        const checkbox = $('toggle-' + eventId);
        if (checkbox) checkbox.checked = !isEnabled;
        showNotification('events-notification', t('test.testError', t('test.error')), 'error');
        simulationTogglesState[eventId] = !isEnabled;
    }
}

// Mapping des événements vers les clés commonEvents
const EVENT_TO_COMMON = {
    'TURN_LEFT': 'turnLeft', 
    'TURN_RIGHT': 'turnRight', 
    'TURN_HAZARD': 'hazard',
    'CHARGING': 'charging', 
    'CHARGE_COMPLETE': 'chargeComplete',
    'DOOR_OPEN': 'doorOpen', 
    'DOOR_CLOSE': 'doorClose',
    'LOCKED': 'locked', 
    'UNLOCKED': 'unlocked',
    'BRAKE_ON': 'brakeOn',
    'BLINDSPOT_LEFT': 'blindspotLeft',
    'BLINDSPOT_RIGHT': 'blindspotRight',
    'SIDE_COLLISION_LEFT': 'sideCollisionLeft',
    'SIDE_COLLISION_RIGHT': 'sideCollisionRight',
    'LANE_DEPARTURE_LEFT_LV1': 'laneDepartureLeftLv1', 
    'LANE_DEPARTURE_LEFT_LV2': 'laneDepartureLeftLv2',
    'LANE_DEPARTURE_RIGHT_LV1': 'laneDepartureRightLv1',
    'LANE_DEPARTURE_RIGHT_LV2': 'laneDepartureRightLv2',
    'SPEED_THRESHOLD': 'speedThreshold'
};
function translateEventId(eventId) {
    if (!eventId) return null;
    const lang = translations[currentLang];
    if (!lang) return null;
    // Chercher d'abord dans commonEvents
    const commonKey = EVENT_TO_COMMON[eventId];
    if (commonKey && lang.commonEvents && lang.commonEvents[commonKey]) {
        return lang.commonEvents[commonKey];
    }
    // Sinon dans eventNames
    if (lang.eventNames && lang.eventNames[eventId]) {
        return lang.eventNames[eventId];
    }
    return null;
}
function translateEffectId(effectId) {
    if (!effectId) return null;
    const map = translations[currentLang] && translations[currentLang].effectNames;
    if (map && map[effectId]) {
        return map[effectId];
    }
    return null;
}
// Helper pour obtenir le nom d'un événement (depuis API uniquement)
function getEventName(eventId) {
    const translated = translateEventId(typeof eventId === 'string' ? eventId : String(eventId));
    if (translated) {
        return translated;
    }
    if (eventTypesList.length > 0) {
        const stringId = typeof eventId === 'string' ? eventId : String(eventId);
        const eventById = eventTypesList.find(e => e.id === stringId);
        if (eventById) {
            return eventById.n;
        }
        const numericIndex = typeof eventId === 'number' ? eventId : parseInt(eventId, 10);
        if (!Number.isNaN(numericIndex) && eventTypesList[numericIndex]) {
            return eventTypesList[numericIndex].id;
        }
    }
    return t('eventNames.unknown', eventId);
}
// Helper pour obtenir le nom d'un effet (depuis API uniquement)
function getEffectName(effectId) {
    const translated = translateEffectId(effectId);
    if (translated) {
        return translated;
    }
    if (effectsList.length > 0) {
        const effect = effectsList.find(e => e.id === effectId);
        return effect ? translateEffectId(effect.id) || effect.n : t('effectNames.unknown', effectId);
    }
    return t('effectNames.unknown', effectId);
}
// Charger la liste des effets depuis l'API
let effectsList = [];
function effectDisplayName(effect) {
    return (translateEffectId(effect.id) || effect.n || effect.id || '').toString().toLowerCase();
}
function sortEffectsByName(effects) {
    return [...effects].sort((a, b) => {
        const na = effectDisplayName(a);
        const nb = effectDisplayName(b);
        if (na < nb) return -1;
        if (na > nb) return 1;
        return 0;
    });
}
// Helper: Convertir enum numérique en ID string
function effectEnumToId(enumValue) {
    if (effectsList[enumValue]) {
        return effectsList[enumValue].id;
    }
    return effectsList[0]?.id || 'OFF';
}
// Helper: Convertir ID string en enum numérique
function effectIdToEnum(id) {
    const index = effectsList.findIndex(e => e.id === id);
    return index >= 0 ? index : 0;
}
async function loadEffects() {
    try {
        const response = await fetch('/api/effects');
        const data = await response.json();
        effectsList = data.effects;
        const allEffects = data.effects || [];
        const offEffect = allEffects.find(e => e.id === 'OFF');
        const audioEffects = sortEffectsByName(allEffects.filter(e => e.ae && e.id !== 'OFF'));
        const baseEffects = sortEffectsByName(allEffects.filter(e => !e.ae && e.id !== 'OFF'));
        const displayEffects = [
            ...(offEffect ? [offEffect] : []),
            ...baseEffects,
            ...audioEffects
        ];
        // Mettre à jour tous les dropdowns d'effets
        const effectSelects = [
            $('default-effect-select'),
            $('effect-select'),
            $('event-effect-select')
        ];
        effectSelects.forEach(select => {
            if (select) {
                select.setAttribute('data-effect-options', 'true');
                const currentValue = select.value;
                select.innerHTML = '';
                // Pour les sélecteurs d'effet par défaut et d'effet manuel,
                // filtrer les effets qui nécessitent le CAN bus
                // Pour le sélecteur d'événement, filtrer aussi les effets audio
                const isEventSelector = select.id === 'event-effect-select';
                let separatorInserted = false;
                displayEffects.forEach(effect => {
                    // Filtrer les effets CAN si ce n'est pas le sélecteur d'événement
                    if (!isEventSelector && effect.cr) {
                        return; // Skip cet effet
                    }
                    // Filtrer les effets audio si c'est le sélecteur d'événement
                    if (isEventSelector && effect.ae) {
                        return; // Skip cet effet audio
                    }
                    if (effect.ae && !separatorInserted) {
                        separatorInserted = true;
                        const separator = doc.createElement('option');
                        separator.disabled = true;
                        separator.value = '';
                        separator.textContent = '---- Audio ----';
                        separator.setAttribute('data-separator', 'true');
                        select.appendChild(separator);
                    }
                    const option = doc.createElement('option');
                    option.value = effect.id;
                    option.textContent = translateEffectId(effect.id) || effect.n;
                    option.setAttribute('data-effect-name', effect.n);
                    if (effect.ae) {
                        option.setAttribute('data-audio-effect', 'true');
                        option.disabled = !audioEnabled;
                    }
                    select.appendChild(option);
                });
                // Restaurer la valeur sélectionnée si elle existe encore
                if (currentValue !== undefined && currentValue !== '') {
                    select.value = currentValue;
                }
            }
        });
        refreshEffectOptionLabels();
        applyAudioEffectAvailability();
        console.log('Loaded', effectsList.length, 'effects from API');
    } catch (error) {
        console.error('Failed to load effects:', error);
    }
}
// Charger la liste des types d'événements depuis l'API
let eventTypesList = [];
async function loadEventTypes() {
    try {
        const response = await fetch('/api/event-types');
        const data = await response.json();
        eventTypesList = data.event_types;
        console.log('Loaded', eventTypesList.length, 'event types from API');
    } catch (error) {
        console.error('Failed to load event types:', error);
    }
}

// Gestion de l'écran de chargement
function updateLoadingProgress(percentage, message) {
    const progressBar = $('loading-progress-bar');
    const statusText = $('loading-status');
    if (progressBar) {
        progressBar.style.width = percentage + '%';
    }
    if (statusText && message) {
        statusText.textContent = message;
    }
    // Si on est en attente d'un geste BLE, afficher un message explicite
    if (bleAutoConnectAwaitingGesture && statusText) {
        statusText.textContent = t('ble.tapToAuthorize');
        statusText.dataset.i18n = 'ble.tapToAuthorize';
    }
}

function hideLoadingOverlay() {
    const overlay = $('loading-overlay');
    if (overlay) {
        overlay.classList.add('hidden');
        setTimeout(() => {
            overlay.style.display = 'none';
        }, 500);
    }
}

function showLoadingOverlay() {
    const overlay = $('loading-overlay');
    if (overlay) {
        overlay.style.display = 'flex';
        overlay.classList.remove('hidden');
        // Réinitialiser la barre de progression
        updateLoadingProgress(0, t('loading.connecting'));
        // Masquer l'erreur si elle était affichée
        const errorEl = $('loading-error');
        if (errorEl) {
            errorEl.style.display = 'none';
        }
        // Afficher le bouton BLE si nécessaire
        updateLoadingBleButton();
    }
}

function updateLoadingBleButton() {
    const bleButton = $('loading-ble-button');
    if (!bleButton) {
        return;
    }
    // Afficher le bouton BLE si:
    // 1. BLE est supporté
    // 2. Pas de WiFi
    // 3. BLE n'est ni connecté ni en cours de connexion
    const status = bleTransport.getStatus();
    const shouldShow = bleTransport.isSupported() &&
                      !wifiOnline &&
                      status !== 'connected' &&
                      status !== 'connecting';
    bleButton.style.display = shouldShow ? 'block' : 'none';
}

async function manualBleConnect() {
    const statusText = $('loading-status');
    const bleButton = $('loading-ble-button');

    try {
        if (statusText) {
            statusText.textContent = t('ble.connecting');
            statusText.dataset.i18n = 'ble.connecting';
        }
        if (bleButton) {
            bleButton.style.display = 'none';
        }

        // Forcer la capture du geste
        bleAutoConnectGestureCaptured = true;
        bleAutoConnectAwaitingGesture = false;

        await bleTransport.connect();
    } catch (error) {
        console.error('Manual BLE connect error', error);
        if (statusText) {
            statusText.textContent = t('loading.connecting');
            statusText.dataset.i18n = 'loading.connecting';
        }
        updateLoadingBleButton();
    }
}

function showLoadingError(message) {
    const errorDiv = $('loading-error');
    const errorMsg = $('loading-error-message');
    if (errorDiv && errorMsg) {
        errorMsg.textContent = message;
        errorDiv.style.display = 'block';
    }
}

async function restart() {
    try {
        await fetch('/api/system/restart', { method: 'POST' });
        $('loading-error-message').textContent = t('loading.connecting');
        setTimeout(() => {
            location.reload();
        }, 5000);
    } catch (error) {
        console.error('Failed to restart:', error);
    }
}

async function factoryReset() {
    if (!confirm(t('loading.factoryReset') + '?')) return;
    try {
        await fetch('/api/system/factory-reset', { method: 'POST' });
        $('loading-error-message').textContent = t('loading.connecting');
        setTimeout(() => {
            location.reload();
        }, 8000);
    } catch (error) {
        console.error('Failed to factory reset:', error);
    }
}

// Generic CAN Server Control Functions
async function updateServerStatus(serverName, serverDisplayName) {
    try {
        const response = await fetch(`/api/${serverName}/status`);
        const data = await response.json();

        const statusDiv = $(`${serverName}-status`);
        const clientsDiv = $(`${serverName}-clients`);
        const toggleBtn = $(`${serverName}-toggle-btn`);
        const autostartCheckbox = $(`${serverName}-autostart`);

        if (data.running) {
            statusDiv.textContent = t(`server.${serverName}Running`) || `Actif (Port ${data.port})`;
            statusDiv.style.color = '#10b981';
            toggleBtn.textContent = t(`server.${serverName}Stop`) || `Arrêter ${serverDisplayName}`;
            toggleBtn.className = 'btn-secondary';
        } else {
            statusDiv.textContent = t(`server.${serverName}Stopped`) || 'Arrêté';
            statusDiv.style.color = 'var(--color-muted)';
            toggleBtn.textContent = t(`server.${serverName}Start`) || `Activer ${serverDisplayName}`;
            toggleBtn.className = 'btn-primary';
        }

        clientsDiv.textContent = data.clients || 0;
        if (autostartCheckbox) {
            autostartCheckbox.checked = data.autostart || false;
        }
    } catch (error) {
        console.error(`Failed to get ${serverDisplayName} status:`, error);
    }
}

async function toggleServer(serverName, serverDisplayName) {
    try {
        const statusResponse = await fetch(`/api/${serverName}/status`);
        const statusData = await statusResponse.json();
        const isRunning = statusData.running;

        const endpoint = isRunning ? `/api/${serverName}/stop` : `/api/${serverName}/start`;
        const action = isRunning ? 'arrêt' : 'démarrage';

        showNotification('diagnostic',
            t(`server.${serverName}${isRunning ? 'Stopping' : 'Starting'}`) ||
            `${action.charAt(0).toUpperCase() + action.slice(1)} du serveur ${serverDisplayName}...`,
            'info');

        const response = await fetch(endpoint, { method: 'POST' });
        const data = await response.json();

        if (data.status === 'ok') {
            showNotification('diagnostic',
                t(`server.${serverName}${isRunning ? 'Stopped' : 'Started'}`) ||
                `Serveur ${serverDisplayName} ${isRunning ? 'arrêté' : 'démarré'} avec succès`,
                'success');
            await updateServerStatus(serverName, serverDisplayName);
        } else {
            showNotification('diagnostic',
                t(`server.${serverName}Error`) ||
                `Erreur lors du ${action} du serveur ${serverDisplayName}`,
                'error');
        }
    } catch (error) {
        console.error(`Failed to toggle ${serverDisplayName}:`, error);
        showNotification('diagnostic',
            t(`server.${serverName}Error`) ||
            `Erreur lors de la communication avec le serveur ${serverDisplayName}`,
            'error');
    }
}

// GVRET TCP Server Control
async function updateGvretTcpStatus() {
    return updateServerStatus('gvret', 'GVRET');
}

async function toggleGvretTcp() {
    return toggleServer('gvret', 'GVRET');
}

// CANServer UDP Server Control
async function updateCanserverStatus() {
    return updateServerStatus('canserver', 'CANSERVER');
}

async function toggleCanserver() {
    return toggleServer('canserver', 'CANServer');
}

// SLCAN TCP Server Control
async function updateSlcanTcpStatus() {
    return updateServerStatus('slcan', 'SLCAN');
}

async function toggleSlcanTcp() {
    return toggleServer('slcan', 'SLCAN');
}

// Generic Server Autostart Control
async function toggleServerAutostart(serverName, enabled) {
    try {
        const response = await fetch(`/api/${serverName}/autostart`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ autostart: enabled })
        });

        const data = await response.json();

        if (data.status === 'ok') {
            showNotification('diagnostic',
                t('server.autostartUpdated') || `Démarrage automatique ${enabled ? 'activé' : 'désactivé'} pour ${serverName.toUpperCase()}`,
                'success');
        } else {
            showNotification('diagnostic',
                t('server.autostartError') || `Erreur lors de la mise à jour du démarrage automatique`,
                'error');
            // Revert checkbox on error
            const checkbox = $(`${serverName}-autostart`);
            if (checkbox) checkbox.checked = !enabled;
        }
    } catch (error) {
        console.error(`Failed to toggle ${serverName} autostart:`, error);
        showNotification('diagnostic',
            t('server.autostartError') || 'Erreur lors de la communication avec le serveur',
            'error');
        // Revert checkbox on error
        const checkbox = $(`${serverName}-autostart`);
        if (checkbox) checkbox.checked = !enabled;
    }
}

// Live Logs Functions (Server-Sent Events)
let logsEnabled = false;
let logsEventSource = null;
const MAX_LOG_LINES = 500;
let logLineCount = 0;

async function toggleLiveLogs() {
    if (logsEnabled) {
        stopLiveLogs();
    } else {
        startLiveLogs();
    }
}

function startLiveLogs() {
    logsEnabled = true;
    const statusDiv = $('logs-status');
    const toggleBtn = $('logs-toggle-btn');
    const container = $('logs-container');

    statusDiv.textContent = t('logs.connecting') || 'Connexion...';
    statusDiv.style.color = '#fbbf24';
    toggleBtn.textContent = t('logs.stop') || 'Arrêter les logs';
    toggleBtn.className = 'btn-secondary';

    // Clear existing logs
    container.innerHTML = '<div style="color: #10b981;">Connexion au flux de logs...</div>';
    logLineCount = 0;

    // Create EventSource connection
    logsEventSource = new EventSource(API_BASE + '/api/logs/stream');

    logsEventSource.onopen = function() {
        statusDiv.textContent = t('logs.connected') || 'Connecté';
        statusDiv.style.color = '#10b981';
        container.innerHTML = '';
        appendLog('info', 'LOGS', 'Flux de logs connecté');
    };

    logsEventSource.onmessage = function(event) {
        try {
            const logData = JSON.parse(event.data);
            appendLog(logData.level, logData.tag, logData.message);
        } catch (e) {
            console.error('Failed to parse log data:', e);
        }
    };

    logsEventSource.onerror = function(error) {
        console.error('SSE connection error:', error);
        statusDiv.textContent = t('logs.error') || 'Erreur de connexion';
        statusDiv.style.color = '#ef4444';

        if (logsEnabled) {
            appendLog('error', 'LOGS', 'Connexion perdue, reconnexion...');
            // Auto-reconnect after 3 seconds
            setTimeout(() => {
                if (logsEnabled) {
                    stopLiveLogs();
                    startLiveLogs();
                }
            }, 3000);
        }
    };
}

function stopLiveLogs() {
    logsEnabled = false;
    const statusDiv = $('logs-status');
    const toggleBtn = $('logs-toggle-btn');

    statusDiv.textContent = t('logs.disconnected') || 'Déconnecté';
    statusDiv.style.color = 'var(--color-muted)';
    toggleBtn.textContent = t('logs.start') || 'Activer les logs';
    toggleBtn.className = 'btn-primary';

    if (logsEventSource) {
        logsEventSource.close();
        logsEventSource = null;
    }
}

function appendLog(level, tag, message) {
    const container = $('logs-container');
    if (!container) return;

    // Remove placeholder if present
    if (container.querySelector('[data-i18n="server.logsEmpty"]')) {
        container.innerHTML = '';
    }

    // Limit number of log lines
    if (logLineCount >= MAX_LOG_LINES) {
        const firstLine = container.firstChild;
        if (firstLine) {
            container.removeChild(firstLine);
        }
    } else {
        logLineCount++;
    }

    // Create log line
    const logLine = document.createElement('div');
    logLine.style.marginBottom = '2px';
    logLine.style.wordWrap = 'break-word';

    // Color based on level
    let color = '#10b981'; // info/default
    if (level === 'E' || level === 'error') color = '#ef4444'; // error
    else if (level === 'W' || level === 'warn') color = '#fbbf24'; // warning
    else if (level === 'D' || level === 'debug') color = '#9ca3af'; // debug
    else if (level === 'V' || level === 'verbose') color = '#6b7280'; // verbose

    // Format: [TAG] message
    const timestamp = new Date().toLocaleTimeString('fr-FR', { hour12: false });
    logLine.innerHTML = `<span style="color: #6b7280;">${timestamp}</span> <span style="color: ${color};">[${tag}]</span> ${escapeHtml(message)}`;

    container.appendChild(logLine);
    container.scrollTop = container.scrollHeight;
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function clearLogs() {
    const container = $('logs-container');
    if (container) {
        container.innerHTML = '<div data-i18n="server.logsEmpty">Aucun log pour le moment...</div>';
        logLineCount = 0;
    }
}

function retryLoading() {
    const errorDiv = $('loading-error');
    if (errorDiv) {
        errorDiv.style.display = 'none';
    }
    location.reload();
}

async function loadInitialData() {
    const delay = (ms) => new Promise(resolve => setTimeout(resolve, ms));
    let progress = 0;
    const totalSteps = 9;
    const updateProgress = (step, message) => {
        progress = (step / totalSteps) * 100;
        updateLoadingProgress(progress, message);
    };

    try {
        // Détecter le mode AP dès le début
        try {
            const statusResponse = await fetch(API_BASE + '/api/status');
            const statusData = await statusResponse.json();
            isApMode = !statusData.wc;
        } catch (e) {
            console.log('Failed to detect AP mode, assuming normal mode');
        }

        // Adapter les délais selon le mode
        const baseDelay = isApMode ? 300 : 100;
        const longDelay = isApMode ? 500 : 150;

        // Étape 1-3: Chargement des données essentielles
        updateProgress(1, t('loading.loadingEffects'));
        await loadEffects();
        await delay(baseDelay);
        await loadEventTypes();
        await delay(baseDelay);
        await ensureEventsConfigData().catch(() => null);
        await delay(baseDelay);

        // Étape 4: Rendu des sections
        updateProgress(4, t('loading.loadingConfig'));
        await delay(baseDelay);

        // Étape 5: Profils
        updateProgress(5, t('loading.loadingProfiles'));
        await loadProfiles();
        await delay(longDelay);

        // Étape 6: Configuration
        updateProgress(6, t('loading.loadingConfig'));
        await loadConfig();
        await delay(longDelay);

        // Étape 7: Configuration matérielle
        updateProgress(7, t('loading.loadingConfig'));
        await loadHardwareConfig();
        await delay(baseDelay);

        // Étape 8: Audio et FFT
        updateProgress(8, t('loading.loadingConfig'));
        await loadAudioStatus();
        await delay(baseDelay);
        await loadFFTStatus();
        await delay(baseDelay);

        // Étape 9: OTA
        updateProgress(9, t('loading.loadingConfig'));
        await loadOTAInfo();

        // Masquer l'écran de chargement
        hideLoadingOverlay();

        // Démarrer le polling de status
        if (!statusIntervalHandle) {
            statusIntervalHandle = 'active';
            updateStatus();
        }
    } catch (error) {
        console.error('Initial data load failed:', error);
        showLoadingError(error.message || t('loading.errorMessage'));
        throw error;
    }
}
// Fonction de nettoyage des timers
function stopAllTimers() {
    console.log('[Cleanup] Arrêt de tous les timers');

    // Arrêter le polling de status
    if (typeof statusIntervalHandle === 'number') {
        clearTimeout(statusIntervalHandle);
    }
    statusIntervalHandle = 'stopped';

    // Arrêter le polling audio
    if (typeof audioUpdateInterval === 'number') {
        clearTimeout(audioUpdateInterval);
    }
    audioUpdateInterval = 'stopped';

    // Arrêter le polling OTA
    if (typeof otaPollingInterval === 'number') {
        clearInterval(otaPollingInterval);
    }
}

function resumeAllTimers() {
    console.log('[Cleanup] Reprise des timers');

    // Reprendre le polling de status si on est connecté
    if (statusIntervalHandle === 'stopped' && initialDataLoaded) {
        statusIntervalHandle = 'active';
        updateStatus();
    }

    // Reprendre le polling audio s'il était actif
    if (audioUpdateInterval === 'stopped' && audioEnabled) {
        audioUpdateInterval = 'active';
        updateAudioData();
    }
}

// Gérer la visibilité de la page pour économiser les ressources
document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
        stopAllTimers();
    } else {
        resumeAllTimers();
    }
});

// Nettoyer avant de quitter la page
window.addEventListener('beforeunload', () => {
    stopAllTimers();
});

// Init
applyTranslations();
applyTheme();
updateLanguageSelector();
updateBleUiState();
updateApiConnectionState();
scheduleInitialDataLoad();
