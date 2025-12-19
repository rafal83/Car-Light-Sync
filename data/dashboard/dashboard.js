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

// Dashboard mode state
let currentMode = 'park'; // 'park' or 'drive'
let lastGear = 1; // Track last gear to detect changes
const gearMap = { 1: 'P', 2: 'R', 3: 'N', 4: 'D' };

// Language state
let currentLang = 'fr';

// BLE packet sizes for different modes
const VEHICLE_STATE_DRIVE_SIZE = 24; // vehicle_state_ble_drive_t
const VEHICLE_STATE_PARK_SIZE = 22;  // vehicle_state_ble_park_t
const VEHICLE_STATE_SIZE = 28;       // vehicle_state_ble_t (deprecated, for compatibility)

// Buffer for reassembling chunked BLE notifications
const MAX_BLE_SIZE = Math.max(VEHICLE_STATE_DRIVE_SIZE, VEHICLE_STATE_PARK_SIZE, VEHICLE_STATE_SIZE);
let vehicleStateBuffer = new Uint8Array(MAX_BLE_SIZE);
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
 * Decode BLE DRIVE mode packet (vehicle_state_ble_drive_t)
 * Structure from include/vehicle_can_unified.h
 * Total: ~24 bytes
 */
function decodeVehicleStateDrive(dataView) {
    let offset = 0;

    // Helper functions
    const readInt16 = () => { const val = dataView.getInt16(offset, true); offset += 2; return val; };
    const readUint8 = () => { const val = dataView.getUint8(offset); offset += 1; return val; };
    const readInt8 = () => { const val = dataView.getInt8(offset); offset += 1; return val; };
    const readUint32 = () => { const val = dataView.getUint32(offset, true); offset += 4; return val; };

    // Dynamique de conduite (int16 pour supporter régénération négative)
    const speed_kph = readInt16() / 10.0;
    const rear_power_kw = readInt16() / 10.0;  // Peut être négatif (régén)
    const front_power_kw = readInt16() / 10.0; // Peut être négatif (régén)
    const soc_percent = readInt16() / 10.0;
    const odometer_km = readUint32();

    // Valeurs uint8
    const gear = readInt8();
    const pedal_map = readInt8();
    const accel_pedal_pos = readUint8();
    const brightness = readUint8() / 100.0;
    const autopilot = readUint8();

    // Bit-packed flags
    const flags0 = readUint8();
    const flags1 = readUint8();
    const flags2 = readUint8();
    const last_update_ms = readUint32();

    const state = {
        // Dynamique
        speed_kph,
        rear_power_kw,
        front_power_kw,
        soc_percent,
        odometer_km,
        gear,
        pedal_map,
        accel_pedal_pos,
        brightness,
        autopilot,

        // Flags0: turn signals & brake
        turn_left: (flags0 & (1<<0)) !== 0,
        turn_right: (flags0 & (1<<1)) !== 0,
        hazard: (flags0 & (1<<2)) !== 0,
        brake_pressed: (flags0 & (1<<3)) !== 0,
        high_beams: (flags0 & (1<<4)) !== 0,
        headlights: (flags0 & (1<<5)) !== 0,
        fog_lights: (flags0 & (1<<6)) !== 0,

        // Flags1: blindspots & collisions
        blindspot_left: (flags1 & (1<<0)) !== 0,
        blindspot_right: (flags1 & (1<<1)) !== 0,
        blindspot_left_alert: (flags1 & (1<<2)) !== 0,
        blindspot_right_alert: (flags1 & (1<<3)) !== 0,
        side_collision_left: (flags1 & (1<<4)) !== 0,
        side_collision_right: (flags1 & (1<<5)) !== 0,
        forward_collision: (flags1 & (1<<6)) !== 0,
        night_mode: (flags1 & (1<<7)) !== 0,

        // Flags2: autopilot alerts & lane departure
        lane_departure_left_lv1: (flags2 & (1<<0)) !== 0,
        lane_departure_left_lv2: (flags2 & (1<<1)) !== 0,
        lane_departure_right_lv1: (flags2 & (1<<2)) !== 0,
        lane_departure_right_lv2: (flags2 & (1<<3)) !== 0,
        autopilot_alert_lv1: (flags2 & (1<<4)) !== 0,
        autopilot_alert_lv2: (flags2 & (1<<5)) !== 0,
        autopilot_alert_lv3: (flags2 & (1<<6)) !== 0,

        // Missing fields from Park mode (defaults)
        charge_power_kw: 0,
        battery_voltage_LV: 0,
        battery_voltage_HV: 0,
        charge_status: 0,
        doors_open_count: 0,
        locked: false,
        door_front_left_open: false,
        door_rear_left_open: false,
        door_front_right_open: false,
        door_rear_right_open: false,
        frunk_open: false,
        trunk_open: false,
        charging_cable: false,
        charging: false,
        charging_port: false,
        sentry_mode: false,
        sentry_alert: false,

        last_update_ms
    };

    return state;
}

/**
 * Decode BLE PARK mode packet (vehicle_state_ble_park_t)
 * Structure from include/vehicle_can_unified.h
 * Total: ~24 bytes
 */
