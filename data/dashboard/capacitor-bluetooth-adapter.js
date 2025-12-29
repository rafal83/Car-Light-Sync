/**
 * Capacitor Bluetooth Adapter
 *
 * Ce module adapte l'API Bluetooth native de Capacitor pour qu'elle soit compatible
 * avec le code existant qui utilise Web Bluetooth API (navigator.bluetooth).
 *
 * L'adaptation se fait de manière transparente :
 * - Si on est sur le web : utilise Web Bluetooth API
 * - Si on est sur mobile : utilise Capacitor Bluetooth LE
 */

// Accéder aux objets globaux Capacitor (injectés par le runtime)
const Capacitor = window.Capacitor;
const isNativePlatform = Capacitor?.isNativePlatform() || false;

// Attendre que BleClient soit chargé (depuis le module ES6)
let BleClient = window.BleClient;

// Fonction helper pour attendre BleClient
async function waitForBleClient() {
    if (window.BleClient) {
        return window.BleClient;
    }

    // Attendre jusqu'à 5 secondes
    for (let i = 0; i < 50; i++) {
        await new Promise(resolve => setTimeout(resolve, 100));
        if (window.BleClient) {
            return window.BleClient;
        }
    }

    throw new Error('BleClient not loaded after 5 seconds');
}

// Classe qui simule un BluetoothDevice compatible avec Web Bluetooth API
class CapacitorBluetoothDevice {
    constructor(deviceId, name) {
        this.id = deviceId;
        this.name = name;
        this.gatt = new CapacitorBluetoothRemoteGATTServer(deviceId, this);
        this._eventListeners = {};
    }

    addEventListener(eventType, callback) {
        if (!this._eventListeners[eventType]) {
            this._eventListeners[eventType] = [];
        }
        this._eventListeners[eventType].push(callback);
    }

    removeEventListener(eventType, callback) {
        if (this._eventListeners[eventType]) {
            const index = this._eventListeners[eventType].indexOf(callback);
            if (index > -1) {
                this._eventListeners[eventType].splice(index, 1);
            }
        }
    }

    _dispatchEvent(eventType, event) {
        if (this._eventListeners[eventType]) {
            this._eventListeners[eventType].forEach(callback => callback(event));
        }
    }
}

// Classe qui simule un BluetoothRemoteGATTServer
class CapacitorBluetoothRemoteGATTServer {
    constructor(deviceId, parentDevice) {
        this.deviceId = deviceId;
        this.device = parentDevice;
        this.connected = false;
    }

    async connect() {
        try {
            console.log('🔍 [DEBUG] GATT connect called with deviceId:', this.deviceId);
            console.log('🔍 [DEBUG] deviceId type:', typeof this.deviceId);
            console.log('🔍 [DEBUG] deviceId value:', JSON.stringify(this.deviceId));

            if (!this.deviceId) {
                throw new Error('Device ID is null or undefined');
            }

            // Définir le callback de déconnexion
            const onDisconnect = (deviceId) => {
                console.log('🔍 [DEBUG] Device disconnected:', deviceId);
                this.connected = false;
                if (this.device) {
                    this.device._dispatchEvent('gattserverdisconnected', {});
                }
            };

            console.log('🔍 [DEBUG] About to call BleClient.connect...');

            // BleClient wrapper prend des paramètres séparés
            await BleClient.connect(this.deviceId, onDisconnect, { timeout: 10000 });

            this.connected = true;
            console.log('✅ Connected to device:', this.deviceId);

            // Forcer explicitement la découverte des services
            console.log('🔍 [DEBUG] Explicitly discovering services...');
            try {
                await BleClient.discoverServices(this.deviceId);
                console.log('✅ Services discovered');

                // Vérifier les services découverts
                const services = await BleClient.getServices(this.deviceId);
                // console.log('🔍 [DEBUG] Available services:', JSON.stringify(services, null, 2));
            } catch (error) {
                console.warn('⚠️ discoverServices failed (may not be needed):', error);
            }

            // Attendre un peu pour que les descripteurs (CCCD) soient bien disponibles
            console.log('⏳ Waiting 1s for descriptors to be ready...');
            await new Promise(resolve => setTimeout(resolve, 1000));
            console.log('✅ Ready for operations');

            return this;
        } catch (error) {
            console.error('GATT connect error:', error);
            console.error('Error details:', JSON.stringify(error));
            throw error;
        }
    }

