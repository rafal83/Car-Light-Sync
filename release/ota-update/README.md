# Mise à Jour OTA (Over-The-Air)

Ce dossier contient le fichier firmware pour une mise à jour sans fil.

## Fichier inclus:
- **car-light-sync-ota.bin** - Firmware pour mise à jour OTA

## Instructions:

### Via l'interface Web:
1. Connectez-vous au WiFi de l'ESP32 (SSID: CarLightSync)
2. Ouvrez un navigateur et allez à: http://192.168.4.1
3. Allez dans la section "🔄 Mise à Jour OTA"
4. Sélectionnez le fichier `car-light-sync-ota.bin`
5. Cliquez sur "Téléverser"
6. Attendez la fin de l'upload (progression affichée)
7. Cliquez sur "Redémarrer" pour appliquer la mise à jour

### Via cURL (ligne de commande):
```bash
curl -F "firmware=@car-light-sync-ota.bin" http://192.168.4.1/api/ota/upload
curl -X POST http://192.168.4.1/api/ota/restart
```

## Notes:
- Taille du firmware: ~1.72 MB
- Durée estimée de l'upload: 30-60 secondes
- L'ESP32 redémarrera automatiquement après la mise à jour
- En cas d'échec, l'ESP32 reviendra automatiquement à la version précédente (rollback)

## Vérification de la version:
```bash
curl http://192.168.4.1/api/ota/info
```

---
Généré le: 2025-12-19 06:50:43
