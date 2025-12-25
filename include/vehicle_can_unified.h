
#ifndef VEHICLE_CAN_UNIFIED_H
#define VEHICLE_CAN_UNIFIED_H

#include "vehicle_can_unified_config.h"

#include <stdint.h>
#include "esp_attr.h"  // For IRAM_ATTR

#ifdef __cplusplus
extern "C" {
#endif

// Trame CAN brute (TWAI, Commander, etc. peuvent la remplir)
typedef struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
  uint32_t timestamp_ms;
  uint8_t bus_id; // ID du bus CAN (0=CAN0, 1=CAN1, etc.)
} can_frame_t;

// Vehicle "business" state: to be enriched as needed
typedef struct {
  // Dynamique de base
  float speed_kph;
  float speed_limit;
  int8_t pedal_map;
  int8_t gear; 
  uint8_t accel_pedal_pos;
  uint8_t brake_pressed; // 0/1

  // Verrouillage / ouvertures
  uint8_t locked;           // 0/1
  uint8_t door_front_left_open;
  uint8_t door_rear_left_open;
  uint8_t door_front_right_open;
  uint8_t door_rear_right_open;

  uint8_t frunk_open; // 0/1
  uint8_t trunk_open; // 0/1

  // Bouttons volant
  uint8_t left_btn_scroll_up;
  uint8_t left_btn_scroll_down;
  uint8_t left_btn_press;
  uint8_t left_btn_dbl_press;
  uint8_t left_btn_tilt_right;
  uint8_t left_btn_tilt_left;

  uint8_t right_btn_scroll_up;
  uint8_t right_btn_scroll_down;
  uint8_t right_btn_press;
  uint8_t right_btn_dbl_press;
  uint8_t right_btn_tilt_right;
  uint8_t right_btn_tilt_left;

  // Lights
  uint8_t turn_left;  // 0/1
  uint8_t turn_right; // 0/1
  uint8_t hazard;     // 0/1
  uint8_t headlights; // 0/1
  uint8_t high_beams; // 0/1
  uint8_t fog_lights; // 0/1

  // Energy
  float soc_percent;      // niveau de batterie (%)
  float pack_energy;
  float remaining_energy;
  uint8_t charging_cable; // 0/1
  uint8_t charging;       // 0/1
  uint8_t charge_status;
  float charge_power_kw;
  uint8_t charging_port;
  float rear_power;
  float rear_power_limit;
  float front_power;
  float front_power_limit;
  float max_regen;
  uint8_t train_type; // 1 RWD, 0 AWD

  // Divers
  uint8_t sentry_mode;  // 0/1
  uint8_t sentry_alert; // 0/1

  float battery_voltage_LV;
  float battery_voltage_HV;
  float odometer_km;
  uint8_t blindspot_left;
  uint8_t blindspot_right;
  uint8_t blindspot_left_alert;
  uint8_t blindspot_right_alert;
  uint8_t side_collision_left;
  uint8_t side_collision_right;

  uint8_t lane_departure_left_lv1;
  uint8_t lane_departure_left_lv2;
  uint8_t lane_departure_right_lv1;
  uint8_t lane_departure_right_lv2;

  uint8_t forward_collision;
  
  uint8_t night_mode;
  float brightness;
  uint8_t autopilot;
  uint8_t autopilot_alert_lv1;
  uint8_t autopilot_alert_lv2;
  uint8_t cruise;

  // Meta
  uint32_t last_update_ms;
} vehicle_state_t;

// Structure compacte BLE pour mode CONFIG
// Total: ~22 bytes
typedef struct __attribute__((packed)) {
  // Driving dynamics
  uint16_t rear_power_limit_kw_x10;       // rear motor max power * 10
  uint16_t front_power_limit_kw_x10;      // front motor max power * 10
  uint16_t max_regen_x10;      // regen power * 10

  uint8_t flags0;  // bits: train type

  // Meta
  uint32_t last_update_ms;
} vehicle_state_ble_config_t;

