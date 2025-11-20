# Guide de Câblage - Tesla Strip Controller

## ⚡ Schéma de Connexion Complet

### Configuration Complète

```
ESP32 DevKit S3                WS2812 LED Strip           CAN Transceiver         Bus CAN Tesla
┌──────────────────┐          ┌───────────────┐          ┌───────────────┐       ┌──────────┐
│                  │          │               │          │               │       │          │
│            GPIO5 │─────────►│ DIN           │          │               │       │          │
│                  │          │               │          │               │       │          │
│           GPIO38 │──────────┼───────────────┼─────────►│ TX            │       │          │
│                  │          │               │          │               │       │          │
│           GPIO39 │◄─────────┼───────────────┼──────────│ RX            │       │          │
│                  │          │               │          │               │       │          │
│             3V3  │──────┬───┼───────────────┼─────────►│ VCC           │       │          │
│                  │      │   │               │          │               │       │          │
│             GND  │──────┼───┼──────────┐    └──────────┤          CAN_H├──────►│ CAN_H    │
│                  │      │   │          │    ┌──────────┤          CAN_L├──────►│ CAN_L    │
└──────────────────┘      │   │          └────┘          │           GND │       │ GND      │
                          │   │                           └───────────────┘       └──────────┘
                          │   └──────────┐
                          │              │
     ┌────────────────────┴──────────────┴──────────┐
     │        Alimentation 5V (2-10A)                │
     │  ┌───────────┐                                │
     │  │ 5V OUT    │───────────────────────────────►│ WS2812 VCC
     │  │ GND       │───────────────────────────────►│ WS2812 GND + ESP32 GND
     │  └───────────┘                                │
     └───────────────────────────────────────────────┘

Composants de protection:
- Condensateur 1000µF sur 5V (entrée LED strip)
- Résistance 470Ω entre GPIO5 et DIN (optionnel)
- Résistance de terminaison 120Ω sur bus CAN (si nécessaire)
```

## 🔌 Détails des Connexions

### 1. Connexion LED Strip (WS2812)

| Pin ESP32 | Pin LED Strip | Note                           |
|-----------|---------------|--------------------------------|
| GPIO5     | DIN           | Signal de données LED (configurable dans [config.h](include/config.h)) |
| GND       | GND           | **Masse commune obligatoire** |

