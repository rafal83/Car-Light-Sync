# Checklist de test - Connexion automatique BLE

## ✅ Tests de base

### Installation

- [ ] `npm install` réussit sans erreurs
- [ ] `npm run init` crée les dossiers `android/` et `ios/`
- [ ] `npm run sync` génère `www/index.html`
- [ ] Le fichier `www/index.html` contient les scripts Capacitor
- [ ] Le fichier `www/index.html` contient le patch `isCapacitorNativeApp`

### Vérification du patch wifiOnline

- [ ] Ouvrir `www/index.html` et chercher "isCapacitorNativeApp"
- [ ] Vérifier que le code suivant est présent :
  ```javascript
  if (window.isCapacitorNativeApp === true) {
      wifiOnline = false;
  }
  ```

### Vérification de l'adaptateur

- [ ] Le fichier `www/capacitor-bluetooth-adapter.js` existe
- [ ] Le fichier contient `forceGestureCaptured()`
- [ ] Le fichier contient `window.isCapacitorNativeApp = true`

## 📱 Tests Android

### Build et lancement

- [ ] `npm run sync:android` réussit
- [ ] `npm run open:android` ouvre Android Studio
- [ ] Le projet compile sans erreurs
- [ ] L'app se lance sur l'émulateur/appareil

### Permissions

- [ ] Les permissions Bluetooth sont dans `AndroidManifest.xml`
- [ ] L'app demande les permissions au lancement (Android 6+)
- [ ] La localisation est activée (Android < 12)

### Connexion automatique

- [ ] Au lancement, l'overlay de connexion s'affiche
- [ ] Le message "Connectez-vous" ou "Connexion..." apparaît
- [ ] **IMPORTANT** : L'app démarre le scan BLE automatiquement (sans clic)
- [ ] L'appareil Tesla Strip est trouvé
- [ ] La connexion s'établit automatiquement
- [ ] L'overlay disparaît
- [ ] L'interface est débloquée

### Logs Android (Logcat)

Vérifier les logs suivants dans Android Studio > Logcat :

- [ ] `🔵 Using Capacitor Bluetooth LE (Native)`
- [ ] `✅ BLE gesture flag created and set to true`
- [ ] `📱 Capacitor native app detected: forcing wifiOnline = false`
- [ ] `🔄 Triggering BLE auto-connect...`
- [ ] `[BLE] Requesting device...`
- [ ] `[BLE] Device found`
- [ ] `[BLE] Connected successfully`

### Timeline Android

Mesurer le temps de connexion :

- [ ] Lancement → Scripts chargés : ~100-200ms
- [ ] Scripts chargés → Scan BLE démarre : ~200-300ms
- [ ] Scan BLE → Appareil trouvé : ~1-5s
- [ ] Appareil trouvé → Connexion établie : ~1-2s
- [ ] **Total** : ~2-7 secondes (acceptable)

## 🍎 Tests iOS

### Build et lancement

- [ ] `npm run sync:ios` réussit
- [ ] `npm run open:ios` ouvre Xcode
- [ ] Le projet compile sans erreurs
- [ ] L'app se lance sur le simulateur/appareil

### Permissions

- [ ] `Info.plist` contient `NSBluetoothAlwaysUsageDescription`
- [ ] `Info.plist` contient `NSBluetoothPeripheralUsageDescription`
- [ ] L'app demande la permission Bluetooth au lancement

### Connexion automatique

- [ ] Au lancement, l'overlay de connexion s'affiche
- [ ] **IMPORTANT** : L'app démarre le scan BLE automatiquement (sans clic)
- [ ] L'appareil Tesla Strip est trouvé
- [ ] La connexion s'établit automatiquement
- [ ] L'overlay disparaît
- [ ] L'interface est débloquée

### Logs iOS (Xcode Console)

Vérifier les logs suivants dans Xcode > Console :

- [ ] `🔵 Using Capacitor Bluetooth LE (Native)`
- [ ] `✅ BLE gesture flag created and set to true`
- [ ] `📱 Capacitor native app detected: forcing wifiOnline = false`
- [ ] `🔄 Triggering BLE auto-connect...`
- [ ] `[BLE] Connected successfully`

### Timeline iOS

- [ ] **Total** : ~2-7 secondes (acceptable)

## 🔄 Tests de reconnexion

### Déconnexion volontaire

- [ ] Connecté, cliquer sur le bouton BLE pour déconnecter
- [ ] L'overlay réapparaît
- [ ] **L'app retente automatiquement** la connexion
- [ ] La connexion se rétablit

### Perte de connexion

- [ ] Éteindre le Tesla Strip
- [ ] L'overlay réapparaît avec message de déconnexion
- [ ] Rallumer le Tesla Strip
- [ ] **L'app retente automatiquement** la connexion
- [ ] La connexion se rétablit

