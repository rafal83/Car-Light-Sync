
#ifndef VEHICLE_CAN_UNIFIED_H
#define VEHICLE_CAN_UNIFIED_H

#include "vehicle_can_unified_config.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Trame CAN brute (TWAI, Commander, etc. peuvent la remplir)
typedef struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
  uint32_t timestamp_ms;
} can_frame_t;

// Etat "métier" du véhicule : à enrichir selon les besoins
typedef struct {
  // Dynamique de base
  float speed_kph;
  float speed_threshold;
  int8_t gear;           // P=0, D=1, R=2... (à mapper selon Tesla)
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

  // Meta
  uint32_t last_update_ms;
} vehicle_state_t;

// Initialise l'historique interne des signaux
void vehicle_can_unified_init(void);

// Pipeline unique : une trame CAN brute => mise à jour éventuelle de l'état +
// events
void vehicle_can_process_frame_static(const can_frame_t *frame, vehicle_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // VEHICLE_CAN_UNIFIED_H
