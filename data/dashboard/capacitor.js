/**
 * Capacitor core initialization
 * This file is auto-included by sync-html.js
 *
 * Note: Capacitor is already injected globally by the Capacitor runtime
 * This script just logs platform info and ensures it's accessible
 */

// Wait for Capacitor to be available (injected by Capacitor runtime)
(function() {
    function initCapacitor() {
        if (window.Capacitor) {
            // Log platform info
            console.log('📱 Capacitor Platform:', window.Capacitor.getPlatform());
            console.log('📱 Is Native:', window.Capacitor.isNativePlatform());
        } else {
            console.warn('⚠️ Capacitor not available - running in web mode');
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initCapacitor);
    } else {
        initCapacitor();
    }
})();
