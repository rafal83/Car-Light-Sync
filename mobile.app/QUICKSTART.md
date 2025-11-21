# Guide de démarrage rapide

## 🚀 Installation et première utilisation

### 1. Installer les dépendances

```bash
cd mobile.app
npm install
```

### 2. Synchroniser le fichier HTML depuis l'ESP32

```bash
npm run sync
```

Cette commande va :
- Copier `../data/index.html` vers `www/index.html`
- Injecter les scripts Capacitor nécessaires
- Préparer les plateformes Android/iOS

### 3. Initialiser les plateformes

```bash
# Pour Android
npx cap add android

# Pour iOS (macOS uniquement)
npx cap add ios
```

### 4. Lancer l'application

#### Sur Android

```bash
# Synchroniser et ouvrir Android Studio
npm run sync:android
npm run open:android
```

Puis dans Android Studio :
1. Connectez votre appareil Android ou lancez un émulateur
2. Cliquez sur "Run" (▶️)

#### Sur iOS (macOS uniquement)

```bash
# Synchroniser et ouvrir Xcode
npm run sync:ios
npm run open:ios
```

Puis dans Xcode :
1. Sélectionnez votre appareil iOS ou un simulateur
2. Cliquez sur "Run" (▶️)

## 📱 Utilisation

L'application mobile se connecte **automatiquement** au démarrage :

1. **Ouvrir l'application** sur votre téléphone
2. **Attendre 2-5 secondes** (connexion automatique en cours)
3. **Connecté !** L'interface est prête à utiliser

**Note** : Contrairement à la version web, **aucun clic sur le bouton BLE n'est nécessaire**. La connexion est automatique.

## 🔄 Workflow de développement

Quand vous modifiez le fichier `../data/index.html` :

```bash
# 1. Synchroniser les changements
npm run sync

# 2. Tester sur Android
npm run open:android

# 3. Tester sur iOS
npm run open:ios
```

## ⚡ Différences entre Web et Mobile

| Fonctionnalité | Web (navigateur) | Mobile (app) |
|----------------|------------------|--------------|
| **Connexion BLE** | **Manuelle (clic bouton)** | **🚀 Automatique** |
| API Bluetooth | Web Bluetooth API | Capacitor BLE (natif) |
| Permissions | Demandées au clic | Demandées à l'installation |
| Performance | Bonne | Excellente (natif) |
| Hors ligne | Non | Possible |
| Installation | Non | Oui (app native) |

## 🐛 Problèmes courants

### Erreur "Bluetooth not supported"

**Sur Android :**
- Vérifiez que le Bluetooth est activé
- Activez la localisation (requis pour BLE)
- Acceptez les permissions dans les paramètres

**Sur iOS :**
- Vérifiez que le Bluetooth est activé
- Acceptez la permission Bluetooth

### Le fichier HTML n'est pas à jour

```bash
npm run sync
```

### Erreur lors du build Android/iOS

```bash
# Nettoyer et réinstaller
rm -rf node_modules android ios www
npm install
npx cap add android
npx cap add ios
npm run sync
```

## 📝 Commandes utiles

### Synchronisation

```bash
# Synchroniser tout
npm run sync

# Synchroniser Android seulement
npm run sync:android

# Synchroniser iOS seulement
npm run sync:ios
```

### Build en ligne de commande (sans IDE)

**Android :**
```bash
# Build APK debug (rapide)
npm run build:apk

# Build APK release (production)
npm run build:apk:release

# Build AAB pour Google Play
npm run build:aab

# Installer directement sur appareil connecté
npm run run:android
```

**iOS :**
```bash
# Lancer sur simulateur
npm run run:ios
```

### Ouvrir les IDE

```bash
# Ouvrir Android Studio
npm run open:android

# Ouvrir Xcode
npm run open:ios
```

### Nettoyage

```bash
# Nettoyer les builds
npm run clean

# Nettoyage complet
rm -rf node_modules android ios www
npm run init
```

**📚 Pour la liste complète des commandes, consultez [CLI_COMMANDS.md](CLI_COMMANDS.md)**

## 🎯 Prochaines étapes

1. **Personnaliser l'icône de l'app** : Remplacez les icônes dans `android/app/src/main/res/` et `ios/App/Assets.xcassets/`
2. **Configurer le nom de l'app** : Éditez `capacitor.config.json` (`appName`)
3. **Publier sur les stores** : Suivez les guides Android/iOS pour la publication

## 💡 Astuces

- **Live Reload** : Utilisez `ionic serve` ou un serveur de développement local
- **Debugging** : Utilisez Chrome DevTools pour Android et Safari pour iOS
- **Logs** : Consultez les logs dans Android Studio / Xcode
- **Mise à jour** : Relancez `npm run sync` après chaque modification du HTML

## 🆘 Aide supplémentaire

- Documentation complète : [README.md](README.md)
- Capacitor docs : https://capacitorjs.com/
- Plugin Bluetooth : https://github.com/capacitor-community/bluetooth-le
