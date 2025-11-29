const API_BASE = '';
// DOM helpers
const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelector(sel);
const doc = document;
const hide = (el) => el.style.display = 'none';
const show = (el, displayType = 'block') => el.style.display = displayType;
// Translations
let currentLang = 'en';
let currentTheme = 'dark';

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
            { id: 'BRAKE_OFF', labelKey: 'simulation.brakeOff' },
            { id: 'SPEED_THRESHOLD', labelKey: 'simulation.speedThreshold' }
        ]
    },
    {
        titleKey: 'simulation.autopilot',
        events: [
            { id: 'AUTOPILOT_ENGAGED', labelKey: 'simulation.autopilotEngaged' },
            { id: 'AUTOPILOT_DISENGAGED', labelKey: 'simulation.autopilotDisengaged' }
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
            { id: 'BLINDSPOT_LEFT_LV1', labelKey: 'simulation.blindspotLeftLv1' },
            { id: 'BLINDSPOT_LEFT_LV2', labelKey: 'simulation.blindspotLeftLv2' },
            { id: 'BLINDSPOT_RIGHT_LV1', labelKey: 'simulation.blindspotRightLv1' },
            { id: 'BLINDSPOT_RIGHT_LV2', labelKey: 'simulation.blindspotRightLv2' }
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
    const fileInput = $('firmware-file');
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
        return;
    }
    currentLang = lang;
    localStorage.setItem('language', currentLang);
    applyTranslations();
    renderSimulationSections();
    renderEventsTable();
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
    updateLanguageSelector();
    const themeSelect = $('theme-select');
    if (themeSelect) {
        themeSelect.value = currentTheme;
    }
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
    }
}
function restoreSimulationToggles() {
    // Restaurer l'état de tous les toggles
    Object.keys(simulationTogglesState).forEach(eventId => {
        const checkbox = $('toggle-' + eventId);
        if (checkbox) {
            checkbox.checked = simulationTogglesState[eventId];
        }
        setToggleContainerState(eventId, simulationTogglesState[eventId]);
    });
    // Restaurer le toggle du mode nuit
    const nightModeCheckbox = $('toggle-nightmode');
    if (nightModeCheckbox && simulationTogglesState['nightmode'] !== undefined) {
        nightModeCheckbox.checked = simulationTogglesState['nightmode'];
    }
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
    await toggleEvent(eventType, false);
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
function renderSimulationSections() {
    const container = $('simulation-sections');
    if (!container) return;
    container.innerHTML = '';
    simulationSections.forEach(section => {
        const sectionDiv = doc.createElement('div');
        sectionDiv.className = 'section';
        const title = doc.createElement('div');
        title.className = 'section-title';
        title.style.fontSize = '16px';
        title.setAttribute('data-i18n', section.titleKey);
        title.textContent = t(section.titleKey);
        sectionDiv.appendChild(title);
        const grid = doc.createElement('div');
        grid.className = 'simulation-grid';
        section.events.forEach(event => {
            const toggleContainer = doc.createElement('div');
            toggleContainer.className = 'event-toggle-container';
            toggleContainer.id = 'event-toggle-' + event.id;
            const eventConfig = getSimulationEventConfig(event.id);
            const eventEnabledInConfig = isSimulationEventEnabled(event.id);
            if (eventConfig && !eventEnabledInConfig) {
                simulationTogglesState[event.id] = false;
            }
            const label = doc.createElement('label');
            label.className = 'event-toggle-label';
            label.htmlFor = 'toggle-' + event.id;
            label.setAttribute('data-i18n', event.labelKey);
            label.textContent = t(event.labelKey);
            const toggle = doc.createElement('label');
            toggle.className = 'toggle-switch';
            const input = doc.createElement('input');
            input.type = 'checkbox';
            input.id = 'toggle-' + event.id;
            input.checked = eventEnabledInConfig && !!simulationTogglesState[event.id];
            input.disabled = !eventEnabledInConfig;
            input.addEventListener('change', (e) => toggleEvent(event.id, e.target.checked));
            const slider = doc.createElement('span');
            slider.className = 'toggle-slider';
            toggle.appendChild(input);
            toggle.appendChild(slider);
            if (simulationTogglesState[event.id]) {
                toggleContainer.classList.add('active');
            }
            toggleContainer.classList.toggle('disabled', !eventEnabledInConfig);
            toggleContainer.appendChild(label);
            toggleContainer.appendChild(toggle);
            grid.appendChild(toggleContainer);
        });
        sectionDiv.appendChild(grid);
        container.appendChild(sectionDiv);
    });
}
function setToggleContainerState(eventType, isActive) {
    const container = $('event-toggle-' + eventType);
    if (container) {
        container.classList.toggle('active', !!isActive);
    }
}
// Conversion pourcentage <-> 0-255
function percentTo255(percent) {
    return Math.round((percent * 255) / 100);
}
function to255ToPercent(value) {
    return Math.round((value * 100) / 255);
}
const defaultNightMode = $('default-night-mode');
if (defaultNightMode) {
    defaultNightMode.addEventListener('change', scheduleDefaultEffectSave);
}
// Mise à jour des sliders avec pourcentage (seulement ceux qui existent)
const nightBrightnessSlider = $('night-brightness-slider');
if (nightBrightnessSlider) {
    nightBrightnessSlider.oninput = function() {
        $('night-brightness-value').textContent = this.value + '%';
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
// Notification helper
function showNotification(elementId, message, type, timeout = 2000) {
    const notification = $(elementId);
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
const EVENT_SAVE_DEBOUNCE_MS = 700;
const DEFAULT_EFFECT_DEBOUNCE_MS = 700;
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
        renderSimulationSections();
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
                        csp: false
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
            summaryHTML += `
                <div class="event-accordion-summary-item effect-item">
                    <span class="event-accordion-summary-label">${t('eventsConfig.effect')}:</span>
                    <span class="event-accordion-summary-value">${effectName}</span>
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
            <div class="event-accordion-header" onclick="toggleEventAccordion(${index})">
                <div class="event-accordion-title">${eventName}</div>
                <div class="event-accordion-summary">
                    ${summaryHTML}
                </div>
                <div class="event-accordion-toggle">▼</div>
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
                        <select onchange="updateEventConfig(${index}, 'at', parseInt(this.value)); renderEventsTable();" ${!event.en ? 'disabled' : ''}>
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
                        <input type="number" min="0" max="255" value="${event.br}"
                            onchange="updateEventConfig(${index}, 'br', parseInt(this.value))" ${!event.en ? 'disabled' : ''}>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.speed">${t('eventsConfig.speed')}</label>
                        <input type="number" min="0" max="255" value="${event.sp}"
                            onchange="updateEventConfig(${index}, 'sp', parseInt(this.value))" ${!event.en ? 'disabled' : ''}>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.color">${t('eventsConfig.color')}</label>
                        <input type="color" value="#${event.c1.toString(16).padStart(6, '0')}"
                            onchange="updateEventConfig(${index}, 'c1', parseInt(this.value.substring(1), 16))" ${!event.en ? 'disabled' : ''}>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.duration">${t('eventsConfig.duration')}</label>
                        <input type="number" min="0" max="60000" step="100" value="${event.dur}"
                            onchange="updateEventConfig(${index}, 'dur', parseInt(this.value))" ${!event.en ? 'disabled' : ''}>
                    </div>
                    <div class="event-form-field">
                        <label class="event-form-label" data-i18n="eventsConfig.priority">${t('eventsConfig.priority')}</label>
                        <input type="number" min="0" max="255" value="${event.pri}"
                            onchange="updateEventConfig(${index}, 'pri', parseInt(this.value))" ${!event.en ? 'disabled' : ''}>
                    </div>
                    ` : ''}
                </div>
            </div>
        `;
        container.appendChild(accordionItem);
    });
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
        summaryHTML += `
            <div class="event-accordion-summary-item effect-item">
                <span class="event-accordion-summary-label">${t('eventsConfig.effect')}:</span>
                <span class="event-accordion-summary-value">${effectName}</span>
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
        'ev', 'fx', 'br', 'sp', 'c1',
        'dur', 'pri', 'en', 'at', 'pid', 'csp'
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
        if (config.srv !== undefined) {
            $('strip-reverse').checked = config.srv;
        }
    } catch (e) {
        console.error('Error:', e);
        showNotification('config-notification', t('config.loadError'), 'error');
    }
}
async function saveHardwareConfig() {
    const config = {
        lc: parseInt($('led-count').value),
        srv: $('strip-reverse').checked
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
            option.textContent = profile.n + (profile.ac ? ' ✓' : '');
            if (profile.ac) option.selected = true;
            select.appendChild(option);
        });
        $('profile-status').textContent = data.an;
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
    const profileId = parseInt($('profile-select').value);
    if (profileId < 0) {
        showNotification('profiles-notification', t('profiles.selectProfile'), 'error');
        return;
    }
    const input = doc.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = async (event) => {
            try {
                const profileData = JSON.parse(event.target.result);
                const response = await fetch(API_BASE + '/api/profile/import', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        profile_id: profileId,
                        profile_data: profileData
                    })
                });
                const result = await response.json();
                if (result.st === 'ok') {
                    showNotification('profiles-notification', t('profiles.importSuccess'), 'success');
                    loadProfiles();
                } else {
                    showNotification('profiles-notification', t('profiles.importError'), 'error');
                }
            } catch (e) {
                console.error('Erreur import:', e);
                showNotification('profiles-notification', t('profiles.importError'), 'error');
            }
        };
        reader.readAsText(file);
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

    // Paramètres profil (mode nuit)
    if (params.settings !== false) {
        payload.anm = $('auto-night-mode').checked;
        payload.nbr = percentTo255(parseInt($('night-brightness-slider').value));
    }

    // Effet par défaut
    if (params.defaultEffect !== false) {
        payload.fx = effectIdToEnum($('default-effect-select').value);
        payload.br = percentTo255(parseInt($('default-brightness-slider').value));
        payload.sp = percentTo255(parseInt($('default-speed-slider').value));
        payload.c1 = parseInt($('default-color1').value.substring(1), 16);
        const audioReactiveCheckbox = $('default-audio-reactive');
        payload.ar = audioReactiveCheckbox ? audioReactiveCheckbox.checked : false;
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
        $('wifi-status').textContent = data.wc ? t('status.connected') : t('status.ap');
        $('wifi-status').className = 'status-value ' + (data.wc ? 'status-online' : 'status-offline');


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
                $('v-turn-signal').textContent = v.lights.tl ? t('simulation.left'): v.lights.tr ? t('simulation.right'): t('vehicle.off')
            }
            // Batterie et autres
            $('v-battery-lv').textContent = v.blv?.toFixed(2) + ' V';
            $('v-battery-hv').textContent = v.bhv?.toFixed(2) + ' V';
            $('v-odometer').textContent = v.odo.toLocaleString() + ' km';
            // Sécurité
            if (v.safety) {
                $('v-night').textContent = v.safety.nm ? t('status.active') : t('status.inactive');
                $('v-brightness').textContent = v.safety.br + '%';
                $('v-blindspot-left-lv1').textContent = v.safety.bl1 ? t('status.active') : t('status.inactive');
                $('v-blindspot-left-lv2').textContent = v.safety.bl2 ? t('status.active') : t('status.inactive');
                $('v-blindspot-right-lv1').textContent = v.safety.br1 ? t('status.active') : t('status.inactive');
                $('v-blindspot-right-lv2').textContent = v.safety.br2 ? t('status.active') : t('status.inactive');

                const sentryModeEl = $('v-sentry-mode');
                if (sentryModeEl) {
                    if (typeof v.safety.sm === 'boolean') {
                        sentryModeEl.textContent = v.safety.sm ? t('simulation.sentryOn') : t('simulation.sentryOff');
                    } else {
                        sentryModeEl.textContent = t('vehicle.none');
                    }
                }

                const sentryRequestEl = $('v-sentry-request');
                if (sentryRequestEl) {
                    const requestMap = {
                        AUTOPILOT_NOMINAL: t('vehicle.sentryNominal'),
                        AUTOPILOT_SENTRY: t('simulation.sentryOn'),
                        AUTOPILOT_SUSPEND: t('vehicle.sentrySuspend')
                    };
                    const requestState = v.safety.sr;
                    sentryRequestEl.textContent = requestState ? (requestMap[requestState] || requestState) : t('vehicle.none');
                }

                const sentryAlertEl = $('v-sentry-alert');
                if (sentryAlertEl) {
                    if (typeof v.safety.sa === 'boolean') {
                        sentryAlertEl.textContent = v.safety.sa ? t('simulation.sentryAlert') : t('vehicle.none');
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
                'v-blindspot-left-lv1', 'v-blindspot-right-lv1',
                'v-blindspot-left-lv2', 'v-blindspot-right-lv2',
                'v-sentry-mode', 'v-sentry-request', 'v-sentry-alert'
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
        if (statusIntervalHandle !== null) {
            statusIntervalHandle = setTimeout(updateStatus, 2000);
        }
    }
}
// Chargement de la config
async function loadConfig() {
    try {
        const response = await fetch(API_BASE + '/api/config');
        const config = await response.json();
        // Convertir 0-255 en pourcentage
        const nightBrightnessPercent = to255ToPercent(config.nbr);
        // Charger uniquement les éléments qui existent encore
        const autoNightMode = $('auto-night-mode');
        if (autoNightMode) {
            autoNightMode.checked = config.anm;
        }
        const nightBrightnessSlider = $('night-brightness-slider');
        if (nightBrightnessSlider) {
            nightBrightnessSlider.value = nightBrightnessPercent;
        }
        const nightBrightnessValue = $('night-brightness-value');
        if (nightBrightnessValue) {
            nightBrightnessValue.textContent = nightBrightnessPercent + '%';
        }
        // Charger aussi l'effet par défaut du profil actif
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

// Charger le statut audio
async function loadAudioStatus() {
    try {
        const response = await fetch(API_BASE + '/api/audio/status');
        const data = await response.json();

        audioEnabled = data.en;
        $('audio-enable').checked = audioEnabled;
        $('audio-sensitivity').value = data.sen;
        $('audio-sensitivity-value').textContent = data.sen;
        $('audio-gain').value = data.gn;
        $('audio-gain-value').textContent = data.gn;
        $('audio-auto-gain').checked = data.ag;

        // Mettre à jour le statut
        const statusEl = $('audio-status');
        if (statusEl) {
            statusEl.textContent = t(`audio.${audioEnabled ? 'enabled' : 'disabled'}`);
            statusEl.style.color = audioEnabled ? '#10B981' : 'var(--color-muted)';
        }

        // Afficher/masquer les paramètres
        const settingsEl = $('audio-settings');
        if (settingsEl) {
            settingsEl.style.display = audioEnabled ? 'block' : 'none';
        }

        if (audioEnabled) {
            startAudioDataPolling();
        }
    } catch (error) {
        console.error('Failed to load audio status:', error);
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
            audioEnabled = enabled;

            // Mettre à jour le statut
            const statusEl = $('audio-status');
            if (statusEl) {
                statusEl.textContent = t(`audio.${enabled ? 'enabled' : 'disabled'}`);
                statusEl.style.color = enabled ? '#10B981' : 'var(--color-muted)';
            }

            // Afficher/masquer les paramètres
            const settingsEl = $('audio-settings');
            if (settingsEl) {
                settingsEl.style.display = enabled ? 'block' : 'none';
            }

            if (enabled) {
                startAudioDataPolling();
            } else {
                stopAudioDataPolling();
            }

            showNotification('audio', enabled ? 'Audio enabled' : 'Audio disabled', 'success');
        }
    } catch (error) {
        console.error('Failed to toggle audio:', error);
        showNotification('audio', 'Failed to toggle audio', 'error');
    }
}

// Mettre à jour les valeurs des sliders
function updateAudioValue(param, value) {
    $(`audio-${param}-value`).textContent = value;
}

// Sauvegarder la configuration audio
async function saveAudioConfig() {
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
            showNotification('audio', 'Audio configuration saved', 'success');
        }
    } catch (error) {
        console.error('Failed to save audio config:', error);
        showNotification('audio', 'Failed to save audio config', 'error');
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
        if (audioUpdateInterval !== null) {
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
    const canvas = $('fftSpectrumCanvas');
    if (!canvas) {
        console.warn('Canvas fftSpectrumCanvas not found');
        return;
    }

    if (!bands || bands.length === 0) {
        console.warn('No FFT bands data to draw');
        return;
    }

    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    // Clear canvas
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, width, height);

    const barWidth = width / bands.length;

    // Draw bars
    for (let i = 0; i < bands.length; i++) {
        const barHeight = bands[i] * height;

        // Calculate rainbow color (bass = red, treble = blue)
        const hue = (i / bands.length) * 255;
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
        const fileInputEl = $('firmware-file');
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
                progressBar.style.width = progressValue + '%';
                progressPercent.textContent = progressValue + '%';
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
    }
}
async function uploadFirmware() {
    const fileInputEl = $('firmware-file');
    const file = fileInputEl.files[0];
    if (!file) {
        showNotification('ota-notification', t('ota.selectFile'), 'error');
        return;
    }
    if (!file.n.endsWith('.bin')) {
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
    // Démarrer le polling OTA pendant l'upload
    otaPollingInterval = setInterval(loadOTAInfo, 1000);
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
// Toggle event on/off
async function toggleEvent(eventType, isEnabled) {
    try {
        if (isEnabled) {
            try {
                await ensureEventsConfigData();
            } catch (error) {
                console.error('Failed to refresh events config for simulation:', error);
            }
            if (!isSimulationEventEnabled(eventType)) {
                const checkbox = $('toggle-' + eventType);
                if (checkbox) {
                    checkbox.checked = false;
                }
                setToggleContainerState(eventType, false);
                simulationTogglesState[eventType] = false;
                showNotification('simulation-notification', t('simulation.disabledEvent'), 'error');
                return;
            }
            cancelSimulationAutoStop(eventType);
            setToggleContainerState(eventType, true);
            showNotification('simulation-notification', t('simulation.sending') + ': ' + getEventName(eventType) + '...', 'info');
            const response = await fetch(API_BASE + '/api/simulate/event', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ event: eventType })
            });
            const result = await response.json();
            if (result.st === 'ok') {
                showNotification('simulation-notification', t('simulation.eventActive', getEventName(eventType)), 'success');
                simulationTogglesState[eventType] = true;
                const durationMs = await getSimulationEventDuration(eventType);
                if (durationMs > 0) {
                    scheduleSimulationAutoStop(eventType, durationMs);
                }
            } else {
                $('toggle-' + eventType).checked = false;
                setToggleContainerState(eventType, false);
                showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
                simulationTogglesState[eventType] = false;
            }
        } else {
            cancelSimulationAutoStop(eventType);
            showNotification('simulation-notification', t('simulation.stoppingEvent', getEventName(eventType)), 'info');
            const response = await fetch(API_BASE + '/api/stop/event', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ event: eventType })
            });
            const result = await response.json();
            if (result.st === 'ok') {
                setToggleContainerState(eventType, false);
                showNotification('simulation-notification', t('simulation.eventDeactivated', getEventName(eventType)), 'success');
                simulationTogglesState[eventType] = false;
                cancelSimulationAutoStop(eventType);
            } else {
                $('toggle-' + eventType).checked = true;
                setToggleContainerState(eventType, true);
                showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
                simulationTogglesState[eventType] = true;
            }
        }
    } catch (e) {
        console.error('Erreur:', e);
        $('toggle-' + eventType).checked = true;
        setToggleContainerState(eventType, true);
        showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
    }
}
// Toggle night mode simulation (single switch for ON/OFF)
async function toggleNightMode(isEnabled) {
    const toggleContainer = $('event-toggle-nightmode');
    const eventType = isEnabled ? 'NIGHT_MODE_ON' : 'NIGHT_MODE_OFF';
    const eventName = isEnabled ? t('canEvents.nightModeOn') : t('canEvents.nightModeOff');
    try {
        showNotification('simulation-notification', t('simulation.sendingEvent', eventName), 'info');
        const response = await fetch(API_BASE + '/api/simulate/event', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ event: eventType })
        });
        const result = await response.json();
        if (result.st === 'ok') {
            toggleContainer.classList.toggle('active', isEnabled);
            showNotification('simulation-notification', t('simulation.eventSimulated', eventName), 'success');
            simulationTogglesState['nightmode'] = isEnabled;
        } else {
            $('toggle-nightmode').checked = !isEnabled;
            simulationTogglesState['nightmode'] = !isEnabled;
            showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        $('toggle-nightmode').checked = !isEnabled;
        simulationTogglesState['nightmode'] = !isEnabled;
        showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
    }
}
// Mapping des événements vers les clés commonEvents
const EVENT_TO_COMMON = {
    'TURN_LEFT': 'turnLeft', 'TURN_RIGHT': 'turnRight', 'TURN_HAZARD': 'hazard',
    'CHARGING': 'charging', 'CHARGE_COMPLETE': 'chargeComplete',
    'DOOR_OPEN': 'doorOpen', 'DOOR_CLOSE': 'doorClose',
    'LOCKED': 'locked', 'UNLOCKED': 'unlocked',
    'BRAKE_ON': 'brakeOn', 'BRAKE_OFF': 'brakeOff',
    'BLINDSPOT_LEFT_LV1': 'blindspotLeftLv1', 'BLINDSPOT_LEFT_LV2': 'blindspotLeftLv2',
    'BLINDSPOT_RIGHT_LV1': 'blindspotRightLv1', 'BLINDSPOT_RIGHT_LV2': 'blindspotRightLv2',
    'NIGHT_MODE_ON': 'nightModeOn', 'NIGHT_MODE_OFF': 'nightModeOff',
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
            return eventTypesList[numericIndex].n;
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
                effectsList.forEach(effect => {
                    // Filtrer les effets CAN si ce n'est pas le sélecteur d'événement
                    if (!isEventSelector && effect.cr) {
                        return; // Skip cet effet
                    }
                    // Filtrer les effets audio si c'est le sélecteur d'événement
                    if (isEventSelector && effect.ae) {
                        return; // Skip cet effet audio
                    }
                    const option = doc.createElement('option');
                    option.value = effect.id;
                    option.textContent = translateEffectId(effect.id) || effect.n;
                    option.setAttribute('data-effect-name', effect.n);
                    select.appendChild(option);
                });
                // Restaurer la valeur sélectionnée si elle existe encore
                if (currentValue !== undefined && currentValue !== '') {
                    select.value = currentValue;
                }
            }
        });
        refreshEffectOptionLabels();
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
    // 3. BLE n'est pas connecté
    const shouldShow = bleTransport.isSupported() &&
                      !wifiOnline &&
                      bleTransport.getStatus() !== 'connected';
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

