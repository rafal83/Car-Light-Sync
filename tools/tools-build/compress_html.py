#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Script to minify and compress web files to GZIP before build"""

import gzip
import os
import subprocess
import sys

# Fix for encoding on Windows
if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

# List of files to minify and compress
files_to_compress = [
    "data/index.html",
    "data/i18n.js",
    "data/script.js",
    "data/style.css",
]

def run_minification():
    """Runs the Node.js minification script"""
    print("\n" + "="*70)
    print("STEP 1: WEB FILES MINIFICATION")
    print("="*70 + "\n")

    # Check if Node.js is available
    try:
        subprocess.run(["node", "--version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("WARNING: Node.js not found. Skipping minification.")
        print("   Install Node.js to enable automatic minification.")
        print("   Continuing with compression only...\n")
        return False

    # Check if node_modules exists
    if not os.path.exists("node_modules"):
        print("Installing npm dependencies...")
        try:
            subprocess.run(["npm", "install"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"Failed to install npm dependencies: {e}")
            print("   Continuing with compression only...\n")
            return False

    # Run minification script
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
    """Compresses a file to GZIP if necessary"""
    # Determine which file to compress
    file_to_compress = source_file
    if use_minified:
        minified_file = source_file + ".min"
        if os.path.exists(minified_file):
            file_to_compress = minified_file
            print(f"  Using minified version: {minified_file}")

    gz_file = source_file + ".gz"

    if not os.path.exists(file_to_compress):
        print("WARN File {} not found".format(file_to_compress))
        return

    # Check if .gz file exists and is up-to-date
    needs_compression = True
    if os.path.exists(gz_file):
        source_mtime = os.path.getmtime(file_to_compress)
        gz_mtime = os.path.getmtime(gz_file)
        if gz_mtime >= source_mtime:
            needs_compression = False
            print("Skip {} (already up-to-date)".format(source_file))

    if needs_compression:
        print("Compressing {}...".format(file_to_compress))

        with open(file_to_compress, 'rb') as f_in:
            with gzip.open(gz_file, 'wb', compresslevel=9) as f_out:
                f_out.write(f_in.read())

        original_size = os.path.getsize(file_to_compress)
        compressed_size = os.path.getsize(gz_file)
        ratio = (1 - compressed_size / original_size) * 100

        print("OK {} ({} bytes) -> {} ({} bytes)".format(file_to_compress, original_size, gz_file, compressed_size))
        print("  Compression: {:.1f}%".format(ratio))

# Step 1: Minification
minification_success = run_minification()

# Step 2: GZIP Compression
print("="*70)
print("STEP 2: GZIP COMPRESSION")
print("="*70 + "\n")

for file in files_to_compress:
    compress_file(file, use_minified=minification_success)

print("\n" + "="*70)
print("BUILD PREPARATION COMPLETE!")
print("="*70 + "\n")
