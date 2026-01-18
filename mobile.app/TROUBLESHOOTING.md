# Guide de dépannage

## ❌ Erreur : "Android Gradle plugin requires Java 17"

### Problème

```
Android Gradle plugin requires Java 17 to run. You are currently using Java 11.
```

### Solution : Configurer Gradle pour utiliser Java 17

Créer ou éditer le fichier `mobile.app/android/gradle.properties` et ajouter :

```properties
org.gradle.java.home=C:\\Program Files\\Java\\jdk-17
```

**Remplacez le chemin** par celui de votre installation Java 17.

**Chemins communs** :
- `C:\\Program Files\\Java\\jdk-17`
- `C:\\Program Files\\Amazon Corretto\\jdk17.0.10_7`
- `C:\\Program Files\\Eclipse Adoptium\\jdk-17.0.x.x-hotspot`

**⚠️ Important** : Utilisez des **doubles backslashes** `\\` sur Windows !

Puis relancer :
```bash
npm run build:apk
```

### Alternative : Installer Java 17

Si vous n'avez pas Java 17 :

1. **Télécharger** :
   [Amazon Corretto 17](https://corretto.aws/downloads/latest/amazon-corretto-17-x64-windows-jdk.msi)

2. **Installer** (suivre l'assistant)

3. **Configurer** `android/gradle.properties` avec le nouveau chemin

---

## ❌ Pas de connexion BLE au lancement

### Symptôme

- L'app se lance normalement
- L'interface HTML s'affiche
- **Mais** : pas de scan Bluetooth, pas de connexion automatique
- L'overlay de connexion reste affiché

### Diagnostic via Chrome DevTools

1. **Connecter votre appareil Android**
2. **Ouvrir Chrome** sur PC
3. **Aller à** `chrome://inspect`
4. **Trouver votre appareil** et cliquer sur "inspect"
5. **Aller dans Console**

### Logs attendus

Vous devriez voir :
```
🔵 Using Capacitor Bluetooth LE (Native)
📱 Capacitor native app flag set
📱 Capacitor native app detected: forcing wifiOnline = false
✅ BLE gesture flag created and set to true
🔄 Triggering BLE auto-connect...
```

### Si les logs manquent

#### Problème 1 : Scripts Capacitor ne se chargent pas

**Vérifier** :
```bash
cd mobile.app
cat www/index.html | grep capacitor
```

**Attendu** :
```html
<script type="module" src="capacitor.js"></script>
<script type="module" src="capacitor-bluetooth-adapter.js"></script>
```

**Si manquant** :
```bash
npm run sync
npm run run:android
```

#### Problème 2 : Fichiers JS manquants

**Vérifier** :
```bash
ls www/capacitor.js
ls www/capacitor-bluetooth-adapter.js
```

**Si manquants**, ils ont été créés dans ce projet. Vérifiez qu'ils existent bien.

#### Problème 3 : Erreur de chargement des modules ES6

Les scripts `type="module"` peuvent ne pas se charger correctement sur certaines versions d'Android.

**Solution** : Modifier `www/capacitor.js` et `www/capacitor-bluetooth-adapter.js` pour retirer les imports.

Dans `www/capacitor.js` :
```javascript
// AVANT
import { Capacitor } from '@capacitor/core';

// APRÈS
const Capacitor = window.Capacitor;
```

Dans `www/capacitor-bluetooth-adapter.js` :
```javascript
// AVANT
import { BluetoothLe } from '@capacitor-community/bluetooth-le';
import { Capacitor } from '@capacitor/core';

// APRÈS
const Capacitor = window.Capacitor;
const BluetoothLe = window.CapacitorCustomPlatform?.plugins?.BluetoothLe;
```

#### Problème 4 : Permissions Bluetooth manquantes

**Android** - Vérifier `android/app/src/main/AndroidManifest.xml` :

Doit contenir (avant `<application>`) :
```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```

Si manquant, ajouter manuellement puis :
```bash
npm run sync:android
npm run run:android
```

### Si l'app demande toujours un clic manuel

Cela signifie que le flag `bleAutoConnectGestureCaptured` n'est pas défini.

**Vérifier dans DevTools Console** :
```javascript
console.log(window.bleAutoConnectGestureCaptured);
// Devrait afficher: true
```

**Si `false` ou `undefined`** :

Le script `capacitor-bluetooth-adapter.js` ne s'exécute pas au bon moment.

**Solution** : Forcer manuellement dans DevTools pour tester :
```javascript
window.bleAutoConnectGestureCaptured = true;
window.maybeAutoConnectBle(true);
```

Si cela fonctionne, le problème est le timing de chargement des scripts.

---

## ❌ "Permission denied: BLUETOOTH_SCAN"

### Solution

**Android 12+** :
1. Paramètres > Applications > Car Light Sync > Permissions
2. Accorder **Bluetooth** et **Localisation**
3. Relancer l'app

**Android < 12** :
1. Activer la **Localisation** dans les paramètres Android
2. Accorder la permission dans l'app

---

## ❌ Build Gradle très lent

Ajouter dans `android/gradle.properties` :

```properties
org.gradle.daemon=true
org.gradle.parallel=true
org.gradle.caching=true
org.gradle.jvmargs=-Xmx4096m -XX:MaxPermSize=512m
```

---

## 🔍 Logs Android en temps réel

```bash
adb logcat | grep -E "chromium|Capacitor|BLE"
```

---

## 📞 Besoin d'aide supplémentaire ?

1. Consultez [MOBILE_BEHAVIOR.md](MOBILE_BEHAVIOR.md)
2. Consultez [BLUETOOTH_ADAPTER.md](BLUETOOTH_ADAPTER.md)
3. Lisez [PERMISSIONS.md](PERMISSIONS.md)
