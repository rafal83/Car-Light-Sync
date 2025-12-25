const fs = require('fs');
const path = require('path');
const { minify: minifyHTML } = require('html-minifier-terser');
const esbuild = require('esbuild');

const dataDir = path.join(__dirname, '..', 'data');

// HTML minifier configuration
const htmlMinifyOptions = {
    collapseWhitespace: true,
    removeComments: true,
    removeRedundantAttributes: true,
    removeScriptTypeAttributes: true,
    removeStyleLinkTypeAttributes: true,
    useShortDoctype: true,
    minifyCSS: true,
    minifyJS: true,
    removeAttributeQuotes: false, // Keep quotes for compatibility
    keepClosingSlash: true
};

// esbuild configuration for CSS
const cssMinifyOptions = {
    minify: true,
    target: 'es2015',
    loader: {
        '.css': 'css'
    }
};

// esbuild configuration for JS
// IMPORTANT: Don't use IIFE as files share global variables
const jsMinifyOptions = {
    minify: true,
    target: 'es2015',
    // NO format: 'iife' as it would encapsulate code and make variables inaccessible
    treeShaking: false, // Don't remove "unused" code as it may be called dynamically

    // Careful mangling (variable renaming)
    // WARNING: We CANNOT enable aggressive mangling because:
    // - HTML IDs (getElementById('wifi-status')) must match the HTML
    // - Global variables (translations, effectsList) are shared between files
    // - API properties (ev, fx, bri) must match the C++ backend
    // - data-* attributes cannot be renamed

    // What esbuild already does with minify:true:
    // ✓ Renames local variables (let x = 5 → let a = 5)
    // ✓ Shortens whitespace
    // ✓ Removes comments
    // ✓ Optimizes expressions

    // To go further SAFELY:
    keepNames: false, // Allows renaming functions/local variables (~5-10% gain)
    // Note: We do NOT touch property names as it's too dangerous

    globalName: undefined // No global wrapper
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
