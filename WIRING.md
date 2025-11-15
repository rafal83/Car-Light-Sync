# Guide de Câblage - Tesla Strip Controller

## ⚡ Schéma de connexion

### Configuration de base

```
ESP32 DevKit                    WS2812 LED Strip
┌─────────────┐                ┌──────────────┐
│             │                │              │
│         3V3 │────────────────│ VCC (3.3V)   │ ⚠️ Pour tests uniquement
│             │                │              │
│        GPIO5│────────────────│ DIN          │ Signal de données
│             │                │              │
│         GND │────────────────│ GND          │ Masse commune
│             │                │              │
└─────────────┘                └──────────────┘

⚠️ IMPORTANT: Pour un ruban complet, utilisez une alimentation 5V séparée!
```

### Configuration avec alimentation externe (RECOMMANDÉ)

```
ESP32 DevKit                    WS2812 LED Strip
┌─────────────┐                ┌──────────────┐
│             │                │              │
│        GPIO5│────────────────│ DIN          │
│             │                │              │
│         GND │────┐       ┌───│ GND          │
│             │    │       │   │              │
└─────────────┘    │       │   └──────────────┘
                   │       │            ▲
                   │       │            │
                   │       │    ┌───────┴──────┐
Alimentation 5V    │       │    │   5V / GND   │
┌─────────────┐    │       │    └──────────────┘
│      5V OUT │────┼───────┼────────────┘
│             │    │       │
│      GND    │────┴───────┘
└─────────────┘

Capacité recommandée: 1000µF sur l'alim 5V
Résistance optionnelle: 470Ω entre GPIO5 et DIN
```

## 🔌 Détails des connexions

### Pin ESP32

| Pin ESP32 | Fonction          | Note                           |
|-----------|-------------------|--------------------------------|
| GPIO5     | LED Data (DIN)    | Pin par défaut (configurable dans config.h) |
| 3V3       | Alimentation 3.3V | Max 500mA (pour tests courts uniquement) |
| 5V        | Alimentation 5V   | Depuis USB uniquement (max 2.5W) |
| GND       | Masse             | **Commune avec LED strip (obligatoire)** |

**Note:** Les autres GPIO sont réservés pour WiFi/Bluetooth et fonctionnalités futures.

### WS2812 LED Strip

| Pin LED   | Description       | Spécifications                 |
|-----------|-------------------|--------------------------------|
| VCC/5V    | Alimentation      | 5V DC, ~60mA par LED (blanc)   |
| DIN       | Signal de données | 3.3V-5V logic compatible       |
| GND       | Masse             | Commune avec ESP32             |
| DOUT      | Sortie données    | Pour chaîner plusieurs strips  |

## ⚙️ Calcul de l'alimentation

### Formule de base
```
Courant total = Nombre de LEDs × Courant par LED × Facteur d'utilisation

Exemple pour 60 LEDs:
- Blanc maximum: 60 × 60mA × 1.0 = 3.6A
- Effets colorés: 60 × 60mA × 0.6 = 2.16A (moyenne)
- Luminosité 50%: 60 × 60mA × 0.5 = 1.8A
```

### Recommandations d'alimentation

| Nombre de LEDs | Courant max | Alimentation recommandée    |
|----------------|-------------|------------------------------|
| 1-30           | 1.8A        | 5V 2A                        |
| 31-60          | 3.6A        | 5V 4A                        |
| 61-100         | 6.0A        | 5V 8A                        |
| 101-150        | 9.0A        | 5V 10A                       |
| 151-300        | 18A         | 5V 20A (injection multiple)  |

⚠️ **Toujours prévoir 20% de marge de sécurité**

## 🛡️ Protection et sécurité

### Composants recommandés

