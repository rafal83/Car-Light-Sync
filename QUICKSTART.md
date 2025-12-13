# 🚀 Guide de Démarrage Rapide - Car Light Sync

## Installation en 5 Minutes

### 1. Matériel Requis ✅
- [ ] ESP32 DevKit (ESP32-S3 recommandé)
- [ ] Ruban LED WS2812 (60-94 LEDs recommandé)
- [ ] Alimentation 5V 3-6A minimum
- [ ] Transceiver CAN (SN65HVD230 ou MCP2551)
- [ ] Câble OBD-II ou câbles de connexion
- [ ] Véhicule compatible (Tesla Model 3, Y, S, X ou autre véhicule avec bus CAN)

### 2. Câblage Rapide ⚡

```
ESP32 GPIO5  ──────► WS2812 DIN
ESP32 GND    ──────► WS2812 GND + Alim GND
Alim 5V      ──────► WS2812 VCC
```

**⚠️ Important:** Masse commune obligatoire !

### 3. Compilation & Flash 💾

#### Option A: PlatformIO (Recommandé)
```bash
cd car-light-sync
pio run -t upload
pio device monitor
```

#### Option B: ESP-IDF
```bash
cd car-light-sync
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

#### Option C: Script automatique
```bash
./car-light-sync.sh all
```

### 4. Configuration Initiale 🔧

#### 4.1 Connexion WiFi
1. Chercher le réseau **CarLightSync**
2. Se connecter sans mot de passe
3. Ouvrir http://192.168.4.1

#### 4.2 Vérification Bus CAN
1. Brancher le câble OBD-II avec le transceiver CAN connecté à l'ESP32
2. Mettre le contact du véhicule (accessoires ON)
3. Ouvrir l'interface web, section "État CAN Bus"
4. Vérifier que le statut affiche "Messages CAN reçus" ✅
5. Vérifier que les données véhicule (vitesse, portes, etc.) sont affichées en temps réel

### 5. Premier Profil 🎨

#### 5.1 Créer un Profil
1. Section "Gestion des Profils"
2. Cliquer sur "Nouveau"
3. Entrer le nom : "Mon Premier Profil"
4. Cliquer sur "Créer"

#### 5.2 Configurer l'Effet par Défaut
1. Section "Effet par Défaut"
2. Choisir "Arc-en-ciel" dans la liste
3. Luminosité : 150
4. Vitesse : 80
5. Cliquer sur "Appliquer"

#### 5.3 Configurer les Événements
1. Section "Association Événements CAN"
2. Sélectionner "Clignotant Gauche"
3. Choisir effet "Clignotants"
4. Durée : 0 (permanent)
5. Priorité : 200
6. Cliquer sur "Assigner"

**Répéter pour:**
- Clignotant Droite
- Angle Mort Gauche (effet Strobe, priorité 250)
- Angle Mort Droite (effet Strobe, priorité 250)
- En Charge (effet État Charge, priorité 150)

#### 5.4 Mode Nuit Automatique
1. Cocher "Mode nuit automatique"
2. Luminosité Mode Nuit : 30
3. Cliquer sur "Appliquer"

## ✅ Vérification

### Test 1: Effet par Défaut
- [ ] Les LEDs affichent l'arc-en-ciel
- [ ] La luminosité est correcte
- [ ] L'animation est fluide

### Test 2: Événements CAN
Dans votre véhicule:
- [ ] Activer clignotant gauche → Animation orange
- [ ] Activer clignotant droit → Animation orange
- [ ] Brancher charge → Animation de charge
- [ ] Approcher véhicule (blindspot) → Flash rouge

### Test 3: Mode Nuit
Le soir, quand il fait sombre:
- [ ] LEDs réduisent automatiquement la luminosité
- [ ] Effet passe à Breathing bleu doux

## 🎯 Profils d'Exemple

### Profil "Sport"

```
Nom: Sport
Effet défaut: Rainbow (luminosité 200, vitesse 150)

Événements:
- Clignotants: Strobe orange (priorité 200)
- Blindspot: Strobe rouge (priorité 255)
- Freinage: Feux Stop (priorité 180)
- Charge: État Charge (priorité 150)

Mode nuit: Non
```

### Profil "Discret"

```
Nom: Discret
Effet défaut: Breathing blanc (luminosité 80, vitesse 30)

