# Car Light Sync — Problèmes & Dépannage

Guide rapide pour résoudre les incidents courants et rappeler les bonnes pratiques de sécurité.

## Dépannage
### LEDs ne s’allument pas
- Vérifier la polarité du ruban (certains inversent rouge/noir).
- Tester avec une alimentation faible (3.3V) et une seule LED.
- Confirmer `LED_PIN` et `NUM_LEDS` dans `include/config.h`.
- Ajouter résistance série (330–470 Ω) et condensateur 1000 µF sur l’alim.

### Pas de messages CAN reçus
- Vérifier le câblage CAN_H/CAN_L et la masse commune.
- S’assurer que le transceiver est 3.3V et correctement alimenté.
- Confirmer les GPIO TX/RX dans `main/can_bus.c` et la vitesse (500 kbit/s par défaut).
- Tester sur un bus connu (ex. OBD) pour isoler le problème.

### Interface web inaccessible
- Se connecter au WiFi `CarLightSync` puis ouvrir `http://192.168.4.1`.
- Couper/relancer l’alimentation après flash si l’AP n’apparaît pas.
- Vérifier qu’aucun autre périphérique n’utilise le même canal WiFi ou IP.

## Sécurité
- Changer les mots de passe par défaut (`config.h`, `wifi_credentials.h`).
- Ne pas exposer l’AP sur un réseau non fiable ; utiliser un VPN si nécessaire.
- Protéger l’accès physique à l’ESP32 et désactiver l’AP quand il n’est pas utile.
- L’interface web n’est pas protégée par mot de passe par défaut.

## Support & communauté
- **Issues GitHub** : signaler bugs et proposer des fonctionnalités.
- **Discussions** : questions, retours d’expérience.
- **Wiki** : documentation communautaire.
