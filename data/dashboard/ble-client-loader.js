/**
 * BleClient simple - Wrapper direct autour du plugin natif BluetoothLe
 * Pas de dépendances, pas d'imports, juste le plugin Capacitor natif
 */

(function() {
    if (!window.Capacitor?.Plugins?.BluetoothLe) {
        console.error('❌ BluetoothLe plugin not found');
        return;
    }

    const plugin = window.Capacitor.Plugins.BluetoothLe;

    // Fonction pour normaliser les UUIDs (comme dans le vrai BleClient)
    const parseUUID = (uuid) => {
        if (typeof uuid !== 'string') {
            throw new Error(`Invalid UUID type ${typeof uuid}. Expected string.`);
        }
        return uuid.toLowerCase();
    };

    // Wrapper minimal qui reproduit l'API BleClient
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
                    // Convertir hex string en DataView
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

            // Convertir DataView en hex string (format attendu par le plugin Android)
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
            // Placer value en dernier pour eviter d'etre ecrase par options.value
            return plugin.write({ deviceId, service, characteristic, ...options, value: hexValue });
        },

        writeWithoutResponse: (deviceId, service, characteristic, value, options) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);

            // Convertir DataView en hex string sans espaces (format attendu par le plugin Android)
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
            // Placer value en dernier pour eviter d'etre ecrase par options.value
            return plugin.writeWithoutResponse({ deviceId, service, characteristic, ...options, value: hexValue });
        },

        startNotifications: async (deviceId, service, characteristic, callback, options) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);
            const key = `notification|${deviceId}|${service}|${characteristic}`;

            // Enregistrer le listener pour les notifications
            plugin.addListener(key, (event) => {
                // Convertir hex string en DataView
                let dataView;
                if (typeof event.value === 'string') {
                    const bytes = new Uint8Array(event.value.match(/.{1,2}/g).map(b => parseInt(b, 16)));
                    dataView = new DataView(bytes.buffer);
                } else {
                    dataView = event.value;
                }
                callback(dataView);
            });

            // Démarrer les notifications
            return await plugin.startNotifications({ deviceId, service, characteristic, ...options });
        },

        stopNotifications: (deviceId, service, characteristic) => {
            service = parseUUID(service);
            characteristic = parseUUID(characteristic);
            return plugin.stopNotifications({ deviceId, service, characteristic });
        },

        requestLEScan: async (options, callback) => {
            // Le plugin utilise l'événement 'onScanResult' pour les résultats de scan
            const key = 'onScanResult';

            // Enregistrer le listener pour les résultats de scan
            plugin.addListener(key, (event) => {
                console.log('🔍 [BleClient] onScanResult event:', event);
                callback(event);
            });

            // Démarrer le scan
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
