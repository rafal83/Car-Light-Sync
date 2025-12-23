#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Générateur de configuration Car Light Sync depuis fichier DBC

Usage:
    python dbc_to_config.py tesla_model3.dbc --output model3_2021.json
    python dbc_to_config.py tesla_model3.dbc --interactive

Auteur: Car Light Sync Team
Licence: MIT
"""

import argparse
import json
import sys
import io
from pathlib import Path

# Fix encoding for Windows console
if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

try:
    import cantools
except ImportError:
    print("ERROR: cantools n'est pas installé")
    print("Installez-le avec: pip install cantools")
    sys.exit(1)

# Mapping des noms de signaux connus vers événements CAN
KNOWN_SIGNAL_EVENTS = {
    'gear': {
        'PARK': 'GEAR_PARK',
        'REVERSE': 'GEAR_REVERSE',
        'DRIVE': 'GEAR_DRIVE',
        'NEUTRAL': None  # Pas d'événement
    },
    'turn_signal': {
        'OFF': None,
        'LEFT': 'TURN_LEFT',
        'RIGHT': 'TURN_RIGHT',
        'HAZARD': 'TURN_HAZARD'
    },
    'door': {
        'OPEN': 'DOOR_OPEN',
        'CLOSED': 'DOOR_CLOSE'
    },
    'lock': {
        'LOCKED': 'LOCKED',
        'UNLOCKED': 'UNLOCKED',
        'LOCK': 'LOCKED',
        'UNLOCK': 'UNLOCKED',
        'REQUEST_LOCK': 'LOCKED',
        'REQUEST_UNLOCK': 'UNLOCKED',
        'REQUEST_REMOTE_LOCK': 'LOCKED',
        'REQUEST_REMOTE_UNLOCK': 'UNLOCKED'
    },
    'brake': {
        'PRESSED': 'BRAKE_ON',
    },
    'blindspot': {
        'WARNING_LEVEL_1': 'BLINDSPOT_WARNING',
        'WARNING_LEVEL_2': 'BLINDSPOT_WARNING',
        'NO_WARNING': None
    },
    'autopilot': {
        'ENGAGED': 'AUTOPILOT_ENGAGED',
        'DISENGAGED': 'AUTOPILOT_DISENGAGED'
    },
    'hazard': {
        'ON': 'TURN_HAZARD',
        'BUTTON': 'TURN_HAZARD',
        'PRESSED': 'TURN_HAZARD',
        'CAR_ALARM': 'SENTRY_ALERT'
    },
    'charging': {
        'CABLE_PRESENT': 'CHARGING_CABLE_CONNECTED',
        'CABLE_NOT_PRESENT': 'CHARGING_CABLE_DISCONNECTED',
        'PCS_CHARGE_ENABLED': 'CHARGING_STARTED',
        'PCS_CHARGE_STANDBY': 'CHARGING_STOPPED',
        'CHARGE_ENABLED': 'CHARGING_STARTED',
        'CHARGE_STANDBY': 'CHARGING_STOPPED'
    },
    'night_mode': {
        # Boolean signal pour ambient lighting
    },
    'sentry_mode': {
        'ON': 'SENTRY_MODE_ON',
        'OFF': 'SENTRY_MODE_OFF',
        'ACTIVE': 'SENTRY_MODE_ON',
        'INACTIVE': 'SENTRY_MODE_OFF',
        'ENABLED': 'SENTRY_MODE_ON',
        'DISABLED': 'SENTRY_MODE_OFF',
        'SENTRY': 'SENTRY_MODE_ON',
        'NOMINAL': 'SENTRY_MODE_OFF',
        'SUSPEND': 'SENTRY_MODE_OFF'
    },
    'sentry_alert': {
        'TRIGGER': 'SENTRY_ALERT',
        'TRIGGERED': 'SENTRY_ALERT',
        'ALARM': 'SENTRY_ALERT',
        'CAR_ALARM': 'SENTRY_ALERT',
        'ACTIVE': 'SENTRY_ALERT'
    }
}

def detect_signal_type(signal):
    """Détecte le type de signal et les événements potentiels"""
    name_lower = signal.name.lower()

    # Sentry / alarm modes avant tout
    if 'sentry' in name_lower:
        return 'sentry_mode', KNOWN_SIGNAL_EVENTS['sentry_mode']

    if 'alarm' in name_lower:
        return 'sentry_alert', KNOWN_SIGNAL_EVENTS['sentry_alert']

    if getattr(signal, 'choices', None):
        for choice in signal.choices.values():
            if 'sentry' in choice.name.lower():
                return 'sentry_mode', KNOWN_SIGNAL_EVENTS['sentry_mode']

    # Détection par nom (ordre important - du plus spécifique au plus général)
    if 'gear' in name_lower or 'prnd' in name_lower or 'shifter' in name_lower:
        return 'gear', KNOWN_SIGNAL_EVENTS['gear']

    # Hazard lights (avant turn signal car peut contenir 'turn')
    if 'hazard' in name_lower:
        return 'hazard', KNOWN_SIGNAL_EVENTS['hazard']

    if 'turn' in name_lower or 'blinker' in name_lower or 'indicator' in name_lower:
        return 'turn_signal', KNOWN_SIGNAL_EVENTS['turn_signal']

    # Blindspot detection (avant door car certains signaux contiennent les deux)
    if 'blindspot' in name_lower or 'blind_spot' in name_lower or 'bsd' in name_lower:
        return 'blindspot', KNOWN_SIGNAL_EVENTS['blindspot']

    # Charging related
    if 'charge' in name_lower and 'cable' in name_lower and 'present' in name_lower:
        return 'charging', KNOWN_SIGNAL_EVENTS['charging']
    if 'charge' in name_lower and 'status' in name_lower:
        return 'charging', KNOWN_SIGNAL_EVENTS['charging']

    # Night mode / ambient lighting
    if 'ambientlighting' in name_lower.replace('_', '') and 'enabled' in name_lower:
        return 'night_mode', KNOWN_SIGNAL_EVENTS['night_mode']

    # Door status (après blindspot)
    if 'door' in name_lower and 'status' in name_lower:
        return 'door', KNOWN_SIGNAL_EVENTS['door']

    # Lock status (vérifier que ce n'est pas door_lock)
    if 'lock' in name_lower and ('request' in name_lower or 'unlock' in name_lower):
        return 'lock', KNOWN_SIGNAL_EVENTS['lock']

    # Brake
    if 'brake' in name_lower and ('pressed' in name_lower or 'switch' in name_lower):
        return 'brake', KNOWN_SIGNAL_EVENTS['brake']

    # Autopilot
    if 'autopilot' in name_lower or 'fsd' in name_lower:
        return 'autopilot', KNOWN_SIGNAL_EVENTS['autopilot']

    return None, None

def normalize_choice_name(choice_name):
    """Normalise un nom de choix pour la comparaison (retire préfixes/suffixes)"""
    # Retire les préfixes courants (DI_GEAR_, VCFRONT_, etc.)
    name = choice_name.upper()

    # Patterns de préfixes à retirer (ordre important - plus spécifique en premier)
    prefixes = ['UI_LOCK_REQUEST_', 'DI_GEAR_', 'GEAR_', 'TURN_SIGNAL_', 'TURN_', 'DOOR_',
                'LOCK_', 'BRAKE_', 'BLINDSPOT_', 'AUTOPILOT_', 'VCFRONT_', 'VCLEFT_',
                'VCRIGHT_', 'DAS_', 'UI_', 'CP_', 'PCS_', 'HAZARD_REQUEST_', 'HAZARD_',
                'DI_', 'DIF_', 'DIR_', 'CABLE_']

    for prefix in prefixes:
        if name.startswith(prefix):
            name = name[len(prefix):]

    # Mapping des noms normalisés vers noms standards
    normalize_map = {
        'P': 'PARK',
        'R': 'REVERSE',
        'N': 'NEUTRAL',
        'D': 'DRIVE',
        'INVALID': None,
        'SNA': None,
        'PRESSED': 'PRESSED',
        'RELEASED': 'RELEASED',
        'ON': 'ON',
        'OFF': None,  # OFF n'est pas un événement
        'ACTIVE_LOW': 'ACTIVE',  # Turn signal actif
        'ACTIVE_HIGH': 'ACTIVE',  # Turn signal actif
        'ACTIVE': 'ACTIVE',
        'LEFT': 'LEFT',
        'RIGHT': 'RIGHT',
        'ENGAGED': 'ENGAGED',
        'DISENGAGED': 'DISENGAGED',
        'OPEN': 'OPEN',
        'CLOSED': 'CLOSED',
        'CLOSE': 'CLOSED',
        'LOCKED': 'LOCKED',
        'UNLOCKED': 'UNLOCKED',
        'HAZARD': 'HAZARD',
        'WARNING': 'WARNING',
        'WARNING_LEVEL_1': 'WARNING_LEVEL_1',
        'WARNING_LEVEL_2': 'WARNING_LEVEL_2',
        'NO_WARNING': 'NO_WARNING',
        'BUTTON': 'BUTTON',
        'PRESENT': 'CABLE_PRESENT',
        'NOT_PRESENT': 'CABLE_NOT_PRESENT',
        'CHARGE_ENABLED': 'PCS_CHARGE_ENABLED',
        'CHARGE_STANDBY': 'PCS_CHARGE_STANDBY',
        'CHARGE_BLOCKED': 'PCS_CHARGE_BLOCKED',
        'CHARGE_FAULTED': 'PCS_CHARGE_FAULTED'
    }

    return normalize_map.get(name, name)

def byte_order_to_string(byte_order):
    """Convertit l'ordre des bytes en string"""
    return "little_endian" if byte_order == "little_endian" else "big_endian"

