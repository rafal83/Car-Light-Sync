#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Filtre une configuration CAN pour ne garder que les messages avec événements

Usage:
    python filter_can_config.py input.json output.json
"""

import argparse
import json
import sys
import io

# Fix encoding for Windows console
if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

def filter_config(input_file, output_file):
    """Filtre la config pour ne garder que les messages avec événements"""

    print(f"📖 Lecture de {input_file}...")

    # Charger le fichier complet
    with open(input_file, 'r', encoding='utf-8') as f:
        data = json.load(f)

    print(f"✅ {len(data['messages'])} messages trouvés")

    # Filtrer pour ne garder que les messages avec événements
    filtered_messages = []
    total_events = 0
    total_signals = 0

    for msg in data['messages']:
        # Filtrer les signaux pour ne garder que ceux avec événements
        signals_with_events = [s for s in msg['signals'] if 'events' in s and len(s['events']) > 0]

        if signals_with_events:
            filtered_msg = msg.copy()
            filtered_msg['signals'] = signals_with_events
            filtered_messages.append(filtered_msg)
            total_signals += len(signals_with_events)
            total_events += sum(len(s['events']) for s in signals_with_events)

    # Créer la config filtrée
    filtered_data = data.copy()
    filtered_data['messages'] = filtered_messages

    # Mettre à jour la description
    vehicle = filtered_data['vehicle']
    filtered_data['vehicle']['description'] = f"Configuration CAN filtrée pour {vehicle['make']} {vehicle['model']} {vehicle['year']} (événements uniquement)"

    # Sauvegarder
    print(f"\n💾 Sauvegarde dans {output_file}...")
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(filtered_data, f, indent=2, ensure_ascii=False)

    print(f"✅ Filtrage terminé!\n")
    print(f"📊 Statistiques:")
    print(f"  - Messages conservés: {len(filtered_messages)}")
    print(f"  - Signaux avec événements: {total_signals}")
    print(f"  - Total événements: {total_events}")
    print(f"  - Réduction: {len(data['messages']) - len(filtered_messages)} messages supprimés")

    # Calculer la taille estimée en mémoire
    bytes_per_message = 32  # Estimation
    bytes_per_signal = 64   # Estimation
    bytes_per_event = 16    # Estimation

    estimated_size = (len(filtered_messages) * bytes_per_message +
                     total_signals * bytes_per_signal +
                     total_events * bytes_per_event)

    print(f"\n💾 Mémoire estimée: ~{estimated_size} bytes ({estimated_size/1024:.1f} KB)")

    return True

def main():
    parser = argparse.ArgumentParser(
        description="Filtre une configuration CAN pour ne garder que les messages avec événements"
    )

    parser.add_argument('input_file', help='Fichier JSON source')
    parser.add_argument('output_file', help='Fichier JSON de sortie', nargs='?')

    args = parser.parse_args()

    # Si pas de fichier de sortie, ajouter _filtered au nom
    if not args.output_file:
        import os
        base, ext = os.path.splitext(args.input_file)
        args.output_file = f"{base}_filtered{ext}"

    try:
        filter_config(args.input_file, args.output_file)
        print(f"\n🎉 Configuration filtrée créée: {args.output_file}")
    except Exception as e:
        print(f"❌ Erreur: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
