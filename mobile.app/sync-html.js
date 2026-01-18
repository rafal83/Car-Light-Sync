const fs = require('fs');
const path = require('path');

const SOURCE_DIR = path.join(__dirname, '..', 'data');
const DEST_DIR = path.join(__dirname, 'www');
const HTML_FILENAME = 'index.html';
const REQUIRED_ASSETS = ['i18n.js', 'script.js', 'style.css', 'carlightsync.png', 'carlightsync64.png'];
const OPTIONAL_ASSETS = [];
const DASHBOARD_DIR = 'dashboard';
const DASHBOARD_FILES = ['dashboard.html', 'dashboard.css', 'dashboard.js', 'capacitor.js', 'ble-client-loader.js', 'capacitor-bluetooth-adapter.js'];
const DASHBOARD_SVG_DIR = 'svg';

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

// Copy dashboard files
const dashboardSourceDir = path.join(SOURCE_DIR, DASHBOARD_DIR);
const dashboardDestDir = path.join(DEST_DIR);
const dashboardSvgSourceDir = path.join(dashboardSourceDir, DASHBOARD_SVG_DIR);
const dashboardSvgDestDir = path.join(dashboardDestDir, DASHBOARD_SVG_DIR);

if (fs.existsSync(dashboardSourceDir)) {
  if (!fs.existsSync(dashboardDestDir)) {
    fs.mkdirSync(dashboardDestDir, { recursive: true });
  }

  for (const file of DASHBOARD_FILES) {
    const sourcePath = path.join(dashboardSourceDir, file);
    const destPath = path.join(dashboardDestDir, file);

    if (fs.existsSync(sourcePath)) {
      // Special handling for dashboard.html to inject Capacitor scripts
      if (file === 'dashboard.html') {
        let dashboardHtml = fs.readFileSync(sourcePath, 'utf8');

        const dashboardCapacitorInjection = [
          '    <script src="capacitor.js"></script>',
          '    <script src="ble-client-loader.js"></script>',
          '    <script src="capacitor-bluetooth-adapter.js"></script>',
        ].join('\n');

        if (!dashboardHtml.includes('ble-client-loader.js')) {
          if (!dashboardHtml.includes('</head>')) {
            console.error('dashboard.html does not contain </head>.');
            process.exit(1);
          }
          dashboardHtml = dashboardHtml.replace('</head>', `${dashboardCapacitorInjection}\n</head>`);
        }

        fs.writeFileSync(destPath, dashboardHtml, 'utf8');
        console.log(`${DASHBOARD_DIR}/${file} synchronized with Capacitor injection.`);
      } else {
        fs.copyFileSync(sourcePath, destPath);
        console.log(`${DASHBOARD_DIR}/${file} copied.`);
      }
    } else {
      console.warn(`Warning: ${DASHBOARD_DIR}/${file} not found, skipping.`);
    }
  }
} else {
  console.warn(`Warning: ${DASHBOARD_DIR} directory not found, skipping dashboard sync.`);
}

// Copy dashboard SVG assets
if (fs.existsSync(dashboardSvgSourceDir)) {
  if (!fs.existsSync(dashboardSvgDestDir)) {
    fs.mkdirSync(dashboardSvgDestDir, { recursive: true });
  }

  const svgFiles = fs.readdirSync(dashboardSvgSourceDir).filter((fileName) => fileName.endsWith('.svg'));
  for (const fileName of svgFiles) {
    const sourcePath = path.join(dashboardSvgSourceDir, fileName);
    const destPath = path.join(dashboardSvgDestDir, fileName);
    fs.copyFileSync(sourcePath, destPath);
    console.log(`${DASHBOARD_DIR}/${DASHBOARD_SVG_DIR}/${fileName} copied.`);
  }
} else {
  console.warn(`Warning: ${DASHBOARD_DIR}/${DASHBOARD_SVG_DIR} directory not found, skipping SVG sync.`);
}

console.log('Web assets ready for Capacitor sync.');
