# Référence API JSON - Clés Optimisées

Ce document liste toutes les clés JSON utilisées par l'API REST du Car Light Sync. Le système utilise des **clés courtes** pour optimiser la taille des réponses JSON et améliorer les performances.

## 📊 Bénéfices de l'Optimisation

- **Réduction de taille** : ~30-40% de réduction de la taille des payloads JSON
- **Performance** : Traitement plus rapide sur l'ESP32
- **Mémoire** : Économie de RAM lors du parsing JSON
- **Bande passante** : Réduction de la consommation réseau

## 🔑 Mapping des Clés

### État Système (`/api/status`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `wifi_connected` | `wc` | bool | Connexion WiFi active |
| `wifi_ip` | `wip` | string | Adresse IP WiFi |
| `can_bus_running` | `cbr` | bool | Bus CAN opérationnel |
| `vehicle_active` | `va` | bool | Véhicule actif (données récentes < 5s) |
| `active_profile_id` | `pid` | number | ID du profil actif |
| `active_profile_name` | `pn` | string | Nom du profil actif |

### État Véhicule (`vehicle`)

#### Général
| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `speed` | `s` | number | Vitesse (km/h) |
| `gear` | `g` | number | Vitesse (0=None, 1=P, 2=R, 3=N, 4=D) |
| `brake_pressed` | `bp` | bool | Frein appuyé |
| `locked` | `lk` | bool | Véhicule verrouillé |
| `battery_lv` | `blv` | number | Tension batterie 12V |
| `battery_hv` | `bhv` | number | Tension batterie HV |
| `odometer_km` | `odo` | number | Odomètre (km) |

#### Portes (`doors`)
| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `front_left` | `fl` | bool | Porte avant gauche |
| `front_right` | `fr` | bool | Porte avant droite |
| `rear_left` | `rl` | bool | Porte arrière gauche |
| `rear_right` | `rr` | bool | Porte arrière droite |
| `trunk` | `t` | bool | Coffre |
| `frunk` | `f` | bool | Frunk |
| `count_open` | `co` | number | Nombre de portes ouvertes |

#### Lumières (`lights`)
| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `headlights` | `h` | bool | Phares |
| `high_beams` | `hb` | bool | Feux de route |
| `fog_lights` | `fg` | bool | Feux de brouillard |
| `turn_left` | `tl` | bool | Clignotant gauche |
| `turn_right` | `tr` | bool | Clignotant droit |

#### Charge (`charge`)
| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `charging` | `ch` | bool | Charge en cours |
| `percent` | `pct` | number | État de charge (%) |
| `power_kw` | `pw` | number | Puissance de charge (kW) |

#### Sécurité (`safety`)
| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `night_mode` | `nm` | bool | Mode nuit actif |
| `brightness` | `br` | number | Luminosité détectée |
| `blindspot_left_lv1` | `bl1` | bool | Angle mort gauche niveau 1 |
| `blindspot_left_lv2` | `bl2` | bool | Angle mort gauche niveau 2 |
| `blindspot_right_lv1` | `br1` | bool | Angle mort droit niveau 1 |
| `blindspot_right_lv2` | `br2` | bool | Angle mort droit niveau 2 |

### Configuration LED (`/api/config`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `effect` | `fx` | string | ID de l'effet (ex: "RAINBOW") |
| `brightness` | `br` | number | Luminosité (0-255) |
| `speed` | `sp` | number | Vitesse de l'effet (0-255) |
| `color1` | `c1` | number | Couleur primaire (RGB décimal) |
| `color2` | `c2` | number | Couleur secondaire |
| `color3` | `c3` | number | Couleur tertiaire |
| `sync_mode` | `sm` | number | Mode de synchronisation |
| `reverse` | `rv` | bool | Sens inverse |
| `auto_night_mode` | `anm` | bool | Mode nuit automatique |
| `night_brightness` | `nbr` | number | Luminosité mode nuit (0-255) |
| `led_count` | `lc` | number | Nombre de LEDs |
| `data_pin` | `dp` | number | GPIO pin données |
| `strip_reverse` | `srv` | bool | Ruban inversé |

