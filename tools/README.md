# Car Light Sync - Python Tools

Ce dossier contient tous les outils Python utilisés pour le développement, la configuration et la compilation du firmware Car Light Sync.

## Structure

```
tools/
├── build/              # Scripts de build PlatformIO
│   ├── compress_html.py
│   ├── inject_version.py
│   ├── create_release.py
│   └── set_idf_env.py
│
├── can/                # Outils de configuration CAN
│   ├── dbc_to_config.py
│   ├── filter_can_config.py
│   └── generate_vehicle_can_config.py
│
└── README.md           # Ce fichier
```

---

## 🔧 Build Tools (`tools/build/`)

Scripts utilisés automatiquement lors de la compilation PlatformIO.

### `compress_html.py`

**Usage:** Automatique (pre-build script)

Compresse les fichiers web `data/index.html`, `data/script.js` et `data/style.css` en format GZIP pour optimiser la taille en mémoire.

**Fonctionnalités:**
- Compression GZIP niveau 9
- Vérification de la date de modification (skip si déjà à jour)
- Affichage du taux de compression

**Appelé par:** PlatformIO `extra_scripts = pre:tools/build/compress_html.py`

---

### `inject_version.py`

**Usage:** Automatique (pre-build script)

Génère automatiquement le fichier `include/version_auto.h` contenant la version du firmware.

**Fonctionnalités:**
- Calcul de version basé sur: `ANNÉE.SEMAINE.COMMIT_COUNT`
- Récupération du nombre de commits depuis `git rev-list --count HEAD`
- Génération d'un header C avec `APP_GIT_VERSION`

**Exemple de version:** `2025.47.342` (année 2025, semaine 47, 342 commits)

**Appelé par:** PlatformIO `extra_scripts = pre:tools/build/inject_version.py`

---

### `generate_icons.py`

**Usage:** Manuel (`python tools/generate_icons.py`)

Génère automatiquement des déclinaisons PNG du logo `data/carlightsync.png` pour la WebUI et l'application mobile.

**Fonctionnalités:**
- Crée des icônes 32→1024 px dans `data/icons/` et `mobile.app/resources/icons/`
- Produit `mobile.app/resources/icon.png` (1024 px) pour `capacitor-assets`
- Paramètres `--source` et `--sizes` pour personnaliser les entrées

**Prérequis:** `pip install Pillow`

**Utilisation typique:**
```bash
python tools/generate_icons.py
```
Ensuite `npm run generate:icons` dans `mobile.app/` pour mettre à jour les assets Android/iOS.

---

### `create_release.py`

**Usage:** Automatique (post-build script) ou manuel

Crée un package de release complet avec tous les fichiers nécessaires pour l'installation et les mises à jour OTA.

**Fonctionnalités:**
- Création de `build/flash-complete/` avec bootloader, partitions et firmware
- Création de `build/ota-update/` avec le firmware OTA
- Génération de scripts de flash (`.bat` pour Windows, `.sh` pour Linux/Mac)
- Création de README avec instructions d'installation
- Fichier VERSION.txt avec informations complètes

**Appelé par:** PlatformIO `extra_scripts = post:tools/build/create_release.py`

**Utilisation manuelle:**
```bash
python tools/build/create_release.py
```

---

## 🚗 CAN Tools (`tools/can/`)

Outils pour configurer et gérer les messages CAN des différents véhicules.

### `dbc_to_config.py`

**Usage:** Manuel

Convertit un fichier DBC (CAN database) en configuration JSON pour Car Light Sync.

**Fonctionnalités:**
- Parsing de fichiers DBC avec `cantools`
- Détection automatique des événements (gear, turn signal, door, lock, charging, etc.)
- Génération de mapping des signaux
- Support de signaux booléens et enum
- Mode interactif pour saisir les informations du véhicule

**Installation des dépendances:**
```bash
pip install cantools
```

**Exemples d'utilisation:**

```bash
# Conversion simple
python tools/can/dbc_to_config.py tesla_model3.dbc --output model3_2021.json

# Avec informations du véhicule
python tools/can/dbc_to_config.py tesla_model3.dbc \
  --output model3_2021.json \
  --make Tesla \
  --model "Model 3" \
  --year 2021 \
  --variant "Long Range"

# Mode interactif
python tools/can/dbc_to_config.py custom.dbc --interactive
```

**Événements détectés automatiquement:**
- `GEAR_PARK`, `GEAR_REVERSE`, `GEAR_DRIVE`
- `TURN_LEFT`, `TURN_RIGHT`, `TURN_HAZARD`
- `DOOR_OPEN_LEFT`, `DOOR_OPEN_RIGHT`, `DOOR_CLOSE_LEFT`, `DOOR_CLOSE_RIGHT`
- `LOCKED`, `UNLOCKED`
- `CHARGING_STARTED`, `CHARGING_STOPPED`
- `SENTRY_MODE_ON`, `SENTRY_MODE_OFF`, `SENTRY_ALERT`
- `AUTOPILOT_ENGAGED`, `AUTOPILOT_DISENGAGED`
- Et bien d'autres...

---

### `filter_can_config.py`

**Usage:** Manuel

Filtre une configuration CAN complète pour ne garder que les messages ayant des événements définis.

**Fonctionnalités:**
- Filtre les messages sans événements
- Conserve uniquement les signaux avec événements
- Affiche des statistiques (messages, signaux, événements conservés)
- Calcule la taille mémoire estimée
- Génère une nouvelle description