def signal_to_json(signal, message):
    """Convertit un signal CAN en format JSON"""
    signal_json = {
        "name": signal.name,
        "start_bit": signal.start,
        "length": signal.length,
        "byte_order": byte_order_to_string(signal.byte_order),
        "value_type": "boolean" if signal.length == 1 else ("signed" if signal.is_signed else "unsigned"),
        "factor": signal.scale,
        "offset": signal.offset,
        "unit": signal.unit or ""
    }

    # Ajouter min/max si disponibles
    if hasattr(signal, 'minimum') and signal.minimum is not None:
        signal_json["min"] = signal.minimum
    if hasattr(signal, 'maximum') and signal.maximum is not None:
        signal_json["max"] = signal.maximum

    # Ajouter le mapping si disponible
    if signal.choices:
        signal_json["mapping"] = {str(k): v.name for k, v in signal.choices.items()}

    # Multiplexage (mux)
    if getattr(signal, 'is_multiplexer', False):
        signal_json["mux"] = "M"
    else:
        mux_ids = getattr(signal, 'multiplexer_ids', None)
        if mux_ids:
            mux_list = sorted(mux_ids)
            signal_json["mux"] = mux_list[0] if len(mux_list) == 1 else mux_list
            mux_signal = getattr(signal, 'multiplexer_signal', None)
            if mux_signal:
                signal_json["mux_signal"] = mux_signal.name if hasattr(mux_signal, 'name') else str(mux_signal)

    # Détecter automatiquement les événements
    signal_type, event_mapping = detect_signal_type(signal)
    events = []

    if event_mapping is not None:
        if signal.choices:
            # Signal avec choix (enum)
            for value, choice in signal.choices.items():
                # Normaliser le nom du choix pour matcher avec le mapping
                normalized = normalize_choice_name(choice.name)

                # Cas spécial pour les turn signals : indicatorLeft + ACTIVE = LEFT
                if signal_type == 'turn_signal' and normalized == 'ACTIVE':
                    if 'left' in signal.name.lower():
                        trigger = 'TURN_LEFT'
                    elif 'right' in signal.name.lower():
                        trigger = 'TURN_RIGHT'
                    else:
                        continue
                    events.append({
                        "condition": "value_equals",
                        "value": str(value),
                        "trigger": trigger
                    })
                elif normalized and normalized in event_mapping and event_mapping[normalized]:
                    events.append({
                        "condition": "value_equals",
                        "value": str(value),
                        "trigger": event_mapping[normalized]
                    })
        elif signal.length == 1:
            # Signal booléen - gérer les rising/falling edges
            if signal_type == 'night_mode':
                # Night mode: ON quand 1, OFF quand 0
                events.append({
                    "condition": "rising_edge",
                    "trigger": "NIGHT_MODE_ON"
                })
                events.append({
                    "condition": "falling_edge",
                    "trigger": "NIGHT_MODE_OFF"
                })
            elif signal_type == 'sentry_mode':
                on_trigger = (event_mapping.get('ON') or
                              event_mapping.get('ACTIVE') or
                              event_mapping.get('ENABLED'))
                off_trigger = (event_mapping.get('OFF') or
                               event_mapping.get('INACTIVE') or
                               event_mapping.get('DISABLED'))
                if on_trigger:
                    events.append({
                        "condition": "rising_edge",
                        "trigger": on_trigger
                    })
                if off_trigger:
                    events.append({
                        "condition": "falling_edge",
                        "trigger": off_trigger
                    })
            elif signal_type == 'sentry_alert':
                trigger = next(iter(event_mapping.values()), None)
                if trigger:
                    events.append({
                        "condition": "rising_edge",
                        "trigger": trigger
                    })
            elif 'PRESSED' in event_mapping or 'LEFT' in event_mapping or 'ENGAGED' in event_mapping:
                events.append({
                    "condition": "rising_edge",
                    "trigger": list(event_mapping.values())[0]
                })
            if 'RELEASED' in event_mapping or 'DISENGAGED' in event_mapping:
                events.append({
                    "condition": "falling_edge",
                    "trigger": list(event_mapping.values())[1] if len(event_mapping) > 1 else None
                })

    if events:
        # Filtrer les événements None
        events = [e for e in events if e.get('trigger')]
        if events:
            signal_json["events"] = events

    return signal_json