### Profils (`/api/profiles`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `id` | `id` | number | ID du profil (0-9) |
| `name` | `n` | string | Nom du profil |
| `active` | `ac` | bool | Profil actif |
| `audio_reactive` | `ar` | bool | Mode audio réactif |

### Audio (`/api/audio/status`, `/api/audio/data`)

#### Configuration
| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `enabled` | `en` | bool | Micro activé |
| `sensitivity` | `sen` | number | Sensibilité (0-255) |
| `gain` | `gn` | number | Gain (0-255) |
| `autoGain` | `ag` | bool | Gain automatique |
| `fftEnabled` | `ffe` | bool | FFT activée |

#### Données Audio
| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `amplitude` | `amp` | number | Amplitude audio |
| `bass` | `ba` | number | Niveau basses |
| `mid` | `md` | number | Niveau médiums |
| `treble` | `tr` | number | Niveau aigus |
| `bpm` | `bpm` | number | Battements par minute |
| `beatDetected` | `bd` | bool | Battement détecté |
| `available` | `av` | bool | Données disponibles |

### FFT (`/api/audio/fft/data`, `/api/audio/fft/status`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `enabled` | `en` | bool | FFT activée |
| `bands` | `bands` | array | Bandes FFT |
| `sampleRate` | `sr` | number | Taux d'échantillonnage |
| `fftSize` | `sz` | number | Taille FFT |
| `peakFreq` | `pf` | number | Fréquence de pic |
| `spectralCentroid` | `sc` | number | Centroïde spectral |
| `dominantBand` | `db` | number | Bande dominante |
| `bassEnergy` | `be` | number | Énergie basses |
| `midEnergy` | `me` | number | Énergie médiums |
| `trebleEnergy` | `te` | number | Énergie aigus |
| `kickDetected` | `kd` | bool | Kick détecté |
| `snareDetected` | `sd` | bool | Snare détecté |
| `vocalDetected` | `vd` | bool | Voix détectée |
| `available` | `av` | bool | Données disponibles |

### Événements CAN (`/api/events`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `event` | `ev` | string | ID de l'événement |
| `effect` | `fx` | string | ID de l'effet |
| `brightness` | `br` | number | Luminosité (0-255) |
| `speed` | `sp` | number | Vitesse (0-255) |
| `color` | `c1` | number | Couleur RGB |
| `duration` | `dur` | number | Durée (ms) |
| `priority` | `pri` | number | Priorité (0-255) |
| `enabled` | `en` | bool | Événement activé |
| `action_type` | `at` | number | Type d'action |
| `profile_id` | `pid` | number | ID profil cible |
| `can_switch_profile` | `csp` | bool | Peut changer de profil |

### Effets Disponibles (`/api/effects`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `id` | `id` | string | ID de l'effet |
| `name` | `n` | string | Nom de l'effet |
| `can_required` | `cr` | bool | Requiert données CAN |
| `audio_effect` | `ae` | bool | Effet audio |

### Types d'Événements (`/api/event-types`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `id` | `id` | string | ID du type |
| `name` | `n` | string | Nom du type |

### OTA (`/api/ota/info`)

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `version` | `v` | string | Version firmware |
| `state` | `st` | number | État OTA |
| `progress` | `pg` | number | Progression (%) |
| `written_size` | `ws` | number | Taille écrite (octets) |
| `total_size` | `ts` | number | Taille totale (octets) |
| `reboot_countdown` | `rc` | number | Compte à rebours redémarrage |
| `error` | `err` | string | Message d'erreur |

### Réponses API Génériques

| Clé longue | Clé courte | Type | Description |
|-----------|-----------|------|-------------|
| `status` | `st` | string | Statut ("ok" ou "error") |
| `message` | `msg` | string | Message de réponse |
| `success` | `ok` | bool | Succès de l'opération |
| `restart_required` | `rr` | bool | Redémarrage requis |
| `updated` | `upd` | number | Nombre d'éléments mis à jour |

