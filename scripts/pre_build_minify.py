#!/usr/bin/env python3
"""
Pre-build script to minify HTML, CSS, and JS files before building filesystem
"""

Import("env")
import subprocess
import os
import sys

def minify_web_files(source, target, env):
    """Run Node.js minification script before building SPIFFS"""
    print("\n" + "="*60)
    print("Running minification script...")
    print("="*60 + "\n")

    project_dir = env.get("PROJECT_DIR")
    minify_script = os.path.join(project_dir, "scripts", "minify.js")

    # Check if Node.js is available
    try:
        subprocess.run(["node", "--version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("⚠️  WARNING: Node.js not found. Skipping minification.")
        print("   Install Node.js to enable automatic minification.")
        return

    # Check if npm dependencies are installed
    node_modules = os.path.join(project_dir, "node_modules")
    if not os.path.exists(node_modules):
        print("📦 Installing npm dependencies...")
        try:
            subprocess.run(["npm", "install"], cwd=project_dir, check=True)
        except subprocess.CalledProcessError as e:
            print(f"❌ Failed to install npm dependencies: {e}")
            return

    # Run minification script
    try:
        result = subprocess.run(
            ["node", minify_script],
            cwd=project_dir,
            check=True,
            capture_output=False
        )
        print("\n" + "="*60)
        print("Minification completed successfully!")
        print("="*60 + "\n")
    except subprocess.CalledProcessError as e:
        print(f"\n❌ Minification failed with exit code {e.returncode}")
        print("   Continuing build with non-minified files...")

# Register the pre-build action for SPIFFS/LittleFS
env.AddPreAction("$BUILD_DIR/spiffs.bin", minify_web_files)
env.AddPreAction("$BUILD_DIR/littlefs.bin", minify_web_files)

print("✓ Pre-build minification script registered")
