/**
 * Capacitor Bluetooth Adapter
 *
 * This module adapts the native Capacitor Bluetooth API to be compatible
 * with existing code that uses the Web Bluetooth API (navigator.bluetooth).
 *
 * The adaptation is transparent:
 * - If on web: uses Web Bluetooth API
 * - If on mobile: uses Capacitor Bluetooth LE
 */

// Access global Capacitor objects (injected by the runtime)
const Capacitor = window.Capacitor;
const isNativePlatform = Capacitor?.isNativePlatform() || false;

// Wait for BleClient to be loaded (from ES6 module)
let BleClient = window.BleClient;

// Helper function to wait for BleClient
async function waitForBleClient() {
    if (window.BleClient) {
        return window.BleClient;
    }

    // Wait up to 5 seconds
    for (let i = 0; i < 50; i++) {
        await new Promise(resolve => setTimeout(resolve, 100));
        if (window.BleClient) {
            return window.BleClient;
        }
    }

    throw new Error('BleClient not loaded after 5 seconds');
}

// Class that simulates a BluetoothDevice compatible with Web Bluetooth API
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

// Class that simulates a BluetoothRemoteGATTServer
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

            // Define disconnect callback
            const onDisconnect = (deviceId) => {
                console.log('🔍 [DEBUG] Device disconnected:', deviceId);
                this.connected = false;
                if (this.device) {
                    this.device._dispatchEvent('gattserverdisconnected', {});
                }
            };

            console.log('🔍 [DEBUG] About to call BleClient.connect...');

            // BleClient wrapper takes separate parameters
            await BleClient.connect(this.deviceId, onDisconnect, { timeout: 10000 });

            this.connected = true;
            console.log('✅ Connected to device:', this.deviceId);

            // Explicitly force service discovery
            console.log('🔍 [DEBUG] Explicitly discovering services...');
            try {
                await BleClient.discoverServices(this.deviceId);
                console.log('✅ Services discovered');

                // Check discovered services
                const services = await BleClient.getServices(this.deviceId);
                // console.log('🔍 [DEBUG] Available services:', JSON.stringify(services, null, 2));
            } catch (error) {
                console.warn('⚠️ discoverServices failed (may not be needed):', error);
            }

            // Wait a bit for descriptors (CCCD) to be ready
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
            // BleClient wrapper takes deviceId as parameter
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

// Class that simulates a BluetoothRemoteGATTService
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

