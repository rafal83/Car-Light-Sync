#include "tesla_can.h"
#include <string.h>

void decode_vehicle_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 8) {
        // Byte 0: État général
        state->ignition_on = (frame->data[0] & 0x01) != 0;
        
        // Position du sélecteur (bits 4-5 du byte 1)
        state->gear = (frame->data[1] >> 4) & 0x03;
        
        state->last_update = frame->timestamp;
    }
}

void decode_speed(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 4) {
        // Vitesse encodée sur 2 bytes (little endian), en 0.01 km/h
        uint16_t speed_raw = (frame->data[1] << 8) | frame->data[0];
        state->speed_kmh = speed_raw * 0.01f;
        
        state->last_update = frame->timestamp;
    }
}

void decode_door_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 2) {
        // Byte 0: État des portes (1 bit par porte)
        state->door_fl = (frame->data[0] & 0x01) != 0;
        state->door_fr = (frame->data[0] & 0x02) != 0;
        state->door_rl = (frame->data[0] & 0x04) != 0;
        state->door_rr = (frame->data[0] & 0x08) != 0;
        
        state->last_update = frame->timestamp;
    }
}

void decode_lock_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 1) {
        // Byte 0, bit 0: Véhicule verrouillé
        state->locked = (frame->data[0] & 0x01) != 0;
        
        state->last_update = frame->timestamp;
    }
}

void decode_window_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 4) {
        // Chaque fenêtre encodée sur 1 byte (0-100%)
        state->window_fl = frame->data[0];
        state->window_fr = frame->data[1];
        state->window_rl = frame->data[2];
        state->window_rr = frame->data[3];
        
        state->last_update = frame->timestamp;
    }
}

void decode_trunk_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 1) {
        // Byte 0: État coffre et frunk
        state->trunk_open = (frame->data[0] & 0x01) != 0;
        state->frunk_open = (frame->data[0] & 0x02) != 0;
        
        state->last_update = frame->timestamp;
    }
}

void decode_light_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 2) {
        // Byte 0: État des lumières
        state->headlights_on = (frame->data[0] & 0x01) != 0;
        state->high_beams_on = (frame->data[0] & 0x02) != 0;
        state->fog_lights_on = (frame->data[0] & 0x04) != 0;
        
        state->last_update = frame->timestamp;
    }
}

void decode_brake_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 1) {
        // Byte 0, bit 0: Frein appuyé
        state->brake_pressed = (frame->data[0] & 0x01) != 0;
        
        state->last_update = frame->timestamp;
    }
}

void decode_turn_signal(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 1) {
        // Byte 0: État clignotants (0=off, 1=left, 2=right, 3=hazard)
        state->turn_signal = frame->data[0] & 0x03;
        
        state->last_update = frame->timestamp;
    }
}

void decode_charge_status(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 4) {
        // Byte 0: État de charge
        state->charging = (frame->data[0] & 0x01) != 0;
        
        // Byte 1: Pourcentage de charge
        state->charge_percent = frame->data[1];
        
        // Bytes 2-3: Puissance de charge en 0.1 kW
        uint16_t power_raw = (frame->data[3] << 8) | frame->data[2];
        state->charge_power_kw = power_raw * 0.1f;
        
        state->last_update = frame->timestamp;
    }
}

void decode_battery_voltage(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 2) {
        // Voltage encodé sur 2 bytes en 0.01V
        uint16_t voltage_raw = (frame->data[1] << 8) | frame->data[0];
        state->battery_voltage = voltage_raw * 0.01f;
        
        state->last_update = frame->timestamp;
    }
}

void decode_blindspot(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 1) {
        // Byte 0: Détection angle mort
        state->blindspot_left = (frame->data[0] & 0x01) != 0;
        state->blindspot_right = (frame->data[0] & 0x02) != 0;
        
        state->last_update = frame->timestamp;
    }
}

void decode_night_mode(const can_frame_t* frame, vehicle_state_t* state) {
    if (frame->dlc >= 1) {
        // Byte 0: Mode nuit basé sur capteur de lumière ambiante
        // 0 = jour (>500 lux), 1 = nuit (<100 lux)
        state->night_mode = (frame->data[0] & 0x01) != 0;
        
        state->last_update = frame->timestamp;
    }
}

void process_can_frame(const can_frame_t* frame, vehicle_state_t* state) {
    switch (frame->id) {
        case CAN_ID_VEHICLE_STATUS:
            decode_vehicle_status(frame, state);
            break;
            
        case CAN_ID_SPEED:
            decode_speed(frame, state);
            break;
            
        case CAN_ID_DOOR_STATUS:
            decode_door_status(frame, state);
            break;
            
        case CAN_ID_LOCK_STATUS:
            decode_lock_status(frame, state);
            break;
            
        case CAN_ID_WINDOW_STATUS:
            decode_window_status(frame, state);
            break;
            
        case CAN_ID_TRUNK_STATUS:
            decode_trunk_status(frame, state);
            break;
            
        case CAN_ID_LIGHT_STATUS:
            decode_light_status(frame, state);
            break;
            
        case CAN_ID_BRAKE_STATUS:
            decode_brake_status(frame, state);
            break;
            
        case CAN_ID_TURN_SIGNAL:
            decode_turn_signal(frame, state);
            break;
            
        case CAN_ID_CHARGE_STATUS:
            decode_charge_status(frame, state);
            break;
            
        case CAN_ID_BATTERY_VOLTAGE:
            decode_battery_voltage(frame, state);
            break;
            
        case CAN_ID_BLINDSPOT:
            decode_blindspot(frame, state);
            break;
            
        case CAN_ID_NIGHT_MODE:
            decode_night_mode(frame, state);
            break;
            
        default:
            // Frame non gérée
            break;
    }
}
