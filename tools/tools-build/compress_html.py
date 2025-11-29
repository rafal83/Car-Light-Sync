#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Script pour minifier et compresser les fichiers web en GZIP avant le build"""

import gzip
import os
import subprocess
import sys

# Fix pour l'encodage sur Windows
if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

# Liste des fichiers à minifier et compresser
files_to_compress = [
    "data/index.html",
    "data/i18n.js",
    "data/script.js",
    "data/style.css",
]

def run_minification():
    """Exécute le script de minification Node.js"""
    print("\n" + "="*70)
    print("STEP 1: WEB FILES MINIFICATION")
    print("="*70 + "\n")

    # Vérifier si Node.js est disponible
    try:
        subprocess.run(["node", "--version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("WARNING: Node.js not found. Skipping minification.")
        print("   Install Node.js to enable automatic minification.")
        print("   Continuing with compression only...\n")
        return False

    # Vérifier si node_modules existe
    if not os.path.exists("node_modules"):
        print("Installing npm dependencies...")
        try:
            subprocess.run(["npm", "install"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"Failed to install npm dependencies: {e}")
            print("   Continuing with compression only...\n")
            return False

    # Exécuter le script de minification
    minify_script = os.path.join("scripts", "minify.js")
    if not os.path.exists(minify_script):
        print(f"WARNING: Minify script not found at {minify_script}")
        print("   Continuing with compression only...\n")
        return False

    try:
        subprocess.run(["node", minify_script], check=True)
        print("\nMinification completed!\n")
        return True
    except subprocess.CalledProcessError as e:
        print(f"\nMinification failed with exit code {e.returncode}")
        print("   Continuing with compression only...\n")
        return False

def compress_file(source_file, use_minified=True):
    """Compresse un fichier en GZIP si nécessaire"""
    # Déterminer quel fichier compresser
    file_to_compress = source_file
    if use_minified:
        minified_file = source_file + ".min"
        if os.path.exists(minified_file):
            file_to_compress = minified_file
            print(f"  Using minified version: {minified_file}")

    gz_file = source_file + ".gz"

    if not os.path.exists(file_to_compress):
        print("WARN Fichier {} introuvable".format(file_to_compress))
        return

    # Vérifier si le fichier .gz existe et est à jour
    needs_compression = True
    if os.path.exists(gz_file):
        source_mtime = os.path.getmtime(file_to_compress)
        gz_mtime = os.path.getmtime(gz_file)
        if gz_mtime >= source_mtime:
            needs_compression = False
            print("Skip {} (already up-to-date)".format(source_file))

    if needs_compression:
        print("Compression de {}...".format(file_to_compress))

        with open(file_to_compress, 'rb') as f_in:
            with gzip.open(gz_file, 'wb', compresslevel=9) as f_out:
                f_out.write(f_in.read())

        original_size = os.path.getsize(file_to_compress)
        compressed_size = os.path.getsize(gz_file)
        ratio = (1 - compressed_size / original_size) * 100

        print("OK {} ({} octets) -> {} ({} octets)".format(file_to_compress, original_size, gz_file, compressed_size))
        print("  Compression: {:.1f}%".format(ratio))

# Étape 1: Minification
minification_success = run_minification()

# Étape 2: Compression GZIP
print("="*70)
print("STEP 2: GZIP COMPRESSION")
print("="*70 + "\n")

for file in files_to_compress:
    compress_file(file, use_minified=minification_success)

print("\n" + "="*70)
print("BUILD PREPARATION COMPLETE!")
print("="*70 + "\n")
