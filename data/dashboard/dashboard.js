/**
 * Dashboard Mode for Car Light Sync
 * Lightweight BLE-only mode for real-time vehicle state display
 */

// Check if we're in Capacitor native app
const usingCapacitor = window.Capacitor?.isNativePlatform() || false;

// BLE Configuration
const BLE_SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
const BLE_CHARACTERISTIC_VEHICLE_STATE_UUID = 'c5c9c331-914b-459e-8fcc-c5c91fb54fad'; // New characteristic for vehicle state

// Device name pattern
const DEVICE_NAME_PREFIX = 'CarLightSync-';

// BLE connection state
let bleDevice = null;
let bleCharacteristicVehicleState = null;
let isConnected = false;

// Compact BLE vehicle state structure (vehicle_state_ble_t)
// 5 int16 (10) + 1 uint32 (4) + 1 uint8 (1) + 1 int8 (1) + 3 uint8 (3) + 5 flags (5) + 1 uint32 (4) = 28 bytes
const VEHICLE_STATE_SIZE = 28;

// Buffer for reassembling chunked BLE notifications (shouldn't be needed with 28 bytes, but keep for safety)
let vehicleStateBuffer = new Uint8Array(VEHICLE_STATE_SIZE);
let vehicleStateBufferOffset = 0;

/**
 * Decode compact BLE vehicle_state_ble_t from binary DataView
 * Structure from include/vehicle_can_unified.h
 */
function decodeVehicleState(dataView) {
    let offset = 0;

    // Read int16 (little-endian)
    const readInt16 = () => {
        const val = dataView.getInt16(offset, true);
        offset += 2;
        return val;
    };

    // Read uint16 (little-endian)
    const readUint16 = () => {
        const val = dataView.getUint16(offset, true);
        offset += 2;
        return val;
    };

    // Read uint8
    const readUint8 = () => {
        const val = dataView.getUint8(offset);
        offset += 1;
        return val;
    };

    // Read int8
    const readInt8 = () => {
        const val = dataView.getInt8(offset);
        offset += 1;
        return val;
    };

    // Read uint32 (little-endian)
    const readUint32 = () => {
        const val = dataView.getUint32(offset, true);
        offset += 4;
        return val;
    };

    // Read floats (converted from int16 * 10)
    const speed_kph = readInt16() / 10.0;
    const soc_percent = readInt16() / 10.0;
    const charge_power_kw = readInt16() / 10.0;
    const battery_voltage_LV = readInt16() / 10.0;
    const battery_voltage_HV = readInt16() / 10.0;
    const odometer_km = readUint32();  // uint32 pour supporter > 65535 km
    const brightness = readUint8() / 100.0;

    // Read uint8 values
    const gear = readInt8();
    const accel_pedal_pos = readUint8();
    const charge_status = readUint8();
    const autopilot = readUint8();

    // Read bit-packed flags (steering wheel buttons retirés, only 5 bytes now)
    const flags0 = readUint8();
    const flags1 = readUint8();
    const flags2 = readUint8();
    const flags3 = readUint8();
    const flags4 = readUint8();

    // Read meta
    const last_update_ms = readUint32();

    // Unpack bits (steering wheel buttons et doors_open_count retirés)
    const state = {
        // Converted floats
        speed_kph,
        soc_percent,
        charge_power_kw,
        battery_voltage_LV,
        battery_voltage_HV,
        odometer_km,
        brightness,

        // Direct uint8 values
        gear,
        accel_pedal_pos,
        charge_status,
        autopilot,

        // Flags0: doors & locks
        locked: (flags0 & (1<<0)) !== 0 ? 1 : 0,
        door_front_left_open: (flags0 & (1<<1)) !== 0 ? 1 : 0,
        door_rear_left_open: (flags0 & (1<<2)) !== 0 ? 1 : 0,
        door_front_right_open: (flags0 & (1<<3)) !== 0 ? 1 : 0,
        door_rear_right_open: (flags0 & (1<<4)) !== 0 ? 1 : 0,
        frunk_open: (flags0 & (1<<5)) !== 0 ? 1 : 0,
        trunk_open: (flags0 & (1<<6)) !== 0 ? 1 : 0,
        brake_pressed: (flags0 & (1<<7)) !== 0 ? 1 : 0,

        // Flags1: lights
        turn_left: (flags1 & (1<<0)) !== 0 ? 1 : 0,
        turn_right: (flags1 & (1<<1)) !== 0 ? 1 : 0,
        hazard: (flags1 & (1<<2)) !== 0 ? 1 : 0,
        headlights: (flags1 & (1<<3)) !== 0 ? 1 : 0,
        high_beams: (flags1 & (1<<4)) !== 0 ? 1 : 0,
        fog_lights: (flags1 & (1<<5)) !== 0 ? 1 : 0,

        // Flags2: charging & sentry
        charging_cable: (flags2 & (1<<0)) !== 0 ? 1 : 0,
        charging: (flags2 & (1<<1)) !== 0 ? 1 : 0,
        charging_port: (flags2 & (1<<2)) !== 0 ? 1 : 0,
        sentry_mode: (flags2 & (1<<3)) !== 0 ? 1 : 0,
        sentry_alert: (flags2 & (1<<4)) !== 0 ? 1 : 0,

        // Flags3: safety 1
        blindspot_left: (flags3 & (1<<0)) !== 0 ? 1 : 0,
        blindspot_right: (flags3 & (1<<1)) !== 0 ? 1 : 0,
        blindspot_left_alert: (flags3 & (1<<2)) !== 0 ? 1 : 0,
        blindspot_right_alert: (flags3 & (1<<3)) !== 0 ? 1 : 0,
        side_collision_left: (flags3 & (1<<4)) !== 0 ? 1 : 0,
        side_collision_right: (flags3 & (1<<5)) !== 0 ? 1 : 0,
        forward_collision: (flags3 & (1<<6)) !== 0 ? 1 : 0,
        night_mode: (flags3 & (1<<7)) !== 0 ? 1 : 0,

        // Flags4: safety 2 & autopilot alerts
        lane_departure_left_lv1: (flags4 & (1<<0)) !== 0 ? 1 : 0,
        lane_departure_left_lv2: (flags4 & (1<<1)) !== 0 ? 1 : 0,
        lane_departure_right_lv1: (flags4 & (1<<2)) !== 0 ? 1 : 0,
        lane_departure_right_lv2: (flags4 & (1<<3)) !== 0 ? 1 : 0,
        autopilot_alert_lv1: (flags4 & (1<<4)) !== 0 ? 1 : 0,
        autopilot_alert_lv2: (flags4 & (1<<5)) !== 0 ? 1 : 0,
        autopilot_alert_lv3: (flags4 & (1<<6)) !== 0 ? 1 : 0,

        // Meta
        last_update_ms
    };

    return state;
}