function decodeVehicleStatePark(dataView) {
    let offset = 0;

    // Helper functions
    const readInt16 = () => { const val = dataView.getInt16(offset, true); offset += 2; return val; };
    const readUint8 = () => { const val = dataView.getUint8(offset); offset += 1; return val; };
    const readUint32 = () => { const val = dataView.getUint32(offset, true); offset += 4; return val; };

    // Energie
    const soc_percent = readInt16() / 10.0;
    const charge_power_kw = readInt16() / 10.0;
    const battery_voltage_LV = readInt16() / 10.0;
    const battery_voltage_HV = readInt16() / 10.0;
    const odometer_km = readUint32();

    // Valeurs uint8
    const charge_status = readUint8();
    const brightness = readUint8() / 100.0;
    const doors_open_count = readUint8();

    // Bit-packed flags
    const flags0 = readUint8();
    const flags1 = readUint8();
    const flags2 = readUint8();
    const last_update_ms = readUint32();

    const state = {
        // Energie
        soc_percent,
        charge_power_kw,
        battery_voltage_LV,
        battery_voltage_HV,
        odometer_km,
        charge_status,
        brightness,
        doors_open_count,

        // Flags0: doors & locks
        locked: (flags0 & (1<<0)) !== 0,
        door_front_left_open: (flags0 & (1<<1)) !== 0,
        door_rear_left_open: (flags0 & (1<<2)) !== 0,
        door_front_right_open: (flags0 & (1<<3)) !== 0,
        door_rear_right_open: (flags0 & (1<<4)) !== 0,
        frunk_open: (flags0 & (1<<5)) !== 0,
        trunk_open: (flags0 & (1<<6)) !== 0,
        brake_pressed: (flags0 & (1<<7)) !== 0,

        // Flags1: lights
        turn_left: (flags1 & (1<<0)) !== 0,
        turn_right: (flags1 & (1<<1)) !== 0,
        hazard: (flags1 & (1<<2)) !== 0,
        headlights: (flags1 & (1<<3)) !== 0,
        high_beams: (flags1 & (1<<4)) !== 0,
        fog_lights: (flags1 & (1<<5)) !== 0,

        // Flags2: charging & sentry
        charging_cable: (flags2 & (1<<0)) !== 0,
        charging: (flags2 & (1<<1)) !== 0,
        charging_port: (flags2 & (1<<2)) !== 0,
        sentry_mode: (flags2 & (1<<3)) !== 0,
        sentry_alert: (flags2 & (1<<4)) !== 0,
        night_mode: (flags2 & (1<<5)) !== 0,

        // Missing fields from Drive mode (defaults)
        speed_kph: 0,
        rear_power_kw: 0,
        front_power_kw: 0,
        gear: 1, // P
        pedal_map: 0,
        accel_pedal_pos: 0,
        autopilot: 0,
        blindspot_left: false,
        blindspot_right: false,
        blindspot_left_alert: false,
        blindspot_right_alert: false,
        side_collision_left: false,
        side_collision_right: false,
        forward_collision: false,
        lane_departure_left_lv1: false,
        lane_departure_left_lv2: false,
        lane_departure_right_lv1: false,
        lane_departure_right_lv2: false,
        autopilot_alert_lv1: false,
        autopilot_alert_lv2: false,
        autopilot_alert_lv3: false,

        last_update_ms
    };

    return state;
}

/**
 * Switch dashboard mode based on gear
 */
function switchMode(gear) {
    const newMode = (gear === 2 || gear === 3 || gear === 4) ? 'drive' : 'park'; // D, R, N = drive mode

    if (newMode !== currentMode) {
        currentMode = newMode;
        console.log('[Dashboard] Switching to mode:', currentMode, '(gear:', gearMap[gear] || 'P', ')');

        // Hide all modes
        document.getElementById('park-mode').classList.remove('active');
        document.getElementById('drive-mode').classList.remove('active');

        // Hide/show header based on mode
        const header = document.getElementById('dashboard-header');
        if (header) {
            if (currentMode === 'drive') {
                header.classList.add('hidden');
            } else {
                header.classList.remove('hidden');
            }
        }

        // Show current mode
        if (currentMode === 'park') {
            document.getElementById('park-mode').classList.add('active');
        } else {
            document.getElementById('drive-mode').classList.add('active');
        }
    }
}

/**
 * Update dashboard UI with vehicle state
 */
function updateDashboard(state) {
    const gearText = gearMap[state.gear] || 'P';

    // Switch mode if gear changed
    if (state.gear !== lastGear) {
        switchMode(state.gear);
        lastGear = state.gear;
    }

    // Update PARK MODE elements
    if (currentMode === 'park') {
        // Battery
        const batteryPercent = Math.round(state.soc_percent);
        document.getElementById('battery-value-park').textContent = batteryPercent;
        document.getElementById('battery-voltage-park').textContent = state.battery_voltage_HV.toFixed(1) + 'V';

        // Update battery progress bar
        const batteryFill = document.getElementById('battery-fill-park');
        if (batteryFill) {
            // Calculate width (max 74px as per SVG)
            const maxWidth = 74;
            const fillWidth = (batteryPercent / 100) * maxWidth;
            batteryFill.setAttribute('width', fillWidth);

            // Update color based on battery level
            batteryFill.classList.remove('low', 'medium', 'high', 'charging');
            if (state.charging) {
                batteryFill.classList.add('charging');
            }
            if (batteryPercent <= 20) {
                batteryFill.classList.add('low');
            } else if (batteryPercent <= 50) {
                batteryFill.classList.add('medium');
            } else {
                batteryFill.classList.add('high');
            }
        }

        // Charging gauge
        const chargingGauge = document.getElementById('charging-gauge-park');
        if (state.charging) {
            chargingGauge.style.display = 'flex';
            document.getElementById('charge-power-park').textContent = state.charge_power_kw.toFixed(1);
        } else {
            chargingGauge.style.display = 'none';
        }

        // Vehicle info
        document.getElementById('odometer-value-park').textContent = Math.round(state.odometer_km) + ' km';
        document.getElementById('battery-lv-park').textContent = state.battery_voltage_LV.toFixed(1) + 'V';
        document.getElementById('gear-display-park').textContent = gearText;

        // Update vehicle top view visualization
        updateVehicleView(state);
    }

    // Update DRIVE MODE elements
    else {
        // Speed (large display)
        document.getElementById('speed-value-drive').textContent = Math.abs(Math.round(state.speed_kph));

        // Gear display
        document.getElementById('gear-display-drive').textContent = gearText;

        // Pedal arc (0-100% maps to stroke-dashoffset 377 to 0) - 240° arc
        const pedalArcFill = document.getElementById('pedal-arc-fill');
        if (pedalArcFill) {
            const dashOffset = 341 - (341 * state.accel_pedal_pos / 100);
            pedalArcFill.style.strokeDashoffset = dashOffset;
        }

        // Compact info below speedometer
        document.getElementById('battery-drive-compact').textContent = Math.round(state.soc_percent) + '%';
        document.getElementById('odometer-drive-compact').textContent = Math.round(state.odometer_km) + 'km';

        // Power & pedal map indicators
        updatePowerIndicators(state.rear_power_kw, state.front_power_kw, state.pedal_map);

        // Show/hide indicators (only when active)
        updateDriveIndicator('ind-turn-left-drive', state.turn_left);
        updateDriveIndicator('ind-turn-right-drive', state.turn_right);

        // Blindspot arcs with progressive intensity (level 1 and level 2)
        updateBlindspotArcs('blindspot-arcs-left', state.blindspot_left, state.blindspot_left_alert);
        updateBlindspotArcs('blindspot-arcs-right', state.blindspot_right, state.blindspot_right_alert);

        updateDriveIndicator('ind-forward-collision-drive', state.forward_collision);
        updateDriveIndicator('ind-brake-drive', state.brake_pressed);
        updateDriveIndicator('ind-autopilot-drive', state.autopilot);
    }
}

