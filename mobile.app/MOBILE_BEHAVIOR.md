# Comportement spécifique Mobile

## 🎯 Connexion automatique en Bluetooth

Contrairement à la version web qui nécessite que l'utilisateur clique sur le bouton BLE, **l'application mobile se connecte automatiquement** au démarrage.

## 🔄 Différences Web vs Mobile

| Fonctionnalité | Web (navigateur) | Mobile (app) |
|----------------|------------------|--------------|
| **Connexion WiFi** | Prioritaire | Désactivée |
| **Connexion BLE** | Manuelle (clic bouton) | **Automatique au démarrage** |
| **Geste utilisateur requis** | Oui (sécurité navigateur) | Non (contourné) |
| **Overlay de connexion** | Affiché si non connecté | Affiché brièvement puis connexion auto |
| **Sélection d'appareil** | Popup navigateur | Auto (premier trouvé) |

## 🛠️ Comment ça fonctionne ?

### 1. Détection de plateforme

Lors du chargement de l'application, `capacitor-bluetooth-adapter.js` détecte qu'on est sur mobile :

```javascript
if (Capacitor.isNativePlatform()) {
  // On est sur mobile (Android/iOS)
  window.isCapacitorNativeApp = true;
}
```

### 2. Désactivation du WiFi

Le script `sync-html.js` injecte automatiquement un patch dans le HTML généré :

```javascript
let wifiOnline = false; // Forcé à false sur mobile
```

Cela force l'application à utiliser **exclusivement le Bluetooth** au lieu du WiFi.

### 3. Contournement du geste utilisateur

Sur navigateur web, les APIs Bluetooth nécessitent un "geste utilisateur" (clic, toucher) pour des raisons de sécurité. Sur mobile natif, ce n'est pas nécessaire.

L'adaptateur force les flags suivants :

```javascript
window.bleAutoConnectGestureCaptured = true;  // Simule qu'un geste a été capturé
window.bleAutoConnectAwaitingGesture = false; // Pas besoin d'attendre
```

### 4. Déclenchement automatique

Une fois ces flags positionnés, l'adaptateur appelle automatiquement :

```javascript
window.maybeAutoConnectBle(true); // Force la connexion BLE
```

## 📱 Flux de connexion mobile

```
1. App démarre
    ↓
2. Capacitor détecte plateforme native
    ↓
3. window.isCapacitorNativeApp = true
    ↓
4. wifiOnline forcé à false
    ↓
5. bleAutoConnectGestureCaptured = true
    ↓
6. maybeAutoConnectBle(true) appelé
    ↓
7. Scan BLE démarre automatiquement
    ↓
8. Premier appareil trouvé → connexion
    ↓
9. Interface débloquée et prête
```

## ⏱️ Timeline de connexion

| Temps | Événement |
|-------|-----------|
| 0ms | Lancement de l'app |
| ~100ms | Scripts Capacitor chargés |
| ~200ms | Flags BLE positionnés |
| ~300ms | Scan BLE démarre |
| ~1000-5000ms | Appareil trouvé et connexion établie |
| ~5100ms | Interface utilisateur débloquée |

## 🎨 Expérience utilisateur

### Sur Web

1. Utilisateur ouvre la page
2. **Overlay affiché** : "Connectez-vous en WiFi ou BLE"
3. Utilisateur clique sur bouton BLE 🔵
4. Popup de sélection d'appareil
5. Utilisateur sélectionne l'appareil
6. Connexion établie
7. Interface débloquée

### Sur Mobile

1. Utilisateur ouvre l'app
2. **Overlay affiché brièvement** : "Connexion en cours..."
3. **Connexion automatique** sans intervention
4. Interface débloquée
5. ✅ Prêt à utiliser !

## 🔧 Modifications apportées automatiquement

Le script `sync-html.js` effectue **automatiquement** ces modifications lors de la synchronisation :

### 1. Injection des scripts Capacitor

```html
<head>
  ...
  <script type="module" src="capacitor.js"></script>
  <script type="module" src="capacitor-bluetooth-adapter.js"></script>
</head>
```

### 2. Détection native dans `script.js`

**Avant** (version web classique) :
```javascript
const usingFileProtocol = window.location.protocol === 'file:';
let wifiOnline = !usingFileProtocol && navigator.onLine;
```

**Après** (bundle généré pour mobile) :
```javascript
const usingFileProtocol = window.location.protocol === 'file:';
const usingCapacitor = window.Capacitor !== undefined;
let wifiOnline = !usingFileProtocol && !usingCapacitor && navigator.onLine;
```

