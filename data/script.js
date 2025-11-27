const API_BASE = '';
// Translations
let currentLang = 'en';
let currentTheme = 'dark';
const translations = {
    fr: {
        app: {
            title: "Car Light Sync",
            subtitle: "Contrôle RGB synchronisé avec votre voiture",
            waitingConnection: "Connectez-vous en WiFi ou BLE pour continuer."
        },
        status: {
            wifi: "WiFi",
            canBus: "CAN Bus",
            vehicle: "Véhicule",
            profile: "Profil",
            connected: "Connecté",
            disconnected: "Déconnecté",
            active: "Actif",
            inactive: "Inactif",
            ap: "AP"
        },
        ble: {
            title: "Connexion BLE",
            connect: "Connexion",
            connecting: "Connexion...",
            disconnect: "Déconnexion",
            connected: "Connecté",
            disconnected: "Déconnecté",
            statusUnsupported: "Non supporté",
            notSupported: "Web Bluetooth non supporté sur ce navigateur.",
            toastConnected: "Connexion BLE établie",
            toastDisconnected: "Connexion BLE fermée",
            toastError: "Erreur de communication BLE",
            requestRejected: "Commande BLE refusée",
            timeout: "Délai d'attente BLE dépassé",
            tapToAuthorize: "Touchez l'écran pour autoriser la connexion BLE."
        },
        tabs: {
            effects: "Effets LED",
            profiles: "Profils",
            eventsConfig: "Événements",
            canEvents: "Événements CAN",
            config: "Configuration",
            vehicle: "Véhicule",
            simulation: "Simulation",
            ota: "Mise à Jour"
        },
        effects: {
            title: "Configuration des Effets",
            effect: "Effet",
            brightness: "Luminosité",
            speed: "Vitesse",
            color: "Couleur",
            apply: "Appliquer",
            save: "Sauvegarder",
            off: "Off",
            solid: "Couleur Unie",
            breathing: "Respiration",
            rainbow: "Arc-en-ciel",
            rainbowCycle: "Arc-en-ciel Cyclique",
            theaterChase: "Theater Chase",
            runningLights: "Running Lights",
            twinkle: "Twinkle",
            fire: "Feu",
            scan: "Scan",
            knightRider: "Knight Rider",
            fade: "Fade",
            strobe: "Strobe",
            vehicleSync: "Sync Véhicule",
            turnSignal: "Clignotant",
            brakeLight: "Feu de Frein",
            chargeStatus: "État de Charge",
            audioReactive: "Audio Réactif",
            audioBpm: "Audio BPM",
            audioReactiveMode: "Mode Audio Réactif"
        },
        audio: {
            title: "Configuration Audio",
            microphone: "Micro INMP441",
            enable: "Activer le micro",
            enabled: "Activé",
            disabled: "Désactivé",
            sensitivity: "Sensibilité",
            gain: "Gain",
            autoGain: "Gain automatique",
            i2sPins: "Pins I2S (INMP441)",
            i2sPinsInfo: "Configuration définie au démarrage",
            status: "Statut",
            liveData: "Données en Direct",
            amplitude: "Amplitude",
            bpm: "BPM Détecté",
            bass: "Basses",
            mid: "Médiums",
            treble: "Aigus",
            gpioConfig: "Configuration GPIO",
            applyGpio: "Appliquer GPIO",
            saveConfig: "Sauvegarder Configuration",
            noData: "Aucune donnée",
            warning: "⚠️ Le micro ne peut être activé que sur l'effet par défaut du profil actif."
        },
        fft: {
            title: "Analyse Spectrale FFT Avancée",
            autoInfo: "ℹ️ Le FFT s'active automatiquement avec les effets audio-réactifs",
            peakFreq: "Fréquence dominante",
            centroid: "Centroïde spectral",
            kick: "Kick",
            snare: "Snare",
            vocal: "Vocal",
            spectrum: "Spectre FFT (32 bandes)",
            detected: "Détecté"
        },
        profiles: {
            title: "Gestion des Profils",
            activeProfile: "Profil Actif",
            loading: "Chargement...",
            activate: "Activer",
            new: "Nouveau",
            delete: "Supprimer",
            export: "Exporter",
            import: "Importer",
            autoNightMode: "Mode nuit automatique (basé sur CAN)",
            nightBrightness: "Luminosité Mode Nuit",
            defaultEffect: "Effet par Défaut",
            defaultEffectDesc: "Configurez l'effet affiché lorsqu'aucun événement CAN n'est actif.",
            saveDefault: "Sauvegarder Effet par Défaut",
            newProfileTitle: "Nouveau Profil",
            profileName: "Nom du profil",
            create: "Créer",
            cancel: "Annuler",
            deleteConfirm: "Supprimer ce profil ?",
            selectProfile: "Sélectionnez un profil",
            exportSuccess: "Profil exporté avec succès",
            exportError: "Erreur lors de l'export",
            importSuccess: "Profil importé avec succès",
            importError: "Erreur lors de l'import"
        },
        eventsConfig: {
            title: "Configuration des Événements",
            description: "Configurez les effets lumineux pour chaque événement CAN détecté par le véhicule.",
            loading: "Chargement de la configuration...",
            eventName: "Événement",
            action: "Action",
            profile: "Profil",
            applyEffect: "Appliquer Effet",
            switchProfile: "Changer Profil",
            effect: "Effet",
            brightness: "Luminosité",
            speed: "Vitesse",
            color: "Couleur",
            duration: "Durée (ms)",
            priority: "Priorité",
            enabled: "Activé",
            save: "Sauvegarder la Configuration",
            reload: "Recharger",
            saveSuccess: "Configuration sauvegardée avec succès",
            saveError: "Erreur lors de la sauvegarde",
            loadError: "Erreur lors du chargement"
        },
        canEvents: {
            title: "Association Événements CAN",
            event: "Événement",
            effectToTrigger: "Effet à déclencher",
            duration: "Durée (ms, 0 = infini)",
            priority: "Priorité",
            assign: "Assigner",
            turnLeft: "Clignotant Gauche",
            turnRight: "Clignotant Droit",
            hazard: "Warning",
            charging: "En Charge",
            chargeComplete: "Charge Complète",
            doorOpen: "Porte Ouverte",
            doorClose: "Porte Fermée",
            locked: "Verrouillé",
            unlocked: "Déverrouillé",
            brakeOn: "Frein Activé",
            brakeOff: "Frein Relâché",
            blindspotLeft: "Angle Mort Gauche",
            blindspotRight: "Angle Mort Droit",
            nightModeOn: "Mode Nuit Activé",
            nightModeOff: "Mode Nuit Désactivé",
            speedThreshold: "Seuil Vitesse Dépassé",
            assignSuccess: "Effet assigné !"
        },
        config: {
            title: "Configuration Matérielle",
            description: "Configurez les paramètres matériels du contrôleur LED.",
            ledCount: "Nombre de LEDs",
            dataPin: "Pin de Données",
            stripReverse: "Inverser le sens de la strip LED",
            language: "Langue",
            languageFrench: "Français",
            languageEnglish: "Anglais",
            theme: "Thème",
            themeDark: "Mode sombre",
            themeLight: "Mode clair",
            save: "Sauvegarder",
            reload: "Recharger",
            restartEsp: "Redémarrer",
            saveSuccess: "Configuration sauvegardée",
            saveError: "Erreur lors de la sauvegarde",
            loadError: "Erreur lors du chargement",
            dangerZone: "Zone Dangereuse",
            factoryReset: "Réinitialisation Usine",
            factoryResetWarning: "La réinitialisation usine supprimera TOUS les profils et configurations. Cette action est irréversible !",
            factoryResetConfirm: "Êtes-vous VRAIMENT sûr de vouloir effacer toutes les configurations ? Cette action ne peut pas être annulée !",
            factoryResetInProgress: "Réinitialisation en cours...",
            factoryResetSuccess: "Réinitialisation réussie !",
            factoryResetError: "Erreur lors de la réinitialisation",
            deviceRestarting: "L'appareil va redémarrer dans quelques secondes..."
        },
        vehicle: {
            title: "Données Véhicule",
            generalState: "État Général",
            speed: "Vitesse",
            gear: "Position de transmission",
            brake: "Frein",
            brightness: "Luminosité",
            off: "Éteint",
            locked: "Verrouillé",
            unlocked: "Déverrouillé",
            doorsTitle: "Portes",
            doorFL: "AV Gauche",
            doorFR: "AV Droite",
            doorRL: "AR Gauche",
            doorRR: "AR Droite",
            trunk: "Coffre",
            frunk: "Frunk",
            chargeTitle: "Charge",
            charging: "En Charge",
            chargePercent: "Niveau",
            chargePower: "Puissance",
            lightsTitle: "Lumières",
            headlights: "Phares",
            highBeams: "Feux Route",
            fogLights: "Anti-brouillard",
            turnSignal: "Clignotant",
            othersTitle: "Autres",
            batterylv: "Batterie LV",
            batteryhv: "Batterie HV",
            odometer: "Odomètre",
            nightMode: "Mode Nuit",
            blindspotLeft: "Angle Mort G",
            blindspotRight: "Angle Mort D",
            open: "Ouvert",
            closed: "Fermé",
            none: "Aucun",
            sentryTitle: "Mode Sentry",
            sentryMode: "État UI",
            sentryRequest: "Requête Autopilot",
            sentryAlert: "Alerte",
            sentryNominal: "Nominal",
            sentrySuspend: "Suspendu"
        },
        simulation: {
            title: "Simulation d'événements CAN",
            description: "Testez les effets lumineux en simulant des événements CAN sans connexion au véhicule.",
            turnSignals: "Clignotants",
            left: "Gauche",
            right: "Droite",
            hazard: "Warning",
            stop: "Arrêter",
            charging: "Charge",
            chargingNow: "En charge",
            chargeComplete: "Charge complète",
            chargingStarted: "Charge démarrée",
            chargingStopped: "Charge arrêtée",
            chargingHardware: "Connectique de charge",
            cableConnected: "Câble connecté",
            cableDisconnected: "Câble déconnecté",
            portOpened: "Port de charge ouvert",
            doors: "Portes",
            doorOpen: "Porte ouverte",
            doorClose: "Porte fermée",
            lock: "Verrouillage",
            locked: "Verrouillé",
            unlocked: "Déverrouillé",
            driving: "Conduite",
            brakeOn: "Frein ON",
            brakeOff: "Frein OFF",
            speedThreshold: "Seuil Vitesse",
            autopilot: "Autopilot",
            autopilotEngaged: "Autopilot activé",
            autopilotDisengaged: "Autopilot désactivé",
            gear: "Transmission",
            gearDrive: "Drive",
            gearReverse: "Marche arrière",
            gearPark: "Park",
            blindspot: "Angle mort",
            blindspotLeft: "Gauche",
            blindspotRight: "Droite",
            blindspotWarning: "Alerte angle mort",
            sentry: "Mode Sentry",
            sentryOn: "Sentry ON",
            sentryOff: "Sentry OFF",
            sentryAlert: "Alerte Sentry",
            nightMode: "Mode Nuit",
            nightModeToggle: "Mode Nuit",
            nightModeOn: "Activer",
            nightModeOff: "Désactiver",
            sending: "Envoi de l'événement",
            sendingEvent: "Envoi : {0}...",
            simulated: "Événement simulé",
            eventSimulated: "OK - Événement simulé : {0}",
            error: "Erreur lors de la simulation",
            simulationError: "Erreur - {0}",
            stopping: "Arrêt de l'événement",
            stopped: "Effet arrêté",
            eventStopped: "Stop - Effet arrêté",
            eventActive: "OK - {0} actif",
            eventDeactivated: "Stop - {0} arrêté",
            stoppingEvent: "Arrêt : {0}...",
            active: "actif",
            disabledEvent: "Événement désactivé dans la configuration"
        },
        eventNames: {
            TURN_LEFT: "Clignotant gauche",
            TURN_RIGHT: "Clignotant droit",
            TURN_HAZARD: "Feux de détresse",
            CHARGING: "Charge en cours",
            CHARGE_COMPLETE: "Charge terminée",
            CHARGING_STARTED: "Charge démarrée",
            CHARGING_STOPPED: "Charge arrêtée",
            CHARGING_CABLE_CONNECTED: "Câble connecté",
            CHARGING_CABLE_DISCONNECTED: "Câble déconnecté",
            CHARGING_PORT_OPENED: "Port de charge ouvert",
            DOOR_OPEN: "Porte ouverte",
            DOOR_CLOSE: "Porte fermée",
            LOCKED: "Véhicule verrouillé",
            UNLOCKED: "Véhicule déverrouillé",
            BRAKE_ON: "Frein activé",
            BRAKE_OFF: "Frein relâché",
            BLINDSPOT_LEFT: "Angle mort gauche",
            BLINDSPOT_RIGHT: "Angle mort droit",
            BLINDSPOT_WARNING: "Alerte angle mort",
            NIGHT_MODE_ON: "Mode nuit activé",
            NIGHT_MODE_OFF: "Mode nuit désactivé",
            SPEED_THRESHOLD: "Seuil de vitesse atteint",
            AUTOPILOT_ENGAGED: "Autopilot activé",
            AUTOPILOT_DISENGAGED: "Autopilot désactivé",
            GEAR_DRIVE: "Passage en Drive",
            GEAR_REVERSE: "Marche arrière",
            GEAR_PARK: "Mode Park",
            SENTRY_MODE_ON: "Sentry activé",
            SENTRY_MODE_OFF: "Sentry désactivé",
            SENTRY_ALERT: "Alerte Sentry",
            unknown: "Événement {0}"
        },
        effectNames: {
            OFF: "Aucun",
            SOLID: "Couleur fixe",
            BREATHING: "Respiration",
            RAINBOW: "Arc-en-ciel",
            RAINBOW_CYCLE: "Arc-en-ciel cyclique",
            THEATER_CHASE: "Théâtre",
            RUNNING_LIGHTS: "Lumières glissantes",
            TWINKLE: "Scintillement",
            FIRE: "Feu",
            SCAN: "Balayage",
            KNIGHT_RIDER: "Knight Rider",
            FADE: "Fondu",
            STROBE: "Stroboscope",
            VEHICLE_SYNC: "Synchro véhicule",
            TURN_SIGNAL: "Clignotant",
            BRAKE_LIGHT: "Feu stop",
            CHARGE_STATUS: "Statut de charge",
            HAZARD: "Avertisseur",
            BLINDSPOT_FLASH: "Flash angle mort",
            unknown: "Effet {0}"
        },
        ota: {
            title: "Mise à Jour OTA",
            currentVersion: "Version Actuelle",
            loading: "Chargement...",
            firmwareFile: "Fichier Firmware (.bin)",
            progress: "Progression",
            upload: "Téléverser",
            restart: "Redémarrer",
            uploading: "Téléversement en cours...",
            success: "Mise à jour réussie ! Vous pouvez redémarrer.",
        error: "Erreur lors de la mise à jour",
        selectFile: "Veuillez sélectionner un fichier firmware",
        wrongExtension: "Le fichier doit avoir l'extension .bin",
        confirmUpdate: "Êtes-vous sûr de vouloir mettre à jour le firmware ? L'appareil redémarrera.",
        confirmRestart: "Redémarrer maintenant ?",
        restarting: "Redémarrage en cours... Reconnexion dans 10 secondes.",
        autoRestartIn: "Redémarrage automatique dans",
        states: {
            idle: "En attente d'une mise à jour",
            receiving: "Réception du firmware...",
            writing: "Écriture du firmware...",
            success: "Mise à jour terminée, redémarrage imminent",
            error: "Erreur pendant la mise à jour"
        },
        bleNote: "Connectez-vous au Wi-Fi de l'appareil (AP) pour effectuer la mise à jour."
    }
},
en: {
        app: {
            title: "Car Light Sync",
            subtitle: "RGB Control Synchronized with your Car",
            waitingConnection: "Connect via WiFi or BLE to continue."
        },
        status: {
            wifi: "WiFi",
            canBus: "CAN Bus",
            vehicle: "Vehicle",
            profile: "Profile",
            connected: "Connected",
            disconnected: "Disconnected",
            active: "Active",
            inactive: "Inactive",
            ap: "AP"
        },
        ble: {
            title: "BLE Link",
            connect: "Connect",
            connecting: "Connecting...",
            disconnect: "Disconnect",
            connected: "Connected",
            disconnected: "Disconnected",
            statusUnsupported: "Unsupported",
            notSupported: "Web Bluetooth is not supported on this browser.",
            toastConnected: "BLE connected",
            toastDisconnected: "BLE disconnected",
            toastError: "BLE communication error",
            requestRejected: "BLE request rejected",
            timeout: "BLE request timed out",
            tapToAuthorize: "Tap anywhere to authorize the BLE connection."
        },
        tabs: {
            effects: "LED Effects",
            profiles: "Profiles",
            eventsConfig: "Events",
            canEvents: "CAN Events",
            config: "Configuration",
            vehicle: "Vehicle",
            simulation: "Simulation",
            ota: "Update"
        },
        effects: {
            title: "Effects Configuration",
            effect: "Effect",
            brightness: "Brightness",
            speed: "Speed",
            color: "Color",
            apply: "Apply",
            save: "Save",
            off: "Off",
            solid: "Solid Color",
            breathing: "Breathing",
            rainbow: "Rainbow",
            rainbowCycle: "Rainbow Cycle",
            theaterChase: "Theater Chase",
            runningLights: "Running Lights",
            twinkle: "Twinkle",
            fire: "Fire",
            scan: "Scan",
            knightRider: "Knight Rider",
            fade: "Fade",
            strobe: "Strobe",
            vehicleSync: "Vehicle Sync",
            turnSignal: "Turn Signal",
            brakeLight: "Brake Light",
            chargeStatus: "Charge Status",
            audioReactive: "Audio Reactive",
            audioBpm: "Audio BPM",
            audioReactiveMode: "Audio Reactive Mode"
        },
        audio: {
            title: "Audio Configuration",
            microphone: "INMP441 Microphone",
            enable: "Enable microphone",
            enabled: "Enabled",
            disabled: "Disabled",
            sensitivity: "Sensitivity",
            gain: "Gain",
            autoGain: "Auto Gain",
            i2sPins: "I2S Pins (INMP441)",
            i2sPinsInfo: "Configuration set at startup",
            status: "Status",
            liveData: "Live Data",
            amplitude: "Amplitude",
            bpm: "BPM Detected",
            bass: "Bass",
            mid: "Mid",
            treble: "Treble",
            gpioConfig: "GPIO Configuration",
            applyGpio: "Apply GPIO",
            saveConfig: "Save Configuration",
            noData: "No data",
            warning: "⚠️ The microphone can only be enabled on the default effect of the active profile."
        },
        fft: {
            title: "Advanced FFT Spectrum Analysis",
            autoInfo: "ℹ️ FFT is automatically enabled with audio-reactive effects",
            peakFreq: "Peak Frequency",
            centroid: "Spectral Centroid",
            kick: "Kick",
            snare: "Snare",
            vocal: "Vocal",
            spectrum: "FFT Spectrum (32 bands)",
            detected: "Detected"
        },
        profiles: {
            title: "Profile Management",
            activeProfile: "Active Profile",
            loading: "Loading...",
            activate: "Activate",
            new: "New",
            delete: "Delete",
            export: "Export",
            import: "Import",
            autoNightMode: "Automatic night mode (based on CAN)",
            nightBrightness: "Night Mode Brightness",
            defaultEffect: "Default Effect",
            defaultEffectDesc: "Configure the effect displayed when no CAN event is active.",
            saveDefault: "Save Default Effect",
            newProfileTitle: "New Profile",
            profileName: "Profile name",
            create: "Create",
            cancel: "Cancel",
            deleteConfirm: "Delete this profile?",
            selectProfile: "Select a profile",
            exportSuccess: "Profile exported successfully",
            exportError: "Error exporting profile",
            importSuccess: "Profile imported successfully",
            importError: "Error importing profile"
        },
        eventsConfig: {
            title: "Events Configuration",
            description: "Configure light effects for each CAN event detected by the vehicle.",
            loading: "Loading configuration...",
            eventName: "Event",
            action: "Action",
            profile: "Profile",
            applyEffect: "Apply Effect",
            switchProfile: "Switch Profile",
            effect: "Effect",
            brightness: "Brightness",
            speed: "Speed",
            color: "Color",
            duration: "Duration (ms)",
            priority: "Priority",
            enabled: "Enabled",
            save: "Save Configuration",
            reload: "Reload",
            saveSuccess: "Configuration saved successfully",
            saveError: "Error saving configuration",
            loadError: "Error loading configuration"
        },
        canEvents: {
            title: "CAN Events Association",
            event: "Event",
            effectToTrigger: "Effect to trigger",
            duration: "Duration (ms, 0 = infinite)",
            priority: "Priority",
            assign: "Assign",
            turnLeft: "Turn Left",
            turnRight: "Turn Right",
            hazard: "Hazard",
            charging: "Charging",
            chargeComplete: "Charge Complete",
            doorOpen: "Door Open",
            doorClose: "Door Close",
            locked: "Locked",
            unlocked: "Unlocked",
            brakeOn: "Brake On",
            brakeOff: "Brake Off",
            blindspotLeft: "Blindspot Left",
            blindspotRight: "Blindspot Right",
            nightModeOn: "Night Mode On",
            nightModeOff: "Night Mode Off",
            speedThreshold: "Speed Threshold",
            assignSuccess: "Effect assigned!"
        },
        config: {
            title: "Hardware Configuration",
            description: "Configure hardware parameters for the LED controller.",
            ledCount: "LED Count",
            dataPin: "Data Pin",
            stripReverse: "Reverse LED strip direction",
            language: "Language",
            languageFrench: "French",
            languageEnglish: "English",
            theme: "Theme",
            themeDark: "Dark mode",
            themeLight: "Light mode",
            save: "Save",
            reload: "Reload",
            restartEsp: "Restart",
            saveSuccess: "Configuration saved",
            saveError: "Error saving configuration",
            loadError: "Error loading configuration",
            dangerZone: "Danger Zone",
            factoryReset: "Factory Reset",
            factoryResetWarning: "Factory reset will delete ALL profiles and configurations. This action is irreversible!",
            factoryResetConfirm: "Are you REALLY sure you want to erase all configurations? This action cannot be undone!",
            factoryResetInProgress: "Reset in progress...",
            factoryResetSuccess: "Reset successful!",
            factoryResetError: "Error during reset",
            deviceRestarting: "The device will restart in a few seconds..."
        },
        vehicle: {
            title: "Vehicle Data",
            generalState: "General State",
            speed: "Speed",
            gear: "Gear",
            brake: "Brake",
            brightness: "Brightness",
            off: "Off",
            locked: "Locked",
            unlocked: "Unlocked",
            doorsTitle: "Doors",
            doorFL: "Front Left",
            doorFR: "Front Right",
            doorRL: "Rear Left",
            doorRR: "Rear Right",
            trunk: "Trunk",
            frunk: "Frunk",
            chargeTitle: "Charge",
            charging: "Charging",
            chargePercent: "Level",
            chargePower: "Power",
            lightsTitle: "Lights",
            headlights: "Headlights",
            highBeams: "High Beams",
            fogLights: "Fog Lights",
            turnSignal: "Turn Signal",
            othersTitle: "Others",
            batterylv: "LV Battery",
            batteryhv: "HV Battery",
            odometer: "Odometer",
            nightMode: "Night Mode",
            blindspotLeft: "Blindspot L",
            blindspotRight: "Blindspot R",
            open: "Open",
            closed: "Closed",
            none: "None",
            sentryTitle: "Sentry Mode",
            sentryMode: "UI State",
            sentryRequest: "Autopilot Request",
            sentryAlert: "Alert",
            sentryNominal: "Nominal",
            sentrySuspend: "Suspend"
        },
        simulation: {
            title: "CAN Event Simulation",
            description: "Test light effects by simulating CAN events without vehicle connection.",
            turnSignals: "Turn Signals",
            left: "Left",
            right: "Right",
            hazard: "Hazard",
            stop: "Stop",
            charging: "Charging",
            chargingNow: "Charging",
            chargeComplete: "Charge complete",
            chargingStarted: "Charging started",
            chargingStopped: "Charging stopped",
            chargingHardware: "Charging hardware",
            cableConnected: "Cable connected",
            cableDisconnected: "Cable disconnected",
            portOpened: "Charge port opened",
            doors: "Doors",
            doorOpen: "Door open",
            doorClose: "Door close",
            lock: "Lock",
            locked: "Locked",
            unlocked: "Unlocked",
            driving: "Driving",
            brakeOn: "Brake ON",
            brakeOff: "Brake OFF",
            speedThreshold: "Speed Threshold",
            autopilot: "Autopilot",
            autopilotEngaged: "Autopilot engaged",
            autopilotDisengaged: "Autopilot disengaged",
            gear: "Transmission",
            gearDrive: "Drive",
            gearReverse: "Reverse",
            gearPark: "Park",
            blindspot: "Blindspot",
            blindspotLeft: "Left",
            blindspotRight: "Right",
            blindspotWarning: "Blindspot warning",
            sentry: "Sentry Mode",
            sentryOn: "Sentry ON",
            sentryOff: "Sentry OFF",
            sentryAlert: "Sentry Alert",
            nightMode: "Night Mode",
            nightModeToggle: "Night Mode",
            nightModeOn: "Activate",
            nightModeOff: "Deactivate",
            sending: "Sending event",
            sendingEvent: "Sending: {0}...",
            simulated: "Event simulated",
            eventSimulated: "OK - Event simulated: {0}",
            error: "Simulation error",
            simulationError: "Error - {0}",
            stopping: "Stopping event",
            stopped: "Effect stopped",
            eventStopped: "Stop - Effect stopped",
            eventActive: "OK - {0} active",
            eventDeactivated: "Stop - {0} stopped",
            stoppingEvent: "Stopping: {0}...",
            active: "active",
            disabledEvent: "Event disabled in configuration"
        },
        eventNames: {
            TURN_LEFT: "Turn signal left",
            TURN_RIGHT: "Turn signal right",
            TURN_HAZARD: "Hazard lights",
            CHARGING: "Charging",
            CHARGE_COMPLETE: "Charge complete",
            CHARGING_STARTED: "Charging started",
            CHARGING_STOPPED: "Charging stopped",
            CHARGING_CABLE_CONNECTED: "Cable connected",
            CHARGING_CABLE_DISCONNECTED: "Cable disconnected",
            CHARGING_PORT_OPENED: "Charge port opened",
            DOOR_OPEN: "Door open",
            DOOR_CLOSE: "Door close",
            LOCKED: "Vehicle locked",
            UNLOCKED: "Vehicle unlocked",
            BRAKE_ON: "Brake pressed",
            BRAKE_OFF: "Brake released",
            BLINDSPOT_LEFT: "Left blindspot",
            BLINDSPOT_RIGHT: "Right blindspot",
            BLINDSPOT_WARNING: "Blindspot warning",
            NIGHT_MODE_ON: "Night mode on",
            NIGHT_MODE_OFF: "Night mode off",
            SPEED_THRESHOLD: "Speed threshold reached",
            AUTOPILOT_ENGAGED: "Autopilot engaged",
            AUTOPILOT_DISENGAGED: "Autopilot disengaged",
            GEAR_DRIVE: "Gear Drive",
            GEAR_REVERSE: "Gear Reverse",
            GEAR_PARK: "Gear Park",
            SENTRY_MODE_ON: "Sentry enabled",
            SENTRY_MODE_OFF: "Sentry disabled",
            SENTRY_ALERT: "Sentry alert",
            unknown: "Event {0}"
        },
        effectNames: {
            OFF: "Off",
            SOLID: "Solid color",
            BREATHING: "Breathing",
            RAINBOW: "Rainbow",
            RAINBOW_CYCLE: "Rainbow cycle",
            THEATER_CHASE: "Theater chase",
            RUNNING_LIGHTS: "Running lights",
            TWINKLE: "Twinkle",
            FIRE: "Fire",
            SCAN: "Scanner",
            KNIGHT_RIDER: "Knight Rider",
            FADE: "Fade",
            STROBE: "Strobe",
            VEHICLE_SYNC: "Vehicle sync",
            TURN_SIGNAL: "Turn signal",
            BRAKE_LIGHT: "Brake light",
            CHARGE_STATUS: "Charge status",
            HAZARD: "Hazard",
            BLINDSPOT_FLASH: "Blindspot flash",
            unknown: "Effect {0}"
        },
        ota: {
            title: "OTA Update",
            currentVersion: "Current Version",
            loading: "Loading...",
            firmwareFile: "Firmware File (.bin)",
            progress: "Progress",
            upload: "Upload",
            restart: "Restart",
            uploading: "Uploading...",
            success: "Update successful! You can restart.",
        error: "Update error",
        selectFile: "Please select a firmware file",
        wrongExtension: "File must have .bin extension",
        confirmUpdate: "Are you sure you want to update the firmware? The device will restart.",
        confirmRestart: "Restart now?",
        restarting: "Restarting... Reconnecting in 10 seconds.",
        autoRestartIn: "Auto reboot in",
        states: {
            idle: "Waiting for an update",
            receiving: "Receiving firmware...",
            writing: "Writing firmware to flash...",
            success: "Update completed, restart pending",
            error: "Update failed"
        },
        bleNote: "Connect to the device's Wi-Fi (AP) to perform the update."
    }
}
};
const OTA_STATE_KEYS = {
    0: 'idle',
    1: 'receiving',
    2: 'writing',
    3: 'success',
    4: 'error'
};
function getOtaStateKey(state) {
    if (state === undefined || state === null) {
        return 'idle';
    }
    const numericState = Number(state);
    return OTA_STATE_KEYS.hasOwnProperty(numericState) ? OTA_STATE_KEYS[numericState] : 'idle';
}
const BLE_CONFIG = {
    serviceUuid: '4fafc201-1fb5-459e-8fcc-c5c9c331914b',
    commandCharacteristicUuid: 'beb5483e-36e1-4688-b7f5-ea07361b26a8',
    responseCharacteristicUuid: '64a0990c-52eb-4c1b-aa30-ea826f4ba9dc',
    maxChunkSize: 180,
    responseTimeoutMs: 8000,
    deviceName: 'CarLightSync'
};
const usingFileProtocol = window.location.protocol === 'file:';
const usingCapacitor = window.Capacitor !== undefined;
const FALLBACK_ORIGIN = (!usingFileProtocol && window.location.origin && window.location.origin !== 'null')
    ? window.location.origin
    : 'http://localhost';