## 📝 Exemples d'Utilisation

### Exemple 1 : Récupérer l'état du système

**Requête :**
```bash
GET /api/status
```

**Réponse (format optimisé) :**
```json
{
  "wc": true,
  "wip": "192.168.1.100",
  "cbr": true,
  "va": true,
  "pid": 0,
  "pn": "Default",
  "vehicle": {
    "s": 45.5,
    "g": 4,
    "bp": false,
    "lk": false,
    "doors": {
      "fl": false,
      "fr": false,
      "rl": false,
      "rr": false,
      "t": false,
      "f": false,
      "co": 0
    },
    "charge": {
      "ch": false,
      "pct": 85.5,
      "pw": 0
    }
  }
}
```

### Exemple 2 : Changer l'effet LED

**Requête :**
```bash
POST /api/effect
Content-Type: application/json

{
  "fx": "RAINBOW",
  "br": 200,
  "sp": 150,
  "c1": 16711680
}
```

**Réponse :**
```json
{
  "st": "ok"
}
```

### Exemple 3 : Configuration audio

**Requête :**
```bash
POST /api/audio/config
Content-Type: application/json

{
  "sen": 180,
  "gn": 150,
  "ag": true,
  "ffe": true
}
```

**Réponse :**
```json
{
  "ok": true
}
```

## 🔧 Migration depuis l'API Ancienne

Si vous utilisez l'ancienne API avec les clés longues, voici comment migrer :

### Script Python de Conversion

```python
# Mapping des clés
KEY_MAPPING = {
    'wifi_connected': 'wc',
    'effect': 'fx',
    'brightness': 'br',
    'speed': 'sp',
    # ... (voir tableau complet ci-dessus)
}

def convert_keys(data):
    """Convertir les clés longues en clés courtes"""
    if isinstance(data, dict):
        return {KEY_MAPPING.get(k, k): convert_keys(v) for k, v in data.items()}
    elif isinstance(data, list):
        return [convert_keys(item) for item in data]
    return data

# Utilisation
old_data = {"effect": "RAINBOW", "brightness": 200}
new_data = convert_keys(old_data)
print(new_data)  # {'fx': 'RAINBOW', 'br': 200}
```

### JavaScript de Conversion

```javascript
const KEY_MAPPING = {
    'wifi_connected': 'wc',
    'effect': 'fx',
    'brightness': 'br',
    'speed': 'sp',
    // ... (voir tableau complet ci-dessus)
};

function convertKeys(data) {
    if (typeof data !== 'object' || data === null) return data;
    if (Array.isArray(data)) return data.map(convertKeys);

    const result = {};
    for (const [key, value] of Object.entries(data)) {
        const newKey = KEY_MAPPING[key] || key;
        result[newKey] = convertKeys(value);
    }
    return result;
}

// Utilisation
const oldData = {effect: "RAINBOW", brightness: 200};
const newData = convertKeys(oldData);
console.log(newData);  // {fx: "RAINBOW", br: 200}
```

## 📚 Notes Importantes

1. **Compatibilité** : Cette version de l'API utilise uniquement les clés courtes. L'ancienne API avec clés longues n'est plus supportée.

2. **Client Web** : Le fichier `script.js` embarqué utilise automatiquement les clés courtes. Aucune modification n'est nécessaire.

3. **API Externe** : Si vous développez votre propre client (app mobile, script Python, etc.), utilisez ce document comme référence pour les clés JSON.

4. **Rétrocompatibilité** : Les versions firmware < v2.3.0 utilisent les clés longues. Vérifiez la version avec `GET /api/ota/info`.

5. **Performance** : L'utilisation des clés courtes est **obligatoire** et permet d'optimiser significativement les performances sur ESP32.

---

**Version du document** : v2.3.0
**Dernière mise à jour** : 2025-11-27
