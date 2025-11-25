const fs = require('fs');
const path = require('path');

const SOURCE_DIR = path.join(__dirname, '..', 'data');
const DEST_DIR = path.join(__dirname, 'www');
const HTML_FILENAME = 'index.html';
const REQUIRED_ASSETS = ['script.js', 'style.css', 'carlightsync.png'];
const OPTIONAL_ASSETS = [];

function ensureFileExists(filePath, label) {
  if (!fs.existsSync(filePath)) {
    console.error(`Missing ${label}.`);
    process.exit(1);
  }
}

function copyAsset(fileName) {
  const sourcePath = path.join(SOURCE_DIR, fileName);
  const destPath = path.join(DEST_DIR, fileName);
  fs.copyFileSync(sourcePath, destPath);
  console.log(`${fileName} copied.`);
}

ensureFileExists(path.join(SOURCE_DIR, HTML_FILENAME), '../data/index.html');
for (const asset of REQUIRED_ASSETS) {
  ensureFileExists(path.join(SOURCE_DIR, asset), `../data/${asset}`);
}

if (!fs.existsSync(DEST_DIR)) {
  fs.mkdirSync(DEST_DIR, { recursive: true });
}

const htmlSourcePath = path.join(SOURCE_DIR, HTML_FILENAME);
const htmlDestPath = path.join(DEST_DIR, HTML_FILENAME);
let htmlContent = fs.readFileSync(htmlSourcePath, 'utf8');

const capacitorInjection = [
  '    <script src="capacitor.js"></script>',
  '    <script src="ble-client-loader.js"></script>',
  '    <script src="capacitor-bluetooth-adapter.js"></script>',
].join('\n');

if (!htmlContent.includes('ble-client-loader.js')) {
  if (!htmlContent.includes('</head>')) {
    console.error('index.html does not contain </head>.');
    process.exit(1);
  }
  htmlContent = htmlContent.replace('</head>', `${capacitorInjection}\n</head>`);
}

fs.writeFileSync(htmlDestPath, htmlContent, 'utf8');
console.log('index.html synchronized with Capacitor injection.');

for (const asset of REQUIRED_ASSETS) {
  copyAsset(asset);
}

console.log('Web assets ready for Capacitor sync.');
