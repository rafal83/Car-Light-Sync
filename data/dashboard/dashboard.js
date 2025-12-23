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

// BLE packet sizes for different modes (payload sizes only)
const BLE_PACKET_HEADER_SIZE = 1;
const BLE_PACKET_TYPE_CONFIG = 0;
const BLE_PACKET_TYPE_PARK = 1;
const BLE_PACKET_TYPE_DRIVE = 2;
const VEHICLE_STATE_CONFIG_PAYLOAD_SIZE = 11; // vehicle_state_ble_drive_t
const VEHICLE_STATE_PARK_PAYLOAD_SIZE = 19;  // vehicle_state_ble_park_t
const VEHICLE_STATE_DRIVE_PAYLOAD_SIZE = 22; // vehicle_state_ble_drive_t
const VEHICLE_STATE_CONFIG_SIZE = BLE_PACKET_HEADER_SIZE + VEHICLE_STATE_CONFIG_PAYLOAD_SIZE;
const VEHICLE_STATE_PARK_SIZE = BLE_PACKET_HEADER_SIZE + VEHICLE_STATE_PARK_PAYLOAD_SIZE;
const VEHICLE_STATE_DRIVE_SIZE = BLE_PACKET_HEADER_SIZE + VEHICLE_STATE_DRIVE_PAYLOAD_SIZE;

let rearPowerMax_kw = 0;
let frontPowerMax_kw = 0;
let maxRegen_kw = 0;
let trainType = 0;

/**
 * Decode BLE CONFIG mode packet (vehicle_state_ble_config_t)
 * Structure from include/vehicle_can_unified.h
 * Total: ~11 bytes
 */
function decodeVehicleStateConfig(dataView) {
    let offset = 0;

    // Helper functions
    const readInt16 = () => { const val = dataView.getInt16(offset, true); offset += 2; return val; };
    const readUint16 = () => { const val = dataView.getUint16(offset, true); offset += 2; return val; };
    const readUint8 = () => { const val = dataView.getUint8(offset); offset += 1; return val; };
    const readInt8 = () => { const val = dataView.getInt8(offset); offset += 1; return val; };
    const readUint32 = () => { const val = dataView.getUint32(offset, true); offset += 4; return val; };

    // Dynamique de conduite
    rearPowerMax_kw = readUint16() / 10;
    frontPowerMax_kw = readUint16() / 10;
    maxRegen_kw = readUint16() / 10;

    // Bit-packed flags
    const flags0 = readUint8();
    const last_update_ms = readUint32();

    trainType = (flags0 & (1<<0)) !== 0;
}

/**
 * Decode BLE DRIVE mode packet (vehicle_state_ble_drive_t)
 * Structure from include/vehicle_can_unified.h
 * Total: ~22 bytes
 */