    async disconnect() {
        try {
            // BleClient wrapper prend le deviceId en paramètre
            await BleClient.disconnect(this.deviceId);
            this.connected = false;
        } catch (error) {
            console.error('GATT disconnect error:', error);
            throw error;
        }
    }

    async getPrimaryService(serviceUuid) {
        console.log('🔍 [DEBUG] getPrimaryService:', serviceUuid, 'for device:', this.deviceId);
        return new CapacitorBluetoothRemoteGATTService(this.deviceId, serviceUuid);
    }
}

// Classe qui simule un BluetoothRemoteGATTService
class CapacitorBluetoothRemoteGATTService {
    constructor(deviceId, serviceUuid) {
        this.deviceId = deviceId;
        this.uuid = serviceUuid;
    }

    async getCharacteristic(characteristicUuid) {
        // console.log('🔍 [DEBUG] getCharacteristic:', characteristicUuid, 'from service:', this.uuid);
        return new CapacitorBluetoothRemoteGATTCharacteristic(
            this.deviceId,
            this.uuid,
            characteristicUuid
        );
    }
}

// Classe qui simule une BluetoothRemoteGATTCharacteristic
class CapacitorBluetoothRemoteGATTCharacteristic {
    constructor(deviceId, serviceUuid, characteristicUuid) {
        this.deviceId = deviceId;
        this.service = { uuid: serviceUuid };
        this.uuid = characteristicUuid;
        this.value = null;
        this._notificationCallback = null;
    }

    async writeValue(value) {
        return this.writeValueWithResponse(value);
    }

    async writeValueWithResponse(value) {
        // Dans Web Bluetooth API, writeValueWithResponse attend une confirmation
        // BleClient.write attend déjà une confirmation par défaut
        try {
            let dataView;
            if (value instanceof DataView) {
                dataView = value;
            } else if (value instanceof Uint8Array) {
                // Créer DataView avec offset et length corrects
                dataView = new DataView(value.buffer, value.byteOffset, value.byteLength);
            } else if (value.buffer) {
                dataView = new DataView(value.buffer, value.byteOffset || 0, value.byteLength || value.buffer.byteLength);
            } else {
                dataView = new DataView(value);
            }

            // console.log('🔍 [Adapter] writeValueWithResponse:', dataView.byteLength, 'bytes');

            // BleClient wrapper prend des paramètres séparés
            await BleClient.write(this.deviceId, this.service.uuid, this.uuid, dataView);
        } catch (error) {
            console.error('Write value with response error:', error);
            throw error;
        }
    }

    async writeValueWithoutResponse(value) {
        // Version sans attendre de confirmation (plus rapide)
        try {
            let dataView;
            if (value instanceof DataView) {
                dataView = value;
            } else if (value instanceof Uint8Array) {
                // Créer DataView avec offset et length corrects
                dataView = new DataView(value.buffer, value.byteOffset, value.byteLength);
            } else if (value.buffer) {
                dataView = new DataView(value.buffer, value.byteOffset || 0, value.byteLength || value.buffer.byteLength);
            } else {
                dataView = new DataView(value);
            }

            console.log('🔍 [Adapter] writeValueWithoutResponse:', dataView.byteLength, 'bytes');

            // BleClient wrapper prend des paramètres séparés
            await BleClient.writeWithoutResponse(this.deviceId, this.service.uuid, this.uuid, dataView);
        } catch (error) {
            console.error('Write value without response error:', error);
            throw error;
        }
    }

    async readValue() {
        try {
            // BleClient wrapper prend des paramètres séparés et retourne directement le DataView
            this.value = await BleClient.read(this.deviceId, this.service.uuid, this.uuid);
            return this.value;
        } catch (error) {
            console.error('Read value error:', error);
            throw error;
        }
    }

