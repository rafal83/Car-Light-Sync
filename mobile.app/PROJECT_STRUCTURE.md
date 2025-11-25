# Structure du projet Mobile

## 📁 Arborescence

```
mobile.app/
│
├── www/                                  # Dossier web (généré automatiquement)
│   ├── index.html                        # Copié depuis ../data/index.html + injection scripts
│   ├── script.js                         # Copié depuis ../data/script.js
│   ├── style.css                         # Copié depuis ../data/style.css
│   ├── carlightsync.png                  # Copié depuis ../data/carlightsync.png
│   ├── capacitor.js                      # Initialisation Capacitor
│   └── capacitor-bluetooth-adapter.js    # Adaptateur BLE Web ↔ Natif
│
├── android/                              # Projet Android natif (généré par Capacitor)
│   ├── app/
│   │   ├── src/main/
│   │   │   ├── AndroidManifest.xml       # Permissions Bluetooth
│   │   │   ├── res/                      # Ressources (icônes, etc.)
│   │   │   └── java/                     # Code Java/Kotlin (si nécessaire)
│   │   └── build.gradle
│   ├── gradle/
│   └── build.gradle
│
├── ios/                                  # Projet iOS natif (généré par Capacitor)
│   └── App/
│       ├── App/
│       │   ├── Info.plist                # Permissions Bluetooth
│       │   ├── Assets.xcassets/          # Icônes et assets
│       │   └── AppDelegate.swift
│       └── App.xcodeproj/
│
├── node_modules/                         # Dépendances npm (ignoré par git)
│
├── capacitor.config.json                 # Configuration Capacitor
├── package.json                          # Dépendances et scripts npm
├── package-lock.json                     # Lockfile npm
│
├── sync-html.js                          # Script de synchronisation des fichiers web
├── init.js                               # Script d'initialisation du projet
│
├── README.md                             # Documentation principale
├── QUICKSTART.md                         # Guide de démarrage rapide
├── PERMISSIONS.md                        # Guide des permissions BLE
├── BLUETOOTH_ADAPTER.md                  # Documentation de l'adaptateur BLE
├── PROJECT_STRUCTURE.md                  # Ce fichier
│
└── .gitignore                            # Fichiers ignorés par git
```

## 🔄 Workflow de fichiers

### 1. Fichiers source

```
../data/index.html   (Structure HTML)
../data/script.js    (Logique front)
../data/style.css    (Styles/thème)
```

### 2. Synchronisation

```bash
npm run sync
```

Exécute `sync-html.js` qui :

1. Lit `../data/index.html`
2. Injecte les scripts Capacitor avant `</head>` :
   ```html
   <script type="module" src="capacitor.js"></script>
   <script type="module" src="capacitor-bluetooth-adapter.js"></script>
   ```
3. Écrit dans `www/index.html`
4. Copie `../data/script.js` vers `www/script.js`
5. Copie `../data/style.css` vers `www/style.css`
6. Copie `../data/carlightsync.png` vers `www/carlightsync.png`
7. Lance `cap sync` pour synchroniser avec Android/iOS

### 3. Résultat

```
www/index.html  (Fichier généré avec scripts Capacitor)
www/script.js   (Logique front copiée)
www/style.css   (Styles copiés)
```

## 📦 Dépendances

### Production (`dependencies`)

```json
{
  "@capacitor/android": "^6.0.0",      // Plateforme Android
  "@capacitor/core": "^6.0.0",         // Core Capacitor
  "@capacitor/ios": "^6.0.0",          // Plateforme iOS
  "@capacitor-community/bluetooth-le": "^6.0.1"  // Plugin Bluetooth LE
}
```

### Développement (`devDependencies`)

```json
{
  "@capacitor/cli": "^6.0.0"           // CLI Capacitor
}
```

## 🛠️ Scripts npm