// Compact BLE structure for DRIVE mode (driving)
// Focus: speed, power, driving assistance, safety
// Total: ~22 bytes
typedef struct __attribute__((packed)) {
  // Driving dynamics
  uint8_t speed_kph;           // speed * 10 (absolute value)
  int16_t rear_power_kw_x10;       // rear motor power * 10
  int16_t front_power_kw_x10;      // front motor power * 10
  uint8_t soc_percent;         // battery %
  uint32_t odometer_km;            // odometer (uint32 = max 4,294,967 km)

  // Valeurs uint8
  int8_t gear;                     // P=1, R=2, N=3, D=4
  int8_t pedal_map;                // driving mode (Chill/Standard/Sport)
  uint8_t accel_pedal_pos;         // 0-100%
  uint8_t brightness;              // 0-100%
  uint8_t autopilot;               // autopilot state

  // Bit-packed flags pour sécurité et indicateurs
  // Byte 0 (8 bits): turn signals & brake
  uint8_t flags0;  // bits: turn_left, turn_right, hazard, brake_pressed, high_beams, headlights, fog_lights, unused

  // Byte 1 (8 bits): blindspots & collisions
  uint8_t flags1;  // bits: blindspot_L, blindspot_R, blindspot_L_alert, blindspot_R_alert, side_collision_L, side_collision_R, forward_collision, night_mode

  // Byte 2 (8 bits): autopilot alerts & lane departure
  uint8_t flags2;  // bits: lane_dep_L_lv1, lane_dep_L_lv2, lane_dep_R_lv1, lane_dep_R_lv2, autopilot_alert_lv1, autopilot_alert_lv2, unused, unused

  // Meta
  uint32_t last_update_ms;
} vehicle_state_ble_drive_t;

// Compact BLE structure for PARK mode (parking)
// Focus: battery, charging, doors, static safety
// Total: ~20 bytes
typedef struct __attribute__((packed)) {
  // Energy
  uint8_t soc_percent;         // battery %
  int16_t charge_power_kw_x10;     // charge power * 10
  uint8_t battery_voltage_LV_x10;  // 12V voltage * 10
  int16_t battery_voltage_HV_x10;  // HV voltage * 10
  uint32_t odometer_km;            // odometer

  // Valeurs uint8
  uint8_t charge_status;           // charge status
  uint8_t brightness;              // 0-100%

  // Bit-packed flags
  // Byte 0 (8 bits): doors & locks
  uint8_t flags0;  // bits: locked, door_FL, door_RL, door_FR, door_RR, frunk, trunk, brake_pressed

  // Byte 1 (8 bits): lights
  uint8_t flags1;  // bits: turn_left, turn_right, hazard, headlights, high_beams, fog_lights, unused, unused

  // Byte 2 (8 bits): charging & sentry
  uint8_t flags2;  // bits: charging_cable, charging, charging_port, sentry_mode, sentry_alert, night_mode, unused, unused

  // Meta
  uint32_t last_update_ms;
} vehicle_state_ble_park_t;

// Convertit vehicle_state_t en format compact BLE Config
void vehicle_state_to_ble_config(const vehicle_state_t *src, vehicle_state_ble_config_t *dst);

// Convertit vehicle_state_t en format compact BLE Drive
void vehicle_state_to_ble_drive(const vehicle_state_t *src, vehicle_state_ble_drive_t *dst);

// Convertit vehicle_state_t en format compact BLE Park
void vehicle_state_to_ble_park(const vehicle_state_t *src, vehicle_state_ble_park_t *dst);

// Initializes internal signal history
void vehicle_can_unified_init(void);

// Single pipeline: raw CAN frame => potential state update +
// events
// IRAM_ATTR: Main entry point for CAN frame decoding, called for every frame (~2000 times/s)
void IRAM_ATTR vehicle_can_process_frame_static(const can_frame_t *frame, vehicle_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // VEHICLE_CAN_UNIFIED_H



