#!/usr/bin/env python3
"""Script pour compresser les fichiers web en GZIP avant le build"""

import gzip
import os

# Liste des fichiers à compresser
files_to_compress = [
    "data/index.html",
    "data/script.js",
    "data/style.css",
]

def compress_file(source_file):
    """Compresse un fichier en GZIP si nécessaire"""
    gz_file = source_file + ".gz"

    if not os.path.exists(source_file):
        print("WARN Fichier {} introuvable".format(source_file))
        return

    # Vérifier si le fichier .gz existe et est à jour
    needs_compression = True
    # if os.path.exists(gz_file):
    #     source_mtime = os.path.getmtime(source_file)
    #     gz_mtime = os.path.getmtime(gz_file)
    #     if gz_mtime >= source_mtime:
    #         needs_compression = False
    #         print("Skip {} (already up-to-date)".format(source_file))

    if needs_compression:
        print("Compression de {}...".format(source_file))

        with open(source_file, 'rb') as f_in:
            with gzip.open(gz_file, 'wb', compresslevel=9) as f_out:
                f_out.write(f_in.read())

        original_size = os.path.getsize(source_file)
        compressed_size = os.path.getsize(gz_file)
        ratio = (1 - compressed_size / original_size) * 100

        print("OK {} ({} octets) -> {} ({} octets)".format(source_file, original_size, gz_file, compressed_size))
        print("  Compression: {:.1f}%".format(ratio))

# Compresser tous les fichiers
for file in files_to_compress:
    compress_file(file)
