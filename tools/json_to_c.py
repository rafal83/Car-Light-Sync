#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Convertit une configuration CAN JSON en code C statique pour l'ESP32

Usage:
    python json_to_c.py input.json output.h
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

def json_to_c_code(input_file, output_file):
    """Convertit le JSON en code C statique"""

    print(f"📖 Lecture de {input_file}...")

    # Charger le fichier JSON
    with open(input_file, 'r', encoding='utf-8') as f:
        data = json.load(f)

    vehicle = data['vehicle']
    messages = data['messages']

    print(f"✅ {len(messages)} messages trouvés")

    # Compter les statistiques
    total_signals = sum(len(msg['signals']) for msg in messages)
    total_events = sum(sum(len(sig.get('events', [])) for sig in msg['signals']) for msg in messages)

    print(f"📊 {total_signals} signaux, {total_events} événements")

    # Générer le code C
    output = []
    output.append(f"// Auto-généré depuis {Path(input_file).name}")
    output.append(f"// Véhicule: {vehicle['make']} {vehicle['model']} {vehicle['year']}")
    output.append(f"// Description: {vehicle.get('description', '')}")
    output.append("")
    output.append("#ifndef VEHICLE_CAN_CONFIG_GENERATED_H")
    output.append("#define VEHICLE_CAN_CONFIG_GENERATED_H")
    output.append("")
    output.append("#include \"vehicle_can_config.h\"")
    output.append("")

    # Générer les structures pour chaque message
    for msg_idx, msg in enumerate(messages):
        msg_id = msg['id']
        msg_name = msg['name'].replace('ID', 'MSG_').replace('-', '_')

        # Générer les événements
        for sig_idx, sig in enumerate(msg['signals']):
            events = sig.get('events', [])
            if not events:
                continue

            event_array_name = f"events_{msg_name}_sig{sig_idx}"
            output.append(f"// Événements pour {sig['name']}")
            output.append(f"static const can_event_config_t {event_array_name}[] = {{")

            for event in events:
                condition = event['condition'].upper()
                trigger = event['trigger']
                value = event.get('value', '0')

                output.append(f"    {{")
                output.append(f"        .condition = EVENT_CONDITION_{condition},")
                output.append(f"        .trigger = CAN_EVENT_{trigger},")
                output.append(f"        .value = {float(value):.1f}f")
                output.append(f"    }},")

            output.append(f"}};")
            output.append("")

        # Générer les signaux
        sig_array_name = f"signals_{msg_name}"
        output.append(f"// Signaux pour message {msg_id} - {msg['name']}")
        output.append(f"static const can_signal_config_static_t {sig_array_name}[] = {{")

        for sig_idx, sig in enumerate(msg['signals']):
            byte_order = "BYTE_ORDER_LITTLE_ENDIAN" if sig['byte_order'] == "little_endian" else "BYTE_ORDER_BIG_ENDIAN"
            value_type_map = {"boolean": "SIGNAL_TYPE_BOOLEAN", "unsigned": "SIGNAL_TYPE_UNSIGNED", "signed": "SIGNAL_TYPE_SIGNED"}
            value_type = value_type_map.get(sig['value_type'], "SIGNAL_TYPE_UNSIGNED")

            events = sig.get('events', [])
            event_array = f"events_{msg_name}_sig{sig_idx}" if events else "NULL"
            event_count = len(events)

            output.append(f"    {{ // {sig['name']}")
            output.append(f"        .start_bit = {sig['start_bit']},")
            output.append(f"        .length = {sig['length']},")
            output.append(f"        .byte_order = {byte_order},")
            output.append(f"        .value_type = {value_type},")
            output.append(f"        .factor = {sig['factor']:.1f}f,")
            output.append(f"        .offset = {sig['offset']:.1f}f,")
            output.append(f"        .events = {event_array},")
            output.append(f"        .event_count = {event_count}")
            output.append(f"    }},")

        output.append(f"}};")
        output.append("")

    # Générer le tableau de messages
    output.append("// Tableau principal des messages CAN")
    output.append("static const can_message_config_static_t can_messages[] = {")

    for msg_idx, msg in enumerate(messages):
        msg_id = int(msg['id'], 16)
        msg_name = msg['name'].replace('ID', 'MSG_').replace('-', '_')
        bus_map = {"chassis": 0, "powertrain": 1, "body": 2}
        bus = bus_map.get(msg['bus'], 0)
        sig_array_name = f"signals_{msg_name}"
        signal_count = len(msg['signals'])

        output.append(f"    {{ // {msg['id']} - {msg['name']}")
        output.append(f"        .message_id = 0x{msg_id:03X},")
        output.append(f"        .bus = {bus},")
        output.append(f"        .signals = {sig_array_name},")
        output.append(f"        .signal_count = {signal_count}")
        output.append(f"    }},")

    output.append("};")
    output.append("")

    # Générer la configuration globale
    output.append("// Configuration globale du véhicule")
    output.append("static const vehicle_can_config_static_t vehicle_config = {")
    output.append(f"    .messages = can_messages,")
    output.append(f"    .message_count = {len(messages)}")
    output.append("};")
    output.append("")

    # Fonction getter
    output.append("// Fonction pour obtenir la configuration")
    output.append("static inline const vehicle_can_config_static_t* get_vehicle_can_config(void) {")
    output.append("    return &vehicle_config;")
    output.append("}")
    output.append("")

    output.append("#endif // VEHICLE_CAN_CONFIG_GENERATED_H")

    # Écrire le fichier
    print(f"\n💾 Sauvegarde dans {output_file}...")
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output))

    print(f"✅ Génération terminée!")
    print(f"\n📊 Statistiques:")
    print(f"  - Messages: {len(messages)}")
    print(f"  - Signaux: {total_signals}")
    print(f"  - Événements: {total_events}")
    print(f"  - Lignes de code: {len(output)}")

    return True

def main():
    parser = argparse.ArgumentParser(
        description="Convertit une configuration CAN JSON en code C statique"
    )

    parser.add_argument('input_file', help='Fichier JSON source')
    parser.add_argument('output_file', help='Fichier .h de sortie', nargs='?')

    args = parser.parse_args()

    # Si pas de fichier de sortie, utiliser le même nom avec .h
    if not args.output_file:
        base = Path(args.input_file).stem
        args.output_file = f"include/vehicle_can_config_generated.h"

    try:
        json_to_c_code(args.input_file, args.output_file)
        print(f"\n🎉 Fichier C créé: {args.output_file}")
    except Exception as e:
        print(f"❌ Erreur: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
