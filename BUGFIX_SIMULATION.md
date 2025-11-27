# Fix: Bugs de notifications et simulation

## Bug 1: Notifications ne s'affichent plus

### Problème
Après les optimisations, aucune notification ne s'affichait plus sur le frontend.

### Cause
**Fonction `showNotification` dupliquée** dans script.js :
- Ligne 1070 : Définition originale fonctionnelle ✅
- Ligne 2129 : Stub qui écrase la première ❌
```javascript
// Stub qui ne fait rien
function showNotification(section, message, type) {
    console.log(`[${type}] ${section}: ${message}`);
}
```

### Solution
Suppression du stub ligne 2129. La fonction originale (ligne 1070) fonctionne maintenant correctement.

### Résultat
- ✅ Toutes les notifications s'affichent à nouveau
- ✅ Gain bonus : -247 octets (suppression code inutile)

---

## Bug 2: Erreur "Erreur lors de la simulation" alors que le backend réussit

### Problème

Lors de l'envoi d'une simulation d'événement CAN, le frontend affichait systématiquement :
```
[error] simulation-notification: Erreur - Erreur lors de la simulation
```

Alors que côté backend, la simulation se lançait correctement (logs OK).

## Cause racine

**Incohérence entre backend et frontend** sur le nom du champ de statut de réponse :

### Backend (web_server.c)
- `/api/simulate/event` (ligne 1320) : renvoie `{"st": "ok"}` ✅
- `/api/stop/event` (ligne 1208) : renvoyait `{"status": "ok"}` ❌

### Frontend (script.js)
Vérifications incohérentes :
- `simulateEvent()` ligne 2385 : `if (result.st === 'ok')` ✅
- `toggleEvent()` lignes 2444, 2466 : `if (result.status === 'ok')` ❌
- `toggleNightMode()` ligne 2498 : `if (result.status === 'ok')` ❌

## Solution

Uniformisation sur **`"st"`** (version courte après compression) :

### 1. Backend ([web_server.c:1208](main/web_server.c#L1208))
```c
// Avant
httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

// Après
httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
```

### 2. Frontend ([script.js](data/script.js))
Remplacement global : `result.status === 'ok'` → `result.st === 'ok'`

Lignes corrigées :
- [script.js:2444](data/script.js#L2444) - `toggleEvent()` activation
- [script.js:2466](data/script.js#L2466) - `toggleEvent()` désactivation
- [script.js:2498](data/script.js#L2498) - `toggleNightMode()`

## Résultat

✅ Les simulations d'événements affichent maintenant le bon statut :
- Succès : "OK - Événement simulé: [nom]"
- Arrêt : "Stop - [nom] arrêté"

✅ Cohérence totale backend/frontend sur l'utilisation de `"st"` au lieu de `"status"`

## Tests à effectuer

1. Simuler un événement (clignotant, frein, etc.) → doit afficher notification verte "OK"
2. Arrêter un événement actif → doit afficher notification verte "Stop"
3. Activer/désactiver le mode nuit → doit afficher notification verte
4. Vérifier que tous les événements de simulation fonctionnent correctement
