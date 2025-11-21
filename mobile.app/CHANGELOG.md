# Changelog - Tesla Strip Mobile App

## [1.0.0] - 2025-01-XX

### 🎉 Version initiale

#### Fonctionnalités

- ✅ Application mobile native iOS et Android
- ✅ Bluetooth LE natif via Capacitor
- ✅ Réutilisation du même `index.html` que l'ESP32
- ✅ Adaptateur transparent Web Bluetooth ↔ Capacitor BLE
- ✅ Support multi-langue (FR/EN)
- ✅ Interface identique à la version web
- ✅ Contrôle complet des LED via Bluetooth
- ✅ Configuration des profils
- ✅ Configuration des événements CAN
- ✅ Simulation d'événements
- ✅ Thème clair/sombre

#### Architecture

- Capacitor 6.0
- Capacitor Bluetooth LE 6.0.1
- Support Android 5.0+ (API 21+)
- Support iOS 13+

#### Documentation

- README.md : Documentation complète
- QUICKSTART.md : Guide de démarrage rapide
- PERMISSIONS.md : Configuration des permissions
- BLUETOOTH_ADAPTER.md : Détails techniques de l'adaptateur
- PROJECT_STRUCTURE.md : Structure du projet

#### Scripts

- `npm run init` : Initialisation complète
- `npm run sync` : Synchronisation HTML + plateformes
- `npm run open:android` : Ouvrir dans Android Studio
- `npm run open:ios` : Ouvrir dans Xcode
- `npm run build:android` : Build APK/AAB
- `npm run build:ios` : Build IPA

#### Fichiers clés

- `sync-html.js` : Synchronisation automatique du HTML
- `capacitor-bluetooth-adapter.js` : Adaptateur BLE transparent
- `capacitor.config.json` : Configuration Capacitor

### 🔄 Workflow

1. Modifier `../data/index.html` (source unique)
2. Lancer `npm run sync`
3. Tester sur Android/iOS
4. Le même HTML fonctionne partout !

### 📱 Plateformes supportées

| Plateforme | Status | Version minimale |
|------------|--------|------------------|
| Android | ✅ Supporté | 5.0 (API 21) |
| iOS | ✅ Supporté | 13.0 |
| Web | ✅ Supporté (fallback) | Navigateurs modernes avec Web Bluetooth |

### 🐛 Limitations connues

- Sélection d'appareil BLE : Actuellement sélectionne le premier appareil trouvé (TODO: UI de sélection)
- Événements de déconnexion : Pas encore propagés au niveau UI (TODO)
- Notifications push : Non implémentées

### 🔮 Améliorations futures

- [ ] UI native de sélection d'appareils Bluetooth
- [ ] Gestion améliorée des événements de déconnexion
- [ ] Mode hors ligne avec cache
- [ ] Notifications push pour événements importants
- [ ] Widget home screen (Android)
- [ ] Support Apple Watch (iOS)
- [ ] Intégration Siri Shortcuts (iOS)
- [ ] Intégration Google Assistant (Android)
- [ ] Thèmes personnalisables
- [ ] Export/import de configuration via fichiers
- [ ] Historique des connexions

### 📦 Dépendances

```json
{
  "@capacitor/android": "^6.0.0",
  "@capacitor/core": "^6.0.0",
  "@capacitor/ios": "^6.0.0",
  "@capacitor-community/bluetooth-le": "^6.0.1"
}
```

### 🙏 Remerciements

- Capacitor team pour le framework
- Capacitor Community pour le plugin Bluetooth LE
- Contributors du projet Tesla Strip

---

## Format du changelog

Ce changelog suit le format [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/),
et ce projet adhère au [Semantic Versioning](https://semver.org/lang/fr/).

### Types de changements

- `Added` : Nouvelles fonctionnalités
- `Changed` : Modifications de fonctionnalités existantes
- `Deprecated` : Fonctionnalités dépréciées
- `Removed` : Fonctionnalités supprimées
- `Fixed` : Corrections de bugs
- `Security` : Corrections de failles de sécurité