let bleTransportInstance = null;
let bleAutoConnectInProgress = false;
let bleAutoConnectGestureCaptured = false;
let bleAutoConnectGestureHandlerRegistered = false;
let bleAutoConnectAwaitingGesture = false;
let wifiOnline = !usingFileProtocol && !usingCapacitor && navigator.onLine;
let apiConnectionReady = wifiOnline;
let apiConnectionResolvers = [];
let initialDataLoaded = false;
let initialDataLoadPromise = null;
let activeTabName = 'vehicle';
let statusIntervalHandle = null;
function isApiConnectionReady() {
    return wifiOnline || (bleTransportInstance && bleTransportInstance.isConnected());
}
function updateApiConnectionState() {
    const ready = isApiConnectionReady();
    if (ready && !apiConnectionReady) {
        apiConnectionReady = true;
        const resolvers = [...apiConnectionResolvers];
        apiConnectionResolvers = [];
        resolvers.forEach(resolve => resolve());
        if (!initialDataLoaded) {
            scheduleInitialDataLoad();
        }
    } else if (!ready) {
        apiConnectionReady = false;
    }
    updateConnectionOverlay();
    if (!ready) {
        maybeAutoConnectBle();
    }
}
function waitForApiConnection() {
    if (isApiConnectionReady()) {
        apiConnectionReady = true;
        return Promise.resolve();
    }
    return new Promise(resolve => {
        apiConnectionResolvers.push(resolve);
    });
}
function registerBleAutoConnectGestureHandler() {
    if (bleAutoConnectGestureHandlerRegistered) {
        return;
    }
    bleAutoConnectGestureHandlerRegistered = true;
    const unlock = () => {
        document.removeEventListener('pointerdown', unlock);
        document.removeEventListener('keydown', unlock);
        bleAutoConnectGestureHandlerRegistered = false;
        bleAutoConnectGestureCaptured = true;
        bleAutoConnectAwaitingGesture = false;
        if (!wifiOnline) {
            maybeAutoConnectBle(true);
        } else {
            updateConnectionOverlay();
        }
    };
    document.addEventListener('pointerdown', unlock, { once: true });
    document.addEventListener('keydown', unlock, { once: true });
}
function maybeAutoConnectBle(fromGesture = false) {
    if (!bleTransport.isSupported()) {
        bleAutoConnectAwaitingGesture = false;
        return;
    }
    if (wifiOnline) {
        bleAutoConnectAwaitingGesture = false;
        return;
    }
    const status = bleTransport.getStatus();
    if (status === 'connected' || status === 'connecting') {
        bleAutoConnectAwaitingGesture = false;
        return;
    }
    if (!fromGesture && !bleAutoConnectGestureCaptured) {
        bleAutoConnectAwaitingGesture = true;
        registerBleAutoConnectGestureHandler();
        updateConnectionOverlay();
        return;
    }
    if (bleAutoConnectInProgress) {
        return;
    }
    bleAutoConnectAwaitingGesture = false;
    bleAutoConnectInProgress = true;
    bleTransport.connect().catch(error => {
        if (error && (error.name === 'SecurityError' || /SecurityError/i.test(error.message || ''))) {
            console.warn('BLE auto-connect blocked by browser security, waiting for interaction');
            bleAutoConnectGestureCaptured = false;
            bleAutoConnectAwaitingGesture = true;
            registerBleAutoConnectGestureHandler();
            updateConnectionOverlay();
        } else {
            console.warn('BLE auto-connect failed', error);
        }
    }).finally(() => {
        bleAutoConnectInProgress = false;
        updateConnectionOverlay();
    });
}
function updateConnectionOverlay() {
    const overlay = document.getElementById('connection-overlay');
    const message = document.getElementById('connection-overlay-message');
    const content = document.getElementById('app-content');
    const ready = isApiConnectionReady();
    if (overlay) {
        overlay.style.display = ready ? 'none' : 'flex';
    }
    if (message) {
        if (!ready && bleAutoConnectAwaitingGesture) {
            message.textContent = t('ble.tapToAuthorize');
            message.dataset.i18n = 'ble.tapToAuthorize';
        } else {
            message.textContent = t('app.waitingConnection');
            message.dataset.i18n = 'app.waitingConnection';
        }
    }
    if (content) {
        content.classList.toggle('disabled', !ready);
    }
    if (!ready && activeTabName !== 'vehicle') {
        switchTab('vehicle');
    }
}
function scheduleInitialDataLoad() {
    if (initialDataLoaded || initialDataLoadPromise) {
        return;
    }
    initialDataLoadPromise = (async () => {
        try {
            await waitForApiConnection();
            await loadInitialData();
            initialDataLoaded = true;
        } catch (error) {
            console.error('Initial data load failed:', error);
            initialDataLoadPromise = null;
        }
    })();
}
window.addEventListener('online', () => {
    wifiOnline = !usingFileProtocol && !usingCapacitor && navigator.onLine;
    updateApiConnectionState();
});
window.addEventListener('offline', () => {
    wifiOnline = false;
    updateApiConnectionState();
});
const bleTextEncoder = new TextEncoder();
const bleTextDecoder = new TextDecoder();
const BLE_BUTTON_ICONS = {
    connect: '🔗',
    disconnect: '⛓️‍💥',
    connecting: '⏳',
    unsupported: '⚠️'
};
function normalizeUrl(url) {
    if (url instanceof URL) {
        return url;
    }
    try {
        if (typeof url === 'string' && !url.match(/^[a-zA-Z][a-zA-Z0-9+.-]*:/)) {
            const trimmed = url.trim();
            const withoutScheme = trimmed.replace(/^[a-zA-Z]+:\/*/i, '');
            const withoutDrive = withoutScheme.replace(/^([a-zA-Z]:)/, '');
            const normalized = withoutDrive.replace(/\\/g, '/');
            const ensured = normalized.startsWith('/') ? normalized : '/' + normalized;
            const finalUrl = new URL(FALLBACK_ORIGIN + ensured);
            return finalUrl;
        }
        const parsed = new URL(url);
        const sanitizedPath = parsed.pathname.replace(/^\/?[a-zA-Z]:/, '').replace(/\\/g, '/');
        parsed.pathname = sanitizedPath.startsWith('/') ? sanitizedPath : '/' + sanitizedPath;
        const fallbackUrl = new URL(parsed.pathname + parsed.search, FALLBACK_ORIGIN);
        return fallbackUrl;
    } catch (e) {
        console.warn('[BLE] normalizeUrl failed for', url, e);
        return null;
    }
}
function pathLooksLikeApi(pathname) {
    if (!pathname) return false;
    const normalized = pathname.replace(/\\/g, '/');
    if (normalized === '/api' || normalized === 'api') return true;
    if (normalized.startsWith('/api/') || normalized.startsWith('api/')) return true;
    return normalized.includes('/api/');
}
function isApiRequestFromInput(input) {
    if (typeof input === 'string') {
        const normalized = normalizeUrl(input);
        return normalized ? pathLooksLikeApi(normalized.pathname) : false;
    } else if (input && typeof Request !== 'undefined' && input instanceof Request) {
        const normalized = normalizeUrl(input.url);
        return normalized ? pathLooksLikeApi(normalized.pathname) : false;
    }
    return false;
}
function isApiRequest(url, originalInput) {
    if (typeof originalInput !== 'undefined' && isApiRequestFromInput(originalInput)) {
        return true;
    }
    return isApiRequestFromInput(url);
}
class BleTransport {
    constructor() {
        this.device = null;
        this.server = null;
        this.commandCharacteristic = null;
        this.responseCharacteristic = null;
        this.responseBuffer = '';
        this.pending = null;
        this.listeners = new Set();
        this.status = this.isSupported() ? t('ble.disconnected') : t('ble.unsupported');
        this.boundDeviceDisconnect = this.handleDeviceDisconnected.bind(this);
        this.boundNotificationHandler = this.handleNotification.bind(this);
        this.requestQueue = Promise.resolve();

        if(this.isSupported()){
          document.getElementById('wifi-status-item').style.display = 'none';
        } else {
          document.getElementById('ble-status-item').style.display = 'none';
        }
    }
    async requestDevice(forceNew = false) {
        if (!forceNew && this.device) {
            return this.device;
        }
        const filters = [{ services: [BLE_CONFIG.serviceUuid] }];
        if (BLE_CONFIG.deviceName) {
            filters.push({ name: BLE_CONFIG.deviceName });
            filters.push({ namePrefix: BLE_CONFIG.deviceName });
        }
        const device = await navigator.bluetooth.requestDevice({
            filters,
            optionalServices: [BLE_CONFIG.serviceUuid]
        });
        if (this.device && this.device !== device) {
            try {
                this.device.removeEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
            } catch (e) {}
        }
        this.device = device;
        this.device.addEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
        return device;
    }
    async ensureGattConnection(device, forceReconnect = false) {
        if (!device || !device.gatt) {
            throw new Error('GATT server unavailable');
        }
        let server = device.gatt;
        if (forceReconnect && typeof server.disconnect === 'function' && server.connected) {
            try {
                server.disconnect();
            } catch (e) {}
        }
        if (!server.connected) {
            server = await server.connect();
        }
        this.server = server;
        return server;
    }
    isSupported() {
        return !!(navigator.bluetooth && navigator.bluetooth.requestDevice);
    }
    getStatus() {
        return this.status;
    }
    setStatus(status) {
        if (this.status === status) {
            return;
        }
        this.status = status;
        this.listeners.forEach(listener => {
            try {
                listener(status);
            } catch (e) {
                console.warn('BLE status listener error', e);
            }
        });
    }
    onStatusChange(cb) {
        if (typeof cb === 'function') {
            this.listeners.add(cb);
        }
    }
    offStatusChange(cb) {
        if (cb && this.listeners.has(cb)) {
            this.listeners.delete(cb);
        }
    }
    isConnected() {
        return this.status === 'connected';
    }
    shouldUseBle() {
        return this.isConnected();
    }
    async connect() {
        if (!this.isSupported()) {
            throw new Error(t('ble.notSupported'));
        }
        if (this.isConnected() || this.status === 'connecting') {
            return;
        }
        this.setStatus('connecting');
        try {
            const device = await this.requestDevice();
            let server = await this.ensureGattConnection(device);
            let service;
            try {
                service = await server.getPrimaryService(BLE_CONFIG.serviceUuid);
            } catch (error) {
                if (error && error.name === 'NetworkError') {
                    server = await this.ensureGattConnection(device, true);
                    service = await server.getPrimaryService(BLE_CONFIG.serviceUuid);
                } else {
                    throw error;
                }
            }
            this.commandCharacteristic = await service.getCharacteristic(BLE_CONFIG.commandCharacteristicUuid);
            this.responseCharacteristic = await service.getCharacteristic(BLE_CONFIG.responseCharacteristicUuid);
            await this.responseCharacteristic.startNotifications();
            this.responseCharacteristic.addEventListener('characteristicvaluechanged', this.boundNotificationHandler);
            this.setStatus('connected');
        } catch (error) {
            console.error('BLE connect error', error);
            this.teardown();
            this.setStatus(this.isSupported() ? 'disconnected' : 'unsupported');
            throw error;
        }
    }
    async disconnect() {
        if (this.device) {
            try {
                this.device.removeEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
            } catch (e) {}
        }
        if (this.device && this.device.gatt && this.device.gatt.connected) {
            try {
                this.device.gatt.disconnect();
            } catch (e) {}
        }
        this.teardown();
        this.setStatus(this.isSupported() ? 'disconnected' : 'unsupported');
    }
    handleDeviceDisconnected() {
        this.teardown();
        this.setStatus(this.isSupported() ? 'disconnected' : 'unsupported');
    }
    teardown() {
        if (this.responseCharacteristic) {
            try {
                this.responseCharacteristic.removeEventListener('characteristicvaluechanged', this.boundNotificationHandler);
            } catch (e) {}
        }
        if (this.device) {
            try {
                this.device.removeEventListener('gattserverdisconnected', this.boundDeviceDisconnect);
            } catch (e) {}
        }
        this.server = null;
        this.commandCharacteristic = null;
        this.responseCharacteristic = null;
        this.responseBuffer = '';
        this.device = null;
        if (this.pending && this.pending.reject) {
            this.pending.reject(new Error(t('ble.disconnected')));
        }
        this.pending = null;
    }
    async sendRequest(message) {
        if (!this.commandCharacteristic) {
            throw new Error(t('ble.disconnected'));
        }
        if (this.pending) {
            throw new Error(t('ble.requestRejected'));
        }
        const framed = JSON.stringify(message) + '\n';
        const encoded = bleTextEncoder.encode(framed);
        return new Promise((resolve, reject) => {
            const timeoutId = setTimeout(() => {
                if (this.pending && this.pending.reject) {
                    this.pending.reject(new Error(t('ble.timeout')));
                }
            }, BLE_CONFIG.responseTimeoutMs);
            this.pending = {
                resolve: (response) => {
                    clearTimeout(timeoutId);
                    this.pending = null;
                    resolve(response);
                },
                reject: (err) => {
                    clearTimeout(timeoutId);
                    this.pending = null;
                    reject(err);
                }
            };
            this.writeChunks(encoded).catch(error => {
                if (this.pending && this.pending.reject) {
                    this.pending.reject(error);
                } else {
                    reject(error);
                }
            });
        });
    }
    async writeChunks(encoded) {
        const chunkSize = BLE_CONFIG.maxChunkSize;
        for (let offset = 0; offset < encoded.length; offset += chunkSize) {
            const chunk = encoded.slice(offset, Math.min(offset + chunkSize, encoded.length));
            await this.commandCharacteristic.writeValueWithResponse(chunk);
        }
    }
    handleNotification(event) {
        const value = event.target.value;
        this.responseBuffer += bleTextDecoder.decode(value);
        let newlineIndex;
        while ((newlineIndex = this.responseBuffer.indexOf('\n')) !== -1) {
            const message = this.responseBuffer.slice(0, newlineIndex).trim();
            this.responseBuffer = this.responseBuffer.slice(newlineIndex + 1);
            if (!message) {
                continue;
            }
            let parsed;
            try {
                parsed = JSON.parse(message);
            } catch (e) {
                console.warn('Invalid BLE payload', message);
                continue;
            }
            if (this.pending && this.pending.resolve) {
                this.pending.resolve(parsed);
            }
        }
    }
    enqueueRequest(taskFn) {
        const next = this.requestQueue.then(() => taskFn()).then(async (result) => {
            await new Promise(resolve => setTimeout(resolve, 20));
            return result;
        });
        this.requestQueue = next.catch(() => {});
        return next;
    }
    clearQueue() {
        // Réinitialise la queue pour éviter l'accumulation
        console.log('[BLE] Queue vidée pour éviter l\'accumulation');

        // Annuler la requête en attente si elle existe
        if (this.pending && this.pending.reject) {
            this.pending.reject(new Error('Queue cleared'));
            this.pending = null;
        }

        this.requestQueue = Promise.resolve();
    }
    async waitForQueue() {
        // Attend que la queue soit vide sans annuler les requêtes en cours
        console.log('[BLE] Attente de la fin de la queue...');
        await this.requestQueue.catch(() => {});
        console.log('[BLE] Queue vide, prêt pour la prochaine requête');
    }
}
const bleTransport = new BleTransport();
bleTransportInstance = bleTransport;
if (!wifiOnline) {
    maybeAutoConnectBle();
}
const nativeFetch = window.fetch.bind(window);
function shouldUseBleForRequest(url, isApiCallOverride) {
    if (!bleTransport.shouldUseBle()) {
        return false;
    }
    if (typeof isApiCallOverride === 'boolean') {
        return isApiCallOverride;
    }
    return isApiRequest(url);
}
window.fetch = async function(input, init = {}) {
    const request = new Request(input, init);
    const isApiCall = isApiRequest(request.url, input);

    if (isApiCall) {
        await waitForApiConnection();
    }
    if (!shouldUseBleForRequest(request.url, isApiCall)) {
        return nativeFetch(request);
    }
    const headers = {};
    request.headers.forEach((value, key) => {
        headers[key] = value;
    });
    let bodyText = undefined;
    const method = (request.method || 'GET').toUpperCase();
    if (method !== 'GET' && method !== 'HEAD') {
        try {
            bodyText = await request.clone().text();
        } catch (e) {
            bodyText = undefined;
        }
    }
    const urlObj = normalizeUrl(request.url);
    let requestPath;
    if (urlObj) {
        const normalizedPath = urlObj.pathname.replace(/\\/g, '/');
        requestPath = normalizedPath + urlObj.search;
    } else if (typeof request.url === 'string') {
        const trimmed = request.url.trim();
        const withoutScheme = trimmed.replace(/^[a-zA-Z]+:\/*/i, '');
        const withoutDrive = withoutScheme.replace(/^([a-zA-Z]:)/, '');
        const sanitized = withoutDrive.replace(/\\/g, '/');
        requestPath = sanitized.startsWith('/') ? sanitized : '/' + sanitized;
    } else {
        requestPath = '/';
    }
    try {
        const bleResponse = await bleTransport.enqueueRequest(() => bleTransport.sendRequest({
            method,
            path: requestPath,
            headers,
            body: bodyText
        }));
        const responseHeaders = new Headers(bleResponse.headers || { 'Content-Type': 'application/json' });
        return new Response(bleResponse.body || '', {
            status: bleResponse.status || 200,
            statusText: bleResponse.statusText || 'OK',
            headers: responseHeaders
        });
    } catch (error) {
        console.error('BLE fetch error', error);
        throw error;
    }
};
let lastBleStatus = bleTransport.getStatus();
bleTransport.onStatusChange((status) => {
    updateBleUiState();
    if (status === 'connected') {
        showNotification('ble-notification', t('ble.toastConnected'), 'success');
        updateApiConnectionState();
    } else if (status === 'disconnected' && lastBleStatus === 'connected') {
        showNotification('ble-notification', t('ble.toastDisconnected'), 'info');
        updateApiConnectionState();
    } else if (status === 'connecting') {
        updateApiConnectionState();
    }
    lastBleStatus = status;
});
const simulationSections = [
    {
        titleKey: 'simulation.turnSignals',
        events: [
            { id: 'TURN_LEFT', labelKey: 'simulation.left' },
            { id: 'TURN_RIGHT', labelKey: 'simulation.right' },
            { id: 'TURN_HAZARD', labelKey: 'simulation.hazard' }
        ]
    },
    {
        titleKey: 'simulation.charging',
        events: [
            { id: 'CHARGING', labelKey: 'simulation.chargingNow' },
            { id: 'CHARGE_COMPLETE', labelKey: 'simulation.chargeComplete' },
            { id: 'CHARGING_STARTED', labelKey: 'simulation.chargingStarted' },
            { id: 'CHARGING_STOPPED', labelKey: 'simulation.chargingStopped' }
        ]
    },
    {
        titleKey: 'simulation.chargingHardware',
        events: [
            { id: 'CHARGING_CABLE_CONNECTED', labelKey: 'simulation.cableConnected' },
            { id: 'CHARGING_CABLE_DISCONNECTED', labelKey: 'simulation.cableDisconnected' },
            { id: 'CHARGING_PORT_OPENED', labelKey: 'simulation.portOpened' }
        ]
    },
    {
        titleKey: 'simulation.doors',
        events: [
            { id: 'DOOR_OPEN', labelKey: 'simulation.doorOpen' },
            { id: 'DOOR_CLOSE', labelKey: 'simulation.doorClose' }
        ]
    },
    {
        titleKey: 'simulation.lock',
        events: [
            { id: 'LOCKED', labelKey: 'simulation.locked' },
            { id: 'UNLOCKED', labelKey: 'simulation.unlocked' }
        ]
    },
    {
        titleKey: 'simulation.driving',
        events: [
            { id: 'BRAKE_ON', labelKey: 'simulation.brakeOn' },
            { id: 'BRAKE_OFF', labelKey: 'simulation.brakeOff' },
            { id: 'SPEED_THRESHOLD', labelKey: 'simulation.speedThreshold' }
        ]
    },
    {
        titleKey: 'simulation.autopilot',
        events: [
            { id: 'AUTOPILOT_ENGAGED', labelKey: 'simulation.autopilotEngaged' },
            { id: 'AUTOPILOT_DISENGAGED', labelKey: 'simulation.autopilotDisengaged' }
        ]
    },
    {
        titleKey: 'simulation.gear',
        events: [
            { id: 'GEAR_DRIVE', labelKey: 'simulation.gearDrive' },
            { id: 'GEAR_REVERSE', labelKey: 'simulation.gearReverse' },
            { id: 'GEAR_PARK', labelKey: 'simulation.gearPark' }
        ]
    },
    {
        titleKey: 'simulation.blindspot',
        events: [
            { id: 'BLINDSPOT_LEFT', labelKey: 'simulation.blindspotLeft' },
            { id: 'BLINDSPOT_RIGHT', labelKey: 'simulation.blindspotRight' },
            { id: 'BLINDSPOT_WARNING', labelKey: 'simulation.blindspotWarning' }
        ]
    },
    {
        titleKey: 'simulation.sentry',
        events: [
            { id: 'SENTRY_MODE_ON', labelKey: 'simulation.sentryOn' },
            { id: 'SENTRY_MODE_OFF', labelKey: 'simulation.sentryOff' },
            { id: 'SENTRY_ALERT', labelKey: 'simulation.sentryAlert' }
        ]
    }
];
// Language & theme management
currentLang = localStorage.getItem('language') || currentLang;
currentTheme = localStorage.getItem('theme') || currentTheme;
function updateBleUiState() {
    const button = document.getElementById('ble-connect-button');
    const statusValue = document.getElementById('ble-status-text');
    const statusItem = document.getElementById('ble-status-item');
    if (!button || !statusValue) {
        return;
    }
    const supported = bleTransport.isSupported();
    if (statusItem) {
        statusItem.hidden = !supported;
    }
    if (!supported) {
        button.disabled = true;
        button.textContent = BLE_BUTTON_ICONS.unsupported;
        button.title = t('ble.notSupported');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value status-offline';
        statusValue.dataset.i18n = 'ble.statusUnsupported';
        statusValue.textContent = t('ble.statusUnsupported');
        updateConnectionOverlay();
        return;
    }
    const status = bleTransport.getStatus();
    if (status === 'connected') {
        button.disabled = false;
        button.textContent = BLE_BUTTON_ICONS.disconnect;
        button.title = t('ble.disconnect');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value status-online';
        statusValue.dataset.i18n = 'ble.connected';
        statusValue.textContent = t('ble.connected');
    } else if (status === 'connecting') {
        button.disabled = true;
        button.textContent = BLE_BUTTON_ICONS.connecting;
        button.title = t('ble.connecting');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value';
        statusValue.dataset.i18n = 'ble.connecting';
        statusValue.textContent = t('ble.connecting');
    } else {
        button.disabled = false;
        button.textContent = BLE_BUTTON_ICONS.connect;
        button.title = t('ble.connect');
        button.setAttribute('aria-label', button.title);
        statusValue.className = 'status-value status-offline';
        statusValue.dataset.i18n = 'ble.disconnected';
        statusValue.textContent = t('ble.disconnected');
    }
    updateOtaBleNote();
    updateConnectionOverlay();
}
function updateOtaBleNote() {
    const note = document.getElementById('ota-ble-note');
    if (!note) {
        return;
    }
    const useBle = bleTransport && typeof bleTransport.shouldUseBle === 'function'
        ? bleTransport.shouldUseBle()
        : false;
    note.style.display = useBle ? 'block' : 'none';
    const fileInput = document.getElementById('firmware-file');
    const uploadBtn = document.getElementById('ota-upload-btn');
    if (fileInput) {
        fileInput.style.display = useBle ? 'none' : 'block';
        fileInput.disabled = useBle;
    }
    if (uploadBtn) {
        uploadBtn.style.display = useBle ? 'none' : 'inline-block';
        uploadBtn.disabled = useBle;
    }
}
async function toggleBleConnection() {
    if (!bleTransport.isSupported()) {
        showNotification('ble-notification', t('ble.notSupported'), 'error');
        return;
    }
    const status = bleTransport.getStatus();
    if (status === 'connecting') {
        return;
    }
    try {
        if (bleTransport.isConnected()) {
            await bleTransport.disconnect();
        } else {
            await bleTransport.connect();
        }
    } catch (error) {
        console.error('BLE toggle error', error);
        const message = t('ble.toastError') + (error && error.message ? ' - ' + error.message : '');
        showNotification('ble-notification', message, 'error');
    } finally {
        updateBleUiState();
    }
}
function setLanguage(lang) {
    if (!lang || lang === currentLang) {
        updateLanguageSelector();
        return;
    }
    currentLang = lang;
    localStorage.setItem('language', currentLang);
    applyTranslations();
    renderSimulationSections();
    renderEventsTable();
}
function toggleLanguage() {
    setLanguage(currentLang === 'fr' ? 'en' : 'fr');
}
function updateLanguageSelector() {
    const select = document.getElementById('language-select');
    if (select) {
        select.value = currentLang;
    }
}
function applyTheme() {
    document.body.classList.toggle('light-theme', currentTheme === 'light');
    const select = document.getElementById('theme-select');
    if (select) {
        select.value = currentTheme;
    }
}
function setTheme(theme) {
    if (!theme) return;
    currentTheme = theme;
    localStorage.setItem('theme', currentTheme);
    applyTheme();
}
function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        const keys = key.split('.');
        let value = translations[currentLang];
        for (let k of keys) {
            value = value[k];
        }
        if (value) {
            el.textContent = value;
        }
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
        const key = el.getAttribute('data-i18n-placeholder');
        const keys = key.split('.');
        let value = translations[currentLang];
        for (let k of keys) {
            value = value[k];
        }
        if (value) {
            el.placeholder = value;
        }
    });
    // Update select options
    updateSelectOptions();
    refreshEffectOptionLabels();
    updateLanguageSelector();
    const themeSelect = document.getElementById('theme-select');
    if (themeSelect) {
        themeSelect.value = currentTheme;
    }
    updateBleUiState();
}
function updateSelectOptions() {
    const effectSelect = document.getElementById('effect-select');
    if (effectSelect) {
        Array.from(effectSelect.options).forEach(opt => {
            const key = opt.getAttribute('data-i18n');
            if (key) {
                const keys = key.split('.');
                let value = translations[currentLang];
                for (let k of keys) {
                    value = value[k];
                }
                if (value) opt.textContent = value;
            }
        });
    }
}
function refreshEffectOptionLabels() {
    const selects = document.querySelectorAll('select[data-effect-options]');
    selects.forEach(select => {
        Array.from(select.options).forEach(opt => {
            const effectId = opt.value;
            if (!effectId) return;
            const translated = translateEffectId(effectId);
            if (translated) {
                opt.textContent = translated;
            } else {
                const fallback = opt.getAttribute('data-effect-name');
                if (fallback) {
                    opt.textContent = fallback;
                }
            }
        });
    });
}
function t(key, ...params) {
    const keys = key.split('.');
    let value = translations[currentLang];
    for (let k of keys) {
        value = value[k];
    }
    let result = value || key;
    // Replace {0}, {1}, etc. with parameters
    params.forEach((param, index) => {
        result = result.replace(`{${index}}`, param);
    });
    return result;
}
// Gestion des tabs
// État des toggles de simulation (persiste entre les changements d'onglets)
let simulationTogglesState = {};
let simulationAutoStopTimers = {};
function isSimulationEventEnabled(eventType) {
    const config = getSimulationEventConfig(eventType);
    if (!config) {
        return true;
    }
    return config.enabled !== false;
}
function switchTab(tabName, evt) {
    const tabs = document.querySelectorAll('.tabs .tab');
    tabs.forEach(tab => tab.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));
    const button = (evt && evt.currentTarget) || document.querySelector(`.tabs .tab[data-tab="${tabName}"]`);
    if (button) {
        button.classList.add('active');
    }
    const target = document.getElementById(tabName + '-tab');
    if (target) {
        target.classList.add('active');
    }
    activeTabName = tabName;

    // Gérer le polling audio et FFT selon l'onglet actif
    if (tabName === 'config') {
      startAudioDataPolling(); // Démarrera seulement si audioEnabled est true
    } else {
      stopAudioDataPolling(); // Arrêter le polling si on quitte l'onglet config
    }

    // Load data for specific tabs
    if (tabName === 'events-config') {
        loadEventsConfig();
    } else if (tabName === 'config') {
        loadHardwareConfig();
    } else if (tabName === 'simulation') {
        // Restaurer l'état des toggles de simulation
        restoreSimulationToggles();
    } else if (tabName === 'profiles') {
        loadProfiles();
    }
}
function restoreSimulationToggles() {
    // Restaurer l'état de tous les toggles
    Object.keys(simulationTogglesState).forEach(eventId => {
        const checkbox = document.getElementById('toggle-' + eventId);
        if (checkbox) {
            checkbox.checked = simulationTogglesState[eventId];
        }
        setToggleContainerState(eventId, simulationTogglesState[eventId]);
    });
    // Restaurer le toggle du mode nuit
    const nightModeCheckbox = document.getElementById('toggle-nightmode');
    if (nightModeCheckbox && simulationTogglesState['nightmode'] !== undefined) {
        nightModeCheckbox.checked = simulationTogglesState['nightmode'];
    }
}
function getSimulationEventConfig(eventType) {
    if (!eventsConfigData || eventsConfigData.length === 0) {
        return null;
    }
    const normalized = typeof eventType === 'string' ? eventType : String(eventType);
    return eventsConfigData.find(evt => evt.event === normalized) || null;
}
function cancelSimulationAutoStop(eventType) {
    if (simulationAutoStopTimers[eventType]) {
        clearTimeout(simulationAutoStopTimers[eventType]);
        delete simulationAutoStopTimers[eventType];
    }
}
function scheduleSimulationAutoStop(eventType, durationMs) {
    if (!durationMs || durationMs <= 0) {
        return;
    }
    cancelSimulationAutoStop(eventType);
    simulationAutoStopTimers[eventType] = setTimeout(() => {
        autoStopSimulationEvent(eventType);
    }, durationMs);
}
async function autoStopSimulationEvent(eventType) {
    cancelSimulationAutoStop(eventType);
    const checkbox = document.getElementById('toggle-' + eventType);
    if (checkbox) {
        checkbox.checked = false;
    }
    await toggleEvent(eventType, false);
}
async function getSimulationEventDuration(eventType) {
    try {
        await ensureEventsConfigData();
    } catch (error) {
        return 0;
    }
    const config = getSimulationEventConfig(eventType);
    return config && typeof config.duration === 'number' ? config.duration : 0;
}
function renderSimulationSections() {
    const container = document.getElementById('simulation-sections');
    if (!container) return;
    container.innerHTML = '';
    simulationSections.forEach(section => {
        const sectionDiv = document.createElement('div');
        sectionDiv.className = 'section';
        const title = document.createElement('div');
        title.className = 'section-title';
        title.style.fontSize = '16px';
        title.setAttribute('data-i18n', section.titleKey);
        title.textContent = t(section.titleKey);
        sectionDiv.appendChild(title);
        const grid = document.createElement('div');
        grid.className = 'simulation-grid';
        section.events.forEach(event => {
            const toggleContainer = document.createElement('div');
            toggleContainer.className = 'event-toggle-container';
            toggleContainer.id = 'event-toggle-' + event.id;
            const eventConfig = getSimulationEventConfig(event.id);
            const eventEnabledInConfig = isSimulationEventEnabled(event.id);
            if (eventConfig && !eventEnabledInConfig) {
                simulationTogglesState[event.id] = false;
            }
            const label = document.createElement('label');
            label.className = 'event-toggle-label';
            label.htmlFor = 'toggle-' + event.id;
            label.setAttribute('data-i18n', event.labelKey);
            label.textContent = t(event.labelKey);
            const toggle = document.createElement('label');
            toggle.className = 'toggle-switch';
            const input = document.createElement('input');
            input.type = 'checkbox';
            input.id = 'toggle-' + event.id;
            input.checked = eventEnabledInConfig && !!simulationTogglesState[event.id];
            input.disabled = !eventEnabledInConfig;
            input.addEventListener('change', (e) => toggleEvent(event.id, e.target.checked));
            const slider = document.createElement('span');
            slider.className = 'toggle-slider';
            toggle.appendChild(input);
            toggle.appendChild(slider);
            if (simulationTogglesState[event.id]) {
                toggleContainer.classList.add('active');
            }
            toggleContainer.classList.toggle('disabled', !eventEnabledInConfig);
            toggleContainer.appendChild(label);
            toggleContainer.appendChild(toggle);
            grid.appendChild(toggleContainer);
        });
        sectionDiv.appendChild(grid);
        container.appendChild(sectionDiv);
    });
}
function setToggleContainerState(eventType, isActive) {
    const container = document.getElementById('event-toggle-' + eventType);
    if (container) {
        container.classList.toggle('active', !!isActive);
    }
}
// Conversion pourcentage <-> 0-255
function percentTo255(percent) {
    return Math.round((percent * 255) / 100);
}
function to255ToPercent(value) {
    return Math.round((value * 100) / 255);
}
// Mise à jour des sliders avec pourcentage (seulement ceux qui existent)
const nightBrightnessSlider = document.getElementById('night-brightness-slider');
if (nightBrightnessSlider) {
    nightBrightnessSlider.oninput = function() {
        document.getElementById('night-brightness-value').textContent = this.value + '%';
    };
}
const defaultBrightnessSlider = document.getElementById('default-brightness-slider');
if (defaultBrightnessSlider) {
    defaultBrightnessSlider.oninput = function() {
        document.getElementById('default-brightness-value').textContent = this.value + '%';
        scheduleDefaultEffectSave();
    };
}
const defaultSpeedSlider = document.getElementById('default-speed-slider');
if (defaultSpeedSlider) {
    defaultSpeedSlider.oninput = function() {
        document.getElementById('default-speed-value').textContent = this.value + '%';
        scheduleDefaultEffectSave();
    };
}
const defaultColorInput = document.getElementById('default-color1');
if (defaultColorInput) {
    defaultColorInput.addEventListener('change', scheduleDefaultEffectSave);
}
const defaultEffectSelect = document.getElementById('default-effect-select');
if (defaultEffectSelect) {
    defaultEffectSelect.addEventListener('change', scheduleDefaultEffectSave);
}
// Notification helper
function showNotification(elementId, message, type, timeout = 2000) {
    const notification = document.getElementById(elementId);
    notification.textContent = message;
    notification.className = 'notification ' + type + ' show';
    setTimeout(() => {
        notification.classList.remove('show');
    }, timeout);
}
async function parseApiResponse(response) {
    const rawText = await response.text();
    let data = null;
    if (rawText) {
        try {
            data = JSON.parse(rawText);
        } catch (e) {
            data = null;
        }
    }
    const status = data && typeof data.status === 'string' ? data.status : null;
    const success = response.ok && (status === null || status === 'ok');
    return { success, data, raw: rawText };
}
// Events Configuration
let eventsConfigData = [];
const EVENT_SAVE_DEBOUNCE_MS = 700;
const DEFAULT_EFFECT_DEBOUNCE_MS = 700;
const eventAutoSaveTimers = new Map();
let defaultEffectSaveTimer = null;
let eventsConfigLoadingPromise = null;
function getDefaultEffectId() {
    if (effectsList.length === 0) {
        return 'OFF';
    }
    const offEffect = effectsList.find(effect => effect.id === 'OFF');
    if (offEffect) {
        return offEffect.id;
    }
    const nonCan = effectsList.find(effect => !effect.can_required);
    if (nonCan) {
        return nonCan.id;
    }
    return effectsList[0].id;
}
async function ensureEventsConfigData(forceRefresh = false) {
    if (!forceRefresh && eventsConfigData.length > 0) {
        return eventsConfigData;
    }
    if (!forceRefresh && eventsConfigLoadingPromise) {
        return eventsConfigLoadingPromise;
    }
    const fetchPromise = (async () => {
        try {
            const response = await fetch(API_BASE + '/api/events');
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            const data = await response.json();
            eventsConfigData = data.events || [];
            return eventsConfigData;
        } catch (error) {
            console.error('Failed to load events config:', error);
            throw error;
        }
    })();
    if (!forceRefresh) {
        eventsConfigLoadingPromise = fetchPromise.finally(() => {
            eventsConfigLoadingPromise = null;
        });
        return eventsConfigLoadingPromise;
    }
    return fetchPromise;
}
async function loadEventsConfig() {
    const loading = document.getElementById('events-loading');
    const content = document.getElementById('events-content');
    loading.style.display = 'block';
    content.style.display = 'none';
    try {
        await ensureEventsConfigData(true);
        renderEventsTable();
        renderSimulationSections();
        loading.style.display = 'none';
        content.style.display = 'block';
    } catch (e) {
        console.error('Error:', e);
        showNotification('events-notification', t('eventsConfig.loadError'), 'error');
        loading.style.display = 'none';
    }
}
function renderEventsTable() {
    const tbody = document.getElementById('events-table-body');
    tbody.innerHTML = '';
    // If no data from API, create default rows for all events
    if (eventsConfigData.length === 0) {
        const defaultEffectId =
            effectsList.find(effect => !effect.can_required)?.id ||
            effectsList[0]?.id ||
            'OFF';
        if (eventTypesList.length > 0) {
            eventTypesList
                .filter(evt => evt.id !== 'NONE')
                .forEach(evt => {
                    eventsConfigData.push({
                        event: evt.id,
                        effect: defaultEffectId,
                        brightness: 128,
                        speed: 128,
                        color: 0xFF0000,
                        duration: 0,
                        priority: 100,
                        enabled: true,
                        action_type: 0,
                        profile_id: -1,
                        can_switch_profile: false
                    });
                });
        }
    }
    eventsConfigData.forEach((event, index) => {
        // Skip CAN_EVENT_NONE only
        if (event.event === 'NONE') {
            return;
        }
        const row = document.createElement('tr');
        const eventName = getEventName(event.event);
        const actionType = event.action_type !== undefined ? event.action_type : 0;
        const canSwitchProfile = event.can_switch_profile || false;
        const profileId = event.profile_id !== undefined ? event.profile_id : -1;
        // Générer les options d'action
        let actionOptions = '';
        actionOptions += `<option value="0" ${actionType === 0 ? 'selected' : ''}>${t('eventsConfig.applyEffect')}</option>`;
        if (canSwitchProfile) {
            actionOptions += `<option value="1" ${actionType === 1 ? 'selected' : ''}>${t('eventsConfig.switchProfile')}</option>`;
        }
        // Générer les options de profil
        const profileSelect = document.getElementById('profile-select');
        let profileOptions = '<option value="-1">--</option>';
        if (profileSelect) {
            for (let i = 0; i < profileSelect.options.length; i++) {
                const opt = profileSelect.options[i];
                profileOptions += `<option value="${opt.value}" ${profileId == opt.value ? 'selected' : ''}>${opt.text}</option>`;
            }
        }
        row.innerHTML = `
            <td class="event-name-cell">${eventName}</td>
            <td>
                <select onchange="updateEventConfig(${index}, 'action_type', parseInt(this.value)); renderEventsTable();" ${!canSwitchProfile ? 'disabled style="display:none"' : ''}>
                    ${actionOptions}
                </select>
            </td>
            <td>
                <select onchange="updateEventConfig(${index}, 'profile_id', parseInt(this.value))" ${actionType === 0 || !canSwitchProfile ? 'disabled style="display:none"' : ''}>
                    ${profileOptions}
                </select>
            </td>
            <td>
                <select data-effect-options="true" onchange="updateEventConfig(${index}, 'effect', this.value)" ${actionType === 1 ? 'disabled style="display:none"' : ''}>
                    ${effectsList
                        .filter(effect => !effect.audio_effect)
                        .map(effect =>
                            `<option value="${effect.id}" data-effect-name="${effect.name}" ${event.effect == effect.id ? 'selected' : ''}>${getEffectName(effect.id)}</option>`
                        ).join('')}
                </select>
            </td>
            <td>
                <input type="number" min="0" max="255" value="${event.brightness}"
                    onchange="updateEventConfig(${index}, 'brightness', parseInt(this.value))" ${actionType === 1 ? 'disabled style="display:none"' : ''}>
            </td>
            <td>
                <input type="number" min="0" max="255" value="${event.speed}"
                    onchange="updateEventConfig(${index}, 'speed', parseInt(this.value))" ${actionType === 1 ? 'disabled style="display:none"' : ''}>
            </td>
            <td>
                <input type="color" value="#${event.color.toString(16).padStart(6, '0')}"
                    onchange="updateEventConfig(${index}, 'color', parseInt(this.value.substring(1), 16))" ${actionType === 1 ? 'disabled style="display:none"' : ''}>
            </td>
            <td>
                <input type="number" min="0" max="60000" step="100" value="${event.duration}"
                    onchange="updateEventConfig(${index}, 'duration', parseInt(this.value))" ${actionType === 1 ? 'disabled style="display:none"' : ''}>
            </td>
            <td>
                <input type="number" min="0" max="255" value="${event.priority}"
                    onchange="updateEventConfig(${index}, 'priority', parseInt(this.value))" ${actionType === 1 ? 'disabled style="display:none"' : ''}>
            </td>
            <td style="text-align: center;">
                <input type="checkbox" ${event.enabled ? 'checked' : ''}
                    onchange="updateEventConfig(${index}, 'enabled', this.checked)">
            </td>
        `;
        tbody.appendChild(row);
    });
}
function updateEventConfig(index, field, value) {
    eventsConfigData[index][field] = value;
    if (field === 'action_type') {
        const event = eventsConfigData[index];
        if (value === 0) {
            event.profile_id = -1;
        } else if (value === 1) {
            event.effect = getDefaultEffectId();
        } else if (value === 2) {
            if (event.profile_id === undefined || event.profile_id === null) {
                event.profile_id = -1;
            }
            if (!event.effect) {
                event.effect = getDefaultEffectId();
            }
        }
    }
    scheduleEventAutoSave(index);
}
function scheduleEventAutoSave(index) {
    if (!eventsConfigData[index]) {
        return;
    }
    if (eventAutoSaveTimers.has(index)) {
        clearTimeout(eventAutoSaveTimers.get(index));
    }
    const timer = setTimeout(() => {
        eventAutoSaveTimers.delete(index);
        autoSaveEvent(index);
    }, EVENT_SAVE_DEBOUNCE_MS);
    eventAutoSaveTimers.set(index, timer);
}
function buildEventPayload(event) {
    if (!event) {
        return null;
    }
    const allowedKeys = [
        'event', 'effect', 'brightness', 'speed', 'color',
        'duration', 'priority', 'enabled', 'action_type', 'profile_id'
    ];
    const payload = {};
    allowedKeys.forEach(key => {
        if (event[key] !== undefined) {
            payload[key] = event[key];
        }
    });
    return payload;
}
async function autoSaveEvent(index) {
    const payload = buildEventPayload(eventsConfigData[index]);
    if (!payload) {
        return;
    }
    try {
        const response = await fetch(API_BASE + '/api/events/update', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ event: payload })
        });
        if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        }
    } catch (error) {
        console.error('Event auto-save failed', error);
        showNotification('events-notification', t('eventsConfig.saveError'), 'error');
    }
}
function scheduleDefaultEffectSave() {
    if (defaultEffectSaveTimer) {
        clearTimeout(defaultEffectSaveTimer);
    }
    defaultEffectSaveTimer = setTimeout(() => {
        defaultEffectSaveTimer = null;
        saveDefaultEffect(true);
    }, DEFAULT_EFFECT_DEBOUNCE_MS);
}
// Hardware Configuration
async function loadHardwareConfig() {
    try {
        const response = await fetch(API_BASE + '/api/config');
        const config = await response.json();
        if (config.led_count !== undefined) {
            document.getElementById('led-count').value = config.led_count;
        }
        if (config.data_pin !== undefined) {
            document.getElementById('data-pin').value = config.data_pin;
        }
        if (config.strip_reverse !== undefined) {
            document.getElementById('strip-reverse').checked = config.strip_reverse;
        }
    } catch (e) {
        console.error('Error:', e);
        showNotification('config-notification', t('config.loadError'), 'error');
    }
}
async function saveHardwareConfig() {
    const config = {
        led_count: parseInt(document.getElementById('led-count').value),
        data_pin: parseInt(document.getElementById('data-pin').value),
        strip_reverse: document.getElementById('strip-reverse').checked
    };
    try {
        const response = await fetch(API_BASE + '/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        if (response.ok) {
            showNotification('config-notification', t('config.saveSuccess'), 'success');
        } else {
            showNotification('config-notification', t('config.saveError'), 'error');
        }
    } catch (e) {
        console.error('Error:', e);
        showNotification('config-notification', t('config.saveError'), 'error');
    }
}
// Factory Reset
function confirmFactoryReset() {
    if (confirm(t('config.factoryResetConfirm'))) {
        performFactoryReset();
    }
}
async function performFactoryReset() {
    try {
        showNotification('config-notification', t('config.factoryResetInProgress'), 'info');
        const response = await fetch(API_BASE + '/api/factory-reset', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });
        const data = await response.json();
        if (response.ok && data.status === 'ok') {
            showNotification('config-notification', t('config.factoryResetSuccess'), 'success');
            // L'ESP32 va redémarrer, afficher un message
            setTimeout(() => {
                alert(t('config.deviceRestarting'));
                location.reload();
            }, 2000);
        } else {
            showNotification('config-notification', data.message || t('config.factoryResetError'), 'error');
        }
    } catch (e) {
        console.error('Error:', e);
        showNotification('config-notification', t('config.factoryResetError'), 'error');
    }
}
// Gestion des profils
async function loadProfiles() {
    try {
        const response = await fetch(API_BASE + '/api/profiles');
        const data = await response.json();
        const select = document.getElementById('profile-select');
        select.innerHTML = '';
        data.profiles.forEach(profile => {
            const option = document.createElement('option');
            option.value = profile.id;
            option.textContent = profile.name + (profile.active ? ' ✓' : '');
            if (profile.active) option.selected = true;
            select.appendChild(option);
        });
        document.getElementById('profile-status').textContent = data.active_name;
    } catch (e) {
        console.error('Erreur:', e);
    }
}
async function activateProfile() {
    const profileId = parseInt(document.getElementById('profile-select').value);
    try {
        await fetch(API_BASE + '/api/profile/activate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ profile_id: profileId })
        });
        loadProfiles();
        loadConfig();
    } catch (e) {
        console.error('Erreur:', e);
    }
}
function showNewProfileDialog() {
    document.getElementById('newProfileModal').classList.add('active');
}
function hideNewProfileDialog() {
    document.getElementById('newProfileModal').classList.remove('active');
}
async function createProfile() {
    const name = document.getElementById('new-profile-name').value;
    if (!name) return;
    try {
        const response = await fetch(API_BASE + '/api/profile/create', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name: name })
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            hideNewProfileDialog();
            document.getElementById('new-profile-name').value = '';
            loadProfiles();
            showNotification('profiles-notification', t('profiles.create') + ' ' + t('config.saveSuccess'), 'success');
        } else {
            const message = apiResult.data?.message || apiResult.raw || t('config.saveError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('profiles-notification', e.message || t('config.saveError'), 'error');
    }
}
async function deleteProfile() {
    const profileId = parseInt(document.getElementById('profile-select').value);
    if (!confirm(t('profiles.deleteConfirm'))) return;
    try {
        const response = await fetch(API_BASE + '/api/profile/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ profile_id: profileId })
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            loadProfiles();
            showNotification('profiles-notification', t('profiles.delete') + ' ' + t('config.saveSuccess'), 'success');
        } else {
            const message = apiResult.data?.message || apiResult.raw || t('config.saveError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('profiles-notification', e.message || t('config.saveError'), 'error');
    }
}
async function exportProfile() {
    const profileId = parseInt(document.getElementById('profile-select').value);
    if (profileId < 0) {
        showNotification('profiles-notification', t('profiles.selectProfile'), 'error');
        return;
    }
    try {
        const response = await fetch(API_BASE + '/api/profile/export?profile_id=' + profileId);
        if (response.ok) {
            const blob = await response.blob();
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.style.display = 'none';
            a.href = url;
            a.download = 'profile_' + profileId + '.json';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
            showNotification('profiles-notification', t('profiles.exportSuccess'), 'success');
        } else {
            showNotification('profiles-notification', t('profiles.exportError'), 'error');
        }
    } catch (e) {
        console.error('Erreur export:', e);
        showNotification('profiles-notification', t('profiles.exportError'), 'error');
    }
}
function showImportDialog() {
    const profileId = parseInt(document.getElementById('profile-select').value);
    if (profileId < 0) {
        showNotification('profiles-notification', t('profiles.selectProfile'), 'error');
        return;
    }
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = async (event) => {
            try {
                const profileData = JSON.parse(event.target.result);
                const response = await fetch(API_BASE + '/api/profile/import', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        profile_id: profileId,
                        profile_data: profileData
                    })
                });
                const result = await response.json();
                if (result.status === 'ok') {
                    showNotification('profiles-notification', t('profiles.importSuccess'), 'success');
                    loadProfiles();
                } else {
                    showNotification('profiles-notification', t('profiles.importError'), 'error');
                }
            } catch (e) {
                console.error('Erreur import:', e);
                showNotification('profiles-notification', t('profiles.importError'), 'error');
            }
        };
        reader.readAsText(file);
    };
    input.click();
}
async function saveProfileSettings() {
    const profileId = parseInt(document.getElementById('profile-select').value);
    const autoNightMode = document.getElementById('auto-night-mode').checked;
    const nightBrightness = percentTo255(parseInt(document.getElementById('night-brightness-slider').value));
    try {
        const response = await fetch(API_BASE + '/api/profile/update', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                profile_id: profileId,
                auto_night_mode: autoNightMode,
                night_brightness: nightBrightness
            })
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            showNotification('profiles-notification', t('config.saveSuccess'), 'success');
        } else {
            const message = apiResult.data?.message || apiResult.raw || t('config.saveError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Error saving profile settings:', e);
        showNotification('profiles-notification', e.message || t('config.saveError'), 'error');
    }
}
async function saveDefaultEffect(silent = false) {
    // Attendre que la queue BLE soit vide avant de sauvegarder
    if (bleTransportInstance && bleTransportInstance.waitForQueue) {
        await bleTransportInstance.waitForQueue();
    }

    const profileId = parseInt(document.getElementById('profile-select').value);
    const effectId = document.getElementById('default-effect-select').value;
    const effect = effectIdToEnum(effectId); // Convert string ID to numeric enum
    const brightness = percentTo255(parseInt(document.getElementById('default-brightness-slider').value));
    const speed = percentTo255(parseInt(document.getElementById('default-speed-slider').value));
    const color1 = parseInt(document.getElementById('default-color1').value.substring(1), 16);

    // Audio reactive
    const audioReactiveCheckbox = document.getElementById('default-audio-reactive');
    const audioReactive = audioReactiveCheckbox ? audioReactiveCheckbox.checked : false;

    try {
        const response = await fetch(API_BASE + '/api/profile/update/default', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                profile_id: profileId,
                effect: effect,
                brightness: brightness,
                speed: speed,
                color1: color1,
                audio_reactive: audioReactive
            })
        });
        const apiResult = await parseApiResponse(response);
        if (apiResult.success) {
            if (!silent) {
                showNotification('profiles-notification', t('profiles.saveDefault') + ' ' + t('config.saveSuccess'), 'success');
            }
        } else {
            const message = apiResult.data?.message || apiResult.raw || t('config.saveError');
            showNotification('profiles-notification', message, 'error');
        }
    } catch (e) {
        console.error('Error saving default effect:', e);
        showNotification('profiles-notification', e.message || t('config.saveError'), 'error');
    }
}
// Appliquer l'effet
async function applyEffect() {
    const effectId = parseInt(document.getElementById('effect-select').value);
    const config = {
        effect: effectId,
        brightness: percentTo255(parseInt(document.getElementById('brightness-slider').value)),
        speed: percentTo255(parseInt(document.getElementById('speed-slider').value)),
        color1: parseInt(document.getElementById('color1').value.substring(1), 16),
        color2: 0,
        color3: 0
    };
    try {
        await fetch(API_BASE + '/api/effect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        // Le backend active automatiquement le FFT si l'effet le nécessite
        // Recharger le statut FFT pour mettre à jour l'affichage
        await loadFFTStatus();
    } catch (e) {
        console.error('Erreur:', e);
    }
}
// Sauvegarder la configuration
async function saveConfig() {
    try {
        await fetch(API_BASE + '/api/save', { method: 'POST' });
        showNotification('profiles-notification', t('effects.save'), 'success');
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('profiles-notification', t('config.saveError'), 'error');
    }
}
// Assigner un effet à un événement
async function assignEventEffect() {
    const data = {
        event: parseInt(document.getElementById('can-event-select').value),
        effect: document.getElementById('event-effect-select').value,
        duration: parseInt(document.getElementById('event-duration').value),
        priority: parseInt(document.getElementById('event-priority-slider').value),
        brightness: percentTo255(parseInt(document.getElementById('brightness-slider').value)),
        speed: percentTo255(parseInt(document.getElementById('speed-slider').value)),
        color1: parseInt(document.getElementById('color1').value.substring(1), 16)
    };
    try {
        await fetch(API_BASE + '/api/event-effect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        showNotification('events-notification', t('canEvents.assignSuccess'), 'success');
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('events-notification', t('config.saveError'), 'error');
    }
}
// Mise à jour du statut
async function updateStatus() {
    try {
        const response = await fetch(API_BASE + '/api/status');
        const data = await response.json();
        document.getElementById('wifi-status').textContent = data.wifi_connected ? t('status.connected') : t('status.ap');
        document.getElementById('wifi-status').className = 'status-value ' + (data.wifi_connected ? 'status-online' : 'status-offline');
        document.getElementById('can-bus-status').textContent = data.can_bus_running ? t('status.connected') : t('status.disconnected');
        document.getElementById('can-bus-status').className = 'status-value ' + (data.can_bus_running ? 'status-online' : 'status-offline');
        document.getElementById('vehicle-status').textContent = data.vehicle_active ? t('status.active') : t('status.inactive');
        document.getElementById('vehicle-status').className = 'status-value ' + (data.vehicle_active ? 'status-online' : 'status-offline');
        if (data.active_profile_name) {
            document.getElementById('profile-status').textContent = data.active_profile_name;
        }
        // Données véhicule complètes
        if (data.vehicle && data.vehicle_active) {
            const v = data.vehicle;
            // État général
            document.getElementById('v-speed').textContent = v.speed.toFixed(1) + ' km/h';
            document.getElementById('v-gear').textContent = ['--', 'P', 'R', 'N', 'D'][v.gear] || '--';
            document.getElementById('v-brake').textContent = v.brake_pressed ? t('status.active') : t('status.inactive');
            document.getElementById('v-locked').textContent = v.locked ? t('vehicle.locked') : t('vehicle.unlocked');
            // Portes
            if (v.doors) {
                document.getElementById('v-door-fl').textContent = v.doors.front_left ? t('vehicle.open') : t('vehicle.closed');
                document.getElementById('v-door-fr').textContent = v.doors.front_right ? t('vehicle.open') : t('vehicle.closed');
                document.getElementById('v-door-rl').textContent = v.doors.rear_left ? t('vehicle.open') : t('vehicle.closed');
                document.getElementById('v-door-rr').textContent = v.doors.rear_right ? t('vehicle.open') : t('vehicle.closed');
                document.getElementById('v-trunk').textContent = v.doors.trunk ? t('vehicle.open') : t('vehicle.closed');
                document.getElementById('v-frunk').textContent = v.doors.frunk ? t('vehicle.open') : t('vehicle.closed');
            }
            // Charge
            if (v.charge) {
                document.getElementById('v-charging').textContent = v.charge.charging ? t('status.active') : t('status.inactive');
                document.getElementById('v-charge').textContent = v.charge.percent?.toFixed(1) + '%';
                document.getElementById('v-charge-power').textContent = v.charge.power_kw?.toFixed(1) + ' kW';
            }
            // Lumières
            if (v.lights) {
                document.getElementById('v-headlights').textContent = v.lights.headlights ? t('status.active') : t('status.inactive');
                document.getElementById('v-high-beams').textContent = v.lights.high_beams ? t('status.active') : t('status.inactive');
                document.getElementById('v-fog-lights').textContent = v.lights.fog_lights ? t('status.active') : t('status.inactive');
                document.getElementById('v-turn-signal').textContent = v.lights.turn_left ? t('simulation.left'): v.lights.turn_right ? t('simulation.right'): t('vehicle.off')
            }
            // Batterie et autres
            document.getElementById('v-battery-lv').textContent = v.battery_lv?.toFixed(2) + ' V';
            document.getElementById('v-battery-hv').textContent = v.battery_hv?.toFixed(2) + ' V';
            document.getElementById('v-odometer').textContent = v.odometer_km.toLocaleString() + ' km';
            // Sécurité
            if (v.safety) {
                document.getElementById('v-night').textContent = v.safety.night_mode ? t('status.active') : t('status.inactive');
                document.getElementById('v-brightness').textContent = v.safety.brightness;
                document.getElementById('v-blindspot-left').textContent = v.safety.blindspot_left ? t('status.active') : t('status.inactive');
                document.getElementById('v-blindspot-right').textContent = v.safety.blindspot_right ? t('status.active') : t('status.inactive');

                const sentryModeEl = document.getElementById('v-sentry-mode');
                if (sentryModeEl) {
                    if (typeof v.safety.sentry_mode === 'boolean') {
                        sentryModeEl.textContent = v.safety.sentry_mode ? t('simulation.sentryOn') : t('simulation.sentryOff');
                    } else {
                        sentryModeEl.textContent = t('vehicle.none');
                    }
                }

                const sentryRequestEl = document.getElementById('v-sentry-request');
                if (sentryRequestEl) {
                    const requestMap = {
                        AUTOPILOT_NOMINAL: t('vehicle.sentryNominal'),
                        AUTOPILOT_SENTRY: t('simulation.sentryOn'),
                        AUTOPILOT_SUSPEND: t('vehicle.sentrySuspend')
                    };
                    const requestState = v.safety.sentry_request;
                    sentryRequestEl.textContent = requestState ? (requestMap[requestState] || requestState) : t('vehicle.none');
                }

                const sentryAlertEl = document.getElementById('v-sentry-alert');
                if (sentryAlertEl) {
                    if (typeof v.safety.sentry_alert === 'boolean') {
                        sentryAlertEl.textContent = v.safety.sentry_alert ? t('simulation.sentryAlert') : t('vehicle.none');
                    } else {
                        sentryAlertEl.textContent = t('vehicle.none');
                    }
                }
            }
        } else {
            // Afficher des tirets quand il n'y a pas de données
            const fields = [
                'v-speed', 'v-gear', 'v-brake', 'v-locked',
                'v-door-fl', 'v-door-fr', 'v-door-rl', 'v-door-rr', 'v-trunk', 'v-frunk',
                'v-charging', 'v-charge', 'v-charge-power',
                'v-headlights', 'v-high-beams', 'v-fog-lights', 'v-turn-signal',
                'v-battery-lv', 'v-battery-hv', 'v-odometer', 'v-night', 'v-brightness', 'v-blindspot-left', 'v-blindspot-right',
                'v-sentry-mode', 'v-sentry-request', 'v-sentry-alert'
            ];
            fields.forEach(id => {
                const element = document.getElementById(id);
                if (element) element.textContent = '--';
            });
        }
    } catch (e) {
        console.error('Erreur:', e);
    } finally {
        // Planifier le prochain appel après avoir terminé (évite les bouchons)
        if (statusIntervalHandle !== null) {
            statusIntervalHandle = setTimeout(updateStatus, 2000);
        }
    }
}
// Chargement de la config
async function loadConfig() {
    try {
        const response = await fetch(API_BASE + '/api/config');
        const config = await response.json();
        // Convertir 0-255 en pourcentage
        const nightBrightnessPercent = to255ToPercent(config.night_brightness);
        // Charger uniquement les éléments qui existent encore
        const autoNightMode = document.getElementById('auto-night-mode');
        if (autoNightMode) {
            autoNightMode.checked = config.auto_night_mode;
        }
        const nightBrightnessSlider = document.getElementById('night-brightness-slider');
        if (nightBrightnessSlider) {
            nightBrightnessSlider.value = nightBrightnessPercent;
        }
        const nightBrightnessValue = document.getElementById('night-brightness-value');
        if (nightBrightnessValue) {
            nightBrightnessValue.textContent = nightBrightnessPercent + '%';
        }
        // Charger aussi l'effet par défaut du profil actif
        loadActiveProfileDefaultEffect();
    } catch (e) {
        console.error('Erreur:', e);
    }
}
// Charger l'effet par défaut du profil actif
async function loadActiveProfileDefaultEffect() {
    try {
        const response = await fetch(API_BASE + '/api/profiles');
        const data = await response.json();
        const activeProfile = data.profiles.find(p => p.active);
        if (activeProfile && activeProfile.default_effect) {
            const defaultEffect = activeProfile.default_effect;
            // Convert numeric enum to string ID for the dropdown
            const effectId = effectEnumToId(defaultEffect.effect);
            document.getElementById('default-effect-select').value = effectId;
            const defBrightnessPercent = to255ToPercent(defaultEffect.brightness);
            const defSpeedPercent = to255ToPercent(defaultEffect.speed);
            document.getElementById('default-brightness-slider').value = defBrightnessPercent;
            document.getElementById('default-brightness-value').textContent = defBrightnessPercent + '%';
            document.getElementById('default-speed-slider').value = defSpeedPercent;
            document.getElementById('default-speed-value').textContent = defSpeedPercent + '%';
            document.getElementById('default-color1').value = '#' + defaultEffect.color1.toString(16).padStart(6, '0');

            // Audio reactive
            const audioReactiveCheckbox = document.getElementById('default-audio-reactive');
            if (audioReactiveCheckbox && defaultEffect.audio_reactive !== undefined) {
                audioReactiveCheckbox.checked = defaultEffect.audio_reactive;
            }
        }
    } catch (e) {
        console.error('Error loading default effect:', e);
    }
}

// ============================================================================
// AUDIO FUNCTIONS
// ============================================================================

let audioEnabled = false;
let audioUpdateInterval = null;

// Charger le statut audio
async function loadAudioStatus() {
    try {
        const response = await fetch(API_BASE + '/api/audio/status');
        const data = await response.json();

        audioEnabled = data.enabled;
        document.getElementById('audio-enable').checked = audioEnabled;
        document.getElementById('audio-sensitivity').value = data.sensitivity;
        document.getElementById('audio-sensitivity-value').textContent = data.sensitivity;
        document.getElementById('audio-gain').value = data.gain;
        document.getElementById('audio-gain-value').textContent = data.gain;
        document.getElementById('audio-auto-gain').checked = data.autoGain;

        // Mettre à jour le statut
        const statusEl = document.getElementById('audio-status');
        if (statusEl) {
            statusEl.textContent = t(`audio.${audioEnabled ? 'enabled' : 'disabled'}`);
            statusEl.style.color = audioEnabled ? '#10B981' : 'var(--color-muted)';
        }

        // Afficher/masquer les paramètres
        const settingsEl = document.getElementById('audio-settings');
        if (settingsEl) {
            settingsEl.style.display = audioEnabled ? 'block' : 'none';
        }

        if (audioEnabled) {
            startAudioDataPolling();
        }
    } catch (error) {
        console.error('Failed to load audio status:', error);
    }
}

// Activer/désactiver le micro
async function toggleAudio(enabled) {
    try {
        const response = await fetch(API_BASE + '/api/audio/enable', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ enabled })
        });

        if (response.ok) {
            audioEnabled = enabled;

            // Mettre à jour le statut
            const statusEl = document.getElementById('audio-status');
            if (statusEl) {
                statusEl.textContent = t(`audio.${enabled ? 'enabled' : 'disabled'}`);
                statusEl.style.color = enabled ? '#10B981' : 'var(--color-muted)';
            }

            // Afficher/masquer les paramètres
            const settingsEl = document.getElementById('audio-settings');
            if (settingsEl) {
                settingsEl.style.display = enabled ? 'block' : 'none';
            }

            if (enabled) {
                startAudioDataPolling();
            } else {
                stopAudioDataPolling();
            }

            showNotification('audio', enabled ? 'Audio enabled' : 'Audio disabled', 'success');
        }
    } catch (error) {
        console.error('Failed to toggle audio:', error);
        showNotification('audio', 'Failed to toggle audio', 'error');
    }
}