| Script | Commande | Description |
|--------|----------|-------------|
| `init` | `node init.js` | Initialisation complète du projet |
| `sync` | `node sync-html.js && cap sync` | Synchroniser les fichiers web + plateformes |
| `sync:android` | `node sync-html.js && cap sync android` | Synchroniser Android uniquement |
| `sync:ios` | `node sync-html.js && cap sync ios` | Synchroniser iOS uniquement |
| `open:android` | `cap open android` | Ouvrir Android Studio |
| `open:ios` | `cap open ios` | Ouvrir Xcode |
| `build:android` | `npm run sync:android && cap build android` | Build APK Android |
| `build:ios` | `npm run sync:ios && cap build ios` | Build IPA iOS |

## 📄 Fichiers clés

### `capacitor.config.json`

Configuration principale de Capacitor :

```json
{
  "appId": "com.CarLightSync.controller",     // ID unique de l'app
  "appName": "Car Light Sync",                 // Nom de l'app
  "webDir": "www",                          // Dossier web source
  "bundledWebRuntime": false,               // Pas de runtime embarqué
  "plugins": {
    "BluetoothLe": {                        // Config plugin BLE
      "displayStrings": { ... }
    }
  }
}
```

### `sync-html.js`

Script Node.js qui :
- Copie `../data/index.html` vers `www/index.html`
- Injecte les scripts Capacitor
- Copie `../data/script.js` et `../data/style.css`
- Copie le logo (`carlightsync.png`)

**Pourquoi ?**
- Maintenir une seule base web partagée
- Adaptation automatique pour mobile
- Pas de modification manuelle nécessaire

### `www/capacitor-bluetooth-adapter.js`

Adaptateur qui :
- Détecte la plateforme (web/mobile)
- Sur mobile : remplace `navigator.bluetooth` par une implémentation Capacitor BLE
- Sur web : laisse l'API Web Bluetooth native
- Convertit les données (Uint8Array ↔ Base64)
- Simule les classes Web Bluetooth (BluetoothDevice, GATT, etc.)

**Architecture** :
```
Application (index.html)
    ↓ utilise
navigator.bluetooth
    ↓ (si mobile)
CapacitorBluetoothNavigator
    ↓ appelle
Capacitor Bluetooth LE Plugin
    ↓ utilise
Bluetooth natif (Android/iOS)
```

### `.gitignore`

Fichiers exclus du versioning :
- `node_modules/` : Dépendances npm
- `android/` : Projet Android généré
- `ios/` : Projet iOS généré
- `www/` : Fichiers web générés

**Pourquoi ?**
- Ces dossiers sont régénérés automatiquement
- Réduit la taille du repo
- Évite les conflits de merge

## 🔍 Fichiers générés automatiquement

### Lors de `npm install`

- `node_modules/` : Toutes les dépendances
- `package-lock.json` : Lockfile des versions exactes

### Lors de `npm run sync`

- `www/index.html` : HTML avec scripts Capacitor
- `www/script.js` : Logique copiée
- `www/style.css` : Styles copiés
- `www/carlightsync.png` : Logo PNG copié
- `android/` : Mis à jour avec le nouveau HTML
- `ios/` : Mis à jour avec le nouveau HTML

### Lors de `npx cap add android`

- `android/` : Projet Android Studio complet
  - `app/src/main/AndroidManifest.xml`
  - `app/src/main/res/`
  - Fichiers Gradle

### Lors de `npx cap add ios`

- `ios/` : Projet Xcode complet
  - `App/App/Info.plist`
  - `App/App/Assets.xcassets/`
  - Fichiers Xcode

## 📱 Plateformes

### Android

**Structure** :
```
android/
├── app/
│   ├── src/main/
│   │   ├── AndroidManifest.xml       ← Permissions
│   │   ├── res/
│   │   │   ├── mipmap-*/             ← Icônes app
│   │   │   └── values/               ← Strings, couleurs
│   │   └── java/com/CarLightSync/controller/
│   │       └── MainActivity.java
│   └── build.gradle                  ← Config build app
├── gradle/
└── build.gradle                      ← Config build projet
```