**Exemples d'utilisation:**

```bash
# Nom de sortie automatique (ajoute _filtered)
python tools/can/filter_can_config.py vehicle_configs/tesla/model3_2021_full.json

# Nom de sortie personnalisé
python tools/can/filter_can_config.py \
  vehicle_configs/tesla/model3_2021_full.json \
  vehicle_configs/tesla/model3_2021.json
```

**Cas d'usage:**
- Réduire la taille de la configuration pour l'ESP32
- Optimiser la mémoire en ne gardant que les messages utiles
- Préparer une configuration pour la production

**Exemple de sortie:**
```
📖 Lecture de vehicle_configs/tesla/model3_2021_full.json...
✅ 156 messages trouvés

💾 Sauvegarde dans vehicle_configs/tesla/model3_2021.json...
✅ Filtrage terminé!

📊 Statistiques:
  - Messages conservés: 24
  - Signaux avec événements: 38
  - Total événements: 52
  - Réduction: 132 messages supprimés

💾 Mémoire estimée: ~4256 bytes (4.2 KB)
```

---

### `generate_vehicle_can_config.py`

**Usage:** Manuel ou via build system

Génère un header C (`vehicle_can_unified_config.generated.h`) depuis un fichier de configuration JSON.

**Fonctionnalités:**
- Génération de structures C `can_message_def_t` et `can_signal_def_t`
- Conversion des types (byte_order, value_type)
- Génération d'identifiants C valides
- Tableaux globaux `g_can_messages[]` et `g_can_message_count`

**Exemples d'utilisation:**

```bash
python tools/can/generate_vehicle_can_config.py \
  vehicle_configs/tesla/model3_2021.json \
  include/vehicle_can_unified_config.generated.h
```

**Cas d'usage:**
- Intégration de la configuration CAN dans le firmware
- Génération automatique lors du build
- Mise à jour rapide de la configuration véhicule

**Structure générée:**
```c
// Signaux pour signals_MSG_DI_state
static const can_signal_def_t signals_MSG_DI_state[] = {
    {
        .name       = "DI_gear",
        .start_bit  = 13,
        .length     = 3,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor     = 1.000000f,
        .offset     = 0.000000f,
    },
};

const can_message_def_t g_can_messages[] = {
    {
        .id           = 0x118,
        .name         = "DI_state",
        .signals      = signals_MSG_DI_state,
        .signal_count = 1,
    },
};

const uint16_t g_can_message_count = 1;
```

---

## 📚 Workflow de développement

### 1. Ajout d'un nouveau véhicule

```bash
# 1. Convertir le fichier DBC
python tools/can/dbc_to_config.py \
  vehicle_dbc/tesla_model_y.dbc \
  --output vehicle_configs/tesla/model_y_2023_full.json \
  --make Tesla --model "Model Y" --year 2023

# 2. Filtrer pour ne garder que les événements
python tools/can/filter_can_config.py \
  vehicle_configs/tesla/model_y_2023_full.json \
  vehicle_configs/tesla/model_y_2023.json

# 3. Générer le header C (si nécessaire)
python tools/can/generate_vehicle_can_config.py \
  vehicle_configs/tesla/model_y_2023.json \
  include/vehicle_can_unified_config.generated.h

# 4. Compiler et tester
pio run -t upload
```

### 2. Build et release

```bash
# Compilation (les scripts pre/post s'exécutent automatiquement)
pio run

# Le script create_release.py génère automatiquement:
# - build/flash-complete/ (installation complète)
# - build/ota-update/ (mise à jour OTA)
# - Scripts de flash (.bat/.sh)
# - Documentation (README.md, VERSION.txt)
```

### 3. Mise à jour de la configuration

```bash
# Modifier le fichier JSON de configuration
# Puis régénérer le header
python tools/can/generate_vehicle_can_config.py \
  vehicle_configs/tesla/model3_2021.json \
  include/vehicle_can_unified_config.generated.h

# Recompiler
pio run
```

---

## 🔍 Dépendances

### Build tools
- **Python 3.7+** (fourni avec PlatformIO)
- Aucune dépendance externe

### CAN tools
- **Python 3.7+**
- **cantools** (pour `dbc_to_config.py`)
  ```bash
  pip install cantools
  ```

---

## 💡 Bonnes pratiques

1. **Versions full vs filtrées:**
   - Garder les versions `*_full.json` comme référence complète
   - Utiliser les versions filtrées pour la production

2. **Nommage des fichiers:**
   - Format: `{make}_{model}_{year}[_variant][_full].json`
   - Exemples: `model3_2021.json`, `model3_2021_full.json`

3. **Validation:**
   - Toujours tester les configurations générées avant flash
   - Vérifier les événements détectés automatiquement
   - Ajuster manuellement si nécessaire

4. **Documentation:**
   - Documenter les événements custom dans les fichiers JSON
   - Ajouter des commentaires dans les configurations complexes

---

## 🆘 Aide et support

Pour plus d'informations sur le projet:
- README principal: [../README.md](../README.md)
- Configuration CAN: [../vehicle_configs/](../vehicle_configs/)
- Documentation du firmware: [../docs/](../docs/)

En cas de problème avec les outils:
1. Vérifier que Python 3.7+ est installé
2. Installer les dépendances nécessaires (`pip install cantools`)
3. Consulter les exemples d'utilisation ci-dessus
4. Vérifier les logs d'erreur pour plus de détails
