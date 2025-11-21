# Tesla Strip Mobile App

Application mobile Capacitor pour contrôler le Tesla Strip via Bluetooth.

## Architecture

Cette application utilise **le même fichier `index.html`** que celui embarqué dans l'ESP32, sans modification nécessaire. L'adaptation se fait de manière transparente :

- **Sur navigateur web** : utilise Web Bluetooth API standard
- **Sur mobile (iOS/Android)** : utilise Capacitor Bluetooth LE natif via un adaptateur

## ⚡ Connexion automatique sur mobile

**Important** : Contrairement à la version web, l'application mobile **se connecte automatiquement** au démarrage en Bluetooth, sans nécessiter de clic sur le bouton BLE.

L'expérience utilisateur est la suivante :
1. Lancer l'app
2. Attendre 2-5 secondes (scan et connexion automatiques)
3. L'interface est prête à utiliser !

Pour plus de détails, consultez [MOBILE_BEHAVIOR.md](MOBILE_BEHAVIOR.md).

## Prérequis

- Node.js >= 16
- npm ou yarn
- Pour Android : Android Studio
- Pour iOS : Xcode (macOS uniquement)

## Installation

```bash
cd mobile.app
npm install
```

## Synchronisation du fichier HTML

Le fichier `index.html` est automatiquement copié depuis `../data/index.html` lors de la synchronisation :

```bash
npm run sync
```

Cette commande :
1. Copie `../data/index.html` vers `www/index.html`
2. Injecte automatiquement les scripts Capacitor
3. Synchronise avec les plateformes Android/iOS

## Développement

### Développement rapide (ligne de commande)

**Android** :
```bash
npm run run:android  # Build + installer sur appareil
```

**iOS** :
```bash
npm run run:ios  # Build + lancer sur simulateur
```

### Avec IDE

**Android** :
```bash
npm run sync:android
npm run open:android
```

**iOS** :
```bash
npm run sync:ios
npm run open:ios
```

### Build pour production

**Android (ligne de commande)** :
```bash
npm run build:apk          # APK debug
npm run build:apk:release  # APK release signé
npm run build:aab          # AAB pour Google Play
```

**iOS** :
```bash
npm run build:ios
```

**📚 Pour toutes les commandes disponibles, consultez [CLI_COMMANDS.md](CLI_COMMANDS.md)**

## Comment ça marche ?

### 1. Synchronisation automatique

Le script `sync-html.js` :
- Copie le fichier `../data/index.html` original
- Injecte les scripts Capacitor avant `</head>` :
  - `capacitor.js` : initialisation de Capacitor
  - `capacitor-bluetooth-adapter.js` : adaptateur Bluetooth

### 2. Adaptateur Bluetooth transparent

Le fichier `capacitor-bluetooth-adapter.js` détecte automatiquement l'environnement :

```javascript
if (Capacitor.isNativePlatform()) {
  // Sur mobile : remplace navigator.bluetooth par l'API Capacitor
  navigator.bluetooth = new CapacitorBluetoothNavigator();
} else {
  // Sur web : utilise Web Bluetooth API native
}
```

### 3. Code applicatif inchangé

Le code dans `index.html` reste **exactement identique** :

```javascript
// Ce code fonctionne sur web ET mobile sans modification !
const device = await navigator.bluetooth.requestDevice({
  filters: [{ services: [serviceUuid] }]
});
const server = await device.gatt.connect();
const service = await server.getPrimaryService(serviceUuid);
const characteristic = await service.getCharacteristic(characteristicUuid);
await characteristic.writeValue(data);
```

## Permissions

### Android (`AndroidManifest.xml`)

Les permissions Bluetooth sont automatiquement ajoutées par le plugin :

```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```

### iOS (`Info.plist`)

Ajout automatique des descriptions d'usage :

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app uses Bluetooth to connect to your Tesla Strip device</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app uses Bluetooth to connect to your Tesla Strip device</string>
```

## Structure du projet

```
mobile.app/
├── www/                          # Dossier web (auto-généré)
│   ├── index.html                # Copié depuis ../data/index.html
│   ├── icon.svg                  # Copié depuis ../data/icon.svg
│   ├── capacitor.js              # Initialisation Capacitor
│   └── capacitor-bluetooth-adapter.js  # Adaptateur BLE
├── android/                      # Projet Android (généré par Capacitor)
├── ios/                          # Projet iOS (généré par Capacitor)
├── capacitor.config.json         # Configuration Capacitor
├── package.json                  # Dépendances npm
├── sync-html.js                  # Script de synchronisation HTML
└── README.md                     # Ce fichier
```

## Workflow de développement

1. **Modifier le fichier HTML** : Éditez `../data/index.html` (le fichier source)
2. **Synchroniser** : `npm run sync`
3. **Tester sur mobile** : `npm run open:android` ou `npm run open:ios`
4. **Compiler pour l'ESP32** : Le même fichier est utilisé via PlatformIO

## Avantages de cette approche

✅ **Un seul fichier source** : `data/index.html` fonctionne partout
✅ **Pas de duplication** : Pas besoin de maintenir 2 versions
✅ **Adaptation transparente** : Le code ne sait pas s'il tourne sur web ou mobile
✅ **API identique** : `navigator.bluetooth` fonctionne partout
✅ **Bluetooth natif** : Performance optimale sur mobile

## Dépannage

### Le Bluetooth ne fonctionne pas sur Android

- Vérifiez les permissions dans Android Studio
- Activez la localisation (nécessaire pour BLE sur Android)
- Vérifiez que le Bluetooth est activé

### Le Bluetooth ne fonctionne pas sur iOS

- Vérifiez les descriptions d'usage dans `Info.plist`
- Le Bluetooth doit être activé dans les réglages

### Le fichier HTML n'est pas à jour

```bash
npm run sync
```

## Notes techniques

### UUID Bluetooth

Les UUID de service et caractéristique sont définis dans `index.html` :

```javascript
const BLE_CONFIG = {
    serviceUuid: 'UUID_HERE',
    commandCharacteristicUuid: 'UUID_HERE',
    responseCharacteristicUuid: 'UUID_HERE'
};
```

### Encodage des données

L'adaptateur gère automatiquement la conversion :
- Web Bluetooth : `Uint8Array` / `DataView`
- Capacitor BLE : Base64

## Ressources

- [Capacitor Documentation](https://capacitorjs.com/)
- [Capacitor Bluetooth LE Plugin](https://github.com/capacitor-community/bluetooth-le)
- [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)