// Mettre à jour les valeurs des sliders
function updateAudioValue(param, value) {
    document.getElementById(`audio-${param}-value`).textContent = value;
}

// Sauvegarder la configuration audio
async function saveAudioConfig() {
    const config = {
        sensitivity: parseInt(document.getElementById('audio-sensitivity').value),
        gain: parseInt(document.getElementById('audio-gain').value),
        autoGain: document.getElementById('audio-auto-gain').checked
        // fftEnabled est géré automatiquement selon l'effet sélectionné
    };

    try {
        const response = await fetch(API_BASE + '/api/audio/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        if (response.ok) {
            showNotification('audio', 'Audio configuration saved', 'success');
        }
    } catch (error) {
        console.error('Failed to save audio config:', error);
        showNotification('audio', 'Failed to save audio config', 'error');
    }
}

// Récupérer les données audio et FFT en temps réel (un seul appel)
async function updateAudioData() {
    try {
        // Un seul appel qui retourne audio + FFT
        const response = await fetch(API_BASE + '/api/audio/data');
        const data = await response.json();

        // Mise à jour des données audio de base
        if (data.available !== false) {
            document.getElementById('audio-amplitude-value').textContent =
                (data.amplitude * 100).toFixed(0) + '%';
            document.getElementById('audio-bpm-value').textContent =
                data.bpm > 0 ? data.bpm.toFixed(1) : '-';
            document.getElementById('audio-bass-value').textContent =
                (data.bass * 100).toFixed(0) + '%';
            document.getElementById('audio-mid-value').textContent =
                (data.mid * 100).toFixed(0) + '%';
            document.getElementById('audio-treble-value').textContent =
                (data.treble * 100).toFixed(0) + '%';
        }

        // Mise à jour des données FFT (si disponibles dans la réponse)
        if (data.fft && data.fft.available) {
            // Update frequency info
            document.getElementById('fftPeakFreq').textContent = Math.round(data.fft.peakFreq);
            document.getElementById('fftCentroid').textContent = Math.round(data.fft.spectralCentroid);

            // Update detections with icons
            const t = translations[currentLang];
            document.getElementById('fftKick').innerHTML = data.fft.kickDetected ?
                `<span style="color: #4CAF50;">🥁 ${t.fft.detected}</span>` : '-';
            document.getElementById('fftSnare').innerHTML = data.fft.snareDetected ?
                `<span style="color: #FF9800;">🎵 ${t.fft.detected}</span>` : '-';
            document.getElementById('fftVocal').innerHTML = data.fft.vocalDetected ?
                `<span style="color: #2196F3;">🎤 ${t.fft.detected}</span>` : '-';

            // Draw spectrum
            drawFFTSpectrum(data.fft.bands);
        }
    } catch (error) {
        console.error('Failed to update audio data:', error);
    } finally {
        // Planifier le prochain appel seulement après avoir terminé celui-ci
        // Évite les bouchons si une requête prend du temps
        if (audioUpdateInterval !== null) {
            audioUpdateInterval = setTimeout(updateAudioData, 500);
        }
    }
}