/**
 * Update power indicators and pedal map
 * @param {number} rearPower - Rear motor power in kW (can be negative for regen)
 * @param {number} frontPower - Front motor power in kW (can be negative for regen)
 * @param {number} pedalMap - Pedal map mode (-1=Chill, 0=Standard, 1=Sport)
 */
function updatePowerIndicators(rearPower, frontPower, pedalMap) {
    const maxPower = 200; // Maximum power in kW for normalization

    // Update rear power arc (left side)
    // Two arcs: positive (upwards) and negative (downwards)
    const rearArcPos = document.getElementById('power-arc-rear-pos');
    const rearArcNeg = document.getElementById('power-arc-rear-neg');
    const rearValue = document.getElementById('power-rear-value');
    if (rearArcPos && rearArcNeg && rearValue) {
        const rearRounded = Math.round(rearPower || 0);
        rearValue.textContent = rearRounded;

        // Half arc length: 79.5 (55° in units)
        const halfArc = 79.5;
        const rearPercent = Math.abs(rearPower) / maxPower;
        const rearClamped = Math.max(0, Math.min(1, rearPercent));
        const rearDashOffset = halfArc - (halfArc * rearClamped);

        if (rearPower >= 0) {
            // Positive power: show positive arc, hide negative arc
            rearArcPos.style.display = 'block';
            rearArcNeg.style.display = 'none';
            rearArcPos.style.strokeDasharray = `${halfArc} ${159}`;
            rearArcPos.style.strokeDashoffset = rearDashOffset;

            if (Math.abs(rearRounded) > 5) {
                rearArcPos.classList.add('active');
            } else {
                rearArcPos.classList.remove('active');
            }
        } else {
            // Negative power (regen): show negative arc, hide positive arc
            rearArcPos.style.display = 'none';
            rearArcNeg.style.display = 'block';
            rearArcNeg.style.strokeDasharray = `${halfArc} ${159}`;
            rearArcNeg.style.strokeDashoffset = rearDashOffset;

            if (Math.abs(rearRounded) > 5) {
                rearArcNeg.classList.add('active');
            } else {
                rearArcNeg.classList.remove('active');
            }
        }

        // Update text color
        rearValue.classList.remove('positive', 'negative', 'zero');
        if (rearRounded > 5) {
            rearValue.classList.add('positive');
        } else if (rearRounded < -5) {
            rearValue.classList.add('negative');
        } else {
            rearValue.classList.add('zero');
        }
    }

    // Update front power arc (right side)
    // Two arcs: positive (upwards) and negative (downwards)
    const frontArcPos = document.getElementById('power-arc-front-pos');
    const frontArcNeg = document.getElementById('power-arc-front-neg');
    const frontValue = document.getElementById('power-front-value');
    if (frontArcPos && frontArcNeg && frontValue) {
        const frontRounded = Math.round(frontPower || 0);
        frontValue.textContent = frontRounded;

        // Half arc length: 79.5 (55° in units)
        const halfArc = 79.5;
        const frontPercent = Math.abs(frontPower) / maxPower;
        const frontClamped = Math.max(0, Math.min(1, frontPercent));
        const frontDashOffset = halfArc - (halfArc * frontClamped);

        if (frontPower >= 0) {
            // Positive power: show positive arc, hide negative arc
            frontArcPos.style.display = 'block';
            frontArcNeg.style.display = 'none';
            frontArcPos.style.strokeDasharray = `${halfArc} ${159}`;
            frontArcPos.style.strokeDashoffset = frontDashOffset;

            if (Math.abs(frontRounded) > 5) {
                frontArcPos.classList.add('active');
            } else {
                frontArcPos.classList.remove('active');
            }
        } else {
            // Negative power (regen): show negative arc, hide positive arc
            frontArcPos.style.display = 'none';
            frontArcNeg.style.display = 'block';
            frontArcNeg.style.strokeDasharray = `${halfArc} ${159}`;
            frontArcNeg.style.strokeDashoffset = frontDashOffset;

            if (Math.abs(frontRounded) > 5) {
                frontArcNeg.classList.add('active');
            } else {
                frontArcNeg.classList.remove('active');
            }
        }

        // Update text color
        frontValue.classList.remove('positive', 'negative', 'zero');
        if (frontRounded > 5) {
            frontValue.classList.add('positive');
        } else if (frontRounded < -5) {
            frontValue.classList.add('negative');
        } else {
            frontValue.classList.add('zero');
        }
    }

    // Update pedal map label
    const pedalMapLabel = document.querySelector('.pedal-map-label');
    if (pedalMapLabel) {
        const pedalMapText = pedalMap === -1 ? 'Chill' :
                            pedalMap === 1 ? 'Sport' :
                            'Standard';
        pedalMapLabel.textContent = pedalMapText;
    }
}

