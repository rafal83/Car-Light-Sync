# Adaptateur Bluetooth : Web vs Native

## 🎯 Objectif

L'adaptateur Bluetooth permet d'utiliser **le même code** sur navigateur web et application mobile native, en abstrayant les différences entre Web Bluetooth API et Capacitor Bluetooth LE.

## 🏗️ Architecture

```
┌─────────────────────────────────────────────┐
│         Application (index.html/script.js)  │
│                                             │
│    navigator.bluetooth.requestDevice()      │
│    device.gatt.connect()                    │
│    characteristic.writeValue()              │
│    characteristic.startNotifications()      │
└─────────────────┬───────────────────────────┘
                  │
                  │ Détection plateforme
                  ▼
        ┌─────────┴─────────┐
        │                   │
┌───────▼──────┐    ┌───────▼──────────┐
│ Web Browser  │    │  Mobile Native   │
│              │    │                  │
│ Web Bluetooth│    │ Capacitor BLE    │
│     API      │    │  + Adaptateur    │
└──────────────┘    └──────────────────┘
```

## 📊 Comparaison des APIs

### Web Bluetooth API (Navigateur)

```javascript
// Demander un appareil
const device = await navigator.bluetooth.requestDevice({
  filters: [{ services: [serviceUuid] }]
});

// Connexion GATT
const server = await device.gatt.connect();
const service = await server.getPrimaryService(serviceUuid);
const characteristic = await service.getCharacteristic(charUuid);

// Écriture
await characteristic.writeValue(uint8Array);

// Lecture
const value = await characteristic.readValue();

// Notifications
await characteristic.startNotifications();
characteristic.addEventListener('characteristicvaluechanged', (event) => {
  const value = event.target.value; // DataView
});
```

### Capacitor Bluetooth LE (Natif)

```javascript
// Initialisation
await BluetoothLe.initialize();

// Scanner
await BluetoothLe.requestLEScan({ services: [serviceUuid] });
BluetoothLe.addListener('onScanResult', (result) => {
  const device = result.device;
});

// Connexion
await BluetoothLe.connect({ deviceId: deviceId });

// Écriture (base64)
await BluetoothLe.write({
  deviceId: deviceId,
  service: serviceUuid,
  characteristic: charUuid,
  value: base64String
});

// Lecture (base64)
const result = await BluetoothLe.read({
  deviceId: deviceId,
  service: serviceUuid,
  characteristic: charUuid
});

// Notifications (base64)
await BluetoothLe.startNotifications({
  deviceId: deviceId,
  service: serviceUuid,
  characteristic: charUuid
});
BluetoothLe.addListener('notification|...', (data) => {
  const value = data.value; // base64
});
```

## 🔄 Mapping de l'adaptateur

### Détection de plateforme

```javascript
import { Capacitor } from '@capacitor/core';

const isNativePlatform = Capacitor.isNativePlatform();

if (isNativePlatform) {
  // Remplacer navigator.bluetooth
  navigator.bluetooth = new CapacitorBluetoothNavigator();
}
```

### Classes d'adaptation

| Classe adaptateur | Simule | API cible |
|-------------------|--------|-----------|
| `CapacitorBluetoothNavigator` | `navigator.bluetooth` | `BluetoothLe.requestLEScan()` |
| `CapacitorBluetoothDevice` | `BluetoothDevice` | Device info |
| `CapacitorBluetoothRemoteGATTServer` | `BluetoothRemoteGATTServer` | `BluetoothLe.connect()` |
| `CapacitorBluetoothRemoteGATTService` | `BluetoothRemoteGATTService` | Service info |
| `CapacitorBluetoothRemoteGATTCharacteristic` | `BluetoothRemoteGATTCharacteristic` | Read/Write/Notify |

### Conversion des données

**Web Bluetooth** : Utilise `Uint8Array` et `DataView`
**Capacitor BLE** : Utilise Base64

L'adaptateur gère automatiquement la conversion :

```javascript
// Uint8Array → Base64
_arrayBufferToBase64(buffer) {
  let binary = '';
  const bytes = new Uint8Array(buffer);
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return btoa(binary);
}

// Base64 → Uint8Array
_base64ToArrayBuffer(base64) {
  const binaryString = atob(base64);
  const bytes = new Uint8Array(binaryString.length);
  for (let i = 0; i < binaryString.length; i++) {
    bytes[i] = binaryString.charCodeAt(i);
  }
  return bytes.buffer;
}
```

## 🔍 Détails d'implémentation

### 1. Request Device (Scan BLE)

**Flux Web** :
```javascript
navigator.bluetooth.requestDevice()
  → Navigateur affiche popup de sélection
  → Utilisateur choisit un appareil
  → Retourne BluetoothDevice
```

**Flux Adapté (Natif)** :
```javascript
CapacitorBluetoothNavigator.requestDevice()
  → BluetoothLe.initialize()
  → BluetoothLe.requestLEScan()
  → Écoute 'onScanResult' pendant 5s
  → Retourne CapacitorBluetoothDevice (premier trouvé)
```

**TODO** : Afficher une liste de sélection native pour l'utilisateur

### 2. GATT Connect

**Flux Web** :
```javascript
device.gatt.connect()
  → Connexion GATT directe
  → Retourne BluetoothRemoteGATTServer
```

**Flux Adapté (Natif)** :
```javascript
CapacitorBluetoothRemoteGATTServer.connect()
  → BluetoothLe.connect({ deviceId, timeout: 10000 })
  → Retourne this (CapacitorBluetoothRemoteGATTServer)
```