// ============================================================================
// FFT Functions
// ============================================================================

let audioFFTEnabled = false;

// Draw FFT spectrum on canvas
function drawFFTSpectrum(bands) {
    const canvas = document.getElementById('fftSpectrumCanvas');
    if (!canvas) {
        console.warn('Canvas fftSpectrumCanvas not found');
        return;
    }

    if (!bands || bands.length === 0) {
        console.warn('No FFT bands data to draw');
        return;
    }

    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    // Clear canvas
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, width, height);

    const barWidth = width / bands.length;

    // Draw bars
    for (let i = 0; i < bands.length; i++) {
        const barHeight = bands[i] * height;

        // Calculate rainbow color (bass = red, treble = blue)
        const hue = (i / bands.length) * 255;
        const r = Math.floor(255 * Math.sin((hue / 255) * Math.PI));
        const g = Math.floor(255 * Math.sin(((hue / 255) + 0.33) * Math.PI));
        const b = Math.floor(255 * Math.sin(((hue / 255) + 0.66) * Math.PI));

        ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
        ctx.fillRect(i * barWidth, height - barHeight, barWidth - 2, barHeight);
    }
}

// Initialise FFT status from backend (called on page load)
async function loadFFTStatus() {
    try {
        // Charger l'état FFT du backend
        const audioResponse = await fetch(API_BASE + '/api/audio/status');
        const audioData = await audioResponse.json();
        audioFFTEnabled = audioData.fftEnabled || false;

        // Le backend décide si le FFT doit être activé selon l'effet
        // Afficher la section FFT uniquement si le FFT est activé
        const fftBox = document.getElementById('fftStatusBox');
        if (fftBox) {
            fftBox.style.display = audioFFTEnabled ? 'block' : 'none';
        }
    } catch (error) {
        console.error('Failed to load FFT status:', error);
    }
}

