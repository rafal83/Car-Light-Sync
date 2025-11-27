# Optimisations effectuées

## 1. Factorisation des traductions (i18n.js)

### Problème
Les sections `canEvents` et `eventNames` contenaient de nombreuses traductions en double (17 doublons en français, 17 en anglais).

### Solution
- Création d'une section `commonEvents` contenant les traductions partagées
- Les traductions communes incluent: turnLeft, turnRight, hazard, charging, chargeComplete, doorOpen/Close, locked/unlocked, brakeOn/Off, blindspot*, nightMode*, speedThreshold
- Les sections `canEvents` et `eventNames` ne contiennent plus que leurs traductions spécifiques

### Résultat
- **Réduction de ~50% des traductions redondantes**
- i18n.js.gz: **6.8 KB** (compression 78.4%)
- Code plus maintenable (une seule source de vérité pour chaque traduction)

### Mapping dans script.js
Ajout de la constante `EVENT_TO_COMMON` pour mapper les événements vers les traductions communes:
```javascript
const EVENT_TO_COMMON = {
    'TURN_LEFT': 'turnLeft', 'TURN_RIGHT': 'turnRight', ...
};
```

## 2. Factorisation des accès DOM (script.js)

### Problème
- 181 occurrences de `document.getElementById()`
- 13 occurrences de `document.createElement()`
- 5 occurrences de `document.querySelectorAll()`
- 6 occurrences de `document.querySelector()`
- Code verbose et répétitif

### Solution
Ajout de helpers au début de script.js:
```javascript
// DOM helpers
const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelector(sel);
const doc = document;
const hide = (el) => el.style.display = 'none';
const show = (el, displayType = 'block') => el.style.display = displayType;
```

### Remplacement global
- `document.getElementById(...)` → `$(...)`
- `document.querySelector(...)` → `$$(...)`
- `document.querySelectorAll(...)` → `doc.querySelectorAll(...)`
- `document.createElement(...)` → `doc.createElement(...)`
- `document.addEventListener/removeEventListener(...)` → `doc.addEventListener/removeEventListener(...)`
- `document.body` → `doc.body`

### Résultat
- **Réduction significative de la verbosité**
- script.js.gz: **20.5 KB** (compression 80.3%)
- Code plus lisible et concis
- Gain estimé: ~2-3 KB avant compression

## 3. Fix erreurs HTTP ESP_ERR_HTTPD_RESP_SEND

### Problème
Erreurs fréquentes lors de l'envoi de fichiers statiques:
```
W (59509) WebServer: Erreur envoi fichier statique pour URI /carlightsync.png: ESP_ERR_HTTPD_RESP_SEND (errno: 11)
W (59515) WebServer: Socket bloquée, connexion sera fermée
```

`errno: 11` = EAGAIN/EWOULDBLOCK → buffer TCP plein

### Solution
Implémentation d'un envoi chunké pour les fichiers > 8KB dans [web_server.c](main/web_server.c:79-105):

```c
// Pour les fichiers > 8KB, envoyer par chunks pour éviter EAGAIN
#define CHUNK_SIZE 4096
if (file_size > 8192) {
  const uint8_t *data = route->start;
  size_t remaining = file_size;

  while (remaining > 0 && err == ESP_OK) {
    size_t chunk_size = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
    err = httpd_resp_send_chunk(req, (const char *)data, chunk_size);
    data += chunk_size;
    remaining -= chunk_size;
    // Petit délai pour laisser le buffer TCP se vider
    if (remaining > 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  // Terminer l'envoi chunké
  httpd_resp_send_chunk(req, NULL, 0);
}
```

### Bénéfices
- ✅ Évite les erreurs EAGAIN pour les gros fichiers (images PNG, etc.)
- ✅ Envoi progressif avec délais pour permettre au buffer TCP de se vider
- ✅ Fallback sur envoi normal pour les petits fichiers (< 8KB)
- ✅ Meilleure robustesse du serveur web

## Résumé des gains

| Fichier | Taille originale | Taille compressée | Ratio |
|---------|-----------------|-------------------|-------|
| i18n.js | 31.8 KB | 6.9 KB | 78.4% |
| script.js | 104.4 KB | 20.5 KB | 80.3% |
| index.html | - | 5.4 KB | - |
| style.css | - | 3.2 KB | - |

**Total compressé: ~36 KB**

## 4. Fix bugs notifications et simulation

### Bug A: Notifications ne s'affichent plus
**Cause:** Fonction `showNotification` dupliquée (ligne 1070 + stub ligne 2129)
**Solution:** Suppression du stub
**Résultat:** ✅ Notifications fonctionnent à nouveau + gain 247 octets

### Bug B: Erreur simulation malgré succès backend
**Cause:** Incohérence `status` vs `st` entre backend/frontend
**Solution:** Uniformisation sur `"st"` partout
**Résultat:** ✅ Notifications de simulation correctes

Détails complets: [BUGFIX_SIMULATION.md](BUGFIX_SIMULATION.md)

## Résumé final

### Améliorations code
- ✅ ~50% de traductions en moins (factorisation commonEvents)
- ✅ 181 `document.getElementById` → `$` (gain ~2-3 KB)
- ✅ 6 fonctions mortes supprimées (gain 411 octets)
- ✅ Fusion saveProfile (gain 62 octets + performance)
- ✅ Code plus lisible et maintenable
- ✅ Fix erreurs HTTP sur gros fichiers (chunked transfer)
- ✅ Fix bugs notifications et simulation

## 5. Nettoyage du code mort

**6 fonctions inutilisées** supprimées automatiquement détectées :
- `toggleLanguage()` - sélection via `<select>` HTML
- `applyEffect()` - remplacée par système de profils
- `saveConfig()` - confusion avec `saveAudioConfig()`
- `assignEventEffect()` - remplacée par nouveau système
- `simulateEvent()` - remplacée par `toggleEvent()`
- `stopEffect()` - jamais utilisée

**Gain:** -96 lignes (-3.6%), -411 octets compressés

Détails: [DEAD_CODE_CLEANUP.md](DEAD_CODE_CLEANUP.md)

## 6. Fusion saveProfile

**Problème:** 2 fonctions + 2 endpoints pour sauvegarder le profil
- `saveProfileSettings()` → `/api/profile/update`
- `saveDefaultEffect()` → `/api/profile/update/default`

**Solution:** Fonction unifiée `saveProfile()` + endpoint unique acceptant tous paramètres

**Gain:** Frontend -29 lignes (-62 octets), Backend -100 lignes, -1 requête HTTP

Détails: [REFACTOR_SAVE_PROFILE.md](REFACTOR_SAVE_PROFILE.md)

### Gains de taille
- script.js: **104 KB** → **97 KB** → **20.0 KB** compressé (-80.8%)
- i18n.js: **31.8 KB** → **6.9 KB** compressé (-78.4%)
- **Total: -7.3 KB par rapport à script.js.gz original (27KB → 19.99KB)**
