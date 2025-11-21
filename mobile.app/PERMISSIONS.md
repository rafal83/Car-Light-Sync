# Configuration des permissions Bluetooth

## 🤖 Android

### Permissions requises

Les permissions suivantes seront automatiquement ajoutées par le plugin Capacitor Bluetooth LE dans `android/app/src/main/AndroidManifest.xml` :

```xml
<!-- Bluetooth Classic (pour compatibilité) -->
<uses-permission android:name="android.permission.BLUETOOTH" android:maxSdkVersion="30" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" android:maxSdkVersion="30" />

<!-- Bluetooth LE (Android 12+) -->
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />

<!-- Localisation (requis pour BLE sur Android < 12) -->
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" android:maxSdkVersion="30" />
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" android:maxSdkVersion="30" />

<!-- Déclaration de fonctionnalité -->
<uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
```

### Vérification manuelle

Après avoir exécuté `npm run sync:android`, vérifiez le fichier :

```
android/app/src/main/AndroidManifest.xml
```

Si les permissions ne sont pas présentes, ajoutez-les manuellement dans la section `<manifest>`.

### Demande de permissions au runtime

Le plugin Capacitor Bluetooth LE gère automatiquement la demande de permissions au runtime pour Android 6.0+ (API 23+).

### Notes importantes

- **Android 12+ (API 31+)** : Requiert `BLUETOOTH_SCAN` et `BLUETOOTH_CONNECT`
- **Android < 12** : Requiert `ACCESS_FINE_LOCATION` pour scanner les appareils BLE
- **Localisation** : L'utilisateur doit activer la localisation sur Android < 12

---

## 🍎 iOS

### Permissions requises

Les permissions suivantes doivent être ajoutées dans `ios/App/Info.plist` :

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app uses Bluetooth to connect to your Tesla Strip LED controller and control lighting effects.</string>

<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app uses Bluetooth to connect to your Tesla Strip LED controller.</string>
```

### Configuration automatique

Le fichier `capacitor.config.json` contient déjà la configuration du plugin qui ajoutera automatiquement ces permissions lors de la première synchronisation.

### Vérification manuelle

Après avoir exécuté `npm run sync:ios`, ouvrez le projet dans Xcode :

```bash
npm run open:ios
```

Puis vérifiez dans **Info.plist** que les clés `NSBluetooth*` sont présentes.

### Personnalisation des messages

Vous pouvez modifier les messages affichés à l'utilisateur en éditant directement `Info.plist` dans Xcode ou en modifiant le fichier `ios/App/App/Info.plist`.

Exemple en français :

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>Cette application utilise le Bluetooth pour se connecter à votre contrôleur Tesla Strip et piloter les effets lumineux.</string>

<key>NSBluetoothPeripheralUsageDescription</key>
<string>Cette application utilise le Bluetooth pour se connecter à votre contrôleur Tesla Strip.</string>
```

### Notes importantes

- **iOS 13+** : Requiert `NSBluetoothAlwaysUsageDescription`
- **iOS < 13** : Requiert `NSBluetoothPeripheralUsageDescription`
- Les deux clés doivent être présentes pour une compatibilité maximale

---

## 🧪 Test des permissions

### Android

1. Installez l'application sur un appareil ou émulateur
2. Ouvrez l'application
3. Cliquez sur le bouton Bluetooth
4. Une popup de permission devrait apparaître
5. Acceptez les permissions

Si les permissions ne sont pas demandées :
- Allez dans **Paramètres** > **Applications** > **Tesla Strip**
- Vérifiez les permissions accordées
- Accordez manuellement si nécessaire

### iOS

1. Installez l'application sur un appareil ou simulateur
2. Ouvrez l'application
3. Cliquez sur le bouton Bluetooth
4. Une popup de permission devrait apparaître
5. Acceptez la permission Bluetooth

Si la permission n'est pas demandée :
- Allez dans **Réglages** > **Tesla Strip**
- Vérifiez que le Bluetooth est autorisé

---

## 🔧 Dépannage

### Android : "Bluetooth scan failed"

**Cause** : Permissions manquantes ou localisation désactivée

**Solution** :
1. Vérifiez que les permissions sont dans `AndroidManifest.xml`
2. Activez la localisation sur l'appareil (requis pour Android < 12)
3. Accordez manuellement les permissions dans les paramètres

### Android : "Location permission denied"

**Cause** : L'utilisateur a refusé la permission de localisation (Android < 12)

**Solution** :
1. Expliquez à l'utilisateur que la localisation est requise pour le BLE sur Android < 12
2. Guidez-le vers **Paramètres** > **Applications** > **Tesla Strip** > **Permissions**
3. Accordez la permission de localisation

### iOS : "Bluetooth is unavailable"

**Cause** : Bluetooth désactivé ou permission refusée

**Solution** :
1. Vérifiez que le Bluetooth est activé dans **Réglages** > **Bluetooth**
2. Vérifiez les permissions dans **Réglages** > **Tesla Strip**
3. Réinstallez l'application si nécessaire

### iOS : Permission popup ne s'affiche pas

**Cause** : Descriptions manquantes dans `Info.plist`

**Solution** :
1. Ouvrez Xcode : `npm run open:ios`
2. Vérifiez `Info.plist`
3. Ajoutez manuellement les clés `NSBluetooth*` si absentes
4. Nettoyez et rebuild : **Product** > **Clean Build Folder** puis **Product** > **Run**

---

## 📋 Checklist de configuration

### Avant de tester sur Android

- [ ] `npm install` exécuté
- [ ] `npm run sync:android` exécuté
- [ ] `AndroidManifest.xml` contient les permissions BLE
- [ ] Bluetooth activé sur l'appareil
- [ ] Localisation activée (Android < 12)

### Avant de tester sur iOS

- [ ] `npm install` exécuté
- [ ] `npm run sync:ios` exécuté
- [ ] `Info.plist` contient `NSBluetoothAlwaysUsageDescription`
- [ ] `Info.plist` contient `NSBluetoothPeripheralUsageDescription`
- [ ] Bluetooth activé sur l'appareil

---

## 🔗 Ressources

- [Android Bluetooth Permissions](https://developer.android.com/guide/topics/connectivity/bluetooth/permissions)
- [iOS Bluetooth Permissions](https://developer.apple.com/documentation/bundleresources/information_property_list/nsbluetoothalwaysusagedescription)
- [Capacitor Bluetooth LE Plugin](https://github.com/capacitor-community/bluetooth-le)
