# Minification et Compression des Fichiers Web

## Vue d'ensemble

Le projet utilise un système automatique de minification et compression pour optimiser les fichiers web (HTML, CSS, JS) avant la compilation du firmware.

## Processus

### 1. Minification (Node.js)
- **Outil**: esbuild + html-minifier-terser
- **Réduction moyenne**: ~40% de la taille originale
- **Fichiers traités**:
  - `data/index.html` → `data/index.html.min`
  - `data/style.css` → `data/style.css.min`
  - `data/script.js` → `data/script.js.min`
  - `data/i18n.js` → `data/i18n.js.min`

### 2. Compression GZIP
- **Niveau**: 9 (maximum)
- **Réduction moyenne**: ~75% après minification
- **Fichiers générés**:
  - `data/index.html.gz`
  - `data/style.css.gz`
  - `data/script.js.gz`
  - `data/i18n.js.gz`

## Utilisation

### Installation des dépendances
```bash
npm install
```

### Minification manuelle
```bash
npm run minify
```

### Build automatique
Le processus de minification et compression est **automatique** lors du build PlatformIO :
```bash
pio run                    # Build avec minification
pio run --target uploadfs  # Upload du filesystem
```

## Résultats

### Tailles (exemple)
| Fichier | Original | Minifié | GZIP | Réduction totale |
|---------|----------|---------|------|------------------|
| index.html | 32 KB | 22 KB | 4.8 KB | 85% |
| script.js | 109 KB | 57 KB | 15 KB | 86% |
| style.css | 18 KB | 13 KB | 3.2 KB | 82% |
| i18n.js | 32 KB | 21 KB | 6.3 KB | 80% |
| **TOTAL** | **191 KB** | **113 KB** | **29 KB** | **85%** |

## Fichiers importants

- `package.json` - Dépendances npm
- `scripts/minify.js` - Script de minification Node.js
- `tools/tools-build/compress_html.py` - Script PlatformIO (minification + compression)
- `.gitignore` - Ignore les fichiers générés (*.min, *.gz, node_modules)

## Notes

- Les fichiers `.min` et `.gz` sont **générés automatiquement** et ne doivent **pas** être commités
- Les fichiers sources (`data/*.html`, `data/*.css`, `data/*.js`) doivent rester **non-minifiés** dans le dépôt Git
- Si Node.js n'est pas installé, le build continuera avec compression GZIP uniquement

## Dépannage

### "Node.js not found"
Installer Node.js depuis https://nodejs.org/

### "npm dependencies failed"
```bash
cd <project-root>
rm -rf node_modules package-lock.json
npm install
```

### Les fichiers ne sont pas minifiés
Vérifier que les fichiers sources ne sont pas déjà minifiés :
```bash
# Les fichiers sources doivent être lisibles
cat data/script.js | head
```

Si les fichiers sont déjà minifiés, restaurer depuis Git :
```bash
git checkout data/index.html data/script.js data/style.css data/i18n.js
```
