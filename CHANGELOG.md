# Changelog

Toutes les modifications notables de ce projet seront documentées dans ce fichier.

Le format est basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/),
et ce projet adhère au [Semantic Versioning](https://semver.org/lang/fr/).

## [Non publié]

### À venir
- Application mobile companion
- Mode musique avec micro I2S
- Intégration HomeAssistant/MQTT
- Effets personnalisables via script

## [2.1.0] - 2024-11-15

### Ajouté
- **🆕 Système d'ID alphanumériques** : Les événements et effets utilisent maintenant des ID strings (ex: "TURN_LEFT", "KNIGHT_RIDER")
- **🆕 API `/api/effects`** : Liste tous les effets disponibles avec leurs IDs et noms
- **🆕 API `/api/event-types`** : Liste tous les types d'événements CAN
- **🆕 API `/api/events` GET/POST** : Gestion complète de la configuration des événements
- **🆕 Import/Export de profils** : Export et import de profils en JSON via l'interface web
- **🆕 OTA Updates** : Mise à jour firmware over-the-air via interface web
- Interface web multilingue (Français/Anglais) avec bouton de changement de langue
- Tableau de configuration des événements dans l'interface web
- Simulation d'événements CAN pour tests sans véhicule

### Modifié
- **API `/api/events` POST** : Accepte maintenant des ID strings au lieu d'IDs numériques
- Frontend utilise des ID strings pour les événements et effets
- Configuration `max_uri_handlers` augmentée à 30 pour supporter toutes les routes
- Documentation complète mise à jour avec ID strings
- Interface web traduite en anglais par défaut avec support français

### Corrigé
- **Fix critique** : Route POST `/api/events` maintenant correctement enregistrée (erreur 405 résolue)
- **Fix** : Conversion correcte entre ID strings et enums numériques pour l'effet par défaut des profils
- **Fix** : Listes d'effets et événements hardcodées supprimées du frontend (dépend maintenant 100% de l'API)
- **Fix** : Ordre d'enregistrement des routes optimisé (POST avant GET)

### Technique
- Fonctions helper `effectEnumToId()` et `effectIdToEnum()` dans le frontend
- Mapping bidirectionnel string ↔ enum pour effets et événements
- Validation stricte des types dans l'API (strings uniquement)
- Suppression de 43 lignes de code hardcodé dans le frontend

## [2.0.0] - 2024-XX-XX

### Ajouté - Système de Profils et Événements CAN
- **🆕 Système de profils multiples** : Jusqu'à 10 profils de configuration sauvegardables
- **🆕 Association événements CAN → Effets** : Chaque événement peut déclencher un effet spécifique
- **🆕 Mode nuit automatique** : Basé sur le message CAN 0x3C8 (capteur de luminosité)
- **🆕 Détection angle mort** : Support du message CAN 0x2A5 avec alertes visuelles
- **🆕 Système de priorité** : Gestion intelligente des effets simultanés
- **🆕 Effets temporaires** : Durée configurable pour chaque effet (retour auto à défaut)
- Gestion de profils via interface web et API REST
- 17 types d'événements CAN détectables
- Configuration par profil : effet défaut, effet nuit, luminosité, vitesse
- Sauvegarde automatique des profils en NVS
- Interface web améliorée avec gestion des profils
- API REST étendue pour profils et événements

### Modifié
- **Configuration Commander** : SSID fixe `S3XY_OBD`, IP `192.168.4.1`
- Structure `vehicle_state_t` étendue avec blindspot et night_mode
- Tâche dédiée pour traitement des événements CAN
- Interface web redesignée avec sections profils et événements
- Amélioration de la réactivité (détection événements à 100ms)

### Technique
- Nouveau module `config_manager` pour gestion profils
- Décodeurs CAN pour blindspot et mode nuit
- Callback système pour mise à jour état véhicule
- Routes API supplémentaires (profils, événements)
- Documentation étendue (FEATURES.md)

## [1.0.0] - 2024-XX-XX

### Ajouté
- Support initial ESP32 avec ESP-IDF
- 16 effets LED différents (Rainbow, Breathing, Fire, etc.)
- Interface web responsive avec contrôle temps réel
- Support protocole Panda pour Commander
- Décodage des messages CAN Tesla Model 3 (2021)
  - État des portes et verrouillage
  - Vitesse et position du sélecteur
  - État de charge
  - Clignotants et lumières
  - Freins
  - Tension batterie 12V
- Point d'accès WiFi pour configuration
- Client WiFi pour connexion au Commander
- Sauvegarde de la configuration en NVS
- Mode synchronisation avec état du véhicule
- API REST pour contrôle externe
- Documentation complète (README, WIRING, ADVANCED)
- Scripts d'aide pour développement
- Support PlatformIO et ESP-IDF
- Effets spéciaux véhicule:
  - Clignotants animés
  - Feux de stop
  - Indicateur de charge
  - Animation d'accueil
- Protection et gestion d'erreurs
- Logging détaillé
- Monitoring système (mémoire, WiFi, Commander)

### Sécurité
- Mots de passe configurables
- Timeout de connexion
- Validation des données CAN
- Protection contre débordements de buffer

## Notes de version

### Configuration requise
- ESP32 (ESP32-WROOM-32 ou compatible)
- ESP-IDF v5.0 ou supérieur
- Strip LED WS2812 ou WS2812B
- Alimentation 5V appropriée
- Commander Panda (optionnel)

### Installation
Voir README.md pour les instructions d'installation détaillées.

### Migration depuis une version antérieure
N/A - Première version

### Problèmes connus
- La latence peut augmenter avec >150 LEDs
- Le mode AP WiFi peut interférer avec certains réseaux 2.4GHz
- Commander doit être sur le même réseau 192.168.42.x

### Corrections prévues
- Optimisation de la latence pour grands strips
- Amélioration de la stabilité WiFi
- Détection automatique de l'IP du Commander

---

[Non publié]: https://github.com/username/tesla-strip/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/username/tesla-strip/releases/tag/v1.0.0
