/**
 * BleClient simple - Direct wrapper around native BluetoothLe plugin
 * No dependencies, no imports, just the native Capacitor plugin
 */

(function() {
    if (!window.Capacitor?.Plugins?.BluetoothLe) {
        console.error('❌ BluetoothLe plugin not found');
        return;
    }

    const plugin = window.Capacitor.Plugins.BluetoothLe;

    // Function to normalize UUIDs (like in the real BleClient)
    const parseUUID = (uuid) => {
        if (typeof uuid !== 'string') {
            throw new Error(`Invalid UUID type ${typeof uuid}. Expected string.`);
        }
        return uuid.toLowerCase();
    };

    // Minimal wrapper that reproduces BleClient API
    window.BleClient = {
        initialize: (options) => plugin.initialize(options),

        requestDevice: (options) => plugin.requestDevice(options),

        connect: (deviceId, onDisconnect, options) => {
            return plugin.connect({ deviceId, ...options }).then(() => {
                if (onDisconnect) {
                    plugin.addListener(`disconnected|${deviceId}`, () => onDisconnect(deviceId));
                }
            });
        },

        disconnect: (deviceId) => plugin.disconnect({ deviceId }),

        discoverServices: (deviceId) => plugin.discoverServices({ deviceId }),

        getServices: (deviceId) => plugin.getServices({ deviceId }),

        read: (deviceId, service, characteristic, options) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);
            return plugin.read({ deviceId, service, characteristic, ...options })
                .then(result => {
                    // Convert hex string to DataView
                    if (typeof result.value === 'string') {
                        const bytes = new Uint8Array(result.value.match(/.{1,2}/g).map(b => parseInt(b, 16)));
                        return new DataView(bytes.buffer);
                    }
                    return result.value;
                });
        },

        write: (deviceId, service, characteristic, value, options) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);

            // Convert DataView to hex string (format expected by Android plugin)
            let bytes;
            if (value instanceof DataView) {
                bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
            } else if (value instanceof Uint8Array) {
                bytes = value;
            } else if (value.buffer) {
                bytes = new Uint8Array(value.buffer);
            } else {
                bytes = new Uint8Array(value);
            }
            const hexValue = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
            // Place value last to avoid being overwritten by options.value
            return plugin.write({ deviceId, service, characteristic, ...options, value: hexValue });
        },

        writeWithoutResponse: (deviceId, service, characteristic, value, options) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);

            // Convert DataView to hex string without spaces (format expected by Android plugin)
            let bytes;
            if (value instanceof DataView) {
                bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
            } else if (value instanceof Uint8Array) {
                bytes = value;
            } else if (value.buffer) {
                bytes = new Uint8Array(value.buffer);
            } else {
                bytes = new Uint8Array(value);
            }
            const hexValue = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
            // Place value last to avoid being overwritten by options.value
            return plugin.writeWithoutResponse({ deviceId, service, characteristic, ...options, value: hexValue });
        },

        startNotifications: async (deviceId, service, characteristic, callback, options) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);
            const key = `notification|${deviceId}|${service}|${characteristic}`;

            // Register listener for notifications
            plugin.addListener(key, (event) => {
                // Convert hex string to DataView
                let dataView;
                if (typeof event.value === 'string') {
                    const bytes = new Uint8Array(event.value.match(/.{1,2}/g).map(b => parseInt(b, 16)));
                    dataView = new DataView(bytes.buffer);
                } else {
                    dataView = event.value;
                }
                callback(dataView);
            });

            // Start notifications
            return await plugin.startNotifications({ deviceId, service, characteristic, ...options });
        },

        stopNotifications: (deviceId, service, characteristic) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);
            return plugin.stopNotifications({ deviceId, service, characteristic });
        },

        requestLEScan: async (options, callback) => {
            // Plugin uses 'onScanResult' event for scan results
            const key = 'onScanResult';

            // Register listener for scan results
            plugin.addListener(key, (event) => {
                console.log('🔍 [BleClient] onScanResult event:', event);
                callback(event);
            });

            // Start scanning
            return await plugin.requestLEScan(options);
        },

        stopLEScan: () => {
            return plugin.stopLEScan();
        },

        getConnectedDevices: (services) => {
            return plugin.getConnectedDevices({ services: services || [] });
        }
    };
})();