### Fermeture/Réouverture de l'app

- [ ] Connecté, fermer l'app (tuer le processus)
- [ ] Rouvrir l'app
- [ ] **La connexion automatique se déclenche**
- [ ] La connexion se rétablit en 2-7s

## 🧪 Tests fonctionnels

Une fois connecté automatiquement :

### Interface

- [ ] Les onglets sont accessibles
- [ ] Le statut BLE affiche "Connecté"
- [ ] Le bouton BLE affiche l'icône de déconnexion

### Contrôle des LED

- [ ] Changer la luminosité fonctionne
- [ ] Changer la vitesse fonctionne
- [ ] Changer la couleur fonctionne
- [ ] Changer l'effet fonctionne

### Profils

- [ ] La liste des profils se charge
- [ ] Créer un profil fonctionne
- [ ] Modifier un profil fonctionne
- [ ] Supprimer un profil fonctionne
- [ ] Changer de profil fonctionne

### Événements CAN

- [ ] La table des événements se charge
- [ ] Modifier un événement fonctionne
- [ ] Activer/désactiver un événement fonctionne

### Simulation

- [ ] Les toggles de simulation fonctionnent
- [ ] Activer un événement déclenche l'effet

## 🐛 Tests d'erreurs

### Pas d'appareil à proximité

- [ ] Aucun Tesla Strip allumé
- [ ] Lancer l'app
- [ ] Le scan BLE dure 5 secondes
- [ ] Message d'erreur : "No devices found"
- [ ] L'overlay reste affiché

### Plusieurs appareils

- [ ] Plusieurs Tesla Strip allumés
- [ ] Lancer l'app
- [ ] **L'app se connecte au premier trouvé** (comportement actuel)
- [ ] TODO : Implémenter UI de sélection

### Permissions refusées

#### Android

- [ ] Désinstaller l'app
- [ ] Réinstaller
- [ ] Refuser les permissions Bluetooth
- [ ] L'app affiche une erreur
- [ ] Aller dans Paramètres > Permissions > Bluetooth
- [ ] Accorder la permission
- [ ] Relancer l'app
- [ ] La connexion fonctionne

#### iOS

- [ ] Désinstaller l'app
- [ ] Réinstaller
- [ ] Refuser la permission Bluetooth
- [ ] L'app affiche une erreur
- [ ] Aller dans Réglages > Bluetooth
- [ ] Activer le Bluetooth
- [ ] Relancer l'app
- [ ] La connexion fonctionne

## 📊 Tests de performance

### Temps de connexion

Mesurer 5 fois le temps entre lancement et interface débloquée :

- [ ] Essai 1 : _____ secondes
- [ ] Essai 2 : _____ secondes
- [ ] Essai 3 : _____ secondes
- [ ] Essai 4 : _____ secondes
- [ ] Essai 5 : _____ secondes
- [ ] **Moyenne** : _____ secondes (cible : < 7s)

### Stabilité de la connexion

Test de durée 10 minutes :

- [ ] Lancer l'app et connecter
- [ ] Attendre 10 minutes
- [ ] La connexion reste stable
- [ ] Aucune déconnexion intempestive

### Utilisation mémoire

- [ ] Surveiller la mémoire dans Android Studio / Xcode
- [ ] Mémoire stable (pas de fuite)
- [ ] Utilisation CPU raisonnable

## 🌐 Tests Web (référence)

Pour comparer avec le comportement web :

- [ ] Ouvrir `www/index.html` dans Chrome/Edge
- [ ] L'overlay affiche "Connectez-vous en WiFi ou BLE"
- [ ] **Cliquer sur le bouton BLE** (manuel)
- [ ] Sélectionner l'appareil dans la popup
- [ ] La connexion s'établit
- [ ] Comportement normal (référence)

## 📝 Notes de test

### Environnement de test

- Date : __________________
- OS Android : __________________
- OS iOS : __________________
- Appareil Android : __________________
- Appareil iOS : __________________
- Version Capacitor : __________________
- Version Bluetooth LE Plugin : __________________

### Problèmes rencontrés

```
(Noter ici les problèmes rencontrés et leurs solutions)
```

### Améliorations suggérées

```
(Noter ici les améliorations possibles)
```

## ✨ Résultat final

- [ ] **PASS** : Tous les tests critiques passent
- [ ] **FAIL** : Au moins un test critique échoue

### Tests critiques

1. [  ] Connexion automatique fonctionne sur Android
2. [  ] Connexion automatique fonctionne sur iOS
3. [  ] Le temps de connexion est < 10 secondes
4. [  ] Aucun clic utilisateur n'est requis
5. [  ] La reconnexion automatique fonctionne

---

**Si tous les tests critiques passent** ✅ → L'implémentation est réussie !