async function restartESP32() {
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

async function factoryResetESP32() {
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
        // Étape 1-3: Chargement des données essentielles (en parallèle)
        updateProgress(1, t('loading.loadingEffects'));
        await Promise.all([
            loadEffects(),
            loadEventTypes(),
            ensureEventsConfigData().catch(() => null)
        ]);
        await delay(100); // Petit délai pour soulager l'ESP32

        // Étape 4: Rendu des sections
        updateProgress(4, t('loading.loadingConfig'));
        renderSimulationSections();
        await delay(100);

        // Étape 5: Profils
        updateProgress(5, t('loading.loadingProfiles'));
        await loadProfiles();
        await delay(150);

        // Étape 6: Configuration
        updateProgress(6, t('loading.loadingConfig'));
        await loadConfig();
        await delay(150);

        // Étape 7: Configuration matérielle
        updateProgress(7, t('loading.loadingConfig'));
        await loadHardwareConfig();
        await delay(100);

        // Étape 8: Audio et FFT
        updateProgress(8, t('loading.loadingConfig'));
        await Promise.all([
            loadAudioStatus(),
            loadFFTStatus()
        ]);
        await delay(100);

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
// Init
renderSimulationSections();
applyTranslations();
applyTheme();
updateLanguageSelector();
updateBleUiState();
updateApiConnectionState();
scheduleInitialDataLoad();