# Changelog

Toutes les modifications notables de ce projet seront documentées dans ce fichier.

Le format est basé sur [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
et ce projet adhère au [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.3.0] - 2025-11-27

### 🎯 Optimisation JSON - Réduction de 30-40% de la taille des API

Cette version apporte une optimisation majeure de l'API REST en utilisant des **clés JSON courtes** pour réduire significativement la taille des payloads et améliorer les performances sur ESP32.

### Added
- Système complet de clés JSON courtes pour toutes les API REST
- Script Python `tools/replace_json_keys.py` pour automatiser les conversions
- Documentation complète : `docs/JSON_API_REFERENCE.md` avec tous les mappings
- Support de 80+ mappings de clés pour optimiser les réponses

### Changed
- **API REST** : Toutes les clés JSON utilisent maintenant des noms courts (ex: `wifi_connected` → `wc`)
- **web_server.c** : Mise à jour de tous les endpoints pour utiliser les clés courtes
- **script.js** : Adaptation du client web pour les nouvelles clés
- **Compression** : Réduction de ~30-40% de la taille des JSON
- **README.md** : Ajout d'une section dédiée à l'optimisation JSON

### Performance
- Réduction de 30-40% de la taille des réponses JSON
- Amélioration de la vitesse de parsing JSON sur ESP32
- Économie de RAM lors du traitement des requêtes
- Réduction de la bande passante réseau

### Documentation
- Nouvelle section "Optimisation JSON" dans le README
- Document de référence complet : `JSON_API_REFERENCE.md`
- Exemples de conversion Python et JavaScript
- Tableau de mapping complet des 80+ clés

### Breaking Changes
⚠️ **ATTENTION** : Cette version introduit des changements incompatibles avec les versions précédentes :
- Les clés JSON longues ne sont plus supportées
- Les clients API externes doivent être mis à jour pour utiliser les clés courtes
- Le client web embarqué est automatiquement compatible (aucune action requise)

### Migration Guide
Pour migrer depuis v2.2.0 :
1. Mettre à jour le firmware ESP32 vers v2.3.0
2. Si vous utilisez l'API REST depuis un client externe, consulter `docs/JSON_API_REFERENCE.md`
3. Utiliser les scripts de conversion fournis (Python/JavaScript) si nécessaire
4. Le client web embarqué est automatiquement mis à jour

---

## [2.2.0] - 2025-11-20

### Added
- Mode audio réactif avec micro I2S INMP441
- Détection BPM et synchronisation musicale
- Analyse spectrale (Bass, Mid, Treble)
- Effets audio : VU-mètre et BPM flash
- Support FFT pour analyse fréquentielle avancée

### Changed
- Optimisation du traitement audio (~50Hz)
- Amélioration de la latence audio (<20ms)
- Interface web : Nouveaux contrôles audio

### Performance
- Traitement audio en tâche dédiée
- Optimisation mémoire (~4KB RAM pour audio)
- Compatible BLE avec polling optimisé

---

## [2.1.0] - 2025-11-10

### Added
- Application mobile iOS/Android (Capacitor)
- Support BLE pour configuration mobile
- Connexion automatique au démarrage de l'app
- Guide complet : `mobile.app/README.md`

### Changed
- Optimisation de l'API BLE
- Amélioration de la stabilité WiFi
- Interface web responsive améliorée

---

## [2.0.0] - 2025-11-01

### Added
- Architecture CAN unifiée avec support multi-véhicules
- Système de mapping DBC vers état véhicule
- Configuration CAN par fichiers auto-générés
- Support de 22+ événements CAN
- Documentation technique : `TECHNICAL.md`

### Changed
- Refonte complète du système CAN
- Migration vers architecture modulaire
- Amélioration des performances CAN

### Breaking Changes
- Nouvelle architecture CAN (incompatible avec v1.x)
- Fichiers de configuration CAN générés automatiquement

---

## [1.5.0] - 2025-10-15

### Added
- Système de profils (jusqu'à 10 profils)
- Import/Export de profils JSON
- Mode nuit automatique avec luminosité réduite
- Association événements CAN → Effets LED

### Changed
- Interface web : Nouvelle gestion des profils
- Optimisation du stockage NVS
- Amélioration de la stabilité

---

## [1.4.0] - 2025-10-01

### Added
- Support OTA (Over-The-Air updates)
- Interface web pour upload firmware
- Indicateur de progression OTA
- Auto-reboot après mise à jour

### Changed
- Amélioration de la sécurité OTA
- Optimisation de la mémoire HTTP
- Interface web : Onglet OTA

---

## [1.3.0] - 2025-09-15

### Added
- 21 effets LED intégrés
- Effets véhicule : Turn Signal, Brake Light, Charge Status
- Effet Blindspot Flash avec priorité maximale
- Système de priorité pour effets simultanés

### Changed
- Optimisation du rendu LED (50 FPS)
- Amélioration des animations
- Latence CAN réduite (<100ms)

---

## [1.2.0] - 2025-09-01

### Added
- Interface web moderne et responsive
- Contrôle en temps réel des effets
- Affichage de l'état du véhicule
- Configuration matérielle LED via interface

### Changed
- Migration vers interface web complète
- Compression des fichiers HTML/JS/CSS
- Optimisation du serveur HTTP

---

## [1.1.0] - 2025-08-15

### Added
- Support WS2812/WS2812B
- Connexion CAN directe via TWAI
- Détection événements véhicule
- API REST basique

### Changed
- Amélioration de la stabilité CAN
- Optimisation mémoire

---

## [1.0.0] - 2025-08-01

### Added
- Version initiale
- Support ESP32-S3
- Effets LED de base
- WiFi AP mode
- Configuration via serial

---

## Format du Changelog

### Types de Changements
- **Added** : Nouvelles fonctionnalités
- **Changed** : Modifications de fonctionnalités existantes
- **Deprecated** : Fonctionnalités obsolètes (à supprimer prochainement)
- **Removed** : Fonctionnalités supprimées
- **Fixed** : Corrections de bugs
- **Security** : Corrections de vulnérabilités
- **Performance** : Améliorations de performances

### Semantic Versioning
- **MAJOR** (X.0.0) : Changements incompatibles avec versions précédentes
- **MINOR** (x.X.0) : Nouvelles fonctionnalités compatibles
- **PATCH** (x.x.X) : Corrections de bugs compatibles

---

**Maintenu par** : Raphaël D.
**Dernière mise à jour** : 2025-11-27