function startAudioDataPolling() {
    // Ne démarrer le polling que si on est sur l'onglet config ET que le micro est activé
    if (!audioEnabled || activeTabName !== 'config') {
        return;
    }

    // En mode BLE, désactiver complètement le polling audio pour économiser la bande passante
    if (bleTransportInstance && bleTransportInstance.shouldUseBle()) {
        const audioDataBox = document.getElementById('audio-data');
        const audioFFTDataBox = document.getElementById('audio-fft-data');
        if (audioDataBox) {
          audioDataBox.style.display = 'none';
        }
        if (audioFFTDataBox) {
            audioFFTDataBox.style.display = 'none';
        }

        console.log('[Audio] Polling désactivé en mode BLE (économie bande passante)');
        return;
    }

    if (audioUpdateInterval === null) {
        // Utiliser un flag non-null pour indiquer que le polling est actif
        audioUpdateInterval = 'active';
        // Démarrer immédiatement, puis utiliser setTimeout dans la fonction
        updateAudioData();
        console.log('[Audio] Polling démarré à ~2Hz (WiFi, auto-régulé)');
    }
}

function stopAudioDataPolling() {
    if (audioUpdateInterval !== null) {
        console.log('[Audio] Polling arrêté');
        // Arrêter le setTimeout planifié (si audioUpdateInterval est un timeout ID)
        if (typeof audioUpdateInterval === 'number') {
            clearTimeout(audioUpdateInterval);
        }
        audioUpdateInterval = null;
        // Réinitialiser l'affichage
        document.getElementById('audio-amplitude-value').textContent = '-';
        document.getElementById('audio-bpm-value').textContent = '-';
        document.getElementById('audio-bass-value').textContent = '-';
        document.getElementById('audio-mid-value').textContent = '-';
        document.getElementById('audio-treble-value').textContent = '-';
    }
}

