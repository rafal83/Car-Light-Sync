# Migration des outils Python - 20 Novembre 2025

## Changements effectués

### Réorganisation de la structure

Les outils Python ont été réorganisés pour améliorer la maintenabilité et la clarté du projet.

#### Avant :
```
esp32-tesla-strip/
├── compress_html.py
├── inject_version.py
├── create_release.py
├── set_idf_env.py
└── tools/
    ├── dbc_to_config.py
    ├── filter_can_config.py
    └── generate_vehicle_can_config.py
```

#### Après :
```
esp32-tesla-strip/
└── tools/
    ├── __init__.py
    ├── README.md
    ├── MIGRATION.md
    ├── build/
    │   ├── __init__.py
    │   ├── compress_html.py
    │   ├── inject_version.py
    │   ├── create_release.py
    │   └── set_idf_env.py
    └── can/
        ├── __init__.py
        ├── dbc_to_config.py
        ├── filter_can_config.py
        └── generate_vehicle_can_config.py
```

### Fichiers déplacés

| Ancien chemin | Nouveau chemin |
|--------------|----------------|
| `compress_html.py` | `tools/build/compress_html.py` |
| `inject_version.py` | `tools/build/inject_version.py` |
| `create_release.py` | `tools/build/create_release.py` |
| `set_idf_env.py` | `tools/build/set_idf_env.py` |
| `tools/dbc_to_config.py` | `tools/can/dbc_to_config.py` |
| `tools/filter_can_config.py` | `tools/can/filter_can_config.py` |
| `tools/generate_vehicle_can_config.py` | `tools/can/generate_vehicle_can_config.py` |

### Fichiers modifiés

#### `platformio.ini`
Mise à jour des chemins des scripts pour les trois environnements (esp32dev, esp32s2, esp32s3) :

```ini
# Avant
extra_scripts =
    pre:compress_html.py
    pre:inject_version.py
    post:create_release.py

# Après
extra_scripts =
    pre:tools/build/set_idf_env.py
    pre:tools/build/compress_html.py
    pre:tools/build/inject_version.py
    post:tools/build/create_release.py
```

Note: `set_idf_env.py` a été ajouté explicitement pour esp32s3 pour plus de clarté.

### Fichiers créés

- `tools/__init__.py` - Package Python principal
- `tools/build/__init__.py` - Package des scripts de build
- `tools/can/__init__.py` - Package des outils CAN
- `tools/README.md` - Documentation complète des outils
- `tools/MIGRATION.md` - Ce fichier

## Impact sur les développeurs

### ✅ Aucun impact sur l'utilisation normale

- Les builds PlatformIO continuent de fonctionner sans modification
- Les scripts sont appelés automatiquement aux bons moments
- Les fonctionnalités restent identiques

### 📝 Mise à jour des commandes manuelles

Si vous utilisiez les outils manuellement, mettez à jour vos commandes :

#### Scripts de build
```bash
# Avant
python compress_html.py
python inject_version.py
python create_release.py

# Après
python tools/build/compress_html.py
python tools/build/inject_version.py
python tools/build/create_release.py
```

#### Outils CAN
```bash
# Avant
python tools/dbc_to_config.py input.dbc -o output.json
python tools/filter_can_config.py input.json output.json
python tools/generate_vehicle_can_config.py input.json output.h

# Après
python tools/can/dbc_to_config.py input.dbc -o output.json
python tools/can/filter_can_config.py input.json output.json
python tools/can/generate_vehicle_can_config.py input.json output.h
```

### 🔄 Mise à jour des scripts/alias personnels

Si vous avez créé des scripts ou alias personnels, pensez à les mettre à jour.

#### Exemple pour Bash/Zsh
```bash
# Avant
alias tesla-build="python create_release.py"

# Après
alias tesla-build="python tools/build/create_release.py"
```

#### Exemple pour PowerShell
```powershell
# Avant
function Build-Release { python create_release.py }

# Après
function Build-Release { python tools/build/create_release.py }
```

## Avantages de cette réorganisation

### 🎯 Meilleure organisation
- Séparation claire entre scripts de build et outils CAN
- Structure logique et facile à comprendre
- Packages Python importables si nécessaire

### 📚 Documentation améliorée
- README dédié avec exemples d'utilisation
- Documentation inline dans les `__init__.py`
- Workflow de développement clairement défini

### 🔧 Maintenance facilitée
- Tous les outils regroupés dans un seul dossier
- Plus facile d'ajouter de nouveaux outils
- Structure évolutive pour de futures fonctionnalités

### 🧪 Testabilité
- Structure de package permet l'import pour les tests
- Isolation des différents types d'outils
- Meilleure organisation du code

## Vérification de la migration

Pour vérifier que tout fonctionne correctement après la migration :

```bash
# Test du build complet
pio run -e esp32s3 -t clean
pio run -e esp32s3

# Test des outils CAN
python tools/can/filter_can_config.py --help
python tools/can/dbc_to_config.py --help

# Test du script de release
python tools/build/create_release.py
```

## Rollback (si nécessaire)

En cas de problème, pour revenir à l'ancienne structure :

```bash
# Déplacer les scripts de build à la racine
mv tools/build/*.py .

# Déplacer les outils CAN à tools/
mv tools/can/*.py tools/

# Restaurer platformio.ini depuis Git
git checkout platformio.ini

# Nettoyer les nouveaux fichiers
rm -rf tools/build tools/can tools/__init__.py tools/MIGRATION.md
```

## Support

Pour toute question ou problème lié à cette migration :

1. Consultez la documentation : [tools/README.md](README.md)
2. Vérifiez les exemples dans le README
3. Assurez-vous que vos chemins sont corrects
4. Testez avec un build propre (`pio run -t clean`)

## Historique

- **20 Novembre 2025** - Migration initiale des outils Python vers la nouvelle structure
  - Création des sous-dossiers `build/` et `can/`
  - Mise à jour de `platformio.ini`
  - Création de la documentation complète