def message_to_json(message, bus_mapping):
    """Convertit un message CAN en format JSON"""
    # Déterminer le bus selon l'ID du message (heuristique)
    bus = "chassis"  # Par défaut
    if message.frame_id >= 0x300:
        bus = "body"
    elif message.frame_id >= 0x200:
        bus = "powertrain"

    # Override si mapping fourni
    if message.frame_id in bus_mapping:
        bus = bus_mapping[message.frame_id]

    message_json = {
        "id": f"0x{message.frame_id:X}",
        "name": message.name,
        "bus": bus,
        "cycle_time_ms": int(message.cycle_time) if hasattr(message, 'cycle_time') and message.cycle_time else 100,
        "signals": []
    }

    for signal in message.signals:
        message_json["signals"].append(signal_to_json(signal, message))

    return message_json

def parse_dbc(dbc_file, output_file, vehicle_info=None, bus_mapping=None, interactive=False):
    """Parse un fichier DBC et génère la configuration JSON"""

    print(f"📖 Lecture du fichier DBC: {dbc_file}")

    try:
        db = cantools.database.load_file(dbc_file)
    except Exception as e:
        print(f"❌ Erreur lors de la lecture du DBC: {e}")
        return False

    print(f"✅ {len(db.messages)} messages trouvés")

    # Informations sur le véhicule
    if not vehicle_info:
        if interactive:
            print("\n📝 Informations sur le véhicule:")
            make = input("  Constructeur (ex: Tesla): ").strip() or "Unknown"
            model = input("  Modèle (ex: Model 3): ").strip() or "Unknown"
            year = input("  Année (ex: 2021): ").strip() or "0"
            variant = input("  Variante (ex: Long Range): ").strip() or ""
        else:
            make = "Unknown"
            model = "Unknown"
            year = "0"
            variant = ""
    else:
        make, model, year, variant = vehicle_info

    # Configuration
    config = {
        "schema_version": "1.0",
        "vehicle": {
            "make": make,
            "model": model,
            "year": int(year) if year.isdigit() else 0,
            "variant": variant,
            "description": f"Configuration CAN pour {make} {model} {year}"
        },
        "can_config": {
            "buses": {
                "chassis": 0,
                "powertrain": 1,
                "body": 2
            },
            "baudrate": 500000
        },
        "messages": []
    }

    # Mapping des bus (par défaut)
    if not bus_mapping:
        bus_mapping = {}

    # Convertir tous les messages
    for message in db.messages:
        config["messages"].append(message_to_json(message, bus_mapping))

    # Sauvegarder
    print(f"\n💾 Sauvegarde dans: {output_file}")
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        print(f"✅ Configuration générée avec succès!")
        print(f"\n📊 Statistiques:")
        print(f"  - Messages CAN: {len(config['messages'])}")
        total_signals = sum(len(msg['signals']) for msg in config['messages'])
        print(f"  - Signaux: {total_signals}")

        # Compter les événements
        total_events = 0
        for msg in config['messages']:
            for signal in msg['signals']:
                if 'events' in signal:
                    total_events += len(signal['events'])
        print(f"  - Événements auto-détectés: {total_events}")

        return True
    except Exception as e:
        print(f"❌ Erreur lors de la sauvegarde: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(
        description="Convertir un fichier DBC en configuration JSON pour Car Light Sync",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exemples:
  %(prog)s tesla.dbc --output model3_2021.json
  %(prog)s tesla.dbc --output model3.json --make Tesla --model "Model 3" --year 2021
  %(prog)s custom.dbc --interactive
        """
    )

    parser.add_argument('dbc_file', help='Fichier DBC source')
    parser.add_argument('-o', '--output', help='Fichier JSON de sortie', required=False)
    parser.add_argument('--make', help='Constructeur du véhicule')
    parser.add_argument('--model', help='Modèle du véhicule')
    parser.add_argument('--year', help='Année du véhicule')
    parser.add_argument('--variant', help='Variante du véhicule', default='')
    parser.add_argument('-i', '--interactive', action='store_true',
                       help='Mode interactif pour saisir les informations')

    args = parser.parse_args()

    # Déterminer le fichier de sortie
    if not args.output:
        dbc_path = Path(args.dbc_file)
        args.output = dbc_path.with_suffix('.json').name

    # Informations véhicule
    vehicle_info = None
    if args.make or args.model or args.year:
        vehicle_info = (
            args.make or "Unknown",
            args.model or "Unknown",
            args.year or "0",
            args.variant
        )

    # Conversion
    success = parse_dbc(
        args.dbc_file,
        args.output,
        vehicle_info=vehicle_info,
        interactive=args.interactive
    )

    if success:
        print(f"\n🎉 Configuration créée: {args.output}")
        print(f"\n📝 Prochaines étapes:")
        print(f"  1. Vérifier le fichier {args.output}")
        print(f"  2. Ajuster les événements si nécessaire")
        print(f"  3. Copier vers vehicle_configs/")
        print(f"  4. Flasher l'ESP32")
    else:
        sys.exit(1)

if __name__ == '__main__':
    main()
