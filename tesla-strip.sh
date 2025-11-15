#!/bin/bash

# Script d'aide pour Tesla Strip Controller

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

function show_help() {
    echo "Tesla Strip Controller - Script d'aide"
    echo ""
    echo "Usage: $0 [commande]"
    echo ""
    echo "Commandes disponibles:"
    echo "  build       - Compiler le projet"
    echo "  flash       - Flasher l'ESP32"
    echo "  monitor     - Moniteur série"
    echo "  clean       - Nettoyer le build"
    echo "  erase       - Effacer la flash complète"
    echo "  config      - Configurer le projet (menuconfig)"
    echo "  all         - Build + Flash + Monitor"
    echo "  help        - Afficher cette aide"
    echo ""
}

function check_idf() {
    if [ -z "$IDF_PATH" ]; then
        echo "❌ ESP-IDF n'est pas configuré"
        echo "Exécutez: . $HOME/esp/esp-idf/export.sh"
        exit 1
    fi
    echo "✓ ESP-IDF trouvé: $IDF_PATH"
}

function build() {
    echo "🔨 Compilation du projet..."
    check_idf
    idf.py build
    echo "✓ Compilation terminée"
}

function flash() {
    echo "📡 Flash de l'ESP32..."
    check_idf
    
    # Détecter le port
    PORT=$(ls /dev/ttyUSB* 2>/dev/null | head -n1)
    if [ -z "$PORT" ]; then
        PORT=$(ls /dev/ttyACM* 2>/dev/null | head -n1)
    fi
    
    if [ -z "$PORT" ]; then
        echo "❌ Aucun ESP32 détecté"
        echo "Vérifiez la connexion USB"
        exit 1
    fi
    
    echo "Port détecté: $PORT"
    idf.py -p $PORT flash
    echo "✓ Flash terminé"
}

function monitor() {
    echo "📺 Démarrage du moniteur série..."
    check_idf
    
    PORT=$(ls /dev/ttyUSB* 2>/dev/null | head -n1)
    if [ -z "$PORT" ]; then
        PORT=$(ls /dev/ttyACM* 2>/dev/null | head -n1)
    fi
    
    if [ -z "$PORT" ]; then
        echo "❌ Aucun ESP32 détecté"
        exit 1
    fi
    
    idf.py -p $PORT monitor
}

function clean() {
    echo "🧹 Nettoyage..."
    check_idf
    idf.py fullclean
    rm -rf build/
    echo "✓ Nettoyage terminé"
}

function erase() {
    echo "⚠️  Effacement complet de la flash..."
    read -p "Êtes-vous sûr? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        check_idf
        idf.py erase-flash
        echo "✓ Flash effacée"
    else
        echo "Annulé"
    fi
}

function config() {
    echo "⚙️  Configuration du projet..."
    check_idf
    idf.py menuconfig
}

function all() {
    build
    flash
    monitor
}

# Menu principal
case "${1:-help}" in
    build)
        build
        ;;
    flash)
        flash
        ;;
    monitor)
        monitor
        ;;
    clean)
        clean
        ;;
    erase)
        erase
        ;;
    config)
        config
        ;;
    all)
        all
        ;;
    help|*)
        show_help
        ;;
esac
