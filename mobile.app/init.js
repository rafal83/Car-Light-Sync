#!/usr/bin/env node

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

console.log('🚀 Initialisation du projet Car Light Sync Mobile...\n');

// Vérifier que nous sommes dans le bon dossier
if (!fs.existsSync('package.json')) {
    console.error('❌ Erreur: package.json introuvable. Exécutez ce script depuis le dossier mobile.app/');
    process.exit(1);
}

// Vérifier que les fichiers source existent
const webSourceDir = path.join(__dirname, '..', 'data');
const requiredSources = ['index.html', 'script.js', 'style.css', 'carlightsync.png'];
for (const fileName of requiredSources) {
    const sourcePath = path.join(webSourceDir, fileName);
    if (!fs.existsSync(sourcePath)) {
        console.error(`❌ Erreur: ../data/${fileName} introuvable.`);
        process.exit(1);
    }
}

function run(command, description) {
    console.log(`\n📦 ${description}...`);
    try {
        execSync(command, { stdio: 'inherit' });
        console.log(`✅ ${description} terminé`);
        return true;
    } catch (error) {
        console.error(`❌ Erreur lors de: ${description}`);
        return false;
    }
}

// Étape 1: Installer les dépendances
if (!run('npm install', 'Installation des dépendances npm')) {
    process.exit(1);
}

// Étape 2: Synchroniser les fichiers web
if (!run('node sync-html.js', 'Synchronisation des fichiers web')) {
    process.exit(1);
}

// Étape 3: Ajouter les plateformes
console.log('\n📱 Ajout des plateformes...');

const platforms = [];
if (process.platform === 'darwin') {
    platforms.push('ios');
}
platforms.push('android');

for (const platform of platforms) {
    const platformDir = path.join(__dirname, platform);
    if (!fs.existsSync(platformDir)) {
        if (!run(`npx cap add ${platform}`, `Ajout de la plateforme ${platform}`)) {
            console.warn(`⚠️  Impossible d'ajouter ${platform}, continuons...`);
        }
    } else {
        console.log(`✅ Plateforme ${platform} déjà présente`);
    }
}

// Étape 4: Synchroniser avec Capacitor
if (!run('npx cap sync', 'Synchronisation avec Capacitor')) {
    console.warn('⚠️  Synchronisation Capacitor échouée, mais continuons...');
}

console.log('\n' + '='.repeat(60));
console.log('✨ Initialisation terminée avec succès! ✨');
console.log('='.repeat(60));
console.log('\n📱 Prochaines étapes:\n');
console.log('  Pour Android:');
console.log('    npm run open:android');
console.log('\n  Pour iOS (macOS uniquement):');
console.log('    npm run open:ios');
console.log('\n  Pour synchroniser après modification des fichiers web:');
console.log('    npm run sync');
console.log('\n📖 Consultez QUICKSTART.md pour plus d\'informations\n');
