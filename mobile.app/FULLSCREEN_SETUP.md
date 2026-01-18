# Configuration Fullscreen pour l'application mobile

## Installation des plugins requis

Pour activer le mode fullscreen (masquage de la barre de statut) et forcer l'orientation paysage, installez les dépendances :

```bash
cd mobile.app
npm install
```

Cela installera :
- `@capacitor/status-bar` - Pour masquer/afficher la barre de statut système
- `@capawesome/capacitor-screen-orientation` - Pour forcer l'orientation paysage

## Synchronisation avec les plateformes natives

Après l'installation, synchronisez avec Android/iOS :

```bash
npm run sync:android   # Pour Android
npm run sync:ios       # Pour iOS
```

## Configuration

Le fichier `capacitor.config.json` est déjà configuré avec :

```json
{
  "plugins": {
    "StatusBar": {
      "style": "Dark",
      "backgroundColor": "#000000"
    },
    "ScreenOrientation": {
      "orientation": "landscape"
    }
  }
}
```

## Fonctionnement

### Fullscreen automatique
- L'application masque automatiquement la barre de statut au démarrage du dashboard
- Le mode fullscreen est activé dès le chargement de la page

### Contrôle manuel
Si besoin, tu peux basculer le fullscreen manuellement depuis la console :

```javascript
toggleFullscreen()  // Bascule entre fullscreen et normal
```

## Build et test

### Android
```bash
npm run build:apk          # Build APK debug
npm run install:android    # Build + Install sur appareil connecté
```

### iOS
```bash
npm run build:ios          # Build iOS
npm run run:ios            # Build + Run sur simulateur/appareil
```

## Notes importantes

1. **Permissions Android** : Le plugin StatusBar nécessite des permissions qui sont déjà gérées automatiquement par Capacitor

2. **Mode immersif** : Sur Android, l'application utilisera le mode immersif complet (pas de barre système visible)

3. **Orientation** : L'orientation paysage est forcée au niveau de la configuration Capacitor, mais aussi via JavaScript pour plus de robustesse

4. **Web vs Mobile** : Le code détecte automatiquement si l'app tourne sur mobile (Capacitor) ou navigateur web et utilise la bonne API