### 3. Write Value

**Flux Web** :
```javascript
characteristic.writeValue(uint8Array)
  → Écriture directe
```

**Flux Adapté (Natif)** :
```javascript
CapacitorBluetoothRemoteGATTCharacteristic.writeValue(uint8Array)
  → Conversion Uint8Array → Base64
  → BluetoothLe.write({ deviceId, service, characteristic, value: base64 })
```

### 4. Notifications

**Flux Web** :
```javascript
characteristic.startNotifications()
characteristic.addEventListener('characteristicvaluechanged', callback)
  → Callback reçoit event.target.value (DataView)
```

**Flux Adapté (Natif)** :
```javascript
CapacitorBluetoothRemoteGATTCharacteristic.startNotifications()
  → BluetoothLe.startNotifications()
  → BluetoothLe.addListener('notification|deviceId|service|char', callback)
  → Conversion Base64 → DataView
  → Appel du callback stocké avec event simulé
```

## 🐛 Debug et logs

### Activer les logs

Les logs sont automatiquement affichés dans la console :

```javascript
console.log('🔵 Using Capacitor Bluetooth LE (Native)');
console.log('🌐 Using Web Bluetooth API (Browser)');
```

### Inspecter la plateforme

```javascript
// Dans la console du navigateur ou du DevTools mobile
console.log('Platform:', Capacitor.getPlatform()); // 'web', 'android', 'ios'
console.log('Is Native:', Capacitor.isNativePlatform()); // true/false
```

### Logs de l'adaptateur

L'adaptateur log toutes les opérations importantes :

```javascript
console.log('[BLE] Requesting device...');
console.log('[BLE] Connecting to GATT...');
console.log('[BLE] Writing value...');
console.log('[BLE] Starting notifications...');
```

### Debug Android

```bash
# Ouvrir Android Studio
npm run open:android

# Puis dans Android Studio:
# View > Tool Windows > Logcat
# Filtrer par "chromium" ou "BLE"
```

### Debug iOS

```bash
# Ouvrir Xcode
npm run open:ios

# Puis dans Xcode:
# View > Debug Area > Activate Console
# Lancer l'app et observer les logs
```

### Debug Web (Chrome DevTools)

Pour tester l'application web avec Web Bluetooth :

1. Ouvrir Chrome ou Edge
2. F12 pour ouvrir DevTools
3. Aller dans l'onglet Console
4. Ouvrir l'application
5. Les logs BLE s'affichent

**Note** : Web Bluetooth nécessite HTTPS (sauf sur localhost)

## ⚠️ Limitations connues

### 1. Sélection d'appareil (Natif)

**Problème** : Actuellement, l'adaptateur sélectionne automatiquement le premier appareil trouvé.

**Solution temporaire** : Modifier le timeout de scan si nécessaire.

**TODO** : Implémenter une UI de sélection d'appareil :

```javascript
// Amélioration future
async requestDevice(options) {
  // Scanner plusieurs appareils
  const devices = await this.scanDevices(options);

  // Afficher une liste native de sélection
  const selectedDevice = await this.showDeviceSelectionUI(devices);

  return new CapacitorBluetoothDevice(selectedDevice.id, selectedDevice.name);
}
```

### 2. Encodage des données

**Problème** : La conversion Uint8Array ↔ Base64 ajoute un léger overhead.

**Impact** : Négligeable pour la plupart des cas d'usage (< 1ms par opération).

### 3. Gestion des erreurs

**Problème** : Les erreurs Capacitor BLE ont un format différent de Web Bluetooth.

**Solution** : L'adaptateur normalise les erreurs pour qu'elles soient cohérentes.

### 4. Événements de déconnexion

**TODO** : Implémenter la gestion des événements de déconnexion :

```javascript
// Web Bluetooth
device.addEventListener('gattserverdisconnected', () => {
  console.log('Disconnected');
});

// Capacitor BLE (à implémenter dans l'adaptateur)
BluetoothLe.addListener('onDisconnect', (data) => {
  // Trigger 'gattserverdisconnected' sur le device simulé
});
```

## 🔧 Maintenance et évolution

### Ajouter un nouveau type de requête BLE

1. Ajouter la méthode dans `CapacitorBluetoothRemoteGATTCharacteristic`
2. Implémenter la conversion des données si nécessaire
3. Appeler l'API Capacitor BLE correspondante
4. Tester sur web ET mobile

Exemple :

```javascript
async writeValueWithoutResponse(value) {
  try {
    const base64Value = this._arrayBufferToBase64(value);
    await BluetoothLe.writeWithoutResponse({
      deviceId: this.deviceId,
      service: this.service.uuid,
      characteristic: this.uuid,
      value: base64Value
    });
  } catch (error) {
    console.error('Write without response error:', error);
    throw error;
  }
}
```

### Mettre à jour les versions de Capacitor

```bash
cd mobile.app
npm update @capacitor/core @capacitor/cli
npm update @capacitor-community/bluetooth-le
npm run sync
```

## 📚 Ressources

- [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)
- [Capacitor Bluetooth LE](https://github.com/capacitor-community/bluetooth-le)
- [Capacitor Core APIs](https://capacitorjs.com/docs/apis)
- [Bluetooth Core Spec](https://www.bluetooth.com/specifications/bluetooth-core-specification/)