/**
 * Update vehicle top view visualization (Park mode only)
 */
function updateVehicleView(state) {
    // Doors
    const doorFL = document.querySelector('.door-fl');
    const doorFR = document.querySelector('.door-fr');
    const doorRL = document.querySelector('.door-rl');
    const doorRR = document.querySelector('.door-rr');
    const frunkDoor = document.querySelector('.frunk-door');
    const trunkDoor = document.querySelector('.trunk-door');

    if (doorFL) doorFL.classList.toggle('open', state.door_front_left_open);
    if (doorFR) doorFR.classList.toggle('open', state.door_front_right_open);
    if (doorRL) doorRL.classList.toggle('open', state.door_rear_left_open);
    if (doorRR) doorRR.classList.toggle('open', state.door_rear_right_open);
    if (frunkDoor) frunkDoor.classList.toggle('open', state.frunk_open);
    if (trunkDoor) trunkDoor.classList.toggle('open', state.trunk_open);

    // Headlights
    const headlightLeft = document.querySelector('.headlight-left');
    const headlightRight = document.querySelector('.headlight-right');
    if (headlightLeft && headlightRight) {
        headlightLeft.classList.toggle('active', state.headlights);
        headlightRight.classList.toggle('active', state.headlights);
        headlightLeft.classList.toggle('high-beam', state.high_beams);
        headlightRight.classList.toggle('high-beam', state.high_beams);
        headlightLeft.classList.toggle('fog', state.fog_lights);
        headlightRight.classList.toggle('fog', state.fog_lights);
    }

    // Tail lights (brake)
    const taillightLeft = document.querySelector('.taillight-left');
    const taillightRight = document.querySelector('.taillight-right');
    if (taillightLeft && taillightRight) {
        taillightLeft.classList.toggle('active', state.brake_pressed);
        taillightRight.classList.toggle('active', state.brake_pressed);
    }

    // Turn signals
    const turnSignalFL = document.querySelector('.turn-signal-fl');
    const turnSignalFR = document.querySelector('.turn-signal-fr');
    const turnSignalRL = document.querySelector('.turn-signal-rl');
    const turnSignalRR = document.querySelector('.turn-signal-rr');

    if (state.hazard) {
        // Hazard lights - all turn signals blink
        if (turnSignalFL) turnSignalFL.classList.add('hazard');
        if (turnSignalFR) turnSignalFR.classList.add('hazard');
        if (turnSignalRL) turnSignalRL.classList.add('hazard');
        if (turnSignalRR) turnSignalRR.classList.add('hazard');
    } else {
        // Individual turn signals
        if (turnSignalFL) {
            turnSignalFL.classList.remove('hazard');
            turnSignalFL.classList.toggle('active', state.turn_left);
        }
        if (turnSignalFR) {
            turnSignalFR.classList.remove('hazard');
            turnSignalFR.classList.toggle('active', state.turn_right);
        }
        if (turnSignalRL) {
            turnSignalRL.classList.remove('hazard');
            turnSignalRL.classList.toggle('active', state.turn_left);
        }
        if (turnSignalRR) {
            turnSignalRR.classList.remove('hazard');
            turnSignalRR.classList.toggle('active', state.turn_right);
        }
    }

    // Charging port
    const chargePort = document.querySelector('.charge-port');
    const chargeIcon = document.querySelector('.charge-icon');
    if (chargePort && chargeIcon) {
        const isCharging = state.charging || state.charging_port;
        chargePort.classList.toggle('active', isCharging);
        chargeIcon.classList.toggle('active', isCharging);
    }

    // Lock indicator
    const lockBg = document.querySelector('.lock-bg');
    const lockIcon = document.querySelector('.lock-icon');
    if (lockBg && lockIcon) {
        lockBg.classList.toggle('locked', state.locked);
        lockBg.classList.toggle('unlocked', !state.locked);
        lockIcon.classList.toggle('locked', state.locked);
        lockIcon.classList.toggle('unlocked', !state.locked);
    }

    // Sentry mode
    const sentryBg = document.querySelector('.sentry-bg');
    const sentryEye = document.querySelector('.sentry-eye');
    const sentryPupil = document.querySelector('.sentry-pupil');
    if (sentryBg && sentryEye && sentryPupil) {
        const sentryActive = state.sentry_mode || state.sentry_alert;
        sentryBg.classList.toggle('active', sentryActive);
        sentryEye.classList.toggle('active', sentryActive);
        sentryPupil.classList.toggle('active', sentryActive);
    }

    // Night mode
    const nightModeBg = document.querySelector('.night-mode-bg');
    const nightModeMoon = document.querySelector('.night-mode-moon');
    if (nightModeBg && nightModeMoon) {
        nightModeBg.classList.toggle('active', state.night_mode);
        nightModeMoon.classList.toggle('active', state.night_mode);
    }
}

/**
 * Update single indicator state (for park mode)
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
 * Update drive mode indicator (show/hide based on state)
 */
function updateDriveIndicator(elementId, active) {
    const el = document.getElementById(elementId);
    if (!el) return;

    if (active) {
        el.style.display = 'flex';
    } else {
        el.style.display = 'none';
    }
}

/**
 * Update blindspot arcs with progressive intensity based on level
 * @param {string} elementId - ID of the SVG container
 * @param {boolean} level1 - Level 1 detection (blindspot_left/right)
 * @param {boolean} level2 - Level 2 alert (blindspot_left_alert/right_alert)
 */