function decodeVehicleStateDrive(dataView) {
    let offset = 0;

    // Helper functions
    const readInt16 = () => { const val = dataView.getInt16(offset, true); offset += 2; return val; };
    const readUint16 = () => { const val = dataView.getUint16(offset, true); offset += 2; return val; };
    const readUint8 = () => { const val = dataView.getUint8(offset); offset += 1; return val; };
    const readInt8 = () => { const val = dataView.getInt8(offset); offset += 1; return val; };
    const readUint32 = () => { const val = dataView.getUint32(offset, true); offset += 4; return val; };

    // Dynamique de conduite
    const speed_kph = readUint8();
    const rear_power_kw = readInt16() / 10.0;  // Peut être négatif (régén)
    const front_power_kw = readInt16() / 10.0; // Peut être négatif (régén)
    const soc_percent = readUint8();
    const odometer_km = readUint32();
    // Valeurs uint8
    const gear = readInt8();
    const pedal_map = readInt8();
    const accel_pedal_pos = readUint8();
    const brightness = readUint8();
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

        // Missing fields from Park mode (defaults)
        charge_power_kw: 0,
        battery_voltage_LV: 0,
        battery_voltage_HV: 0,
        charge_status: 0,
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
 * Total: ~21 bytes
 */
function decodeVehicleStatePark(dataView) {
    let offset = 0;

    // Helper functions
    const readInt16 = () => { const val = dataView.getInt16(offset, true); offset += 2; return val; };
    const readUint16 = () => { const val = dataView.getUint16(offset, true); offset += 2; return val; };
    const readUint8 = () => { const val = dataView.getUint8(offset); offset += 1; return val; };
    const readInt8 = () => { const val = dataView.getInt8(offset); offset += 1; return val; };
    const readUint32 = () => { const val = dataView.getUint32(offset, true); offset += 4; return val; };

    // Energie
    const soc_percent = readUint8();
    const charge_power_kw = readInt16() / 10.0;
    const battery_voltage_LV = readUint8() / 10.0;
    const battery_voltage_HV = readInt16() / 10.0;
    const odometer_km = readUint32(); 
    // Valeurs uint8
    const charge_status = readUint8();
    const brightness = readUint8();

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

    // Sync phone brightness with car brightness
    if (state.brightness !== undefined) {
        syncBrightness(state.brightness); // Convert 0-1 to 0-100
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
        document.getElementById('speed-value-drive').textContent = state.speed_kph;

        // Gear display
        document.getElementById('gear-display-drive').textContent = gearText;

        // Pedal arc (0-100% maps to stroke-dashoffset 377 to 0) - 240° arc
        const pedalArcFill = document.getElementById('pedal-arc-fill');
        if (pedalArcFill) {
            const dashOffset = 340 - (340 * state.accel_pedal_pos / 100);
            pedalArcFill.style.strokeDashoffset = dashOffset;
        }

        // Compact info below speedometer
        document.getElementById('battery-drive-compact').textContent = Math.round(state.soc_percent) + '%';
        document.getElementById('odometer-drive-compact').textContent = Math.round(state.odometer_km) + 'km';

        // Power & pedal map indicators
        updatePowerIndicators(state.rear_power_kw, state.front_power_kw, state.pedal_map, trainType);

        // Show/hide indicators (only when active)
        updateDriveIndicator('ind-turn-left-drive', state.turn_left);
        updateDriveIndicator('ind-turn-right-drive', state.turn_right);

        // Blindspot arcs with progressive intensity (level 1 and level 2)
        updateBlindspotArcs('blindspot-arcs-left', state.blindspot_left, state.blindspot_left_alert);
        updateBlindspotArcs('blindspot-arcs-right', state.blindspot_right, state.blindspot_right_alert);

        updateDriveWarningBar(state);
    }
}

/**
 * Update power indicators and pedal map
 * @param {number} rearPower - Rear motor power in kW (can be negative for regen)
 * @param {number} frontPower - Front motor power in kW (can be negative for regen)
 * @param {number} pedalMap - Pedal map mode (-1=Chill, 0=Standard, 1=Sport)
 * @param {number} trainType - Train type (0 = RWD, 1 = AWD)
 */
function updatePowerIndicators(rearPower, frontPower, pedalMap, trainType) {
    const fallbackMaxPower = 200; // Fallback when config values are unavailable
    const rearMaxPower = rearPowerMax_kw > 0 ? rearPowerMax_kw : fallbackMaxPower;
    const frontMaxPower = frontPowerMax_kw > 0 ? frontPowerMax_kw : fallbackMaxPower;
    const regenMaxPower = maxRegen_kw > 0 ? maxRegen_kw : fallbackMaxPower;

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
        const rearLimit = rearPower >= 0 ? rearMaxPower : regenMaxPower;
        const rearPercent = Math.abs(rearPower) / rearLimit;
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
      if(trainType == 0) { // 0 = RWD, no front motor
        document.getElementById('power-arc-front').style.display = 'none'; // Hide entire
        document.getElementById('power-front-indicator').style.display = 'none'; // Hide entire
      } else {
        const frontRounded = Math.round(frontPower || 0);
        frontValue.textContent = frontRounded;

        // Half arc length: 79.5 (55° in units)
        const halfArc = 79.5;
        const frontLimit = frontPower >= 0 ? frontMaxPower : regenMaxPower;
        const frontPercent = Math.abs(frontPower) / frontLimit;
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

function updateDriveWarningBar(state) {
    const container = document.getElementById('drive-warning-bar');
    const bar = document.getElementById('warning-bar');
    const label = document.getElementById('warning-bar-label');
    if (!container || !bar || !label) {
        return;
    }

    let type = '';
    let text = '';

    if (state.forward_collision) {
        type = 'collision';
        text = 'WARNING';
    } else if (state.brake_pressed) {
        type = 'brake';
        text = 'BRAKE';
    } else {
        const autopilotMap = {
            2: 'AVAILABLE',
            3: 'ACTIVE_NOMINAL',
            4: 'ACTIVE_RESTRICTED',
            5: 'ACTIVE_NAV',
            8: 'ABORTING',
            9: 'ABORTED'
        };
        const autopilotKey = typeof state.autopilot === 'number' ? state.autopilot : parseInt(state.autopilot, 10);
        if (autopilotMap[autopilotKey]) {
            type = 'autopilot';
            text = autopilotMap[autopilotKey];
        }
    }

    if (!type) {
        container.style.display = 'none';
        return;
    }

    container.style.display = 'flex';
    bar.classList.remove('warning-bar-collision', 'warning-bar-brake', 'warning-bar-autopilot');
    bar.classList.add(`warning-bar-${type}`);
    label.textContent = text;
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
        arcs[0].setAttribute('stroke', '#ff0000'); // Red
        arcs[1].style.display = 'block';
        arcs[1].setAttribute('stroke', '#ff0000'); // Red
        arcs[2].style.display = 'block';
        arcs[2].setAttribute('stroke', '#ff0000'); // Red
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

        // Don't retry if stopping BLE or if user cancelled the device selection
        if (stoppingBLE) {
            console.log('[Dashboard] BLE operations stopped, not retrying');
            return;
        }

        if (error.name === 'NotFoundError' || error.message.includes('User cancelled')) {
            console.log('[Dashboard] User cancelled device selection, not retrying');
            return;
        }

        // Retry after 3 seconds for other errors
        setTimeout(() => {
            if (!stoppingBLE) {
                connectBLE();
            }
        }, 3000);
    }
}

/**
 * Handle vehicle state notification
 * Auto-detects packet format based on header
 */
function handleVehicleStateNotification(event) {
    const dataView = event.target.value;

    if (dataView.byteLength < BLE_PACKET_HEADER_SIZE) {
        console.log('[Dashboard] BLE packet too short:', dataView.byteLength);
        return;
    }

    const header = dataView.getUint8(0);
    const packetType = (header >> 5) & 0x07;

    let payloadSize = 0;
    let mode = null;

    if (packetType === BLE_PACKET_TYPE_CONFIG) {
        payloadSize = VEHICLE_STATE_CONFIG_PAYLOAD_SIZE;
        mode = 'config';
    } else if (packetType === BLE_PACKET_TYPE_PARK) {
        payloadSize = VEHICLE_STATE_PARK_PAYLOAD_SIZE;
        mode = 'park';
    } else if (packetType === BLE_PACKET_TYPE_DRIVE) {
        payloadSize = VEHICLE_STATE_DRIVE_PAYLOAD_SIZE;
        mode = 'drive';
    } else {
        console.log('[Dashboard] Unknown BLE packet type:', packetType);
        return;
    }

    if (dataView.byteLength < BLE_PACKET_HEADER_SIZE + payloadSize) {
        console.log('[Dashboard] BLE packet too short for type:', packetType, 'len:', dataView.byteLength);
        return;
    }

    try {
        const payloadView = new DataView(
            dataView.buffer,
            dataView.byteOffset + BLE_PACKET_HEADER_SIZE,
            payloadSize
        );
        let state;

        if (mode === 'config') {
            decodeVehicleStateConfig(payloadView);
            // console.log('[Dashboard] Decoded CONFIG mode state:', state);
        } else if (mode === 'drive') {
            state = decodeVehicleStateDrive(payloadView);
            // console.log('[Dashboard] Decoded DRIVE mode state:', state);
        } else if (mode === 'park') {
            state = decodeVehicleStatePark(payloadView);
            // console.log('[Dashboard] Decoded PARK mode state:', state);
        }

        if (state) {
            updateDashboard(state);
        }
    } catch (error) {
        console.error('[Dashboard] Failed to decode vehicle state:', error);
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

    // Set flag to prevent any new BLE operations and reconnection attempts
    stoppingBLE = true;

    // Unlock orientation to allow rotation in config mode
    await unlockOrientation();

    // Allow screen to sleep in config mode
    await allowScreenSleep();

    // Restore default brightness (disable auto brightness sync)
    if (usingCapacitor && window.Capacitor?.Plugins?.ScreenBrightness) {
        try {
            await window.Capacitor.Plugins.ScreenBrightness.setBrightness({ brightness: -1 }); // -1 = system default
            console.log('[Dashboard] Brightness restored to system default');
        } catch (error) {
            console.log('[Dashboard] Could not restore brightness:', error.message);
        }
    }

    // Disconnect BLE and stop notifications
    try {
        if (bleCharacteristicVehicleState) {
            console.log('[Dashboard] Stopping notifications...');
            try {
                await bleCharacteristicVehicleState.stopNotifications();
            } catch (e) {
                console.log('[Dashboard] Could not stop notifications:', e.message);
            }
            bleCharacteristicVehicleState.removeEventListener('characteristicvaluechanged', handleVehicleStateNotification);
            bleCharacteristicVehicleState = null;
        }

        if (bleDevice) {
            console.log('[Dashboard] Disconnecting BLE device...');
            try {
                bleDevice.removeEventListener('gattserverdisconnected', onDisconnected);
                if (bleDevice.gatt && bleDevice.gatt.connected) {
                    bleDevice.gatt.disconnect();
                }
            } catch (e) {
                console.log('[Dashboard] Could not disconnect:', e.message);
            }
            bleDevice = null;

            // Wait a bit to ensure disconnection is processed
            await new Promise(resolve => setTimeout(resolve, 300));
        }
    } catch (error) {
        console.error('[Dashboard] Error during BLE cleanup:', error);
    }

    // Mark that we're coming from dashboard to prevent redirect loop
    sessionStorage.setItem('from-dashboard', 'true');

    // Wait a bit longer to ensure all BLE operations are fully stopped
    await new Promise(resolve => setTimeout(resolve, 500));

    // Navigate to config page (use replace to ensure dashboard is fully unloaded)
    console.log('[Dashboard] Navigating to config page...');
    window.location.replace(usingCapacitor ? '/index.html' : '../index.html');
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
    updateAutoBrightnessButton();
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
 * Force landscape orientation (dashboard mode)
 */
async function forceLandscapeOrientation() {
    if (usingCapacitor && window.Capacitor?.Plugins?.ScreenOrientation) {
        try {
            await window.Capacitor.Plugins.ScreenOrientation.lock({ type: 'landscape' });
            console.log('[Dashboard] Screen orientation locked to landscape');
        } catch (error) {
            console.log('[Dashboard] Could not lock orientation:', error.message);
        }
    } else if (!usingCapacitor) {
        // For web browsers, try to lock to landscape
        if (screen.orientation && screen.orientation.lock) {
            try {
                await screen.orientation.lock('landscape');
                console.log('[Dashboard] Screen orientation locked to landscape');
            } catch (error) {
                console.log('[Dashboard] Could not lock orientation:', error.message);
            }
        }
    }
}

/**
 * Unlock orientation (allow auto-rotate for config mode)
 */
async function unlockOrientation() {
    if (usingCapacitor && window.Capacitor?.Plugins?.ScreenOrientation) {
        try {
            await window.Capacitor.Plugins.ScreenOrientation.unlock();
            console.log('[Dashboard] Screen orientation unlocked');
        } catch (error) {
            console.log('[Dashboard] Could not unlock orientation:', error.message);
        }
    } else if (!usingCapacitor) {
        // For web browsers
        if (screen.orientation && screen.orientation.unlock) {
            try {
                screen.orientation.unlock();
                console.log('[Dashboard] Screen orientation unlocked');
            } catch (error) {
                console.log('[Dashboard] Could not unlock orientation:', error.message);
            }
        }
    }
}

// Global variable to store wake lock
let wakeLock = null;

// HUD mode state
let isHUDMode = false;

// Auto brightness sync state
let autoBrightnessEnabled = true;
let lastSyncedBrightnessPercent = null;
let lastBrightnessBeforeHud = null;

// Flag to stop all BLE operations when switching to config
let stoppingBLE = false;

/**
 * Keep screen awake (prevent screen from turning off)
 */
async function keepScreenAwake() {
    if (usingCapacitor && window.Capacitor?.Plugins?.KeepAwake) {
        try {
            await window.Capacitor.Plugins.KeepAwake.keepAwake();
            console.log('[Dashboard] Screen will stay awake');
        } catch (error) {
            console.log('[Dashboard] Could not keep screen awake:', error.message);
        }
    } else if ('wakeLock' in navigator) {
        // Web Wake Lock API (modern browsers)
        try {
            wakeLock = await navigator.wakeLock.request('screen');
            console.log('[Dashboard] Wake Lock activated');

            // Re-acquire wake lock if page becomes visible again
            document.addEventListener('visibilitychange', async () => {
                if (document.visibilityState === 'visible' && wakeLock !== null) {
                    wakeLock = await navigator.wakeLock.request('screen');
                }
            });
        } catch (error) {
            console.log('[Dashboard] Could not activate Wake Lock:', error.message);
        }
    }
}

/**
 * Allow screen to sleep again
 */
async function allowScreenSleep() {
    if (usingCapacitor && window.Capacitor?.Plugins?.KeepAwake) {
        try {
            await window.Capacitor.Plugins.KeepAwake.allowSleep();
            console.log('[Dashboard] Screen sleep allowed');
        } catch (error) {
            console.log('[Dashboard] Could not allow screen sleep:', error.message);
        }
    }
    // Note: Web Wake Lock is automatically released when page is not visible
}

/**
 * Synchronize phone brightness with car brightness
 */
async function syncBrightness(carBrightnessPercent) {
    if (!autoBrightnessEnabled) {
        return;
    }

    const targetPercent = isHUDMode ? 100 : carBrightnessPercent;
    const normalizedPercent = Math.round(Math.max(0, Math.min(100, targetPercent)));
    if (lastSyncedBrightnessPercent === normalizedPercent) {
        return;
    }

    // Convert percentage (0-100) to brightness value (0-1)
    const brightness = normalizedPercent / 100;

    if (usingCapacitor && window.Capacitor?.Plugins?.ScreenBrightness) {
        try {
            await window.Capacitor.Plugins.ScreenBrightness.setBrightness({ brightness });
            // console.log('[Dashboard] Screen brightness set to:', brightness);
            lastSyncedBrightnessPercent = normalizedPercent;
        } catch (error) {
            console.log('[Dashboard] Could not set brightness:', error.message);
        }
    }
    // Note: Web browsers don't have a standard API to control screen brightness
}

/**
 * Toggle auto brightness sync
 */
function toggleAutoBrightness() {
    autoBrightnessEnabled = !autoBrightnessEnabled;
    localStorage.setItem('autoBrightness', autoBrightnessEnabled ? 'true' : 'false');
    console.log('[Dashboard] Auto brightness:', autoBrightnessEnabled ? 'enabled' : 'disabled');

    // If disabled, restore default brightness
    if (!autoBrightnessEnabled && usingCapacitor && window.Capacitor?.Plugins?.ScreenBrightness) {
        window.Capacitor.Plugins.ScreenBrightness.setBrightness({ brightness: -1 }); // -1 = system default
    }
    if (!autoBrightnessEnabled) {
        lastSyncedBrightnessPercent = null;
    }
    updateAutoBrightnessButton();
}

function updateAutoBrightnessButton() {
    const button = document.getElementById('auto-brightness-btn');
    if (!button) {
        return;
    }
    const state = document.getElementById('auto-brightness-state');
    button.classList.toggle('active', autoBrightnessEnabled);
    button.setAttribute('aria-pressed', autoBrightnessEnabled ? 'true' : 'false');
    if (state) {
        state.textContent = autoBrightnessEnabled ? t('dashboard.on') : t('dashboard.off');
    }
}
/**
 * Initialize HUD mode (Head-Up Display)
 * Clicking on the speed display toggles HUD mode: flip vertical + force dark theme
 */
function initHUDMode() {
    // Load HUD mode state from localStorage
    isHUDMode = localStorage.getItem('hudMode') === 'true';
    if (isHUDMode) {
        applyHUDMode();
    }

    const speedDisplay = document.getElementById('speed-display-drive');
    if (speedDisplay) {
        speedDisplay.addEventListener('click', () => {
            toggleHUDMode();
        });
    }
}

/**
 * Toggle HUD mode
 */
function toggleHUDMode() {
    isHUDMode = !isHUDMode;
    localStorage.setItem('hudMode', isHUDMode ? 'true' : 'false');

    if (isHUDMode) {
        applyHUDMode();
    } else {
        removeHUDMode();
    }
}

/**
 * Apply HUD mode: flip vertical + force dark theme
 */
function applyHUDMode() {
    const content = document.querySelector('.dashboard-content');
    if (content) {
        content.classList.add('hud-mode');
    }
    const header = document.querySelector('.dashboard-header');
    if (header) {
        header.classList.add('hidden');
    }
    document.body.classList.remove('light-theme');
    document.body.classList.add('hud-mode');
    if (usingCapacitor && window.Capacitor?.Plugins?.ScreenBrightness) {
        const screenBrightness = window.Capacitor.Plugins.ScreenBrightness;
        screenBrightness.getBrightness()
            .then(result => {
                if (lastBrightnessBeforeHud === null && typeof result?.brightness === 'number') {
                    lastBrightnessBeforeHud = result.brightness;
                }
            })
            .catch(() => {});
        screenBrightness.setBrightness({ brightness: 1 });
        lastSyncedBrightnessPercent = 100;
    }
    // Force dark theme in HUD mode
    document.documentElement.setAttribute('data-theme', 'dark');
    console.log('[Dashboard] HUD mode activated');
}

/**
 * Remove HUD mode: restore normal orientation and theme
 */
function removeHUDMode() {
    const content = document.querySelector('.dashboard-content');
    if (content) {
        content.classList.remove('hud-mode');
    }
    const header = document.querySelector('.dashboard-header');
    if (header) {
        header.classList.remove('hidden');
    }
    document.body.classList.remove('hud-mode');
    if (usingCapacitor && window.Capacitor?.Plugins?.ScreenBrightness) {
        const screenBrightness = window.Capacitor.Plugins.ScreenBrightness;
        if (lastBrightnessBeforeHud !== null) {
            screenBrightness.setBrightness({ brightness: lastBrightnessBeforeHud });
        } else {
            screenBrightness.setBrightness({ brightness: -1 });
        }
        lastBrightnessBeforeHud = null;
        lastSyncedBrightnessPercent = null;
    }
    // Restore user's preferred theme
    applyTheme();
    console.log('[Dashboard] HUD mode deactivated');
}

/**
 * Setup background handler - close app when going to background to save battery
 */
function setupBackgroundHandler() {
    if (window.Capacitor?.Plugins?.App) {
        const App = window.Capacitor.Plugins.App;

        App.addListener('appStateChange', (state) => {
            console.log('[Dashboard] App state changed:', state.isActive ? 'active' : 'background');

            if (!state.isActive) {
                // App went to background - close it completely
                console.log('[Dashboard] App going to background - closing to save battery');

                // Disconnect BLE first
                if (bleDevice) {
                    bleDevice.disconnect();
                }

                // Close the app
                App.exitApp();
            }
        });

        console.log('[Dashboard] Background handler setup - app will close when backgrounded');
    }
}

/**
 * Optimize battery consumption by reducing update frequency when not in focus
 */
function optimizeBatteryConsumption() {
    // Reduce animation frequency
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            console.log('[Dashboard] Page hidden - pausing animations');
            // Stop CSS animations
            document.body.style.animationPlayState = 'paused';
        } else {
            console.log('[Dashboard] Page visible - resuming animations');
            // Resume CSS animations
            document.body.style.animationPlayState = 'running';
        }
    });
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

    let battery = 85;
    let odometer = 12345;
    const stepDurationMs = 3000;
    const startTime = Date.now();
    const steps = [
        (s) => { s.speed_kph = 100; s.accel_pedal_pos = 60; s.rear_power_kw = 120; s.front_power_kw = 60; },
        (s) => { s.turn_left = true; },
        (s) => { s.turn_right = true; },
        (s) => { s.blindspot_left = true; },
        (s) => { s.blindspot_left = true; s.blindspot_left_alert = true; },
        (s) => { s.blindspot_right = true; },
        (s) => { s.blindspot_right = true; s.blindspot_right_alert = true; },
        (s) => { s.forward_collision = true; },
        (s) => { s.brake_pressed = true; },
        (s) => { s.autopilot = 2; },
        (s) => { s.autopilot = 3; },
        (s) => { s.autopilot = 4; },
        (s) => { s.autopilot = 5; },
        (s) => { s.autopilot = 8; },
        (s) => { s.autopilot = 9; }
    ];

    const simulationInterval = setInterval(() => {
        const elapsedMs = Date.now() - startTime;
        const stepIndex = Math.floor(elapsedMs / stepDurationMs) % steps.length;
        const pedalMapCycle = Math.floor(elapsedMs / (stepDurationMs * 2)) % 3;
        const pedalMap = pedalMapCycle === 0 ? -1 : pedalMapCycle === 1 ? 0 : 1;

        // Simulate state
        const simulatedState = {
            speed_kph: 60,
            soc_percent: battery,
            accel_pedal_pos: 25,
            gear: 4, // D
            pedal_map: pedalMap,
            rear_power_kw: 40,
            front_power_kw: 20,
            odometer_km: odometer,
            turn_left: false,
            turn_right: false,
            blindspot_left: false,
            blindspot_right: false,
            blindspot_left_alert: false,
            blindspot_right_alert: false,
            forward_collision: false,
            brake_pressed: false,
            autopilot: 0
        };
        steps[stepIndex](simulatedState);

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
        updatePowerIndicators(simulatedState.rear_power_kw, simulatedState.front_power_kw, simulatedState.pedal_map, 1);

        // Indicators
        updateDriveIndicator('ind-turn-left-drive', simulatedState.turn_left);
        updateDriveIndicator('ind-turn-right-drive', simulatedState.turn_right);

        // Use new blindspot arc SVGs with progressive levels
        updateBlindspotArcs('blindspot-arcs-left', simulatedState.blindspot_left, simulatedState.blindspot_left_alert);
        updateBlindspotArcs('blindspot-arcs-right', simulatedState.blindspot_right, simulatedState.blindspot_right_alert);

        updateDriveWarningBar(simulatedState);

    }, 500); // UI refresh, state changes every stepDurationMs

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
    const stepDurationMs = 3000;
    const startTime = Date.now();
    const steps = [
        (s) => { s.door_front_left_open = true; },
        (s) => { s.door_front_right_open = true; },
        (s) => { s.door_rear_left_open = true; },
        (s) => { s.door_rear_right_open = true; },
        (s) => { s.frunk_open = true; },
        (s) => { s.trunk_open = true; },
        (s) => { s.headlights = true; },
        (s) => { s.high_beams = true; },
        (s) => { s.fog_lights = true; },
        (s) => { s.turn_left = true; },
        (s) => { s.turn_right = true; },
        (s) => { s.hazard = true; },
        (s) => { s.brake_pressed = true; },
        (s) => { s.locked = false; },
        (s) => { s.sentry_mode = true; },
        (s) => { s.sentry_alert = true; },
        (s) => {
            s.charging_cable = true;
            s.charging = true;
            s.charging_port = true;
            s.charge_status = 1;
            s.night_mode = true;
            s.charge_power_kw = 11.0;
            s.soc_percent = 78;
            s.battery_voltage_HV = 386.5;
        }
    ];

    const simulationInterval = setInterval(() => {
        const elapsedMs = Date.now() - startTime;
        const stepIndex = Math.floor(elapsedMs / stepDurationMs) % steps.length;
        chargePower = 0;
        battery = 75;
        batteryHV = 385.0;

        // Create simulated state based on cycle step
        const simulatedState = {
            soc_percent: battery,
            battery_voltage_HV: batteryHV,
            battery_voltage_LV: batteryLV,
            charge_power_kw: chargePower,
            odometer_km: odometer,
            gear: 1, // P

            // Doors - open one at a time in sequence
            door_front_left_open: false,
            door_front_right_open: false,
            door_rear_left_open: false,
            door_rear_right_open: false,
            frunk_open: false,
            trunk_open: false,

            // Lights - cycle through different combinations
            headlights: false,
            high_beams: false,
            fog_lights: false,
            turn_left: false,
            turn_right: false,
            hazard: false,
            brake_pressed: false,

            // Security
            locked: true,
            sentry_mode: false,
            sentry_alert: false,

            // Charging
            charging_cable: false,
            charging: false,
            charging_port: false,

            // Night mode
            night_mode: false,

            // Other
            charge_status: 0,
            brightness: 0.5,
            autopilot: 0,
            last_update_ms: Date.now()
        };
        steps[stepIndex](simulatedState);

        // Update dashboard using the main updateDashboard function
        updateDashboard(simulatedState);

    }, 500); // UI refresh, state changes every stepDurationMs

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

    // Load auto brightness preference
    const savedAutoBrightness = localStorage.getItem('autoBrightness');
    if (savedAutoBrightness !== null) {
        autoBrightnessEnabled = savedAutoBrightness === 'true';
    }

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

    // Keep screen awake while on dashboard
    await keepScreenAwake();

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

    // Initialize HUD mode toggle
    initHUDMode();

    // Handle app going to background - close completely to save battery
    if (usingCapacitor) {
        setupBackgroundHandler();
    }

    // Optimize battery consumption
    optimizeBatteryConsumption();

    // Config button
    document.getElementById('config-btn').addEventListener('click', switchToConfig);
    const autoBrightnessBtn = document.getElementById('auto-brightness-btn');
    if (autoBrightnessBtn) {
        autoBrightnessBtn.addEventListener('click', toggleAutoBrightness);
    }

    // Connect to BLE
    await connectBLE();
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
