
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

// Etat "métier" du véhicule : à enrichir selon les besoins
typedef struct {
  // Dynamique de base
  float speed_kph;
  float speed_threshold;
  int8_t gear; // P=0, D=1, R=2... (à mapper selon Tesla)
  uint8_t accel_pedal_pos;
  uint8_t brake_pressed; // 0/1

  // Verrouillage / ouvertures
  uint8_t locked;           // 0/1
  uint8_t doors_open_count; // nombre de portes ouvertes
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

  // Lumières
  uint8_t turn_left;  // 0/1
  uint8_t turn_right; // 0/1
  uint8_t hazard;     // 0/1
  uint8_t headlights; // 0/1
  uint8_t high_beams; // 0/1
  uint8_t fog_lights; // 0/1

  // Energie
  float soc_percent;      // niveau de batterie (%)
  uint8_t charging_cable; // 0/1
  uint8_t charging;       // 0/1
  uint8_t charge_status;
  float charge_power_kw;
  uint8_t charging_port;

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
  uint8_t autopilot_alert_lv3;

  // Meta
  uint32_t last_update_ms;
} vehicle_state_t;

// Structure compacte pour BLE dashboard (bit-packed pour réduire la taille)
// Total: 28 bytes (vs 86 bytes pour vehicle_state_t) - tient dans un seul paquet BLE !
typedef struct __attribute__((packed)) {
  // Floats convertis en int16 (résolution réduite mais suffisante pour affichage)
  int16_t speed_kph_x10;           // vitesse * 10 (ex: 1234 = 123.4 km/h)
  int16_t soc_percent_x10;         // batterie % * 10
  int16_t charge_power_kw_x10;     // puissance charge * 10
  int16_t battery_voltage_LV_x10;  // tension 12V * 10
  int16_t battery_voltage_HV_x10;  // tension HV * 10
  uint32_t odometer_km;            // odomètre (uint32 = max 4 294 967 km)
  uint8_t brightness;              // 0-100%

  // Valeurs uint8 non-booléennes
  int8_t gear;                     // P=0, D=1, R=2...
  uint8_t accel_pedal_pos;         // 0-100%
  uint8_t charge_status;           // statut charge
  uint8_t autopilot;               // état autopilot

  // Bit-packed booleans (27 bits = 4 bytes) - boutons volant et doors_open_count retirés
  // Byte 0 (8 bits): doors & locks
  uint8_t flags0;  // bits: locked, door_FL, door_RL, door_FR, door_RR, frunk, trunk, brake_pressed

  // Byte 1 (8 bits): lights
  uint8_t flags1;  // bits: turn_left, turn_right, hazard, headlights, high_beams, fog_lights, unused, unused

  // Byte 2 (8 bits): charging & sentry
  uint8_t flags2;  // bits: charging_cable, charging, charging_port, sentry_mode, sentry_alert, unused, unused, unused

  // Byte 3 (11 bits): safety & autopilot
  uint8_t flags3;  // bits: blindspot_L, blindspot_R, blindspot_L_alert, blindspot_R_alert, side_collision_L, side_collision_R, forward_collision, night_mode
  uint8_t flags4;  // bits: lane_dep_L_lv1, lane_dep_L_lv2, lane_dep_R_lv1, lane_dep_R_lv2, autopilot_alert_lv1, autopilot_alert_lv2, autopilot_alert_lv3, unused

  // Meta
  uint32_t last_update_ms;
} vehicle_state_ble_t;

// Convertit vehicle_state_t en format compact BLE
void vehicle_state_to_ble(const vehicle_state_t *src, vehicle_state_ble_t *dst);

// Initialise l'historique interne des signaux
void vehicle_can_unified_init(void);

// Pipeline unique : une trame CAN brute => mise à jour éventuelle de l'état +
// events
// IRAM_ATTR: Main entry point for CAN frame decoding, called for every frame (~2000 times/s)
void IRAM_ATTR vehicle_can_process_frame_static(const can_frame_t *frame, vehicle_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // VEHICLE_CAN_UNIFIED_H