// Fonction d'aide pour afficher une notification audio
function showNotification(section, message, type) {
    // Utiliser le système de notification existant si disponible
    console.log(`[${type}] ${section}: ${message}`);
}

// OTA Functions
let otaReloadScheduled = false;
let otaManualUploadRunning = false;
let otaPollingInterval = null;
function scheduleOtaReload(delayMs) {
    if (otaReloadScheduled) {
        return;
    }
    otaReloadScheduled = true;
    setTimeout(() => window.location.reload(), delayMs);
}
async function loadOTAInfo() {
    try {
        const response = await fetch(API_BASE + '/api/ota/info');
        const data = await response.json();
        document.getElementById('ota-version').textContent = 'v' + data.version;
        const progressContainer = document.getElementById('ota-progress-container');
        const progressBar = document.getElementById('ota-progress-bar');
        const progressPercent = document.getElementById('ota-progress-percent');
        const statusMessage = document.getElementById('ota-status-message');
        const uploadBtn = document.getElementById('ota-upload-btn');
        const restartBtn = document.getElementById('ota-restart-btn');
        const fileInputEl = document.getElementById('firmware-file');
        const backendStateKey = getOtaStateKey(data.state);
        if (otaManualUploadRunning && backendStateKey !== 'idle') {
            otaManualUploadRunning = false;
        }
        const stateKey = otaManualUploadRunning ? 'receiving' : backendStateKey;
        const showProgress = otaManualUploadRunning || (typeof data.state === 'number' && data.state !== 0);
        if (progressContainer && progressBar && progressPercent && statusMessage) {
            const progressValue = Math.max(0, Math.min(100, Number(data.progress) || 0));
            if (showProgress || stateKey === 'success' || stateKey === 'error') {
                progressContainer.style.display = 'block';
                progressBar.style.width = progressValue + '%';
                progressPercent.textContent = progressValue + '%';
            } else {
                progressContainer.style.display = 'none';
                progressBar.style.width = '0%';
                progressPercent.textContent = '0%';
            }
            let statusText = t('ota.states.' + stateKey);
            if (stateKey === 'error' && data.error) {
                statusText += ' - ' + data.error;
            }
            statusMessage.textContent = statusText;
            if (stateKey === 'success') {
                statusMessage.style.color = '#10b981';
            } else if (stateKey === 'error') {
                statusMessage.style.color = '#f87171';
            } else {
                statusMessage.style.color = 'var(--color-muted)';
            }
        }
        const busy = stateKey === 'receiving' || stateKey === 'writing';
        if (uploadBtn) {
            const effectiveBusy = busy || otaManualUploadRunning;
            if (effectiveBusy) {
                uploadBtn.disabled = true;
                uploadBtn.style.display = 'none';
            } else if (!bleTransport.shouldUseBle()) {
                uploadBtn.disabled = false;
                uploadBtn.style.display = 'inline-block';
            }
        }
        if (fileInputEl) {
            const effectiveBusy = busy || otaManualUploadRunning;
            if (effectiveBusy) {
                fileInputEl.disabled = true;
                fileInputEl.style.display = 'none';
            } else if (!bleTransport.shouldUseBle()) {
                fileInputEl.disabled = false;
                fileInputEl.style.display = 'block';
            }
        }
        if (restartBtn) {
            restartBtn.style.display = stateKey === 'success' ? 'inline-block' : 'none';
        }
        const rebootCountdownEl = document.getElementById('ota-reboot-countdown');
        if (rebootCountdownEl) {
            if (typeof data.reboot_countdown === 'number' && data.reboot_countdown >= 0) {
                if (progressContainer) {
                    progressContainer.style.display = 'block';
                }
                rebootCountdownEl.style.display = 'block';
                if (data.reboot_countdown === 0) {
                    rebootCountdownEl.textContent = t('ota.restarting');
                    scheduleOtaReload(5000);
                } else {
                    rebootCountdownEl.textContent = t('ota.autoRestartIn') + ' ' + data.reboot_countdown + 's';
                    if (data.reboot_countdown <= 5) {
                        scheduleOtaReload((data.reboot_countdown + 2) * 1000);
                    }
                }
            } else {
                rebootCountdownEl.style.display = 'none';
            }
        }
    } catch (e) {
        console.error('Erreur:', e);
        document.getElementById('ota-version').innerHTML = '<span data-i18n="ota.loading">' + t('ota.loading') + '</span>';
        const rebootCountdownEl = document.getElementById('ota-reboot-countdown');
        if (rebootCountdownEl) {
            rebootCountdownEl.style.display = 'none';
        }
    }
}
async function uploadFirmware() {
    const fileInputEl = document.getElementById('firmware-file');
    const file = fileInputEl.files[0];
    if (!file) {
        showNotification('ota-notification', t('ota.selectFile'), 'error');
        return;
    }
    if (!file.name.endsWith('.bin')) {
        showNotification('ota-notification', t('ota.wrongExtension'), 'error');
        return;
    }
    if (!confirm(t('ota.confirmUpdate'))) {
        return;
    }
    const progressTitle = document.getElementById('ota-progress-title');
    const progressContainer = document.getElementById('ota-progress-container');
    const progressBar = document.getElementById('ota-progress-bar');
    const progressPercent = document.getElementById('ota-progress-percent');
    const statusMessage = document.getElementById('ota-status-message');
    const uploadBtn = document.getElementById('ota-upload-btn');
    const restartBtn = document.getElementById('ota-restart-btn');
    otaManualUploadRunning = true;
    progressTitle.style.display = 'block';
    progressContainer.style.display = 'block';
    uploadBtn.disabled = true;
    uploadBtn.style.display = 'none';
    if (fileInputEl) {
        fileInputEl.disabled = true;
        fileInputEl.style.display = 'none';
    }
    statusMessage.textContent = t('ota.states.receiving');
    statusMessage.style.color = 'var(--color-muted)';
    // Nettoyer l'ancien intervalle s'il existe
    if (otaPollingInterval) {
        clearInterval(otaPollingInterval);
    }
    // Démarrer le polling OTA pendant l'upload
    otaPollingInterval = setInterval(loadOTAInfo, 1000);
    try {
        await waitForApiConnection();
        const xhr = new XMLHttpRequest();
        xhr.upload.addEventListener('progress', (e) => {
            if (e.lengthComputable) {
                const percent = Math.round((e.loaded / e.total) * 100);
                progressBar.style.width = percent + '%';
                progressPercent.textContent = percent + '%';
                statusMessage.textContent = t('ota.states.receiving') + ' (' + percent + '%)';
            }
        });
        xhr.addEventListener('load', () => {
            if (xhr.status === 200) {
                progressBar.style.width = '100%';
                progressPercent.textContent = '100%';
                statusMessage.textContent = t('ota.states.writing');
                statusMessage.style.color = 'var(--color-muted)';
                restartBtn.style.display = 'inline-block';
                uploadBtn.style.display = 'none';
                // L'intervalle continue pour suivre l'écriture en flash
            } else {
                statusMessage.textContent = t('ota.states.error');
                statusMessage.style.color = '#f87171';
                uploadBtn.disabled = false;
                uploadBtn.style.display = 'inline-block';
                if (fileInputEl) {
                    fileInputEl.disabled = false;
                    fileInputEl.style.display = 'block';
                }
                // Arrêter le polling OTA en cas d'erreur
                if (otaPollingInterval) {
                    clearInterval(otaPollingInterval);
                    otaPollingInterval = null;
                }
            }
        });
        xhr.addEventListener('error', (e) => {
            statusMessage.textContent = t('ota.states.error') + (e.message ? ': ' + e.message : '');
            statusMessage.style.color = '#f87171';
            uploadBtn.disabled = false;
            uploadBtn.style.display = 'inline-block';
            if (fileInputEl) {
                fileInputEl.disabled = false;
                fileInputEl.style.display = 'block';
            }
            // Arrêter le polling OTA en cas d'erreur réseau
            if (otaPollingInterval) {
                clearInterval(otaPollingInterval);
                otaPollingInterval = null;
            }
        });
        xhr.open('POST', API_BASE + '/api/ota/upload');
        xhr.send(file);
    } catch (e) {
        console.error('Erreur:', e);
        statusMessage.textContent = t('ota.error') + ': ' + e.message;
        statusMessage.style.color = '#E82127';
        uploadBtn.disabled = false;
        uploadBtn.style.display = 'inline-block';
        if (fileInputEl) {
            fileInputEl.disabled = false;
            fileInputEl.style.display = 'block';
        }
        // Arrêter le polling OTA en cas d'exception
        if (otaPollingInterval) {
            clearInterval(otaPollingInterval);
            otaPollingInterval = null;
        }
    }
}
async function restartDevice() {
    if (!confirm(t('ota.confirmRestart'))) {
        return;
    }
    try {
        await fetch(API_BASE + '/api/ota/restart', { method: 'POST' });
        const restartMessage = t('ota.restarting');
        const otaStatusEl = document.getElementById('ota-status-message');
        if (otaStatusEl) {
            otaStatusEl.textContent = restartMessage;
        }
        const configNotificationEl = document.getElementById('config-notification');
        if (configNotificationEl) {
            showNotification('config-notification', restartMessage, 'info', 10000);
        }
        setTimeout(() => {
            location.reload();
        }, 10000);
    } catch (e) {
        console.error('Erreur:', e);
    }
}
// Simulation des événements CAN
async function simulateEvent(eventType) {
    // Attendre que la queue BLE soit vide avant de simuler l'événement
    if (bleTransportInstance && bleTransportInstance.waitForQueue) {
        await bleTransportInstance.waitForQueue();
    }

    try {
        showNotification('simulation-notification', t('simulation.sendingEvent', getEventName(eventType)), 'info');
        const response = await fetch(API_BASE + '/api/simulate/event', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ event: eventType })
        });
        const result = await response.json();
        if (result.status === 'ok') {
            showNotification('simulation-notification', t('simulation.eventSimulated', getEventName(eventType)), 'success');
        } else {
            showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
    }
}
// Arrêter l'effet en cours
async function stopEffect() {
    try {
        await fetch(API_BASE + '/api/effect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                effect: 0,
                brightness: 0,
                speed: 0,
                color1: 0,
                color2: 0,
                color3: 0
            })
        });
        showNotification('simulation-notification', t('simulation.eventStopped'), 'success');
    } catch (e) {
        console.error('Erreur:', e);
        showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
    }
}
// Toggle event on/off
async function toggleEvent(eventType, isEnabled) {
    try {
        if (isEnabled) {
            try {
                await ensureEventsConfigData();
            } catch (error) {
                console.error('Failed to refresh events config for simulation:', error);
            }
            if (!isSimulationEventEnabled(eventType)) {
                const checkbox = document.getElementById('toggle-' + eventType);
                if (checkbox) {
                    checkbox.checked = false;
                }
                setToggleContainerState(eventType, false);
                simulationTogglesState[eventType] = false;
                showNotification('simulation-notification', t('simulation.disabledEvent'), 'error');
                return;
            }
            cancelSimulationAutoStop(eventType);
            setToggleContainerState(eventType, true);
            showNotification('simulation-notification', t('simulation.sending') + ': ' + getEventName(eventType) + '...', 'info');
            const response = await fetch(API_BASE + '/api/simulate/event', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ event: eventType })
            });
            const result = await response.json();
            if (result.status === 'ok') {
                showNotification('simulation-notification', t('simulation.eventActive', getEventName(eventType)), 'success');
                simulationTogglesState[eventType] = true;
                const durationMs = await getSimulationEventDuration(eventType);
                if (durationMs > 0) {
                    scheduleSimulationAutoStop(eventType, durationMs);
                }
            } else {
                document.getElementById('toggle-' + eventType).checked = false;
                setToggleContainerState(eventType, false);
                showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
                simulationTogglesState[eventType] = false;
            }
        } else {
            cancelSimulationAutoStop(eventType);
            showNotification('simulation-notification', t('simulation.stoppingEvent', getEventName(eventType)), 'info');
            const response = await fetch(API_BASE + '/api/stop/event', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ event: eventType })
            });
            const result = await response.json();
            if (result.status === 'ok') {
                setToggleContainerState(eventType, false);
                showNotification('simulation-notification', t('simulation.eventDeactivated', getEventName(eventType)), 'success');
                simulationTogglesState[eventType] = false;
                cancelSimulationAutoStop(eventType);
            } else {
                document.getElementById('toggle-' + eventType).checked = true;
                setToggleContainerState(eventType, true);
                showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
                simulationTogglesState[eventType] = true;
            }
        }
    } catch (e) {
        console.error('Erreur:', e);
        document.getElementById('toggle-' + eventType).checked = true;
        setToggleContainerState(eventType, true);
        showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
    }
}
// Toggle night mode simulation (single switch for ON/OFF)
async function toggleNightMode(isEnabled) {
    const toggleContainer = document.getElementById('event-toggle-nightmode');
    const eventType = isEnabled ? 'NIGHT_MODE_ON' : 'NIGHT_MODE_OFF';
    const eventName = isEnabled ? t('canEvents.nightModeOn') : t('canEvents.nightModeOff');
    try {
        showNotification('simulation-notification', t('simulation.sendingEvent', eventName), 'info');
        const response = await fetch(API_BASE + '/api/simulate/event', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ event: eventType })
        });
        const result = await response.json();
        if (result.status === 'ok') {
            toggleContainer.classList.toggle('active', isEnabled);
            showNotification('simulation-notification', t('simulation.eventSimulated', eventName), 'success');
            simulationTogglesState['nightmode'] = isEnabled;
        } else {
            document.getElementById('toggle-nightmode').checked = !isEnabled;
            simulationTogglesState['nightmode'] = !isEnabled;
            showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
        }
    } catch (e) {
        console.error('Erreur:', e);
        document.getElementById('toggle-nightmode').checked = !isEnabled;
        simulationTogglesState['nightmode'] = !isEnabled;
        showNotification('simulation-notification', t('simulation.simulationError', t('simulation.error')), 'error');
    }
}
function translateEventId(eventId) {
    if (!eventId) return null;
    const map = translations[currentLang] && translations[currentLang].eventNames;
    if (map && map[eventId]) {
        return map[eventId];
    }
    return null;
}
function translateEffectId(effectId) {
    if (!effectId) return null;
    const map = translations[currentLang] && translations[currentLang].effectNames;
    if (map && map[effectId]) {
        return map[effectId];
    }
    return null;
}
// Helper pour obtenir le nom d'un événement (depuis API uniquement)
function getEventName(eventId) {
    const translated = translateEventId(typeof eventId === 'string' ? eventId : String(eventId));
    if (translated) {
        return translated;
    }
    if (eventTypesList.length > 0) {
        const stringId = typeof eventId === 'string' ? eventId : String(eventId);
        const eventById = eventTypesList.find(e => e.id === stringId);
        if (eventById) {
            return eventById.name;
        }
        const numericIndex = typeof eventId === 'number' ? eventId : parseInt(eventId, 10);
        if (!Number.isNaN(numericIndex) && eventTypesList[numericIndex]) {
            return eventTypesList[numericIndex].name;
        }
    }
    return t('eventNames.unknown', eventId);
}
// Helper pour obtenir le nom d'un effet (depuis API uniquement)
function getEffectName(effectId) {
    const translated = translateEffectId(effectId);
    if (translated) {
        return translated;
    }
    if (effectsList.length > 0) {
        const effect = effectsList.find(e => e.id === effectId);
        return effect ? translateEffectId(effect.id) || effect.name : t('effectNames.unknown', effectId);
    }
    return t('effectNames.unknown', effectId);
}
// Charger la liste des effets depuis l'API
let effectsList = [];
// Helper: Convertir enum numérique en ID string
function effectEnumToId(enumValue) {
    if (effectsList[enumValue]) {
        return effectsList[enumValue].id;
    }
    return effectsList[0]?.id || 'OFF';
}
// Helper: Convertir ID string en enum numérique
function effectIdToEnum(id) {
    const index = effectsList.findIndex(e => e.id === id);
    return index >= 0 ? index : 0;
}
async function loadEffects() {
    try {
        const response = await fetch('/api/effects');
        const data = await response.json();
        effectsList = data.effects;
        // Mettre à jour tous les dropdowns d'effets
        const effectSelects = [
            document.getElementById('default-effect-select'),
            document.getElementById('effect-select'),
            document.getElementById('event-effect-select')
        ];
        effectSelects.forEach(select => {
            if (select) {
                select.setAttribute('data-effect-options', 'true');
                const currentValue = select.value;
                select.innerHTML = '';
                // Pour les sélecteurs d'effet par défaut et d'effet manuel,
                // filtrer les effets qui nécessitent le CAN bus
                // Pour le sélecteur d'événement, filtrer aussi les effets audio
                const isEventSelector = select.id === 'event-effect-select';
                effectsList.forEach(effect => {
                    // Filtrer les effets CAN si ce n'est pas le sélecteur d'événement
                    if (!isEventSelector && effect.can_required) {
                        return; // Skip cet effet
                    }
                    // Filtrer les effets audio si c'est le sélecteur d'événement
                    if (isEventSelector && effect.audio_effect) {
                        return; // Skip cet effet audio
                    }
                    const option = document.createElement('option');
                    option.value = effect.id;
                    option.textContent = translateEffectId(effect.id) || effect.name;
                    option.setAttribute('data-effect-name', effect.name);
                    select.appendChild(option);
                });
                // Restaurer la valeur sélectionnée si elle existe encore
                if (currentValue !== undefined && currentValue !== '') {
                    select.value = currentValue;
                }
            }
        });
        refreshEffectOptionLabels();
        console.log('Loaded', effectsList.length, 'effects from API');
    } catch (error) {
        console.error('Failed to load effects:', error);
    }
}
// Charger la liste des types d'événements depuis l'API
let eventTypesList = [];
async function loadEventTypes() {
    try {
        const response = await fetch('/api/event-types');
        const data = await response.json();
        eventTypesList = data.event_types;
        console.log('Loaded', eventTypesList.length, 'event types from API');
    } catch (error) {
        console.error('Failed to load event types:', error);
    }
}
async function loadInitialData() {
    await Promise.all([
        loadEffects(),
        loadEventTypes(),
        ensureEventsConfigData().catch(() => null)
    ]);
    renderSimulationSections();
    loadProfiles();
    loadConfig();
    loadHardwareConfig(); // Charger la configuration matérielle LED
    loadAudioStatus(); // Charger la configuration audio
    loadFFTStatus(); // Charger l'état FFT
    loadOTAInfo();
    // Démarrer le polling de status avec setTimeout auto-régulé
    if (!statusIntervalHandle) {
        statusIntervalHandle = 'active'; // Flag pour indiquer que le polling est actif
        updateStatus(); // Premier appel, puis auto-planification via finally
    }
}
// Init
renderSimulationSections();
applyTranslations();
applyTheme();
updateLanguageSelector();
updateBleUiState();
updateConnectionOverlay();
updateApiConnectionState();
scheduleInitialDataLoad();