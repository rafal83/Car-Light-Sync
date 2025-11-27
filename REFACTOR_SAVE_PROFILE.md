# Refactoring: Fusion des fonctions de sauvegarde de profil

## Problème

Deux fonctions distinctes pour sauvegarder le profil :
1. `saveProfileSettings()` - Sauvegarde mode nuit (endpoint `/api/profile/update`)
2. `saveDefaultEffect()` - Sauvegarde effet par défaut (endpoint `/api/profile/update/default`)

**Inconvénients:**
- ❌ Code dupliqué (gestion d'erreurs, notifications, parsing...)
- ❌ Deux requêtes HTTP au lieu d'une
- ❌ Incohérence potentielle entre les deux endpoints
- ❌ Maintenance difficile

## Solution

### 1. Backend unifié ([web_server.c:744-810](main/web_server.c#L744-L810))

Extension du handler `/api/profile/update` pour accepter **tous** les paramètres :

```c
// Paramètres mode nuit (optionnels)
if (auto_night_mode) profile->auto_night_mode = ...;
if (night_brightness) profile->night_brightness = ...;

// Paramètres effet par défaut (optionnels)
if (effect_json) profile->default_effect.effect = ...;
if (brightness_json) profile->default_effect.brightness = ...;
if (speed_json) profile->default_effect.speed = ...;
if (color_json) profile->default_effect.color1 = ...;
if (audio_reactive_json) profile->default_effect.audio_reactive = ...;
```

**Avantages:**
- ✅ Un seul endpoint pour tout le profil
- ✅ Paramètres optionnels → flexibilité
- ✅ Application intelligente selon ce qui est modifié

### 2. Frontend unifié ([script.js:1558-1608](data/script.js#L1558-L1608))

Nouvelle fonction `saveProfile(params)` :

```javascript
async function saveProfile(params = {}) {
    const payload = { pid: profileId };

    // Inclure settings si params.settings !== false
    if (params.settings !== false) {
        payload.anm = ...;
        payload.nbr = ...;
    }

    // Inclure effet par défaut si params.defaultEffect !== false
    if (params.defaultEffect !== false) {
        payload.fx = ...;
        payload.br = ...;
        payload.sp = ...;
        payload.c1 = ...;
        payload.ar = ...;
    }

    // Une seule requête HTTP
    fetch(API_BASE + '/api/profile/update', { ... });
}

// Aliases pour compatibilité
const saveProfileSettings = () => saveProfile({ defaultEffect: false });
const saveDefaultEffect = (silent) => saveProfile({ settings: false, silent });
```

**Avantages:**
- ✅ Code mutualisé (try/catch, notifications, parsing)
- ✅ Une seule requête HTTP (économie réseau)
- ✅ Rétrocompatibilité via aliases
- ✅ Possibilité de tout sauver en une fois si besoin

## Gains

### Frontend
- **-29 lignes** de JavaScript (-1.1%)
- **-62 octets** compressés

### Backend
- **-100 lignes** de C dans web_server.c (-4.4%)
- **-1 handler HTTP** (simplifie l'API)

### Performance
- **-1 requête HTTP** lors de modifications multiples
- Latence réduite (1 round-trip au lieu de 2)
- Moins de code serveur à exécuter

### Maintenance
- ✅ Un seul point de vérité pour la sauvegarde
- ✅ Gestion d'erreurs unifiée
- ✅ Plus facile à débugger

## Nettoyage backend

Le handler `/api/profile/update/default` a été **supprimé** (inutile) :
- ✅ **-93 lignes** de code C (fonction handler)
- ✅ **-7 lignes** d'enregistrement de route
- ✅ **-100 lignes au total** dans web_server.c
- ✅ Un endpoint en moins à maintenir

## Tests à effectuer

1. Modifier le mode nuit → vérifie `saveProfileSettings()`
2. Modifier l'effet par défaut → vérifie `saveDefaultEffect()`
3. Les deux devraient fonctionner correctement via le nouvel endpoint unifié