// Class that simulates a BluetoothRemoteGATTCharacteristic
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
        // In Web Bluetooth API, writeValueWithResponse waits for confirmation
        // BleClient.write waits for confirmation by default
        try {
            let dataView;
            if (value instanceof DataView) {
                dataView = value;
            } else if (value instanceof Uint8Array) {
                // Create DataView with correct offset and length
                dataView = new DataView(value.buffer, value.byteOffset, value.byteLength);
            } else if (value.buffer) {
                dataView = new DataView(value.buffer, value.byteOffset || 0, value.byteLength || value.buffer.byteLength);
            } else {
                dataView = new DataView(value);
            }

            // console.log('🔍 [Adapter] writeValueWithResponse:', dataView.byteLength, 'bytes');

            // BleClient wrapper takes separate parameters
            await BleClient.write(this.deviceId, this.service.uuid, this.uuid, dataView);
        } catch (error) {
            console.error('Write value with response error:', error);
            throw error;
        }
    }

    async writeValueWithoutResponse(value) {
        // Version without waiting for confirmation (faster)
        try {
            let dataView;
            if (value instanceof DataView) {
                dataView = value;
            } else if (value instanceof Uint8Array) {
                // Create DataView with correct offset and length
                dataView = new DataView(value.buffer, value.byteOffset, value.byteLength);
            } else if (value.buffer) {
                dataView = new DataView(value.buffer, value.byteOffset || 0, value.byteLength || value.buffer.byteLength);
            } else {
                dataView = new DataView(value);
            }

            console.log('🔍 [Adapter] writeValueWithoutResponse:', dataView.byteLength, 'bytes');

            // BleClient wrapper takes separate parameters
            await BleClient.writeWithoutResponse(this.deviceId, this.service.uuid, this.uuid, dataView);
        } catch (error) {
            console.error('Write value without response error:', error);
            throw error;
        }
    }

    async readValue() {
        try {
            // BleClient wrapper takes separate parameters and returns DataView directly
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

            // BleClient wrapper takes separate parameters and a callback
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
            // BleClient wrapper takes separate parameters
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

// Adapter for navigator.bluetooth
class CapacitorBluetoothNavigator {
    async getDevices() {
        // On Capacitor, we cannot retrieve the list of authorized devices
        // Return empty array and use scan instead
        return [];
    }

    async scanForDeviceByName(deviceName, timeoutMs = 5000) {
        try {
            // Wait for BleClient to be loaded
            BleClient = await waitForBleClient();

            // Initialize Bluetooth
            await BleClient.initialize();

            // IMPORTANT: First, check if device is already connected at native level
            // This avoids unnecessary scanning if we just changed pages
            console.log('🔍 [Capacitor] Checking already connected devices first...');
            const lastDeviceId = localStorage.getItem('ble-last-device-id');

            if (lastDeviceId) {
                try {
                    // Check if this device is already connected
                    const result = await BleClient.getConnectedDevices([]);
                    console.log('🔍 [Capacitor] getConnectedDevices result:', result);

                    // Plugin returns {devices: Array} not directly an array
                    const connectedDevices = result.devices || result;
                    console.log('🔍 [Capacitor] Connected devices array:', connectedDevices);

                    if (Array.isArray(connectedDevices)) {
                        const alreadyConnected = connectedDevices.find(d =>
                            d.deviceId === lastDeviceId || d.name === deviceName
                        );

                        if (alreadyConnected) {
                            console.log('✅ [Capacitor] Device already connected!', alreadyConnected);
                            return new CapacitorBluetoothDevice(
                                alreadyConnected.deviceId,
                                alreadyConnected.name || deviceName
                            );
                        }
                    }
                } catch (e) {
                    console.log('⚠️ [Capacitor] Could not check connected devices:', e.message);
                }
            }

            console.log('🔍 [Capacitor] Scanning for device with name:', deviceName);

            let foundDevice = null;

            // Start scanning
            await BleClient.requestLEScan({}, (result) => {
                console.log('🔍 [Capacitor] Found device:', result.device.name, result.device.deviceId);
                if (result.device.name === deviceName) {
                    console.log('✅ [Capacitor] Found matching device by name!');
                    foundDevice = result.device;
                }
            });

            // Wait until device found or timeout
            const startTime = Date.now();
            while (!foundDevice && (Date.now() - startTime) < timeoutMs) {
                await new Promise(resolve => setTimeout(resolve, 100));
            }

            // Stop scanning
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
            // Wait for BleClient to be loaded
            BleClient = await waitForBleClient();

            // Initialize Bluetooth
            await BleClient.initialize();

            // Search for name and prefix in all filters
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

            // Use plugin's requestDevice which displays a native selection dialog
            // This is the recommended method by @capacitor-community/bluetooth-le
            const bleDevice = await BleClient.requestDevice({
                namePrefix: filterNamePrefix || filterName || undefined,
                // Don't filter by services because ESP32 doesn't always advertise them
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

// If on native platform, replace navigator.bluetooth
if (isNativePlatform) {
    console.log('🔵 Using Capacitor Bluetooth LE (Native)');

    // Create a compatible navigator.bluetooth
    if (!navigator.bluetooth) {
        Object.defineProperty(navigator, 'bluetooth', {
            value: new CapacitorBluetoothNavigator(),
            writable: false,
            configurable: false
        });
    }

    // On native mobile, simulate that user gesture was already captured
    // This allows automatic connection without user intervention
    console.log('📱 Native platform detected: bypassing gesture requirement for BLE auto-connect');

    // Function to force the captured gesture flag
    const forceGestureCaptured = () => {
        // Try to access application global variables
        if (typeof window.bleAutoConnectGestureCaptured !== 'undefined') {
            window.bleAutoConnectGestureCaptured = true;
            console.log('✅ BLE gesture flag set to true (native platform)');
        } else {
            // If variable doesn't exist yet, create it
            window.bleAutoConnectGestureCaptured = true;
            console.log('✅ BLE gesture flag created and set to true (native platform)');
        }

        // Also force auto-connect in progress flag to false
        if (typeof window.bleAutoConnectInProgress !== 'undefined') {
            window.bleAutoConnectInProgress = false;
        }

        // Ensure "awaiting gesture" flag is false
        if (typeof window.bleAutoConnectAwaitingGesture !== 'undefined') {
            window.bleAutoConnectAwaitingGesture = false;
        }

        // Debug: display bleTransport state
        if (window.bleTransport) {
            console.log('🔍 [DEBUG] bleTransport exists');
            console.log('🔍 [DEBUG] bleTransport.isSupported():', window.bleTransport.isSupported ? window.bleTransport.isSupported() : 'method not found');
            console.log('🔍 [DEBUG] bleTransport.getStatus():', window.bleTransport.getStatus ? window.bleTransport.getStatus() : 'method not found');
        } else {
            console.warn('⚠️ bleTransport not found yet');
        }

        // Trigger automatic connection attempt if function exists
        if (typeof window.maybeAutoConnectBle === 'function') {
            console.log('🔄 Triggering BLE auto-connect...');
            window.maybeAutoConnectBle(true);
        } else {
            console.warn('⚠️ maybeAutoConnectBle function not found yet');
        }
    };

    // Try immediately
    forceGestureCaptured();

    // Retry after DOM loading
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

    // Hook to intercept wifiOnline check and force offline on mobile
    // This forces BLE usage instead of WiFi
    Object.defineProperty(window, 'isCapacitorNativeApp', {
        value: true,
        writable: false,
        configurable: false
    });
    console.log('✅ Capacitor native app flag set');
} else {
    console.log('🌐 Using Web Bluetooth API (Browser)');
}

// Expose globally for debugging
window.__capacitorBleAdapter = { isNativePlatform, CapacitorBluetoothNavigator };
