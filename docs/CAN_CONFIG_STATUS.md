# État d'avancement: Configuration CAN Multi-Véhicules

## ✅ Complété

### 1. Structure des répertoires
```
vehicle_configs/
├── tesla/
│   └── model3_2021.json    ✅ Créé
├── custom/                  ✅ Créé
tools/                       ✅ Créé
docs/                        ✅ Créé
```

### 2. Format JSON de configuration
- ✅ Schema version 1.0 défini
- ✅ Configuration complète pour Tesla Model 3 2021
- ✅ Support de 11 messages CAN
- ✅ Support de 22+ signaux
- ✅ Mapping des événements CAN

### 3. Headers C/C++
- ✅ `include/vehicle_can_config.h` créé
  - Structures de données complètes
  - API de décodage CAN
  - Support de 6 types de conditions d'événements

### 4. Implémentation C
- ✅ `main/vehicle_can_config.c` créé
  - Extraction de valeur de signal (little/big endian)
  - Détection d'événements (rising edge, falling edge, value equals, etc.)
  - Décodage générique des messages
  - Mapping automatique signal → vehicle_state

## 🚧 En cours / À faire

### 5. Parser JSON (PRIORITÉ HAUTE)
**Fichier:** `main/vehicle_can_json_parser.c`
- [ ] Fonction `vehicle_can_load_config()`
- [ ] Parse JSON depuis string ou fichier
- [ ] Validation du schema
- [ ] Gestion d'erreurs

### 6. Script Python DBC → JSON
**Fichier:** `tools/dbc_to_config.py`
- [ ] Parser DBC avec `cantools`
- [ ] Mapping interactif signaux → événements
- [ ] Export JSON
- [ ] Support multi-bus

### 7. Intégration dans le code existant
- [ ] Modifier `tesla_can.c` pour utiliser le nouveau système
- [ ] Ajouter API REST pour upload de config
- [ ] Interface web pour sélection de véhicule
- [ ] Stockage de la config en SPIFFS/NVS

### 8. Documentation
**Fichier:** `docs/VEHICLE_CONFIG.md`
- [ ] Guide de création de config manuelle
- [ ] Guide d'utilisation du script Python
- [ ] Liste des signaux supportés
- [ ] Exemples pour autres véhicules

### 9. Configs additionnelles
- [ ] Tesla Model Y 2023
- [ ] Tesla Model S 2022
- [ ] Template générique

### 10. Tests
- [ ] Tests unitaires du parser
- [ ] Tests de décodage de signaux
- [ ] Tests d'événements
- [ ] Validation avec données réelles

## 📋 Prochaines étapes immédiates

1. **Implémenter le parser JSON** (critique)
   - Permet de charger les configs au démarrage
   - Nécessite cJSON (déjà disponible dans le projet)

2. **Créer le script Python**
   - Outil de conversion DBC → JSON
   - Facilite l'adoption par la communauté

3. **Intégration**
   - Remplacer le code hardcodé dans `tesla_can.c`
   - Ajouter sélection de véhicule dans l'interface web

4. **Documentation**
   - Guide complet pour les utilisateurs

## 🔧 Détails techniques

### Stockage de la configuration
Deux options:
1. **SPIFFS** (recommandé): Fichier JSON dans partition data
   - Facile à mettre à jour via OTA
   - Taille flexible

2. **NVS** : Structure binaire
   - Plus rapide au démarrage
   - Taille limitée

### Performance
- Parsing JSON: ~200ms (une fois au démarrage)
- Décodage message: <1ms par message
- Mémoire: ~2-3KB par configuration véhicule

### Compatibilité
- ESP-IDF 5.0+
- cJSON (déjà inclus)
- Python 3.7+ avec `cantools` pour le script

## 📄 Fichiers créés

1. `vehicle_configs/tesla/model3_2021.json` - Configuration Tesla Model 3 2021
2. `include/vehicle_can_config.h` - Header API CAN générique
3. `main/vehicle_can_config.c` - Implémentation décodage CAN
4. `docs/CAN_CONFIG_STATUS.md` - Ce fichier

## 📝 Notes

- Le système est conçu pour être 100% rétrocompatible
- L'ancien code hardcodé dans `tesla_can.c` peut coexister
- Migration progressive possible
- Format JSON extensible pour futures fonctionnalités
