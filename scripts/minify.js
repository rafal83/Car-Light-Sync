const fs = require('fs');
const path = require('path');
const { minify: minifyHTML } = require('html-minifier-terser');
const esbuild = require('esbuild');

const dataDir = path.join(__dirname, '..', 'data');

// Configuration HTML minifier
const htmlMinifyOptions = {
    collapseWhitespace: true,
    removeComments: true,
    removeRedundantAttributes: true,
    removeScriptTypeAttributes: true,
    removeStyleLinkTypeAttributes: true,
    useShortDoctype: true,
    minifyCSS: true,
    minifyJS: true,
    removeAttributeQuotes: false, // Garder les quotes pour compatibilité
    keepClosingSlash: true
};

// Configuration esbuild pour CSS
const cssMinifyOptions = {
    minify: true,
    target: 'es2015',
    loader: {
        '.css': 'css'
    }
};

// Configuration esbuild pour JS
// IMPORTANT: Ne pas utiliser IIFE car les fichiers partagent des variables globales
const jsMinifyOptions = {
    minify: true,
    target: 'es2015',
    // PAS de format: 'iife' car cela encapsulerait le code et rendrait les variables inaccessibles
    treeShaking: false, // Ne pas supprimer le code "inutilisé" car il peut être appelé dynamiquement

    // Mangling prudent (renommage des variables)
    // ATTENTION: On ne peut PAS activer un mangling agressif car:
    // - Les IDs HTML (getElementById('wifi-status')) doivent correspondre au HTML
    // - Les variables globales (translations, effectsList) sont partagées entre fichiers
    // - Les propriétés API (ev, fx, bri) doivent matcher le backend C++
    // - Les attributs data-* ne peuvent pas être renommés

    // Ce qu'esbuild fait déjà avec minify:true:
    // ✓ Renomme les variables locales (let x = 5 → let a = 5)
    // ✓ Raccourcit les espaces
    // ✓ Supprime les commentaires
    // ✓ Optimise les expressions

    // Pour aller plus loin de manière SÛRE:
    keepNames: false, // Permet de renommer les fonctions/variables locales (gain ~5-10%)
    // Note: On ne touche PAS aux noms des propriétés car trop dangereux

    globalName: undefined // Pas de wrapper global
};

async function minifyFile(filePath, type) {
    console.log(`Minifying ${path.basename(filePath)}...`);

    const gzPath = filePath + '.gz';
    if (fs.existsSync(gzPath)) {
        try {
            const sourceMtime = fs.statSync(filePath).mtimeMs;
            const gzMtime = fs.statSync(gzPath).mtimeMs;
            if (gzMtime >= sourceMtime) {
                console.log(`  Skipping ${path.basename(filePath)} (gzip newer than source)`);
                return { success: true, skipped: true };
            }
        } catch (error) {
            console.warn(`  Could not compare timestamps for ${path.basename(filePath)}: ${error.message}`);
        }
    }

    const content = fs.readFileSync(filePath, 'utf8');
    const outputPath = filePath + '.min';

    let minified;
    let originalSize = Buffer.byteLength(content, 'utf8');

    try {
        if (type === 'html') {
            minified = await minifyHTML(content, htmlMinifyOptions);
        } else if (type === 'css') {
            const result = await esbuild.transform(content, {
                ...cssMinifyOptions,
                loader: 'css'
            });
            minified = result.code;
        } else if (type === 'js') {
            const result = await esbuild.transform(content, {
                ...jsMinifyOptions,
                loader: 'js'
            });
            minified = result.code;
        }

        fs.writeFileSync(outputPath, minified, 'utf8');

        const minifiedSize = Buffer.byteLength(minified, 'utf8');
        const reduction = ((1 - minifiedSize / originalSize) * 100).toFixed(1);

        console.log(`  ✓ ${path.basename(filePath)}: ${originalSize} → ${minifiedSize} bytes (${reduction}% reduction)`);

        return { success: true, originalSize, minifiedSize, reduction };
    } catch (error) {
        console.error(`  ✗ Error minifying ${path.basename(filePath)}:`, error.message);
        return { success: false, error: error.message };
    }
}

async function minifyAll() {
    console.log('🔧 Starting minification process...\n');

    const files = [
        { path: path.join(dataDir, 'index.html'), type: 'html' },
        { path: path.join(dataDir, 'style.css'), type: 'css' },
        { path: path.join(dataDir, 'script.js'), type: 'js' },
        { path: path.join(dataDir, 'i18n.js'), type: 'js' }
    ];

    let totalOriginal = 0;
    let totalMinified = 0;
    let successCount = 0;
    let skipCount = 0;

    for (const file of files) {
        if (fs.existsSync(file.path)) {
            const result = await minifyFile(file.path, file.type);
            if (result.success) {
                if (result.skipped) {
                    skipCount++;
                } else {
                    totalOriginal += result.originalSize;
                    totalMinified += result.minifiedSize;
                    successCount++;
                }
            }
        } else {
            console.log(`  ⚠ Skipping ${path.basename(file.path)} (not found)`);
        }
    }

    console.log('\n📊 Summary:');
    console.log(`  Files processed: ${successCount + skipCount}/${files.length} (minified: ${successCount}, skipped: ${skipCount})`);
    if (successCount > 0 && totalOriginal > 0) {
        console.log(`  Total size: ${totalOriginal} → ${totalMinified} bytes`);
        console.log(`  Total reduction: ${((1 - totalMinified / totalOriginal) * 100).toFixed(1)}%`);
    } else {
        console.log('  No files minified (all gzip files already up to date)');
    }
    console.log('\n✅ Minification complete!\n');
}

// Execute minification
minifyAll().catch(error => {
    console.error('❌ Minification failed:', error);
    process.exit(1);
});