👉 Résultat : sur mobile Capacitor, `window.Capacitor` existe, donc `wifiOnline` est automatiquement mis à `false` pour forcer le mode BLE.

## 🐛 Debug et logs

Pour vérifier que la connexion automatique fonctionne, regardez les logs dans la console :

### Logs attendus sur mobile

```
🔵 Using Capacitor Bluetooth LE (Native)
📱 Native platform detected: bypassing gesture requirement for BLE auto-connect
✅ BLE gesture flag created and set to true (native platform)
✅ Capacitor native app flag set
🔄 Triggering BLE auto-connect...
[BLE] Requesting device...
[BLE] Device found: Car Light Sync
[BLE] Connecting to GATT...
[BLE] Connected successfully
✅ Interface unlocked
```

### Android Studio (Logcat)

```bash
npm run open:android
# Dans Android Studio: View > Tool Windows > Logcat
# Filtrer par "chromium" ou "console"
```

### Xcode (Console)

```bash
npm run open:ios
# Dans Xcode: View > Debug Area > Activate Console
```

## ⚙️ Configuration

### Désactiver la connexion automatique (si nécessaire)

Si vous voulez forcer l'utilisateur à cliquer manuellement sur le bouton BLE même sur mobile, modifiez `capacitor-bluetooth-adapter.js` :

```javascript
// Commenter ces lignes :
// forceGestureCaptured();
// setTimeout(forceGestureCaptured, 100);
// etc.
```

### Modifier le délai de scan

Par défaut, le scan BLE dure 5 secondes. Pour modifier :

```javascript
// Dans capacitor-bluetooth-adapter.js, ligne ~170
setTimeout(async () => {
  // ... code
}, 5000); // Modifier ici (en millisecondes)
```

## 🎯 Cas d'usage

### Utilisation normale

L'utilisateur :
1. Lance l'app
2. Attend 2-5 secondes
3. L'app est connectée et prête

### Plusieurs appareils à proximité

**Problème actuel** : L'app se connecte au premier appareil trouvé.

**Solution future** : Implémenter une UI de sélection d'appareil.

**Workaround actuel** :
1. Éloigner les autres appareils Car Light Sync
2. Lancer l'app
3. Se connecter au seul appareil à proximité

### Reconnexion après déconnexion

Si la connexion BLE est perdue :
1. L'overlay réapparaît
2. La connexion automatique est **re-déclenchée**
3. Reconnexion dans les 2-5 secondes

## 📋 Checklist de test

Pour vérifier que la connexion automatique fonctionne :

- [ ] Compiler l'app : `npm run sync:android` ou `npm run sync:ios`
- [ ] Lancer sur appareil/émulateur
- [ ] Vérifier que l'overlay de connexion apparaît brièvement
- [ ] Vérifier que le scan BLE démarre automatiquement (logs)
- [ ] Vérifier que la connexion s'établit sans clic
- [ ] Vérifier que l'interface se débloque automatiquement
- [ ] Tester la reconnexion après fermeture/réouverture de l'app

## 🔍 Troubleshooting

### L'app demande toujours de cliquer

**Cause** : Le flag `isCapacitorNativeApp` n'est pas défini ou le patch n'a pas été appliqué.

**Solution** :
```bash
# Re-synchroniser
npm run sync

# Vérifier les logs : devrait afficher "Capacitor native app detected"
```

### Le scan BLE ne démarre pas

**Cause** : Permissions Bluetooth manquantes.

**Solution** : Consultez [PERMISSIONS.md](PERMISSIONS.md)

### L'app trouve le mauvais appareil

**Cause** : Plusieurs appareils à proximité, connexion au premier trouvé.

**Solution** : Éloigner les autres appareils ou implémenter une UI de sélection.

### La connexion prend trop de temps

**Cause** : Timeout de scan trop court ou appareil éloigné.

**Solution** :
- Rapprocher l'appareil
- Augmenter le timeout de scan dans `capacitor-bluetooth-adapter.js`

## 🚀 Améliorations futures

- [ ] UI de sélection d'appareils BLE
- [ ] Mémorisation du dernier appareil connecté
- [ ] Reconnexion automatique au dernier appareil connu
- [ ] Indicateur visuel de progression du scan
- [ ] Gestion d'erreurs plus robuste
- [ ] Mode "manuel" pour désactiver la connexion auto

## 📚 Ressources

- [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)
- [Capacitor Bluetooth LE](https://github.com/capacitor-community/bluetooth-le)
- [Permissions Bluetooth](PERMISSIONS.md)
- [Documentation de l'adaptateur](BLUETOOTH_ADAPTER.md)
