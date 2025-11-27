# Nettoyage du code mort

## Fonctions supprimées

Suite à une analyse approfondie du code, **6 fonctions inutilisées** ont été identifiées et supprimées :

### 1. `toggleLanguage()` (ligne 774)
```javascript
function toggleLanguage() {
    setLanguage(currentLang === 'fr' ? 'en' : 'fr');
}
```
**Raison:** Non utilisée, la sélection de langue se fait via `<select>` dans le HTML

---

### 2. `applyEffect()` (lignes 1632-1655)
```javascript
async function applyEffect() {
    const effectId = parseInt($('effect-select').value);
    // ... 24 lignes ...
}
```
**Raison:** Ancienne fonction remplacée par le système de profils et d'effets par défaut

---

### 3. `saveConfig()` (lignes 1657-1665)
```javascript
async function saveConfig() {
    await fetch(API_BASE + '/api/save', { method: 'POST' });
    // ...
}
```
**Raison:** Confusion avec `saveAudioConfig()`, jamais appelée

---

### 4. `assignEventEffect()` (lignes 1667-1688)
```javascript
async function assignEventEffect() {
    const data = { event, effect, duration, priority, ... };
    // ... 22 lignes ...
}
```
**Raison:** Remplacée par le nouveau système de configuration d'événements dans `eventsConfig`

---

### 5. `simulateEvent(eventType)` (lignes 2365-2388)
```javascript
async function simulateEvent(eventType) {
    showNotification(...);
    await fetch(API_BASE + '/api/simulate/event', ...);
    // ... 24 lignes ...
}
```
**Raison:** Remplacée par `toggleEvent()` qui gère mieux les états on/off

---

### 6. `stopEffect()` (lignes 2390-2407)
```javascript
async function stopEffect() {
    await fetch(API_BASE + '/api/effect', {
        method: 'POST',
        body: JSON.stringify({ effect: 0, brightness: 0, ... })
    });
    // ... 18 lignes ...
}
```
**Raison:** Jamais utilisée, fonctionnalité intégrée dans `toggleEvent()`

---

## Statistiques

### Avant nettoyage
- Lignes de code: **2673**
- Taille compressée: **20458 octets**

### Après nettoyage
- Lignes de code: **2577** (-96 lignes, **-3.6%**)
- Taille compressée: **20047 octets** (-411 octets, **-2.0%**)

### Gain total
- ✅ **96 lignes** de code mort supprimées
- ✅ **411 octets** économisés après compression
- ✅ Code plus maintenable (moins de confusion)
- ✅ Réduction de la surface d'attaque potentielle

---

## Méthode de détection

Script bash utilisé pour détecter les fonctions mortes :

```bash
# Extraire toutes les fonctions
grep -E "^(async )?function [a-zA-Z_]" script.js | \
  sed -E 's/^(async )?function ([a-zA-Z_]*).*/\2/' | \
  sort -u > all_functions.txt

# Pour chaque fonction
while read func; do
  # Compter les occurrences dans .js et .html
  count=$(grep -oE "\b$func\s*\(" script.js index.html | wc -l)
  # Si seulement 1 = définition uniquement = non utilisée
  [ "$count" -eq 1 ] && echo "❌ $func - NON UTILISÉE"
done < all_functions.txt
```

---

## Opportunités futures

D'autres optimisations possibles (non implémentées pour éviter la sur-ingénierie) :

1. **Helper pour fetch JSON** (14 occurrences)
   ```javascript
   const postJSON = (url, data) => fetch(API_BASE + url, {
       method: 'POST',
       headers: { 'Content-Type': 'application/json' },
       body: JSON.stringify(data)
   });
   ```
   Gain estimé: ~200 octets

2. **Factorisation try/catch** (32 blocks)
   Mais risque de complexifier la gestion d'erreurs

3. **Suppression des commentaires**
   Inutile car gzip les compresse déjà très bien