```
Circuit de protection complet:

ESP32 GPIO5 ────┬────[470Ω]────┬──── WS2812 DIN
                │               │
              [3.3V]          [TVS]
              Zener           Diode
                │               │
                └───────┬───────┘
                        │
                       GND

Alimentation 5V ────[1000µF]────┬──── VCC LED
                                 │
                    [10µF + 0.1µF] (près de chaque groupe de ~10 LEDs)
                                 │
                                GND
```

### Liste des composants de protection

1. **Résistance 470Ω** (optionnelle mais recommandée)
   - Rôle: Limiter le courant, protège contre les pics
   - Placement: Entre GPIO5 et DIN

2. **Condensateur 1000µF**
   - Rôle: Stabilise l'alimentation 5V
   - Placement: Au plus près de l'entrée d'alimentation
   - Voltage rating: 16V minimum

3. **Condensateurs de découplage (100nF + 10µF)**
   - Rôle: Filtrage local
   - Placement: Tous les 10-15 LEDs le long du strip

4. **Diode TVS (optionnelle)**
   - Rôle: Protection contre les surtensions
   - Modèle: SMBJ5.0A ou équivalent
   - Placement: Entre DIN et GND

5. **Diode Schottky 1N5819** (si alimentation USB)
   - Rôle: Protection contre retour de courant
   - Placement: Entre 5V USB et VCC strip

## 🔧 Installation physique

### Montage dans un véhicule Tesla

#### Emplacements recommandés

1. **Coffre arrière**
   ```
   - Avantages: Facile d'accès, grande surface
   - Connexion: Câblage via passage de roue
   - Fixation: Ruban adhésif 3M VHB ou profilé aluminium
   ```

2. **Sous-caisse (footwell)**
   ```
   - Avantages: Effet d'éclairage ambiant
   - Connexion: Passage sous les sièges
   - Protection: Gaine thermorétractable conseillée
   ```

3. **Compartiment frunk**
   ```
   - Avantages: Visible lors de l'ouverture
   - Connexion: Câblage le long du capot
   - Attention: Températures plus élevées
   ```

#### Fixation du strip LED

**Méthode 1: Adhésif double-face (par défaut)**
- Nettoyer la surface (alcool isopropylique)
- Chauffer légèrement l'adhésif (sèche-cheveux)
- Appuyer fermement pendant 30 secondes
- Laisser reposer 24h avant utilisation

**Méthode 2: Profilé aluminium (recommandé)**
- Avantages: Meilleure dissipation thermique, aspect professionnel
- Fixation: Vis ou adhésif VHB
- Diffuseur: Optionnel pour effet plus doux

**Méthode 3: Clips de fixation**
- Avantages: Amovible, pas de résidu
- Inconvénients: Moins discret
- Utilisation: Tests ou installation temporaire

### Câblage du Commander Panda

```
Commander Panda
┌─────────────────┐
│                 │
│   WiFi Module   │ ──── Connexion sans fil
│                 │       SSID: panda-XXXXX
│   CAN Interface │ ──── Vers bus CAN Tesla
│                 │
└─────────────────┘

ESP32 (Tesla Strip)
┌─────────────────┐
│                 │
│   WiFi Client   │ ──── Se connecte au Panda
│                 │       Port TCP: 1338
└─────────────────┘
```

### Connexion à la batterie 12V (optionnel)

⚠️ **Pour utilisateurs avancés uniquement**

```
Batterie 12V Tesla ────[Fusible 5A]────[Buck Converter]──── ESP32 5V
                                         (12V → 5V 3A)
                                                │
                                               GND
```

**Buck converter recommandé:**
- LM2596 ou équivalent
- Entrée: 7-35V DC
- Sortie: 5V 3A
- Protection: Court-circuit, surchauffe

## 🧪 Tests et validation

### Checklist de connexion

- [ ] Masse commune entre ESP32 et LED strip
- [ ] Tension d'alimentation LED = 5V ±5%
- [ ] Signal de données connecté à GPIO5 (ou pin configuré)
- [ ] Condensateur de filtrage installé
- [ ] Pas de court-circuit visible
- [ ] Polarité respectée (VCC/GND)

