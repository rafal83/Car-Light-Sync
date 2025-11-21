const fs = require('fs');
const path = require('path');

// Chemins source et destination
const sourcePath = path.join(__dirname, '..', 'data', 'index.html');
const destDir = path.join(__dirname, 'www');
const destPath = path.join(destDir, 'index.html');
const iconSourcePath = path.join(__dirname, '..', 'data', 'icon.svg');
const iconDestPath = path.join(destDir, 'icon.svg');

// Créer le dossier www s'il n'existe pas
if (!fs.existsSync(destDir)) {
    fs.mkdirSync(destDir, { recursive: true });
}

// Lire le fichier source
let htmlContent = fs.readFileSync(sourcePath, 'utf8');

// Injecter le script Capacitor juste avant </head>
// Simple: Capacitor charge automatiquement le plugin natif, on crée juste le wrapper
const capacitorScript = `    <script src="capacitor.js"></script>
    <script src="ble-client-loader.js"></script>
    <script src="capacitor-bluetooth-adapter.js"></script>
`;

htmlContent = htmlContent.replace('</head>', capacitorScript + '</head>');

// Patch pour forcer le mode BLE sur mobile natif
// Modifier la déclaration de wifiOnline pour qu'elle soit toujours false sur mobile
const wifiOnlinePatch = `let usingFileProtocol = window.location.protocol === 'file:';
        let wifiOnline = !usingFileProtocol && navigator.onLine;
        // Sur Capacitor natif, forcer wifiOnline à false pour utiliser BLE exclusivement
        if (window.isCapacitorNativeApp === true) {
            console.log('📱 Capacitor native app detected: forcing wifiOnline = false to use BLE');
            wifiOnline = false;
        }`;

htmlContent = htmlContent.replace(
    /let usingFileProtocol = window\.location\.protocol === 'file:';[\s\n]*let wifiOnline = !usingFileProtocol && navigator\.onLine;/,
    wifiOnlinePatch
);

// Écrire le fichier modifié
fs.writeFileSync(destPath, htmlContent, 'utf8');
console.log('✅ index.html synchronized and Capacitor scripts injected');

// Copier l'icône SVG si elle existe
if (fs.existsSync(iconSourcePath)) {
    fs.copyFileSync(iconSourcePath, iconDestPath);
    console.log('✅ icon.svg copied');
}

console.log('📱 Ready for Capacitor sync!');