    async startNotifications() {
        try {
            console.log('🔍 [DEBUG] startNotifications for:', {
                deviceId: this.deviceId,
                service: this.service.uuid,
                characteristic: this.uuid
            });

            // BleClient wrapper prend des paramètres séparés et un callback
            await BleClient.startNotifications(
                this.deviceId,
                this.service.uuid,
                this.uuid,
                (dataView) => {
                    if (this._notificationCallback) {
                        const event = {
                            target: {
                                value: dataView
                            }
                        };
                        this._notificationCallback(event);
                    }
                }
            );

            return this;
        } catch (error) {
            console.error('Start notifications error:', error);
            throw error;
        }
    }

    async stopNotifications() {
        try {
            // BleClient wrapper prend des paramètres séparés
            await BleClient.stopNotifications(this.deviceId, this.service.uuid, this.uuid);
        } catch (error) {
            console.error('Stop notifications error:', error);
            throw error;
        }
    }

    addEventListener(eventType, callback) {
        if (eventType === 'characteristicvaluechanged') {
            this._notificationCallback = callback;
        }
    }

    removeEventListener(eventType, callback) {
        if (eventType === 'characteristicvaluechanged') {
            this._notificationCallback = null;
        }
    }
}

// Adapter pour navigator.bluetooth
class CapacitorBluetoothNavigator {
    async getDevices() {
        // Sur Capacitor, on ne peut pas récupérer la liste des devices autorisés
        // On retourne un tableau vide et on utilisera le scan à la place
        return [];
    }

    async scanForDeviceByName(deviceName, timeoutMs = 5000) {
        try {
            // Attendre que BleClient soit chargé
            BleClient = await waitForBleClient();

            // Initialiser le Bluetooth
            await BleClient.initialize();

            console.log('🔍 [Capacitor] Scanning for device with name:', deviceName);

            let foundDevice = null;

            // Démarrer le scan
            await BleClient.requestLEScan({}, (result) => {
                console.log('🔍 [Capacitor] Found device:', result.device.name, result.device.deviceId);
                if (result.device.name === deviceName) {
                    console.log('✅ [Capacitor] Found matching device by name!');
                    foundDevice = result.device;
                }
            });

            // Attendre jusqu'à trouver le device ou timeout
            const startTime = Date.now();
            while (!foundDevice && (Date.now() - startTime) < timeoutMs) {
                await new Promise(resolve => setTimeout(resolve, 100));
            }

            // Arrêter le scan
            await BleClient.stopLEScan();

            if (foundDevice) {
                // console.log('✅ [Capacitor] Device found via scan:', foundDevice.name);
                return new CapacitorBluetoothDevice(foundDevice.deviceId, foundDevice.name || 'Unknown Device');
            }

            console.log('⚠️ [Capacitor] Device not found during scan');
            return null;
        } catch (error) {
            console.error('Scan for device error:', error);
            try {
                await BleClient.stopLEScan();
            } catch (e) {}
            return null;
        }
    }

    async requestDevice(options) {
        try {
            // Attendre que BleClient soit chargé
            BleClient = await waitForBleClient();

            // Initialiser le Bluetooth
            await BleClient.initialize();

            // Chercher le nom et le préfixe dans tous les filtres
            let filterName = null;
            let filterNamePrefix = null;

            if (options.filters && Array.isArray(options.filters)) {
                for (const filter of options.filters) {
                    if (filter.name) filterName = filter.name;
                    if (filter.namePrefix) filterNamePrefix = filter.namePrefix;
                }
            }

            console.log('🔍 [DEBUG] All filters received:', JSON.stringify(options.filters));
            console.log('🔍 [DEBUG] Extracted - name:', filterName, 'prefix:', filterNamePrefix);

            // Utiliser requestDevice du plugin qui affiche un dialogue natif de sélection
            // C'est la méthode recommandée par @capacitor-community/bluetooth-le
            const bleDevice = await BleClient.requestDevice({
                namePrefix: filterNamePrefix || filterName || undefined,
                // Ne pas filtrer par services car l'ESP32 ne les annonce pas toujours
                optionalServices: options.optionalServices || []
            });

            console.log('🔵 Selected device:', bleDevice.name, bleDevice.deviceId);

            const device = new CapacitorBluetoothDevice(
                bleDevice.deviceId,
                bleDevice.name || 'Unknown Device'
            );

            return device;
        } catch (error) {
            console.error('Request device error:', error);
            throw error;
        }
    }
}

