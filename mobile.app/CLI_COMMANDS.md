# Référence des commandes CLI

Guide de référence rapide pour compiler et déployer l'application mobile en ligne de commande.

## 📦 Installation et initialisation

```bash
# Installation des dépendances
npm install

# Initialisation complète du projet (plateformes + sync)
npm run init
```

## 🔄 Synchronisation

```bash
# Synchroniser HTML + toutes les plateformes
npm run sync

# Synchroniser Android uniquement
npm run sync:android

# Synchroniser iOS uniquement
npm run sync:ios
```

## 🤖 Android - Build

### APK Debug (développement)

```bash
# Méthode 1 : Via npm (recommandé)
npm run build:apk

# Méthode 2 : Manuelle
npm run sync:android
cd android
gradlew assembleDebug
```

**Sortie** : `android/app/build/outputs/apk/debug/app-debug.apk`

### APK Release (production)

```bash
# Via npm
npm run build:apk:release

# Manuelle
npm run sync:android
cd android
gradlew assembleRelease
```

**Sortie** : `android/app/build/outputs/apk/release/app-release.apk`

**Note** : Nécessite une clé de signature (voir section Signature ci-dessous)

### AAB (Google Play)

```bash
# Via npm
npm run build:aab

# Manuelle
npm run sync:android
cd android
gradlew bundleRelease
```

**Sortie** : `android/app/build/outputs/bundle/release/app-release.aab`

**Note** : Format requis pour publier sur Google Play Store

## 🤖 Android - Installation et exécution

### Installer sur appareil connecté

```bash
# Via npm (recommandé)
npm run install:android

# Ou alias court
npm run run:android

# Manuelle
npm run sync:android
cd android
gradlew installDebug
```

**Prérequis** :
- Appareil Android connecté en USB
- Mode développeur activé
- Débogage USB activé

### Lancer directement

```bash
# Installer + lancer
npm run run:android
```

## 🍎 iOS - Build et exécution

### Lancer sur simulateur

```bash
# Via npm (recommandé)
npm run run:ios

# Manuelle
npm run sync:ios
npx cap run ios
```

### Build en ligne de commande

```bash
# Sync d'abord
npm run sync:ios

# Build
cd ios/App
xcodebuild -workspace App.xcworkspace -scheme App -configuration Debug
```

### Build pour App Store

```bash
npm run build:ios
# Puis ouvrir dans Xcode pour archiver
npm run open:ios
# Dans Xcode: Product > Archive
```

## 🧹 Nettoyage

```bash
# Nettoyer Android
npm run clean:android

# Nettoyer iOS
npm run clean:ios

# Nettoyer tout
npm run clean

# Nettoyage complet (suppression totale)
rm -rf node_modules android ios www
npm install
npm run init
```

## 🛠️ Ouvrir les IDE

```bash
# Android Studio
npm run open:android

# Xcode
npm run open:ios
```

## 📋 Tableau récapitulatif des commandes npm

| Commande | Description |
|----------|-------------|
| `npm run init` | Initialisation complète du projet |
| `npm run sync` | Synchroniser HTML + toutes plateformes |
| `npm run sync:android` | Synchroniser Android uniquement |
| `npm run sync:ios` | Synchroniser iOS uniquement |
| **Android - Build** | |
| `npm run build:apk` | Build APK debug |
| `npm run build:apk:release` | Build APK release signé |
| `npm run build:aab` | Build AAB pour Google Play |
| **Android - Run** | |
| `npm run install:android` | Build + installer sur appareil |
| `npm run run:android` | Alias de install:android |
| **iOS - Run** | |
| `npm run run:ios` | Build + lancer sur simulateur |
| **Nettoyage** | |
| `npm run clean:android` | Nettoyer build Android |
| `npm run clean:ios` | Nettoyer build iOS |
| `npm run clean` | Nettoyer tout |
| **IDE** | |
| `npm run open:android` | Ouvrir Android Studio |
| `npm run open:ios` | Ouvrir Xcode |

## 🔐 Signature Android (pour release)

### Générer une clé de signature

```bash
# Créer le keystore
keytool -genkey -v -keystore tesla-strip-release.keystore -alias tesla-strip -keyalg RSA -keysize 2048 -validity 10000

# Le placer dans android/app/
mv tesla-strip-release.keystore android/app/
```

### Configurer Gradle

Créer `android/gradle.properties` :

```properties
TESLA_STRIP_RELEASE_STORE_FILE=tesla-strip-release.keystore
TESLA_STRIP_RELEASE_KEY_ALIAS=tesla-strip
TESLA_STRIP_RELEASE_STORE_PASSWORD=votre_mot_de_passe
TESLA_STRIP_RELEASE_KEY_PASSWORD=votre_mot_de_passe
```

Modifier `android/app/build.gradle` :