function updateBlindspotArcs(elementId, level1, level2) {
    const el = document.getElementById(elementId);
    if (!el) return;

    const arcs = el.querySelectorAll('path');
    if (arcs.length !== 3) return;

    // Level 2 (alert): Show all 3 arcs with full WiFi-wave effect in RED
    if (level2) {
        el.style.display = 'block';
        arcs[0].style.display = 'block';
        arcs[0].setAttribute('stroke', '#fc8181'); // Red
        arcs[1].style.display = 'block';
        arcs[1].setAttribute('stroke', '#fc8181'); // Red
        arcs[2].style.display = 'block';
        arcs[2].setAttribute('stroke', '#fc8181'); // Red
    }
    // Level 1 (detection): Show all 3 arcs in ORANGE
    else if (level1) {
        el.style.display = 'block';
        arcs[0].style.display = 'block';
        arcs[0].setAttribute('stroke', '#ed8936'); // Orange
        arcs[1].style.display = 'block';
        arcs[1].setAttribute('stroke', '#ed8936'); // Orange
        arcs[2].style.display = 'block';
        arcs[2].setAttribute('stroke', '#ed8936'); // Orange
    }
    // No detection: Hide all
    else {
        el.style.display = 'none';
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
        statusText.textContent = t('dashboard.connected');
    } else {
        statusDot.classList.remove('connected');
        statusText.textContent = t('dashboard.disconnected');
    }
}

/**
 * Show connection progress indicator
 */
function showConnectionProgress(text, percent) {
    const progressEl = document.getElementById('connection-progress');
    const fillEl = document.getElementById('progress-fill');
    const textEl = document.getElementById('progress-text');

    if (progressEl && fillEl && textEl) {
        progressEl.style.display = 'flex';
        fillEl.style.width = percent + '%';
        textEl.textContent = text;
    }
}

/**
 * Hide connection progress indicator
 */
function hideConnectionProgress() {
    const progressEl = document.getElementById('connection-progress');
    if (progressEl) {
        progressEl.style.display = 'none';
    }
}

/**
 * Connect to BLE device
 */