// Si on est sur une plateforme native, remplacer navigator.bluetooth
if (isNativePlatform) {
    console.log('🔵 Using Capacitor Bluetooth LE (Native)');

    // Créer un nouveau navigator.bluetooth compatible
    if (!navigator.bluetooth) {
        Object.defineProperty(navigator, 'bluetooth', {
            value: new CapacitorBluetoothNavigator(),
            writable: false,
            configurable: false
        });
    }

    // Sur mobile natif, simuler que le geste utilisateur a déjà été capturé
    // Cela permet la connexion automatique sans intervention utilisateur
    console.log('📱 Native platform detected: bypassing gesture requirement for BLE auto-connect');

    // Fonction pour forcer le flag de geste capturé
    const forceGestureCaptured = () => {
        // Essayer d'accéder aux variables globales de l'application
        if (typeof window.bleAutoConnectGestureCaptured !== 'undefined') {
            window.bleAutoConnectGestureCaptured = true;
            console.log('✅ BLE gesture flag set to true (native platform)');
        } else {
            // Si la variable n'existe pas encore, la créer
            window.bleAutoConnectGestureCaptured = true;
            console.log('✅ BLE gesture flag created and set to true (native platform)');
        }

        // Forcer aussi le flag d'auto-connect en cours à false
        if (typeof window.bleAutoConnectInProgress !== 'undefined') {
            window.bleAutoConnectInProgress = false;
        }

        // S'assurer que le flag "awaiting gesture" est à false
        if (typeof window.bleAutoConnectAwaitingGesture !== 'undefined') {
            window.bleAutoConnectAwaitingGesture = false;
        }

        // Debug: afficher l'état de bleTransport
        if (window.bleTransport) {
            console.log('🔍 [DEBUG] bleTransport exists');
            console.log('🔍 [DEBUG] bleTransport.isSupported():', window.bleTransport.isSupported ? window.bleTransport.isSupported() : 'method not found');
            console.log('🔍 [DEBUG] bleTransport.getStatus():', window.bleTransport.getStatus ? window.bleTransport.getStatus() : 'method not found');
        } else {
            console.warn('⚠️ bleTransport not found yet');
        }

        // Déclencher la tentative de connexion automatique si la fonction existe
        if (typeof window.maybeAutoConnectBle === 'function') {
            console.log('🔄 Triggering BLE auto-connect...');
            window.maybeAutoConnectBle(true);
        } else {
            console.warn('⚠️ maybeAutoConnectBle function not found yet');
        }
    };

    // Essayer immédiatement
    forceGestureCaptured();

    // Réessayer après chargement du DOM
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => {
            setTimeout(forceGestureCaptured, 100);
            setTimeout(forceGestureCaptured, 500);
            setTimeout(forceGestureCaptured, 1000);
        });
    } else {
        setTimeout(forceGestureCaptured, 100);
        setTimeout(forceGestureCaptured, 500);
        setTimeout(forceGestureCaptured, 1000);
    }

    // Hook pour intercepter le check de wifiOnline et forcer offline sur mobile
    // Cela force l'utilisation de BLE au lieu de WiFi
    Object.defineProperty(window, 'isCapacitorNativeApp', {
        value: true,
        writable: false,
        configurable: false
    });
    console.log('✅ Capacitor native app flag set');
} else {
    console.log('🌐 Using Web Bluetooth API (Browser)');
}

// Exposer globalement pour debug
window.__capacitorBleAdapter = { isNativePlatform, CapacitorBluetoothNavigator };