**Alimentation des LEDs :**
- **VCC LED** → Alimentation 5V externe (PAS depuis ESP32)
- **Capacité** : 1000µF entre VCC et GND (près de l'entrée du strip)
- **Résistance** : 470Ω entre GPIO5 et DIN (optionnel, protection signal)

### 2. Connexion Module CAN

| Pin ESP32 | Pin Transceiver | Description                    |
|-----------|-----------------|--------------------------------|
| GPIO38    | TX              | Transmission vers transceiver (configurable dans [can_bus.c](main/can_bus.c)) |
| GPIO39    | RX              | Réception depuis transceiver (configurable) |
| 3V3       | VCC             | Alimentation 3.3V              |
| GND       | GND             | Masse commune                  |

| Pin Transceiver | Bus CAN Tesla | Description                    |
|-----------------|---------------|--------------------------------|
| CAN_H           | Pin 6 OBD-II  | Signal CAN High                |
| CAN_L           | Pin 14 OBD-II | Signal CAN Low                 |
| GND             | Pin 4/5 OBD-II| Masse commune                  |

**Transceivers CAN recommandés :**
- **SN65HVD230** : 3.3V, faible consommation (~10mA)
- **MCP2551** : 5V, plus robuste (nécessite un level shifter 5V↔3.3V si utilisé directement)
- **TJA1050** : 5V, haute fiabilité industrielle

⚠️ **Utiliser un transceiver 3.3V** (SN65HVD230) ou **ajouter un level shifter** pour les transceivers 5V.

### 3. Alimentation

#### Option A : Alimentation USB + Externe (Recommandée)

```
USB 5V (ESP32) ────► ESP32 DevKit (alimentation uniquement)

Alimentation 5V ────► WS2812 Strip VCC (2-10A selon nb de LEDs)
externe (DC)         + GND commun avec ESP32
```

#### Option B : Alimentation Unique 5V

```
Alimentation 5V ────┬────► ESP32 VIN (via régulateur interne)
(3-10A)             │
                    └────► WS2812 Strip VCC
                    └────► GND commun
```

### Calcul de l'Alimentation

| Nombre de LEDs | Courant max | Alimentation recommandée    |
|----------------|-------------|------------------------------|
| 1-30           | 1.8A        | 5V 2A                        |
| 31-60          | 3.6A        | 5V 4A                        |
| 61-94          | 5.6A        | 5V 6A                        |
| 95-150         | 9.0A        | 5V 10A                       |

**Formule :**
```
Courant max = Nombre de LEDs × 60mA × Facteur d'utilisation (0.6-1.0)
```

⚠️ **Toujours prévoir 20% de marge de sécurité**

## 🚗 Connexion au Bus CAN Tesla

### Accès via le Port OBD-II

Le moyen le plus simple d'accéder au bus CAN est via le port OBD-II :

```
      Port OBD-II (16 pins)
   ┌─────────────────────┐
   │  8  7  6  5  4  3  2  1  │
   │ 16 15 14 13 12 11 10 9  │
   └─────────────────────┘

Pins utilisés:
- Pin 6  : CAN_H (Chassis, 500 kbit/s)
- Pin 14 : CAN_L (Chassis, 500 kbit/s)
- Pin 4  : GND Chassis
- Pin 5  : GND Signal
```

### Câble OBD-II Custom

Vous pouvez créer un câble OBD-II custom :

**Matériel nécessaire :**
- Connecteur OBD-II mâle (16 pins)
- Câble 4 conducteurs blindé
- Connecteur Dupont ou JST pour connexion au transceiver

**Connexions :**
```
OBD-II Pin 6 (CAN_H)   → Fil Rouge    → Transceiver CAN_H
OBD-II Pin 14 (CAN_L)  → Fil Jaune   → Transceiver CAN_L
OBD-II Pin 4 ou 5 (GND) → Fil Noir    → Transceiver GND
```

### Alternative : Connexion Interne

Pour une installation permanente, vous pouvez vous connecter directement aux bus CAN internes :

**Model 3 / Model Y :**
- Derrière l'écran central : Connecteur du contrôleur de carrosserie
- Sous le siège conducteur : Faisceau CAN Chassis

**Model S / Model X :**
- Sous le siège conducteur : Faisceau CAN Gateway
- Dans le coffre avant : Connecteur BCM (Body Control Module)

⚠️ **Attention** : Intervention sur les connecteurs internes nécessite des connaissances avancées. Privilégier le port OBD-II pour une installation non invasive.

## 🛡️ Protection et Sécurité

### Composants de Protection Recommandés

**1. Protection Alimentation LED**
```
Alimentation 5V ─┬─[1000µF]─┬─► VCC LED Strip
                 │           │
                [Fusible 5A] │
                 │           │
                GND          └─► Condensateurs de découplage
                                 (100nF + 10µF tous les 10-15 LEDs)
```

**2. Protection Signal LED**
```
ESP32 GPIO5 ───[470Ω]───► WS2812 DIN

Optionnel: Diode Zener 3.3V entre DIN et GND
```

**3. Protection Bus CAN**
```
Transceiver ───[120Ω]─┬─ CAN_H
                       │
                       └─ CAN_L

Note: La résistance de terminaison 120Ω est déjà présente
dans la plupart des véhicules. Ajouter seulement si nécessaire.
```

### Liste des Composants

| Composant | Quantité | Spécifications | Rôle |
|-----------|----------|----------------|------|
| Condensateur électrolytique | 1 | 1000µF, 16V | Stabilisation alimentation 5V |
| Condensateurs céramiques | 5-10 | 100nF | Découplage local |
| Condensateurs électrolytiques | 5-10 | 10µF, 16V | Filtrage local |
| Résistance | 1 | 470Ω, 1/4W | Protection signal LED |
| Fusible | 1 | 5-10A selon LEDs | Protection surcharge |
| Résistance de terminaison | 1* | 120Ω, 1/4W | Terminaison bus CAN (si nécessaire) |

*Généralement pas nécessaire car déjà présente dans le véhicule

## 🔧 Installation Physique dans le Véhicule

### Emplacements Recommandés

#### 1. Coffre Arrière (Recommandé)
- **Avantages** : Facile d'accès, grande surface, bonne dissipation
- **LED Strip** : Le long du rebord intérieur du coffre
- **ESP32 + Alim** : Fixé sur le côté, protégé des chocs
- **Accès CAN** : Câble vers port OBD-II (passage sous tapis)

#### 2. Sous-Caisse (Footwell)
- **Avantages** : Effet d'éclairage ambiant, discret
- **LED Strip** : Sous les sièges avant et arrière
- **ESP32 + Alim** : Sous le siège conducteur
- **Accès CAN** : Accès direct au port OBD-II

#### 3. Compartiment Frunk
- **Avantages** : Visible lors de l'ouverture, effet spectaculaire
- **LED Strip** : Pourtour du frunk
- **Attention** : Températures plus élevées en été

### Fixation du LED Strip

**Méthode 1 : Adhésif 3M VHB (Par défaut)**
1. Nettoyer la surface (alcool isopropylique)
2. Chauffer légèrement l'adhésif (sèche-cheveux, 30 secondes)
3. Appliquer le strip et presser fermement (30 secondes)
4. Laisser reposer 24h avant utilisation

**Méthode 2 : Profilé Aluminium (Recommandé pour >60 LEDs)**
- **Avantages** : Meilleure dissipation thermique, aspect professionnel, diffusion homogène
- **Fixation** : Vis ou adhésif VHB sur le profilé
- **Diffuseur** : Couvercle translucide pour effet plus doux

**Méthode 3 : Clips de Fixation**
- **Avantages** : Amovible, pas de résidu, idéal pour tests
- **Inconvénients** : Moins discret, peut vibrer

### Protection de l'Installation

**Gaine Thermorétractable :**
- Protéger toutes les soudures et connexions
- Diamètre adapté au câblage (2-5mm)

**Boîtier pour ESP32 :**
- Boîtier IP54 minimum pour protection contre poussière et humidité
- Ventilation suffisante pour dissipation thermique
- Accès USB pour programmation

**Câblage :**
- Câbles souples résistants à la température (-20°C à +85°C)
- Gaine tressée pour protection mécanique
- Serre-câbles pour organisation

## 🧪 Procédure de Test

### Étape 1 : Test Bench (Hors Véhicule)

**1.1 Test Alimentation**
```
[ ] Mesurer tension 5V sans charge : 4.9-5.1V
[ ] Vérifier masse commune ESP32 ↔ LEDs
[ ] Vérifier tension 3.3V sur ESP32
```

**1.2 Test LED Strip**
```
[ ] Uploader code avec effet de test (Solid blanc)
[ ] Vérifier que première LED s'allume
[ ] Vérifier propagation sur tout le strip
[ ] Tester plusieurs effets (Rainbow, Breathing)
```

**1.3 Test Transceiver CAN**
```
[ ] Vérifier alimentation 3.3V sur transceiver
[ ] Vérifier connexions TX/RX GPIO38/39
[ ] Brancher analyseur CAN ou loopback pour test
```

### Étape 2 : Test dans le Véhicule

**2.1 Connexion au Bus CAN**
```
[ ] Véhicule à l'arrêt, contact OFF
[ ] Brancher câble OBD-II avec transceiver
[ ] Mettre le contact (accessoires ON, pas de démarrage)
[ ] Vérifier logs série : "Bus CAN démarré"
[ ] Vérifier logs : "CAN frame received: ID=0x..."
```

**2.2 Test des Événements**
```
[ ] Activer clignotant gauche → Animation orange
[ ] Activer clignotant droit → Animation orange
[ ] Ouvrir une porte → Effet défini
[ ] Appuyer sur frein → Effet défini
[ ] Brancher charge (si possible) → Animation de charge
```

**2.3 Test Interface Web**
```
[ ] Se connecter au WiFi "Tesla-Strip"
[ ] Ouvrir http://192.168.4.1
[ ] Vérifier affichage état véhicule en temps réel
[ ] Tester changement d'effet
[ ] Tester création/activation de profil
```

### Étape 3 : Test Longue Durée

```
[ ] Laisser tourner 1 heure → Vérifier température ESP32 (<70°C)
[ ] Vérifier stabilité effets LED
[ ] Vérifier pas de reboot ESP32 (logs série)
[ ] Vérifier consommation courant dans spec
```

## 🐛 Diagnostic des Problèmes

### Problème : LEDs ne s'allument pas

| Cause possible | Vérification | Solution |
|----------------|--------------|----------|
| Pas d'alimentation 5V | Multimètre sur VCC/GND | Vérifier alimentation et connexions |
| Signal incorrect | Oscilloscope sur DIN | Vérifier GPIO5 et résistance 470Ω |
| LEDs défectueuses | Tester avec strip différent | Remplacer strip ou section défectueuse |
| Mauvaise config | Vérifier NUM_LEDS, LED_PIN | Ajuster config.h et recompiler |

### Problème : Messages CAN non reçus

| Cause possible | Vérification | Solution |
|----------------|--------------|----------|
| Câblage CAN incorrect | Vérifier CAN_H/CAN_L | Inverser ou reconnecter |
| GPIO incorrect | Vérifier GPIO38/39 | Ajuster dans can_bus.c |
| Transceiver non alimenté | Mesurer 3.3V sur VCC | Vérifier connexion 3.3V |
| Mauvaise vitesse CAN | 500 kbit/s | Vérifier config dans can_bus.c |
| Bus CAN en erreur | Vérifier terminaison | Ajouter résistance 120Ω si nécessaire |

### Problème : Scintillement des LEDs

| Cause possible | Vérification | Solution |
|----------------|--------------|----------|
| Drop de tension | Mesurer tension sous charge | Augmenter capacité alimentation |
| Câble signal trop long | Longueur GPIO5→DIN | Ajouter résistance 470Ω ou réduire longueur |
| Alimentation insuffisante | Mesurer courant max | Utiliser alimentation plus puissante |
| Interférences | Proximité moteurs/WiFi | Ajouter ferrite sur câble ou blindage |

### Problème : ESP32 Redémarre

| Cause possible | Vérification | Solution |
|----------------|--------------|----------|
| Drop de tension 3.3V | Mesurer tension 3.3V | Ajouter condensateur 100µF près ESP32 |
| Surcharge WiFi | Désactiver temporairement | Réduire nombre de clients ou requêtes |
| Stack overflow | Vérifier logs série | Mettre à jour firmware (v2.1+) |
| Température excessive | Mesurer température | Améliorer ventilation boîtier |

## 📐 Schémas Électriques Détaillés

### Schéma Complet avec Protection

```
                                Protection & Filtrage
                          ┌──────────────────────────┐
                          │                          │
USB 5V ───[Diode]─────┬───┤ ESP32-S3 DevKit         │
                      │   │                          │
Alim 5V ──[Fusible]───┼───┤ VIN         GPIO5  ├────[470Ω]───► WS2812 DIN
          5-10A       │   │                          │
                      │   │ 3V3         GPIO38 ├──────────────► CAN TX
                   [1000µF]│                          │
                      │   │ GND         GPIO39 ├◄─────────────  CAN RX
                      │   │                          │
                     GND  └──────────────────────────┘
                      │                          │
                      └──────┬───────────────────┘
                             │
                          Masse Commune

LED Strip:
VCC ──[1000µF]──┬─ Alimentation 5V
                │
GND ────────────┴─ Masse Commune
```

## 🔐 Sécurité et Conformité

### Avertissements Importants

⚠️ **Électrique :**
- Ne jamais brancher/débrancher sous tension
- Respecter les polarités (destruction possible)
- Isoler toutes les connexions (gaine thermorétractable)
- Fusible obligatoire sur alimentation principale

⚠️ **Véhicule :**
- Installation réversible recommandée
- Ne pas obstruer airbags ou systèmes de sécurité
- Ne pas surcharger le circuit 12V du véhicule
- Vérifier réglementation locale sur LEDs dans véhicules

⚠️ **Bus CAN :**
- Connexion en parallèle uniquement (non invasive)
- Ne jamais interrompre le bus CAN existant
- Pas de modification des messages CAN (lecture seule)
- Déconnecter lors de mises à jour véhicule (service Tesla)

### Conformité Véhicule

- **Réglementation** : Vérifier les lois locales sur éclairage véhicule
- **Homologation** : Pas d'éclairage visible de l'extérieur pendant conduite
- **Garantie** : Installation non invasive ne devrait pas affecter garantie
- **Assurance** : Informer assureur si installation permanente

---

Pour toute question sur le câblage, consultez le [README principal](README.md) ou ouvrez une issue sur GitHub.

**Version :** 2.2.0
**Date :** 2025-11-20