**Outils** :
- Android Studio
- Gradle
- SDK Android 21+

### iOS

**Structure** :
```
ios/
└── App/
    ├── App/
    │   ├── Info.plist                ← Permissions
    │   ├── Assets.xcassets/
    │   │   └── AppIcon.appiconset/   ← Icônes app
    │   ├── AppDelegate.swift
    │   └── capacitor.config.json     ← Lien vers config
    └── App.xcodeproj/
```

**Outils** :
- Xcode
- CocoaPods
- iOS 13+

## 🚀 Déploiement

### Android (APK/AAB)

```bash
# 1. Build
npm run build:android

# 2. Dans Android Studio
Build > Generate Signed Bundle/APK

# 3. Upload sur Google Play Console
```

### iOS (IPA)

```bash
# 1. Build
npm run build:ios

# 2. Dans Xcode
Product > Archive

# 3. Upload sur App Store Connect
```

## 🔗 Liens entre fichiers

```
../data/index.html
../data/script.js
../data/style.css
    ↓ copiés par sync-html.js
www/index.html
www/script.js
www/style.css
    ↓ référencés par capacitor.config.json
android/app/src/main/assets/public/index.html
ios/App/App/public/index.html
    ↓ chargés par WebView Capacitor
Application mobile
```

## 💡 Bonnes pratiques

1. **Ne jamais éditer directement `www/index.html`**
   - Toujours éditer les fichiers dans `../data/` (index/script/style)
   - Lancer `npm run sync` pour propager les changements

2. **Ne jamais commit les dossiers générés**
   - `node_modules/`
   - `android/`
   - `ios/`
   - `www/`

3. **Toujours synchroniser après modification**
   ```bash
   # Modifier ../data/index.html / ../data/script.js / ../data/style.css
   npm run sync
   # Tester sur mobile
   ```

4. **Tester sur les deux plateformes**
   - Android et iOS peuvent avoir des comportements différents
   - Tester régulièrement sur les deux

5. **Versionner les lockfiles**
   - Commit `package-lock.json`
   - Assure la reproductibilité des builds

## 🧪 Tests

### Test local (navigateur)

```bash
# Servir le dossier www/ avec un serveur HTTP local
cd www
npx http-server -p 8080
# Ouvrir http://localhost:8080
```

**Note** : Web Bluetooth nécessite HTTPS (sauf localhost)

### Test mobile (émulateur)

```bash
# Android
npm run sync:android
npm run open:android
# Puis Run dans Android Studio

# iOS
npm run sync:ios
npm run open:ios
# Puis Run dans Xcode
```

### Test mobile (appareil réel)

**Android** :
1. Activer le mode développeur
2. Activer le débogage USB
3. Connecter via USB
4. Autoriser le débogage
5. Run depuis Android Studio

**iOS** :
1. Ajouter l'appareil dans Xcode
2. Signer avec un certificat
3. Trust le certificat sur l'appareil
4. Run depuis Xcode

## 📚 Documentation

| Fichier | Description |
|---------|-------------|
| `README.md` | Vue d'ensemble et architecture |
| `QUICKSTART.md` | Guide de démarrage rapide |
| `PERMISSIONS.md` | Configuration des permissions BLE |
| `BLUETOOTH_ADAPTER.md` | Détails de l'adaptateur BLE |
| `PROJECT_STRUCTURE.md` | Structure du projet (ce fichier) |

## 🎯 Points d'entrée

### Développeur web
→ Lire `QUICKSTART.md`
→ Lancer `npm run init`
→ Tester sur émulateur

### Développeur mobile
→ Lire `PERMISSIONS.md`
→ Lire `BLUETOOTH_ADAPTER.md`
→ Contribuer aux adaptateurs natifs

### Utilisateur final
→ Installer l'APK/IPA
→ Accorder les permissions Bluetooth
→ Connecter au Car Light Sync
