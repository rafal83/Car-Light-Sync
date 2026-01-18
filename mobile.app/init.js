#!/usr/bin/env node

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

console.log('🚀 Initializing Car Light Sync Mobile project...\n');

// Check that we're in the correct folder
if (!fs.existsSync('package.json')) {
    console.error('❌ Error: package.json not found. Run this script from the mobile.app/ folder');
    process.exit(1);
}

// Check that source files exist
const webSourceDir = path.join(__dirname, '..', 'data');
const requiredSources = ['index.html', 'i18n.js', 'script.js', 'style.css', 'carlightsync.png', 'carlightsync64.png'];
for (const fileName of requiredSources) {
    const sourcePath = path.join(webSourceDir, fileName);
    if (!fs.existsSync(sourcePath)) {
        console.error(`❌ Error: ../data/${fileName} not found.`);
        process.exit(1);
    }
}

function run(command, description) {
    console.log(`\n📦 ${description}...`);
    try {
        execSync(command, { stdio: 'inherit' });
        console.log(`✅ ${description} complete`);
        return true;
    } catch (error) {
        console.error(`❌ Error during: ${description}`);
        return false;
    }
}

// Step 1: Install dependencies
if (!run('npm install', 'Installing npm dependencies')) {
    process.exit(1);
}

// Step 2: Sync web files
if (!run('node sync-html.js', 'Syncing web files')) {
    process.exit(1);
}

// Step 3: Add platforms
console.log('\n📱 Adding platforms...');

const platforms = [];
if (process.platform === 'darwin') {
    platforms.push('ios');
}
platforms.push('android');

for (const platform of platforms) {
    const platformDir = path.join(__dirname, platform);
    if (!fs.existsSync(platformDir)) {
        if (!run(`npx cap add ${platform}`, `Adding ${platform} platform`)) {
            console.warn(`⚠️  Unable to add ${platform}, continuing...`);
        }
    } else {
        console.log(`✅ Platform ${platform} already present`);
    }
}

// Step 4: Sync with Capacitor
if (!run('npx cap sync', 'Syncing with Capacitor')) {
    console.warn('⚠️  Capacitor sync failed, but continuing...');
}

console.log('\n' + '='.repeat(60));
console.log('✨ Initialization completed successfully! ✨');
console.log('='.repeat(60));
console.log('\n📱 Next steps:\n');
console.log('  For Android:');
console.log('    npm run open:android');
console.log('\n  For iOS (macOS only):');
console.log('    npm run open:ios');
console.log('\n  To sync after modifying web files:');
console.log('    npm run sync');
console.log('\n📖 See QUICKSTART.md for more information\n');
