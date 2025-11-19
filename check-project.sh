#!/bin/bash

# Script de vérification du projet Tesla Strip Controller v2.0

echo "🔍 Vérification du projet Tesla Strip Controller v2.0"
echo "======================================================"
echo ""

# Compteurs
ERRORS=0
WARNINGS=0

# Fonction de vérification
check_file() {
    if [ -f "$1" ]; then
        echo "✓ $1"
    else
        echo "✗ MANQUANT: $1"
        ((ERRORS++))
    fi
}

check_dir() {
    if [ -d "$1" ]; then
        echo "✓ Répertoire: $1"
    else
        echo "✗ MANQUANT: Répertoire $1"
        ((ERRORS++))
    fi
}

echo "📁 Vérification de la structure..."
check_dir "include"
check_dir "main"
check_dir "data"
echo ""

echo "📄 Vérification des fichiers d'en-tête..."
check_file "include/config.h"
check_file "include/wifi_manager.h"
check_file "include/can_bus.h"
check_file "include/vehicle_can_unified.h"
check_file "include/led_effects.h"
check_file "include/web_server.h"
check_file "include/config_manager.h"
echo ""

echo "💻 Vérification des fichiers source..."
check_file "main/main.c"
check_file "main/wifi_manager.c"
check_file "main/can_bus.c"
check_file "main/tesla_can.c"
check_file "main/led_effects.c"
check_file "main/web_server.c"
check_file "main/config_manager.c"
echo ""

echo "🌐 Vérification de l'interface web..."
check_file "data/index.html"
echo ""

echo "🔧 Vérification de la configuration..."
check_file "CMakeLists.txt"
check_file "main/CMakeLists.txt"
check_file "platformio.ini"
check_file "sdkconfig.defaults"
check_file "partitions.csv"
check_file "main/Kconfig.projbuild"
echo ""

echo "📚 Vérification de la documentation..."
check_file "README.md"
check_file "FEATURES.md"
check_file "ADVANCED.md"
check_file "WIRING.md"
check_file "CHANGELOG.md"
check_file "CHANGES_V2.md"
check_file "LICENSE"
echo ""

echo "🛠️ Vérification des scripts..."
check_file "tesla-strip.sh"
check_file ".gitignore"
echo ""

# Vérification du contenu
echo "🔍 Vérification du contenu..."

# Vérifier les includes importants dans main.c
if grep -q "#include \"config_manager.h\"" main/main.c; then
    echo "✓ main.c inclut config_manager.h"
else
    echo "⚠ ATTENTION: main.c n'inclut pas config_manager.h"
    ((WARNINGS++))
fi

# Vérifier les nouveaux messages CAN
if grep -q "CAN_ID_BLINDSPOT" include/vehicle_can_unified.h; then
    echo "✓ Message CAN blindspot présent"
else
    echo "✗ ERREUR: Message CAN blindspot manquant"
    ((ERRORS++))
fi

if grep -q "CAN_ID_NIGHT_MODE" include/vehicle_can_unified.h; then
    echo "✓ Message CAN night_mode présent"
else
    echo "✗ ERREUR: Message CAN night_mode manquant"
    ((ERRORS++))
fi

# Vérifier config_manager dans CMakeLists
if grep -q "config_manager.c" main/CMakeLists.txt; then
    echo "✓ config_manager.c dans CMakeLists"
else
    echo "✗ ERREUR: config_manager.c manquant dans CMakeLists"
    ((ERRORS++))
fi

# Vérifier les routes API dans web_server.c
if grep -q "profiles_handler" main/web_server.c; then
    echo "✓ Handler profils présent"
else
    echo "✗ ERREUR: Handler profils manquant"
    ((ERRORS++))
fi

if grep -q "event_effect_handler" main/web_server.c; then
    echo "✓ Handler événements présent"
else
    echo "✗ ERREUR: Handler événements manquant"
    ((ERRORS++))
fi

echo ""

# Statistiques
echo "📊 Statistiques du projet..."
TOTAL_FILES=$(find . -name "*.c" -o -name "*.h" | wc -l)
TOTAL_LINES=$(cat main/*.c include/*.h data/*.html 2>/dev/null | wc -l)
echo "Fichiers sources: $TOTAL_FILES"
echo "Lignes de code: $TOTAL_LINES"
echo ""

# Résumé
echo "======================================================"
if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo "✅ Projet OK - Prêt à compiler !"
elif [ $ERRORS -eq 0 ]; then
    echo "⚠️  Projet OK avec $WARNINGS avertissement(s)"
else
    echo "❌ Projet incomplet - $ERRORS erreur(s), $WARNINGS avertissement(s)"
fi
echo "======================================================"

exit $ERRORS