Événements:
- Clignotants: Couleur unie orange (priorité 200)
- Blindspot: Breathing rouge (priorité 220)
- Portes: Breathing bleu (priorité 100, durée 3000ms)

Mode nuit: Oui (luminosité 20)
```

### Profil "Sécurité Max"

```
Nom: Sécurité
Effet défaut: Solid blanc (luminosité 100)

Événements:
- Blindspot: Strobe rouge (priorité 255, permanent)
- Clignotants: Strobe orange (priorité 250)
- Freinage: Solid rouge (priorité 240)
- Porte ouverte déverrouillée: Strobe jaune (priorité 230, durée 5000ms)

Mode nuit: Oui (luminosité 50 - plus élevé pour sécurité)
```

## 🔧 Dépannage Rapide

### Problème: LEDs ne s'allument pas
1. Vérifier connexion GPIO5
2. Vérifier alimentation 5V
3. Vérifier masse commune
4. Dans config.h, vérifier `LED_PIN` et `NUM_LEDS`

### Problème: Pas de messages CAN reçus
1. Vérifier le câblage du transceiver CAN (CAN_H, CAN_L, GND)
2. Vérifier les GPIO TX (38) et RX (39) dans can_bus.c
3. Vérifier que le transceiver est alimenté en 3.3V
4. Vérifier dans les logs série : "Bus CAN démarré" et "CAN frame received"
5. Mettre le contact du véhicule (accessoires ON minimum)

### Problème: Événements CAN ne déclenchent pas
1. Vérifier que des messages CAN sont reçus (logs série : "CAN frame received")
2. Vérifier que le profil est bien activé
3. Vérifier que l'événement est bien assigné avec un effet
4. Vérifier la priorité de l'effet
5. Tester un événement simple (clignotant) pour valider le système

### Problème: Interface web inaccessible
1. Vérifier connexion au WiFi "CarLightSync"
2. Essayer http://192.168.4.1 (pas https)
3. Vider le cache du navigateur
4. Essayer un autre navigateur

## 📱 Utilisation Mobile

### iOS
1. Réglages → WiFi
2. Se connecter à "CarLightSync"
3. Ouvrir Safari
4. Aller sur http://192.168.4.1
5. Ajouter à l'écran d'accueil (optionnel)

### Android
1. Paramètres → WiFi
2. Se connecter à "CarLightSync"
3. Ouvrir Chrome
4. Aller sur http://192.168.4.1
5. Menu → Ajouter à l'écran d'accueil (optionnel)

## 🎓 Prochaines Étapes

### Niveau Débutant
- [x] Installation et connexion
- [ ] Créer 2-3 profils personnalisés
- [ ] Tester tous les événements CAN
- [ ] Comprendre le système de priorité

### Niveau Intermédiaire
- [ ] Lire FEATURES.md pour fonctionnalités avancées
- [ ] Créer des profils spécialisés (sport, nuit, ville)
- [ ] Utiliser l'API REST depuis curl/Postman
- [ ] Modifier les couleurs et timings

### Niveau Avancé
- [ ] Lire ADVANCED.md pour extensions
- [ ] Créer des effets personnalisés
- [ ] Ajouter des nouveaux messages CAN
- [ ] Intégrer avec HomeAssistant/MQTT

## 📚 Ressources

### Documentation
- **README.md** - Documentation complète
- **FEATURES.md** - Guide des fonctionnalités v2.0
- **ADVANCED.md** - Exemples avancés et extensions
- **WIRING.md** - Guide de câblage détaillé

### Support
- GitHub Issues pour bugs et questions
- Documentation ESP-IDF: https://docs.espressif.com
- Forums véhicules: teslaownersonline.com et autres forums spécialisés

### Communauté
- Partagez vos profils !
- Proposez de nouveaux effets
- Contribuez au projet

## 🎉 Félicitations !

Vous avez maintenant un **système de LEDs hautement personnalisable pour votre véhicule** avec:
- ✅ 10 profils configurables
- ✅ 17 événements CAN réactifs
- ✅ Mode nuit automatique
- ✅ Alertes blindspot
- ✅ Interface web complète

**Bon éclairage ! 🌈**

---

**Astuce Pro:** Créez un profil pour chaque type de trajet (ville, autoroute, nuit) et switchez selon vos besoins !
