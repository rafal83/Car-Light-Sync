#!/usr/bin/env python3
"""Script pour compresser index.html en GZIP avant le build"""

import gzip
import os

# Compresser uniquement si nécessaire
html_file = "data/index.html"
gz_file = "data/index.html.gz"

if os.path.exists(html_file):
    # Vérifier si le fichier .gz existe et est à jour
    needs_compression = True
    if os.path.exists(gz_file):
        html_mtime = os.path.getmtime(html_file)
        gz_mtime = os.path.getmtime(gz_file)
        if gz_mtime >= html_mtime:
            needs_compression = False
            print("Skip {} (already up-to-date)".format(html_file))

    if needs_compression:
        print("Compression de {}...".format(html_file))

        with open(html_file, 'rb') as f_in:
            with gzip.open(gz_file, 'wb', compresslevel=9) as f_out:
                f_out.write(f_in.read())

        original_size = os.path.getsize(html_file)
        compressed_size = os.path.getsize(gz_file)
        ratio = (1 - compressed_size / original_size) * 100

        print("OK {} ({} octets) -> {} ({} octets)".format(html_file, original_size, gz_file, compressed_size))
        print("  Compression: {:.1f}%".format(ratio))
else:
    print("WARN Fichier {} introuvable".format(html_file))