/**
 * Update dashboard UI with vehicle state
 */
function updateDashboard(state) {
    // Speed
    document.getElementById('speed-value').textContent = Math.round(state.speed_kph);

    // Battery
    document.getElementById('battery-value').textContent = Math.round(state.soc_percent);
    document.getElementById('battery-voltage').textContent = state.battery_voltage_HV.toFixed(1) + 'V';

    // Gear
    const gearMap = { 0: 'P', 1: 'D', 2: 'R', 3: 'N' };
    document.getElementById('gear-display').textContent = gearMap[state.gear] || 'P';

    // Odometer
    document.getElementById('odometer-value').textContent = Math.round(state.odometer_km) + ' km';

    // Charging
    const chargingRow = document.getElementById('charging-row');
    if (state.charging) {
        chargingRow.style.display = 'flex';
        document.getElementById('charge-power').textContent = state.charge_power_kw.toFixed(1) + ' kW';
    } else {
        chargingRow.style.display = 'none';
    }

    // Doors
    updateIndicator('ind-door-fl', state.door_front_left_open);
    updateIndicator('ind-door-fr', state.door_front_right_open);
    updateIndicator('ind-door-rl', state.door_rear_left_open);
    updateIndicator('ind-door-rr', state.door_rear_right_open);
    updateIndicator('ind-frunk', state.frunk_open);
    updateIndicator('ind-trunk', state.trunk_open);

    // Lights
    updateIndicator('ind-headlights', state.headlights);
    updateIndicator('ind-high-beams', state.high_beams);
    updateIndicator('ind-fog', state.fog_lights);
    updateIndicator('ind-turn-left', state.turn_left, state.turn_left ? 'warning' : null);
    updateIndicator('ind-turn-right', state.turn_right, state.turn_right ? 'warning' : null);
    updateIndicator('ind-hazard', state.hazard, state.hazard ? 'alert' : null);

    // Security
    updateIndicator('ind-locked', state.locked);
    updateIndicator('ind-sentry', state.sentry_mode);
    updateIndicator('ind-autopilot', state.autopilot);

    // Safety alerts
    const blindspotLeft = state.blindspot_left || state.blindspot_left_alert;
    const blindspotRight = state.blindspot_right || state.blindspot_right_alert;
    updateIndicator('ind-blindspot-left', blindspotLeft, blindspotLeft ? 'warning' : null);
    updateIndicator('ind-blindspot-right', blindspotRight, blindspotRight ? 'warning' : null);
    updateIndicator('ind-forward-collision', state.forward_collision, state.forward_collision ? 'alert' : null);
}

/**
 * Update single indicator state
 */
function updateIndicator(elementId, active, className = null) {
    const el = document.getElementById(elementId);
    if (!el) return;

    el.classList.remove('active', 'warning', 'alert');

    if (active) {
        if (className) {
            el.classList.add(className);
        } else {
            el.classList.add('active');
        }
    }
}

/**
 * Update connection status in UI
 */
function updateConnectionStatus(connected) {
    isConnected = connected;
    const statusDot = document.getElementById('status-dot');
    const statusText = document.getElementById('status-text');

    if (connected) {
        statusDot.classList.add('connected');
        statusText.textContent = 'Connecté';
    } else {
        statusDot.classList.remove('connected');
        statusText.textContent = 'Déconnecté';
    }
}

/**
 * Connect to BLE device
 */