```gradle
android {
    ...
    signingConfigs {
        release {
            storeFile file(TESLA_STRIP_RELEASE_STORE_FILE)
            storePassword TESLA_STRIP_RELEASE_STORE_PASSWORD
            keyAlias TESLA_STRIP_RELEASE_KEY_ALIAS
            keyPassword TESLA_STRIP_RELEASE_KEY_PASSWORD
        }
    }
    buildTypes {
        release {
            signingConfig signingConfigs.release
            ...
        }
    }
}
```

Puis :

```bash
npm run build:apk:release
# Ou
npm run build:aab
```

## 🚀 Workflow de développement rapide

### Développement Android

```bash
# 1. Modifier ../data/index.html
# 2. Rebuild et installer
npm run run:android
```

L'app sera automatiquement réinstallée sur votre appareil.

### Développement iOS

```bash
# 1. Modifier ../data/index.html
# 2. Rebuild et lancer
npm run run:ios
```

## 📱 Commandes Gradle directes (Android)

Si vous êtes dans `mobile.app/android/` :

```bash
# Build debug APK
./gradlew assembleDebug

# Build release APK
./gradlew assembleRelease

# Build AAB release
./gradlew bundleRelease

# Installer debug sur appareil
./gradlew installDebug

# Lister toutes les tâches
./gradlew tasks

# Build et installer en une commande
./gradlew installDebug

# Désinstaller
./gradlew uninstallDebug

# Clean
./gradlew clean
```

## 📱 Commandes xcodebuild (iOS)

Si vous êtes dans `mobile.app/ios/App/` :

```bash
# Build debug
xcodebuild -workspace App.xcworkspace -scheme App -configuration Debug

# Build release
xcodebuild -workspace App.xcworkspace -scheme App -configuration Release

# Clean
xcodebuild clean

# Lister les simulateurs disponibles
xcrun simctl list devices

# Build pour simulateur spécifique
xcodebuild -workspace App.xcworkspace -scheme App -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPhone 15'
```

## 🐛 Debug et logs

### Android Logcat (sans Android Studio)

```bash
# Afficher les logs en temps réel
adb logcat

# Filtrer par tag
adb logcat -s chromium

# Filtrer par niveau (Error seulement)
adb logcat *:E

# Effacer les logs puis afficher
adb logcat -c && adb logcat
```

### iOS Logs (sans Xcode)

```bash
# Logs du simulateur
xcrun simctl spawn booted log stream --predicate 'processImagePath endswith "App"'

# Ou utiliser Console.app
open -a Console
```

## ⚡ Astuces de productivité

### Alias Bash/Zsh

Ajoutez dans votre `~/.bashrc` ou `~/.zshrc` :

```bash
alias ts-sync="npm run sync"
alias ts-android="npm run run:android"
alias ts-ios="npm run run:ios"
alias ts-apk="npm run build:apk"
```

Puis :

```bash
source ~/.bashrc  # ou source ~/.zshrc

# Utilisation
ts-android  # Au lieu de npm run run:android
```

### Watch mode (auto-rebuild)

Pour rebuilder automatiquement à chaque changement :

```bash
# Installer nodemon
npm install -g nodemon

# Surveiller index.html et rebuilder
nodemon --watch ../data/index.html --exec "npm run run:android"
```

## 🔍 Vérification de l'environnement

### Android

```bash
# Vérifier que Java est installé
java -version

# Vérifier que Gradle fonctionne
cd android
./gradlew --version

# Lister les appareils connectés
adb devices

# Vérifier les SDK installés
sdkmanager --list
```

### iOS

```bash
# Vérifier Xcode
xcodebuild -version

# Lister les simulateurs
xcrun simctl list devices

# Vérifier CocoaPods
pod --version
```

## 📦 Outputs des builds

Après un build réussi, les fichiers se trouvent ici :

### Android

```
mobile.app/
└── android/
    └── app/
        └── build/
            └── outputs/
                ├── apk/
                │   ├── debug/
                │   │   └── app-debug.apk           ← APK debug
                │   └── release/
                │       └── app-release.apk         ← APK release
                └── bundle/
                    └── release/
                        └── app-release.aab         ← AAB pour Play Store
```

### iOS

```
mobile.app/
└── ios/
    └── App/
        └── build/
            └── Build/
                └── Products/
                    ├── Debug-iphonesimulator/
                    │   └── App.app                 ← Build debug
                    └── Release-iphoneos/
                        └── App.app                 ← Build release
```

## 🎯 Commandes essentielles (mémo rapide)

```bash
# Développement rapide Android
npm run run:android

# Développement rapide iOS
npm run run:ios

# Build APK pour tester
npm run build:apk

# Build AAB pour publier
npm run build:aab

# Nettoyer et recommencer
npm run clean && npm run init

# Voir les logs Android
adb logcat -s chromium
```

---

**Astuce** : Gardez ce fichier ouvert dans un onglet pour référence rapide ! 📌
