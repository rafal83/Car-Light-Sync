#!/usr/bin/env python3
"""
Script pour remplacer les clés JSON longues par des clés courtes dans script.js
"""

import re

# Mapping des clés à remplacer
replacements = {
    # Configuration et effets
    r'\.effect\b': '.fx',
    r'\.brightness\b': '.br',
    r'\.speed\b': '.sp',
    r'\.color1\b': '.c1',
    r'\.color2\b': '.c2',
    r'\.color3\b': '.c3',
    r'\.sync_mode\b': '.sm',
    r'\.reverse\b': '.rv',
    r'\.auto_night_mode\b': '.anm',
    r'\.night_brightness\b': '.nbr',
    r'\.led_count\b': '.lc',
    r'\.data_pin\b': '.dp',
    r'\.strip_reverse\b': '.srv',

    # Profiles
    r'\.name\b': '.n',
    r'\.active\b': '.ac',
    r'\.audio_reactive\b': '.ar',

    # Audio
    r'\.enabled\b': '.en',
    r'\.sensitivity\b': '.sen',
    r'\.gain\b': '.gn',
    r'\.autoGain\b': '.ag',
    r'\.fftEnabled\b': '.ffe',
    r'\.amplitude\b': '.amp',
    r'\.bass\b': '.ba',
    r'\.mid\b': '.md',
    r'\.treble\b': '.tr',
    r'\.beatDetected\b': '.bd',
    r'\.available\b': '.av',

    # FFT
    r'\.peakFreq\b': '.pf',
    r'\.spectralCentroid\b': '.sc',
    r'\.dominantBand\b': '.db',
    r'\.bassEnergy\b': '.be',
    r'\.midEnergy\b': '.me',
    r'\.trebleEnergy\b': '.te',
    r'\.kickDetected\b': '.kd',
    r'\.snareDetected\b': '.sd',
    r'\.vocalDetected\b': '.vd',

    # Events
    r'\.event\b': '.ev',
    r'\.duration\b': '.dur',
    r'\.priority\b': '.pri',
    r'\.action_type\b': '.at',
    r'\.profile_id\b': '.pid',
    r'\.can_switch_profile\b': '.csp',

    # Effects list
    r'\.can_required\b': '.cr',
    r'\.audio_effect\b': '.ae',

    # OTA
    r'\.version\b': '.v',
    r'\.state\b': '.st',
    r'\.progress\b': '.pg',
    r'\.written_size\b': '.ws',
    r'\.total_size\b': '.ts',
    r'\.reboot_countdown\b': '.rc',
    r'\.error\b': '.err',

    # Réponses API (dans JSON strings)
    r'"status"': '"st"',
    r'"message"': '"msg"',
    r'"success"': '"ok"',
    r'"restart_required"': '"rr"',
    r'"updated"': '"upd"',
}

# Replacements pour les strings JSON (sans le point)
json_replacements = {
    r'"effect"': '"fx"',
    r'"brightness"': '"br"',
    r'"speed"': '"sp"',
    r'"color1"': '"c1"',
    r'"color2"': '"c2"',
    r'"color3"': '"c3"',
    r'"sync_mode"': '"sm"',
    r'"reverse"': '"rv"',
    r'"auto_night_mode"': '"anm"',
    r'"night_brightness"': '"nbr"',
    r'"led_count"': '"lc"',
    r'"data_pin"': '"dp"',
    r'"strip_reverse"': '"srv"',
    r'"enabled"': '"en"',
    r'"name"': '"n"',
    r'"active"': '"ac"',
    r'"audio_reactive"': '"ar"',
    r'"sensitivity"': '"sen"',
    r'"gain"': '"gn"',
    r'"autoGain"': '"ag"',
    r'"fftEnabled"': '"ffe"',
    r'"event"': '"ev"',
    r'"duration"': '"dur"',
    r'"priority"': '"pri"',
    r'"action_type"': '"at"',
    r'"profile_id"': '"pid"',
    r'"can_required"': '"cr"',
    r'"audio_effect"': '"ae"',
    r'"sampleRate"': '"sr"',
    r'"fftSize"': '"sz"',
}

def replace_in_file(filepath):
    """Remplace toutes les clés dans le fichier"""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    original_content = content

    # Appliquer les remplacements pour les accès propriétés (.property)
    for pattern, replacement in replacements.items():
        content = re.sub(pattern, replacement, content)

    # Appliquer les remplacements pour les strings JSON ("property")
    for pattern, replacement in json_replacements.items():
        content = re.sub(pattern, replacement, content)

    # Vérifier si des changements ont été faits
    if content != original_content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"[OK] Fichier {filepath} mis a jour")
        return True
    else:
        print(f"[INFO] Aucun changement dans {filepath}")
        return False

if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("Usage: python replace_json_keys.py <file>")
        sys.exit(1)

    filepath = sys.argv[1]
    replace_in_file(filepath)