async function connectBLE() {
    try {
        console.log('[Dashboard] Attempting BLE connection...');

        // Try to get last connected device first
        const lastDeviceName = localStorage.getItem('last-ble-device-name');

        if (lastDeviceName && usingCapacitor && navigator.bluetooth.scanForDeviceByName) {
            console.log('[Dashboard] Trying to reconnect to:', lastDeviceName);
            try {
                bleDevice = await navigator.bluetooth.scanForDeviceByName(lastDeviceName, 5000);
            } catch (scanError) {
                console.log('[Dashboard] Auto-reconnect scan failed:', scanError);
                bleDevice = null;
            }
        }

        // If not found, request device (show picker)
        if (!bleDevice) {
            console.log('[Dashboard] Requesting new device...');
            bleDevice = await navigator.bluetooth.requestDevice({
                filters: [{ namePrefix: DEVICE_NAME_PREFIX }],
                optionalServices: [BLE_SERVICE_UUID]
            });
        }

        console.log('[Dashboard] Device selected:', bleDevice.name);

        // Connect
        const server = await bleDevice.gatt.connect();
        console.log('[Dashboard] Connected to GATT server');

        // Get service
        const service = await server.getPrimaryService(BLE_SERVICE_UUID);
        console.log('[Dashboard] Got service');

        // Get vehicle state characteristic
        bleCharacteristicVehicleState = await service.getCharacteristic(BLE_CHARACTERISTIC_VEHICLE_STATE_UUID);
        console.log('[Dashboard] Got vehicle state characteristic');

        // Start notifications
        await bleCharacteristicVehicleState.startNotifications();
        bleCharacteristicVehicleState.addEventListener('characteristicvaluechanged', handleVehicleStateNotification);
        console.log('[Dashboard] Notifications started');

        // Save device
        if (bleDevice.name) {
            localStorage.setItem('last-ble-device-name', bleDevice.name);
        }

        updateConnectionStatus(true);

        // Listen for disconnection
        bleDevice.addEventListener('gattserverdisconnected', onDisconnected);

    } catch (error) {
        console.error('[Dashboard] BLE connection error:', error);
        updateConnectionStatus(false);

        // Don't retry if user cancelled the device selection
        if (error.name === 'NotFoundError' || error.message.includes('User cancelled')) {
            console.log('[Dashboard] User cancelled device selection, not retrying');
            return;
        }

        // Retry after 3 seconds for other errors
        setTimeout(connectBLE, 3000);
    }
}

/**
 * Handle vehicle state notification (may be chunked)
 */
function handleVehicleStateNotification(event) {
    const dataView = event.target.value;
    const chunkSize = dataView.byteLength;

    console.log('[Dashboard] Received chunk, size:', chunkSize, 'offset:', vehicleStateBufferOffset);

    // If this chunk would overflow the buffer, assume it's a new message
    // (this handles the case where messages arrive faster than we process them)
    if (vehicleStateBufferOffset + chunkSize > VEHICLE_STATE_SIZE) {
        console.log('[Dashboard] Buffer overflow detected, resetting (offset:', vehicleStateBufferOffset, 'chunk:', chunkSize, ')');
        vehicleStateBufferOffset = 0;
    }

    // Copy chunk to buffer
    const chunk = new Uint8Array(dataView.buffer);
    vehicleStateBuffer.set(chunk, vehicleStateBufferOffset);
    vehicleStateBufferOffset += chunkSize;

    // Check if we have received the complete structure
    if (vehicleStateBufferOffset >= VEHICLE_STATE_SIZE) {
        try {
            const completeDataView = new DataView(vehicleStateBuffer.buffer, 0, VEHICLE_STATE_SIZE);
            const state = decodeVehicleState(completeDataView);
            console.log('[Dashboard] Decoded complete state:', state);
            updateDashboard(state);

            // Reset buffer for next message
            vehicleStateBufferOffset = 0;
        } catch (error) {
            console.error('[Dashboard] Failed to decode vehicle state:', error);
            vehicleStateBufferOffset = 0; // Reset on error
        }
    }
}

/**
 * Handle BLE disconnection
 */
function onDisconnected() {
    console.log('[Dashboard] BLE disconnected');
    updateConnectionStatus(false);
    bleDevice = null;
    bleCharacteristicVehicleState = null;

    // Auto-reconnect after 2 seconds
    setTimeout(connectBLE, 2000);
}

/**
 * Switch to config mode
 */
function switchToConfig() {
    // Disconnect BLE
    if (bleDevice && bleDevice.gatt.connected) {
        bleDevice.gatt.disconnect();
    }

    // Mark that we're coming from dashboard to prevent redirect loop
    sessionStorage.setItem('from-dashboard', 'true');

    // Navigate to config page
    window.location.href = usingCapacitor ? '/index.html' : '../index.html';
}

/**
 * Initialize dashboard
 */
async function init() {
    console.log('[Dashboard] Initializing...');
    console.log('[Dashboard] Platform:', usingCapacitor ? 'Capacitor' : 'Web');

    // Config button
    document.getElementById('config-btn').addEventListener('click', switchToConfig);

    // Connect to BLE
    await connectBLE();
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
