# Guide de Migration vers v2.3.0

Ce guide vous aide à migrer vers la version 2.3.0 qui introduit l'optimisation JSON avec clés courtes.

## 🎯 Changements Principaux

La version 2.3.0 remplace toutes les clés JSON longues par des clés courtes pour optimiser les performances et réduire la taille des réponses API.

### Exemple de Changement

**Avant (v2.2.0) :**
```json
{
  "wifi_connected": true,
  "effect": "RAINBOW",
  "brightness": 200,
  "speed": 150
}
```

**Après (v2.3.0) :**
```json
{
  "wc": true,
  "fx": "RAINBOW",
  "br": 200,
  "sp": 150
}
```

## 🔄 Qui est Concerné ?

### ✅ Pas d'Action Nécessaire

Vous n'avez **rien à faire** si vous utilisez :
- L'interface web embarquée (`http://192.168.10.1`)
- L'application mobile officielle
- Aucune intégration API externe

L'interface web et l'app mobile sont automatiquement mises à jour avec le nouveau firmware.

### ⚠️ Action Requise

Vous devez **mettre à jour votre code** si vous avez :
- Un script Python qui utilise l'API REST
- Une application mobile personnalisée
- Un service externe qui interroge l'API
- Des tests automatisés qui vérifient les réponses JSON

## 📝 Étapes de Migration

### 1. Mettre à Jour le Firmware ESP32

```bash
# Via PlatformIO
pio run -e esp32s3 -t upload

# Via OTA (interface web)
# Onglet Mise à jour > Upload firmware.bin
```

### 2. Vérifier la Version

```bash
curl http://192.168.10.1/api/ota/info

# Réponse avec v2.3.0+ :
{
  "v": "2.3.0",    # Clé courte "v" au lieu de "version"
  ...
}
```

### 3. Mettre à Jour Votre Code Client

#### Option A : Utiliser le Mapping de Conversion

Créez un dictionnaire de conversion dans votre code :

**Python :**
```python
KEY_MAPPING = {
    'wc': 'wifi_connected',
    'fx': 'effect',
    'br': 'brightness',
    'sp': 'speed',
    'c1': 'color1',
    # ... voir JSON_API_REFERENCE.md pour la liste complète
}

def convert_to_long_keys(data):
    """Convertir les clés courtes en clés longues"""
    if isinstance(data, dict):
        return {KEY_MAPPING.get(k, k): convert_to_long_keys(v)
                for k, v in data.items()}
    elif isinstance(data, list):
        return [convert_to_long_keys(item) for item in data]
    return data

# Utilisation
response = requests.get('http://192.168.10.1/api/status')
data = response.json()
data_with_long_keys = convert_to_long_keys(data)
```

**JavaScript/TypeScript :**
```javascript
const KEY_MAPPING = {
    'wc': 'wifi_connected',
    'fx': 'effect',
    'br': 'brightness',
    'sp': 'speed',
    'c1': 'color1',
    // ... voir JSON_API_REFERENCE.md pour la liste complète
};

function convertToLongKeys(data) {
    if (typeof data !== 'object' || data === null) return data;
    if (Array.isArray(data)) return data.map(convertToLongKeys);

    const result = {};
    for (const [key, value] of Object.entries(data)) {
        const newKey = KEY_MAPPING[key] || key;
        result[newKey] = convertToLongKeys(value);
    }
    return result;
}

// Utilisation
const response = await fetch('http://192.168.10.1/api/status');
const data = await response.json();
const dataWithLongKeys = convertToLongKeys(data);
```

#### Option B : Adapter Directement Votre Code

Modifiez votre code pour utiliser directement les clés courtes :

**Avant :**
```python
if data['wifi_connected']:
    effect = data['effect']
    brightness = data['brightness']
```

**Après :**
```python
if data['wc']:
    effect = data['fx']
    brightness = data['br']
```

### 4. Mettre à Jour les Requêtes POST

Les requêtes POST doivent également utiliser les clés courtes :

**Avant :**
```python
requests.post('http://192.168.10.1/api/effect', json={
    'effect': 'RAINBOW',
    'brightness': 200,
    'speed': 150,
    'color1': 16711680
})
```

**Après :**
```python
requests.post('http://192.168.10.1/api/effect', json={
    'fx': 'RAINBOW',
    'br': 200,
    'sp': 150,
    'c1': 16711680
})
```

## 📋 Checklist de Migration

- [ ] Firmware ESP32 mis à jour vers v2.3.0+
- [ ] Version vérifiée via `/api/ota/info`
- [ ] Code client mis à jour (scripts Python, apps, etc.)
- [ ] Tests effectués sur toutes les API utilisées
- [ ] Documentation interne mise à jour
- [ ] Équipe informée des changements

## 🔍 Référence Complète

Pour le mapping complet de toutes les clés JSON, consultez :
**[docs/JSON_API_REFERENCE.md](JSON_API_REFERENCE.md)**

## 🆘 Résolution de Problèmes

### Erreur : Clés JSON manquantes

**Symptôme :**
```python
KeyError: 'wifi_connected'
```

**Solution :**
Votre code utilise encore les anciennes clés. Utilisez les clés courtes ou implémentez la conversion.

### Erreur : Requête API échouée

**Symptôme :**
```
HTTP 400 Bad Request
```

**Solution :**
Vérifiez que vos requêtes POST utilisent les clés courtes dans le payload JSON.

### Comment vérifier la compatibilité ?

```bash
# Test rapide de l'API
curl http://192.168.10.1/api/status | python -m json.tool

# Si vous voyez "wc", "fx", "br" -> v2.3.0+ ✅
# Si vous voyez "wifi_connected", "effect" -> v2.2.0 ou antérieur ❌
```

## 💡 Recommandations

1. **Testez d'abord en environnement de dev** : Ne déployez pas directement en production
2. **Gardez une version de backup** : Conservez le firmware v2.2.0 en cas de besoin
3. **Utilisez la conversion automatique** : Pendant la transition, utilisez le mapping de clés
4. **Migrez progressivement** : Commencez par un seul script/service avant de tout migrer

## 📞 Support

En cas de problème :
1. Consultez [JSON_API_REFERENCE.md](JSON_API_REFERENCE.md) pour la liste complète des clés
2. Vérifiez le [CHANGELOG.md](../CHANGELOG.md) pour les détails de la v2.3.0
3. Ouvrez une issue sur GitHub avec votre configuration

---

**Version du guide** : v2.3.0
**Dernière mise à jour** : 2025-11-27