async function connectBLE() {
    try {
        console.log('[Dashboard] Attempting BLE connection...');

        // Show progress: Scanning
        showConnectionProgress(t('dashboard.scanning') || 'Scanning for device...', 0);

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

        // Show progress: Connecting
        showConnectionProgress(t('dashboard.connecting') || 'Connecting...', 25);

        // Connect
        const server = await bleDevice.gatt.connect();
        console.log('[Dashboard] Connected to GATT server');

        // Show progress: Getting service
        showConnectionProgress(t('dashboard.gettingService') || 'Getting BLE service...', 50);

        // Get service
        const service = await server.getPrimaryService(BLE_SERVICE_UUID);
        console.log('[Dashboard] Got service');

        // Get vehicle state characteristic
        bleCharacteristicVehicleState = await service.getCharacteristic(BLE_CHARACTERISTIC_VEHICLE_STATE_UUID);
        console.log('[Dashboard] Got vehicle state characteristic');

        // Show progress: Starting notifications
        showConnectionProgress(t('dashboard.startingNotifications') || 'Starting notifications...', 75);

        // Start notifications
        // Remove existing listener first to avoid duplicates
        bleCharacteristicVehicleState.removeEventListener('characteristicvaluechanged', handleVehicleStateNotification);
        await bleCharacteristicVehicleState.startNotifications();
        bleCharacteristicVehicleState.addEventListener('characteristicvaluechanged', handleVehicleStateNotification);
        console.log('[Dashboard] Notifications started');

        // Save device
        if (bleDevice.name) {
            localStorage.setItem('last-ble-device-name', bleDevice.name);
        }

        // Show progress: Connected
        showConnectionProgress(t('dashboard.connected') || 'Connected!', 100);

        updateConnectionStatus(true);

        // Listen for disconnection (remove old listener first to avoid duplicates)
        bleDevice.removeEventListener('gattserverdisconnected', onDisconnected);
        bleDevice.addEventListener('gattserverdisconnected', onDisconnected);

        // Hide progress after a short delay
        setTimeout(() => {
            hideConnectionProgress();
        }, 1000);

    } catch (error) {
        console.error('[Dashboard] BLE connection error:', error);
        hideConnectionProgress();
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
 * Auto-detects packet format based on size
 */
function handleVehicleStateNotification(event) {
    const dataView = event.target.value;
    const chunkSize = dataView.byteLength;

    // console.log('[Dashboard] Received chunk, size:', chunkSize, 'offset:', vehicleStateBufferOffset);

    // If this chunk would overflow the buffer, assume it's a new message
    if (vehicleStateBufferOffset + chunkSize > MAX_BLE_SIZE) {
        console.log('[Dashboard] Buffer overflow detected, resetting (offset:', vehicleStateBufferOffset, 'chunk:', chunkSize, ')');
        vehicleStateBufferOffset = 0;
    }

    // Copy chunk to buffer
    const chunk = new Uint8Array(dataView.buffer);
    vehicleStateBuffer.set(chunk, vehicleStateBufferOffset);
    vehicleStateBufferOffset += chunkSize;

    // Auto-detect packet type based on accumulated size
    let expectedSize = 0;
    let packetType = null;

    if (vehicleStateBufferOffset >= VEHICLE_STATE_PARK_SIZE) {
        // Try to detect which packet type we have
        if (vehicleStateBufferOffset === VEHICLE_STATE_PARK_SIZE ||
            (vehicleStateBufferOffset < VEHICLE_STATE_DRIVE_SIZE && chunkSize === VEHICLE_STATE_PARK_SIZE)) {
            expectedSize = VEHICLE_STATE_PARK_SIZE;
            packetType = 'park';
        } else if (vehicleStateBufferOffset >= VEHICLE_STATE_DRIVE_SIZE) {
            expectedSize = VEHICLE_STATE_DRIVE_SIZE;
            packetType = 'drive';
        }
    }

    // Check if we have received a complete packet
    if (packetType && vehicleStateBufferOffset >= expectedSize) {
        try {
            const completeDataView = new DataView(vehicleStateBuffer.buffer, 0, expectedSize);
            let state;

            // Decode based on packet type
            if (packetType === 'drive') {
                state = decodeVehicleStateDrive(completeDataView);
                // console.log('[Dashboard] Decoded DRIVE mode state:', state);
            } else if (packetType === 'park') {
                state = decodeVehicleStatePark(completeDataView);
                // console.log('[Dashboard] Decoded PARK mode state:', state);
            }

            if (state) {
                updateDashboard(state);
            }

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
async function switchToConfig() {
    console.log('[Dashboard] Switching to config mode...');

    // Disconnect BLE and stop notifications
    try {
        if (bleCharacteristicVehicleState) {
            console.log('[Dashboard] Stopping notifications...');
            await bleCharacteristicVehicleState.stopNotifications();
            bleCharacteristicVehicleState.removeEventListener('characteristicvaluechanged', handleVehicleStateNotification);
        }

        if (bleDevice && bleDevice.gatt && bleDevice.gatt.connected) {
            console.log('[Dashboard] Disconnecting BLE device...');
            bleDevice.removeEventListener('gattserverdisconnected', onDisconnected);
            bleDevice.gatt.disconnect();

            // Wait a bit to ensure disconnection is processed
            await new Promise(resolve => setTimeout(resolve, 200));
        }
    } catch (error) {
        console.error('[Dashboard] Error during BLE cleanup:', error);
    }

    // Mark that we're coming from dashboard to prevent redirect loop
    sessionStorage.setItem('from-dashboard', 'true');

    // Navigate to config page
    console.log('[Dashboard] Navigating to config page...');
    window.location.href = usingCapacitor ? '/index.html' : '../index.html';
}

/**
 * Apply theme from localStorage
 */
function applyTheme() {
    const savedTheme = localStorage.getItem('theme') || 'dark';
    console.log('[Dashboard] Applying theme:', savedTheme);

    if (savedTheme === 'light') {
        document.body.classList.add('light-theme');
    } else {
        document.body.classList.remove('light-theme');
    }
}

/**
 * Get translation for a key
 */
function t(key) {
    const keys = key.split('.');
    let value = translations[currentLang];

    for (const k of keys) {
        if (value && typeof value === 'object') {
            value = value[k];
        } else {
            return key; // Return key if translation not found
        }
    }

    return value || key;
}

/**
 * Apply translations to all elements with data-i18n attribute
 */
function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        el.textContent = t(key);
    });

    // Update rotate device message
    document.body.setAttribute('data-rotate-text', t('dashboard.rotateDevice'));
}

/**
 * Load and apply language from localStorage
 */
function applyLanguage() {
    currentLang = localStorage.getItem('language') || 'fr';
    console.log('[Dashboard] Applying language:', currentLang);
    applyTranslations();
}

/**
 * Update connection status text with translation
 */
function updateConnectionStatusText(connected) {
    const statusText = document.getElementById('status-text');
    if (statusText) {
        statusText.textContent = connected ? t('dashboard.connected') : t('dashboard.disconnected');
    }
}

/**
 * Configure StatusBar for mobile app
 */
async function configureStatusBar() {
    if (usingCapacitor && window.Capacitor?.Plugins?.StatusBar) {
        try {
            const StatusBar = window.Capacitor.Plugins.StatusBar;

            // Set dark theme (light text on dark background)
            await StatusBar.setStyle({ style: 'DARK' });

            // Set background color to match app theme (black)
            await StatusBar.setBackgroundColor({ color: '#0a0a0a' });

            // Make status bar overlay the content (immersive mode)
            await StatusBar.setOverlaysWebView({ overlay: true });

            console.log('[Dashboard] StatusBar configured with dark theme');
        } catch (error) {
            console.log('[Dashboard] Could not configure StatusBar:', error.message);
        }
    }
}

/**
 * Force landscape orientation
 */
async function forceLandscapeOrientation() {
    if (!usingCapacitor) {
        // For web browsers, try to lock to landscape
        if (screen.orientation && screen.orientation.lock) {
            try {
                await screen.orientation.lock('landscape');
                console.log('[Dashboard] Screen orientation locked to landscape');
            } catch (error) {
                console.log('[Dashboard] Could not lock orientation:', error.message);
                // Fallback: Just show a message if in portrait (handled by CSS)
            }
        }
    }
    // For Capacitor, orientation should be configured in capacitor.config.json
}

/**
 * Request fullscreen mode
 */
async function requestFullscreen() {
    try {
        // For Capacitor mobile app, use StatusBar plugin to hide status bar
        if (usingCapacitor && window.Capacitor?.Plugins?.StatusBar) {
            await window.Capacitor.Plugins.StatusBar.hide();
            console.log('[Dashboard] Mobile fullscreen mode activated (status bar hidden)');
            return;
        }

        // For web browsers
        const elem = document.documentElement;

        if (elem.requestFullscreen) {
            await elem.requestFullscreen();
        } else if (elem.webkitRequestFullscreen) { // Safari
            await elem.webkitRequestFullscreen();
        } else if (elem.mozRequestFullScreen) { // Firefox
            await elem.mozRequestFullScreen();
        } else if (elem.msRequestFullscreen) { // IE/Edge
            await elem.msRequestFullscreen();
        }

        console.log('[Dashboard] Web fullscreen mode activated');
    } catch (error) {
        console.log('[Dashboard] Could not enter fullscreen:', error.message);
        // Fullscreen requires user interaction, so it might fail on auto-call
    }
}

/**
 * Exit fullscreen mode
 */
async function exitFullscreen() {
    try {
        // For Capacitor mobile app, show status bar
        if (usingCapacitor && window.Capacitor?.Plugins?.StatusBar) {
            await window.Capacitor.Plugins.StatusBar.show();
            console.log('[Dashboard] Mobile fullscreen mode deactivated (status bar shown)');
            return;
        }

        // For web browsers
        if (document.exitFullscreen) {
            await document.exitFullscreen();
        } else if (document.webkitExitFullscreen) {
            await document.webkitExitFullscreen();
        } else if (document.mozCancelFullScreen) {
            await document.mozCancelFullScreen();
        } else if (document.msExitFullscreen) {
            await document.msExitFullscreen();
        }

        console.log('[Dashboard] Web fullscreen mode deactivated');
    } catch (error) {
        console.log('[Dashboard] Could not exit fullscreen:', error.message);
    }
}

/**
 * Toggle fullscreen mode
 */
window.toggleFullscreen = async function() {
    // For Capacitor mobile app, we can't easily check status bar state
    // Just toggle between hide/show
    if (usingCapacitor) {
        // Try to hide first, if already hidden it will show
        try {
            if (window.Capacitor?.Plugins?.StatusBar) {
                // Simple approach: just hide the status bar
                await window.Capacitor.Plugins.StatusBar.hide();
            }
        } catch (error) {
            console.log('[Dashboard] Could not toggle mobile fullscreen:', error.message);
        }
        return;
    }

    // For web browsers
    const isFullscreen = document.fullscreenElement ||
                        document.webkitFullscreenElement ||
                        document.mozFullScreenElement ||
                        document.msFullscreenElement;

    if (isFullscreen) {
        await exitFullscreen();
    } else {
        await requestFullscreen();
    }
}

/**
 * Simulate vehicle data for testing (DEV ONLY)
 * Call this function from browser console: simulateDriveMode()
 */
window.simulateDriveMode = function() {
    console.log('[Dashboard] Starting drive mode simulation...');

    // Force drive mode
    currentMode = 'drive';
    document.getElementById('park-mode').classList.remove('active');
    document.getElementById('drive-mode').classList.add('active');

    // Hide header in drive mode
    const header = document.getElementById('dashboard-header');
    if (header) {
        header.classList.add('hidden');
    }

    let speed = 0;
    let pedalPos = 0;
    let battery = 85;
    let odometer = 12345;
    let turnLeftActive = false;
    let turnRightActive = false;
    let blindspotLeftLv1 = false;
    let blindspotLeftLv2 = false;
    let blindspotRightLv1 = false;
    let blindspotRightLv2 = false;

    const simulationInterval = setInterval(() => {
        // Simulate speed changes (0-120 km/h)
        speed = Math.abs(Math.sin(Date.now() / 2000) * 120);

        // Simulate pedal position (0-100%)
        pedalPos = Math.abs(Math.sin(Date.now() / 1500) * 100);

        // Toggle turn signals every 3 seconds
        if (Math.floor(Date.now() / 3000) % 2 === 0) {
            turnLeftActive = true;
            turnRightActive = false;
        } else {
            turnLeftActive = false;
            turnRightActive = true;
        }

        // Simulate blindspot with progressive levels
        // Cycle through: no detection -> level 1 -> level 2 -> no detection
        const blindspotCycle = Math.floor(Date.now() / 2000) % 3;

        // Left side
        if (blindspotCycle === 1) {
            blindspotLeftLv1 = true;
            blindspotLeftLv2 = false;
        } else if (blindspotCycle === 2) {
            blindspotLeftLv1 = true;
            blindspotLeftLv2 = true;
        } else {
            blindspotLeftLv1 = false;
            blindspotLeftLv2 = false;
        }

        // Right side (offset by 1 second)
        const blindspotCycleRight = Math.floor((Date.now() + 1000) / 2000) % 3;
        if (blindspotCycleRight === 1) {
            blindspotRightLv1 = true;
            blindspotRightLv2 = false;
        } else if (blindspotCycleRight === 2) {
            blindspotRightLv1 = true;
            blindspotRightLv2 = true;
        } else {
            blindspotRightLv1 = false;
            blindspotRightLv2 = false;
        }

        // Simulate power (with regeneration)
        const rearPower = Math.sin(Date.now() / 1000) * 150; // -150 to +150 kW
        const frontPower = Math.cos(Date.now() / 1200) * 100; // -100 to +100 kW

        // Cycle pedal map every 4 seconds: Chill -> Standard -> Sport
        const pedalMapCycle = Math.floor(Date.now() / 4000) % 3;
        const pedalMap = pedalMapCycle === 0 ? -1 : pedalMapCycle === 1 ? 0 : 1;

        // Simulate state
        const simulatedState = {
            speed_kph: speed,
            soc_percent: battery,
            accel_pedal_pos: Math.round(pedalPos),
            gear: 4, // D
            pedal_map: pedalMap,
            rear_power_kw: rearPower,
            front_power_kw: frontPower,
            odometer_km: odometer,
            turn_left: turnLeftActive,
            turn_right: turnRightActive,
            blindspot_left: blindspotLeftLv1,
            blindspot_right: blindspotRightLv1,
            blindspot_left_alert: blindspotLeftLv2,
            blindspot_right_alert: blindspotRightLv2,
            forward_collision: Math.random() > 0.9,
            brake_pressed: Math.random() > 0.8,
            autopilot: Math.random() > 0.5
        };

        // Update display
        document.getElementById('speed-value-drive').textContent = Math.round(simulatedState.speed_kph);
        document.getElementById('gear-display-drive').textContent = 'D';

        // Pedal arc (240° arc)
        const pedalArcFill = document.getElementById('pedal-arc-fill');
        if (pedalArcFill) {
            const dashOffset = 341 - (341 * simulatedState.accel_pedal_pos / 100);
            pedalArcFill.style.strokeDashoffset = dashOffset;
        }

        // Compact info
        document.getElementById('battery-drive-compact').textContent = Math.round(simulatedState.soc_percent) + '%';
        document.getElementById('odometer-drive-compact').textContent = Math.round(simulatedState.odometer_km) + 'km';

        // Power & pedal map indicators
        updatePowerIndicators(simulatedState.rear_power_kw, simulatedState.front_power_kw, simulatedState.pedal_map);

        // Indicators
        updateDriveIndicator('ind-turn-left-drive', simulatedState.turn_left);
        updateDriveIndicator('ind-turn-right-drive', simulatedState.turn_right);

        // Use new blindspot arc SVGs with progressive levels
        updateBlindspotArcs('blindspot-arcs-left', simulatedState.blindspot_left, simulatedState.blindspot_left_alert);
        updateBlindspotArcs('blindspot-arcs-right', simulatedState.blindspot_right, simulatedState.blindspot_right_alert);

        updateDriveIndicator('ind-forward-collision-drive', simulatedState.forward_collision);
        updateDriveIndicator('ind-brake-drive', simulatedState.brake_pressed);
        updateDriveIndicator('ind-autopilot-drive', simulatedState.autopilot);

    }, 500); // Update every 500ms (2 Hz for smoother, less intensive simulation)

    console.log('[Dashboard] Drive simulation running. To stop: stopSimulation()');
    window.stopSimulation = () => {
        clearInterval(simulationInterval);
        console.log('[Dashboard] Simulation stopped');
    };
};

/**
 * Simulate vehicle data in Park mode for testing (DEV ONLY)
 * Call this function from browser console: simulateParkMode()
 */
window.simulateParkMode = function() {
    console.log('[Dashboard] Starting park mode simulation...');

    // Force park mode
    currentMode = 'park';
    document.getElementById('drive-mode').classList.remove('active');
    document.getElementById('park-mode').classList.add('active');

    // Show header in park mode
    const header = document.querySelector('.dashboard-header');
    if (header) {
        header.classList.remove('hidden');
    }

    let battery = 75;
    let batteryHV = 385.0;
    let batteryLV = 12.5;
    let odometer = 12345;
    let chargePower = 0;
    let cycleStep = 0;

    const simulationInterval = setInterval(() => {
        // Cycle through different states every 3 seconds
        const currentTime = Date.now();
        cycleStep = Math.floor(currentTime / 3000) % 8;

        // Simulate charging (slowly increase battery)
        if (cycleStep >= 4) {
            chargePower = 11.0;
            battery = Math.min(100, 75 + (currentTime % 10000) / 1000);
            batteryHV = 385.0 + Math.sin(currentTime / 1000) * 2;
        } else {
            chargePower = 0;
            battery = 75;
            batteryHV = 385.0;
        }

        // Create simulated state based on cycle step
        const simulatedState = {
            soc_percent: battery,
            battery_voltage_HV: batteryHV,
            battery_voltage_LV: batteryLV,
            charge_power_kw: chargePower,
            odometer_km: odometer,
            gear: 1, // P

            // Doors - open one at a time in sequence
            door_front_left_open: cycleStep === 0,
            door_front_right_open: cycleStep === 1,
            door_rear_left_open: cycleStep === 2,
            door_rear_right_open: cycleStep === 3,
            frunk_open: cycleStep === 4,
            trunk_open: cycleStep === 5,

            // Lights - cycle through different combinations
            headlights: cycleStep >= 2 && cycleStep <= 4,
            high_beams: cycleStep === 3,
            fog_lights: cycleStep === 4,
            turn_left: cycleStep === 1 || cycleStep === 5,
            turn_right: cycleStep === 2 || cycleStep === 6,
            hazard: cycleStep === 7,
            brake_pressed: cycleStep % 2 === 0,

            // Security
            locked: cycleStep < 4,
            sentry_mode: cycleStep === 6,
            sentry_alert: cycleStep === 7,

            // Charging
            charging_cable: cycleStep >= 4,
            charging: cycleStep >= 5,
            charging_port: cycleStep >= 4,

            // Night mode
            night_mode: cycleStep >= 4,

            // Other
            charge_status: cycleStep >= 4 ? 1 : 0,
            doors_open_count: cycleStep < 6 ? 1 : 0,
            brightness: 0.5,
            autopilot: false,
            last_update_ms: currentTime
        };

        // Update dashboard using the main updateDashboard function
        updateDashboard(simulatedState);

    }, 500); // Update every 500ms (2 Hz for smoother, less intensive simulation)

    console.log('[Dashboard] Park mode simulation running. To stop: stopSimulation()');
    window.stopSimulation = () => {
        clearInterval(simulationInterval);
        console.log('[Dashboard] Simulation stopped');
    };
};

/**
 * Initialize dashboard
 */
async function init() {
    console.log('[Dashboard] Initializing...');
    console.log('[Dashboard] Platform:', usingCapacitor ? 'Capacitor' : 'Web');

    // Apply language
    applyLanguage();

    // Apply theme
    applyTheme();

    // Configure StatusBar for mobile (must be done early)
    if (usingCapacitor) {
        await configureStatusBar();
    }

    // Force landscape orientation
    await forceLandscapeOrientation();

    // Request fullscreen
    if (usingCapacitor) {
        // For mobile app, hide status bar immediately
        await requestFullscreen();
    } else {
        // For web browsers, request on first click (requires user gesture)
        document.addEventListener('click', async () => {
            const isFullscreen = document.fullscreenElement ||
                                document.webkitFullscreenElement ||
                                document.mozFullScreenElement ||
                                document.msFullscreenElement;

            if (!isFullscreen) {
                await requestFullscreen();
            }
        }, { once: true }); // Only on first click
    }

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