### Procédure de test

1. **Test de l'alimentation**
   ```
   - Mesurer tension 5V sans charge: 4.9-5.1V
   - Vérifier masse commune
   - Tester avec multimètre
   ```

2. **Test du signal**
   ```
   - Uploader le code avec effet de test
   - Observer si première LED s'allume
   - Vérifier propagation sur tout le strip
   ```

3. **Test de charge**
   ```
   - Activer blanc 100%
   - Mesurer courant total
   - Vérifier stabilité tension
   - Surveiller température ESP32 et alim
   ```

### Diagnostic des problèmes courants

| Problème | Cause probable | Solution |
|----------|----------------|----------|
| Aucune LED ne s'allume | Pas d'alimentation | Vérifier 5V et GND |
| | Signal incorrect | Vérifier GPIO et câblage DIN |
| Première LED OK, autres non | Strip défectueux | Tester continuité DOUT→DIN |
| | Problème d'alimentation | Ajouter injection de courant |
| Couleurs incorrectes | Ordre RGB/GRB | Modifier COLOR_ORDER config |
| Scintillement | Alimentation insuffisante | Augmenter capacité alim |
| | Câble signal trop long | Ajouter résistance 470Ω |
| LEDs s'éteignent aléatoirement | Drop de tension | Réduire longueur ou ajouter injection |
| | Surchauffe | Améliorer ventilation |

## 📐 Configuration Avancée

### ⚠️ Fonctionnalités Non Implémentées

Les fonctionnalités suivantes ne sont **pas encore supportées** dans la version actuelle:

❌ **Multi-strips** : Un seul strip LED supporté (GPIO5)
❌ **Capteurs additionnels** : Pas de support DHT22/autres capteurs
❌ **Multiples GPIO LED** : Seul GPIO5 est configuré

Ces fonctionnalités sont prévues pour les versions futures. Consultez la [Roadmap](README.md#-roadmap) pour plus d'informations.

## 🎨 Exemples de Montage (Single Strip)

### Configuration Standard: Strip Unique

```
┌────────────────────────────────┐
│                                 │
│         Habitacle Tesla         │
│                                 │
│   [====== LED Strip 60-94 ======] │ ← Coffre arrière
│                                 │
└────────────────────────────────┘
```

**Emplacements recommandés pour un strip unique:**
1. **Coffre arrière** (recommandé) : Facile d'accès, grande surface
2. **Sous-caisse (footwell)** : Éclairage ambiant
3. **Contour plafond** : Éclairage indirect

**Longueur conseillée:** 60-94 LEDs (environ 1-1.5 mètres)

**Note:** La configuration multi-strips n'est pas encore supportée. Un seul strip LED peut être connecté à GPIO5.

## 📏 Longueurs de câble recommandées

| Connexion | Longueur max | Type de câble |
|-----------|--------------|---------------|
| ESP32 → 1ère LED | 2m | AWG22-24 blindé |
| Entre groupes LEDs | 5m | AWG18-20 |
| Alimentation | 1m par section | AWG14-16 |
| Commander → ESP32 | N/A (WiFi) | - |

## 🔐 Sécurité

### ⚠️ Avertissements importants

1. **Ne jamais connecter/déconnecter** le strip LED lorsqu'il est alimenté
2. **Respecter la polarité** - Inversion = destruction possible
3. **Ne pas dépasser** la puissance de l'alimentation USB (2.5W)
4. **Isoler les connexions** - Utiliser gaine thermorétractable
5. **Tester à faible luminosité** d'abord avant montage final
6. **Ne pas bloquer** la ventilation de l'ESP32

### Conformité véhicule

- Vérifier la réglementation locale sur les LED dans les véhicules
- Ne pas interférer avec les systèmes de sécurité
- Installation réversible recommandée
- Pas d'obstruction de la visibilité

---

Pour toute question sur le câblage, consultez le README principal ou ouvrez une issue sur GitHub.
