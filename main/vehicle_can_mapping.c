#include "vehicle_can_mapping.h"

#include "config_manager.h" // pour can_event_type_t + can_event_trigger
#include "esp_log.h"
#include "espnow_link.h"
#include "vehicle_can_unified.h"
#include "vehicle_can_unified_config.h"

#include <string.h>

// Helper latch -> ouvert/fermé
// IRAM_ATTR: appelé dans callback CAN temps-réel
static inline int IRAM_ATTR is_latch_open(float raw) {
  int v = (int)(raw + 0.5f);
  // DBC mapping (typique Tesla) :
  // 2 = LATCH_CLOSED
  // 0 = SNA, les autres = ouvert / en mouvement / défaut
  if (v == 2)
    return 0; // closed
  if (v == 0)
    return 0; // SNA
  return 1;
}

// Historique de portes pour calculer doors_open_count
static uint8_t s_door_front_left_open  = 0;
static uint8_t s_door_rear_left_open   = 0;
static uint8_t s_door_front_right_open = 0;
static uint8_t s_door_rear_right_open  = 0;

// Helpers pour envoyer l'Ã©tat ESP-NOW uniquement si une valeur a changÃ©
#define UPDATE_AND_SEND_U8(field, value, state_ptr)                \
  do {                                                             \
    uint8_t _nv = (uint8_t)(value);                                \
    if ((field) != _nv) {                                          \
      (field) = _nv;                                               \
      espnow_link_send_vehicle_state((state_ptr));                 \
    } else {                                                       \
      (field) = _nv;                                               \
    }                                                              \
  } while (0)

#define UPDATE_AND_SEND_I8(field, value, state_ptr)                \
  do {                                                             \
    int8_t _nv = (int8_t)(value);                                  \
    if ((field) != _nv) {                                          \
      (field) = _nv;                                               \
      espnow_link_send_vehicle_state((state_ptr));                 \
    } else {                                                       \
      (field) = _nv;                                               \
    }                                                              \
  } while (0)

#define UPDATE_AND_SEND_FLOAT(field, value, state_ptr)             \
  do {                                                             \
    float _nv = (float)(value);                                    \
    if ((field) != _nv) {                                          \
      (field) = _nv;                                               \
      espnow_link_send_vehicle_state((state_ptr));                 \
    } else {                                                       \
      (field) = _nv;                                               \
    }                                                              \
  } while (0)

// IRAM_ATTR: recalcul rapide dans callback CAN
static void IRAM_ATTR recompute_doors_open(vehicle_state_t *state) {
  if (!state)
    return;
  uint8_t c = 0;
  c += s_door_front_left_open ? 1 : 0;
  c += s_door_rear_left_open ? 1 : 0;
  c += s_door_front_right_open ? 1 : 0;
  c += s_door_rear_right_open ? 1 : 0;
  if (state->doors_open_count != c) {
    state->doors_open_count = c;
    espnow_link_send_vehicle_state(state);
  }
}

// ============================================================================
// Mapping signaux -> vehicle_state_t
// ============================================================================

// Callback pour les événements de scroll wheel
static vehicle_wheel_scroll_callback_t s_wheel_scroll_callback = NULL;

void vehicle_can_set_wheel_scroll_callback(vehicle_wheel_scroll_callback_t callback) {
  s_wheel_scroll_callback = callback;
}

// IRAM_ATTR défini dans le header
void vehicle_state_apply_signal(const can_message_def_t *msg, const can_signal_def_t *sig, float value, uint8_t bus_id, vehicle_state_t *state) {
  if (!msg || !sig || !state)
    return;

  uint32_t id      = msg->id;
  const char *name = sig->name ? sig->name : "";

  // Le bus_id est maintenant disponible pour les logs
  (void)bus_id; // Éviter warning unused pour l'instant

  if ((id == 0x3C2) && (bus_id == 1)) {
    if (strcmp(name, "VCLEFT_swcLeftScrollTicks") == 0) {
      if (value != 21 && value != 0) {
        UPDATE_AND_SEND_U8(state->left_btn_scroll_up, value > 0 ? 1 : 0, state);
        UPDATE_AND_SEND_U8(state->left_btn_scroll_down, value < 0 ? 1 : 0, state);
      }
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftDoublePress") == 0) {
      UPDATE_AND_SEND_U8(state->left_btn_dbl_press, value > 0.5f ? 1 : 0, state);
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftPressed") == 0) {
      UPDATE_AND_SEND_U8(state->left_btn_press, value == 2 ? 1 : 0, state);
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftTiltLeft") == 0) {
      UPDATE_AND_SEND_U8(state->left_btn_tilt_left, value == 2 ? 1 : 0, state);
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftTiltRight") == 0) {
      UPDATE_AND_SEND_U8(state->left_btn_tilt_right, value == 2 ? 1 : 0, state);
      return;
    } else if (strcmp(name, "VCLEFT_swcRightScrollTicks") == 0) {

      if (value != 21 && value != 0) {
        // Appeler le callback immédiatement avec la valeur du scroll
        // value > 0 = scroll up, value < 0 = scroll down
        if (s_wheel_scroll_callback) {
          s_wheel_scroll_callback(value, state);
        }
      }
      return;
    } else if (strcmp(name, "VCLEFT_swcRightDoublePress") == 0) {
      UPDATE_AND_SEND_U8(state->right_btn_dbl_press, value > 0.5f ? 1 : 0, state);
      return;
    } else if (strcmp(name, "VCLEFT_swcRightPressed") == 0) {
      UPDATE_AND_SEND_U8(state->right_btn_press, value == 2 ? 1 : 0, state);
      return;
    } else if (strcmp(name, "VCLEFT_swcRightTiltLeft") == 0) {
      UPDATE_AND_SEND_U8(state->right_btn_tilt_left, value == 2 ? 1 : 0, state);
      return;
    } else if (strcmp(name, "VCLEFT_swcRightTiltRight") == 0) {
      UPDATE_AND_SEND_U8(state->right_btn_tilt_right, value == 2 ? 1 : 0, state);
      return;
    }
    return;
  }
  // ---------------------------------------------------------------------
  // Vitesse véhicule : ID257DIspeed / DI_vehicleSpeed (kph)
  // ---------------------------------------------------------------------
  if (id == 0x257) {
    if (strcmp(name, "DI_vehicleSpeed") == 0) {
      UPDATE_AND_SEND_FLOAT(state->speed_kph, value, state); // déjà en kph
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Etat drive system : ID118DriveSystemStatus
  //  - DI_gear
  //  - DI_brakePedalState
  // ---------------------------------------------------------------------
  if (id == 0x118) {
    if (strcmp(name, "DI_gear") == 0) {
      UPDATE_AND_SEND_I8(state->gear, (int8_t)(value + 0.5f), state);
      return;
    }
    if (strcmp(name, "DI_accelPedalPos") == 0) {
      UPDATE_AND_SEND_I8(state->accel_pedal_pos, (int8_t)(value + 0.5f), state);
      return;
    }
    return;
  }
  if (id == 0x39D) {
    if (strcmp(name, "IBST_driverBrakeApply") == 0) {
      // 0=OFF, 1=ON
      UPDATE_AND_SEND_U8(state->brake_pressed, (value == 2) ? 1 : 0, state);
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Odomètre
  // ---------------------------------------------------------------------
  if (id == 0x3F3) {
    if (strcmp(name, "UI_odometer") == 0) {
      UPDATE_AND_SEND_FLOAT(state->odometer_km, value, state);
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Portes : ID102VCLEFT_doorStatus & ID103VCRIGHT_doorStatus
  // ---------------------------------------------------------------------
  if (id == 0x102) { // gauche
    if (strcmp(name, "VCLEFT_frontLatchStatus") == 0) {
      s_door_front_left_open      = is_latch_open(value);
      UPDATE_AND_SEND_U8(state->door_front_left_open, s_door_front_left_open, state);
      recompute_doors_open(state);
      return;
    }
    if (strcmp(name, "VCLEFT_rearLatchStatus") == 0) {
      s_door_rear_left_open      = is_latch_open(value);
      UPDATE_AND_SEND_U8(state->door_rear_left_open, s_door_rear_left_open, state);
      recompute_doors_open(state);
      return;
    }
    return;
  }

  if (id == 0x103) { // droite + trunk
    if (strcmp(name, "VCRIGHT_frontLatchStatus") == 0) {
      s_door_front_right_open      = is_latch_open(value);
      UPDATE_AND_SEND_U8(state->door_front_right_open, s_door_front_right_open, state);
      recompute_doors_open(state);
      return;
    }
    if (strcmp(name, "VCRIGHT_rearLatchStatus") == 0) {
      s_door_rear_right_open      = is_latch_open(value);
      UPDATE_AND_SEND_U8(state->door_rear_right_open, s_door_rear_right_open, state);
      recompute_doors_open(state);
      return;
    }
    if (strcmp(name, "VCRIGHT_trunkLatchStatus") == 0) {
      UPDATE_AND_SEND_U8(state->trunk_open, is_latch_open(value) ? 1 : 0, state);
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Frunk : ID2E1VCFRONT_status / VCFRONT_frunkLatchStatus
  // ---------------------------------------------------------------------
  if (id == 0x2E1) {
    if (strcmp(name, "VCFRONT_frunkLatchStatus") == 0) {
      if ((value > 0) && (value <= 5)) {
        UPDATE_AND_SEND_U8(state->frunk_open, (value == 1 /* || value == 5*/) ? 1 : 0, state);
      }
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Lumières + clignos + fog : ID3F5VCFRONT_lighting
  // ---------------------------------------------------------------------
  if (id == 0x3F5) {
    // Clignotants : on stocke seulement l'état binaire, les events
    // sont gérés plus bas
    if (strcmp(name, "VCFRONT_indicatorLeftRequest") == 0) {
      int v            = (int)(value + 0.5f);
      UPDATE_AND_SEND_U8(state->turn_left, (v >= 1) ? 1 : 0, state);
      if (state->turn_left && state->turn_right) {
        UPDATE_AND_SEND_U8(state->hazard, 1, state);
      } else {
        UPDATE_AND_SEND_U8(state->hazard, 0, state);
      }
      return;
    }
    if (strcmp(name, "VCFRONT_indicatorRightRequest") == 0) {
      int v             = (int)(value + 0.5f);
      UPDATE_AND_SEND_U8(state->turn_right, (v >= 1) ? 1 : 0, state);
      if (state->turn_left && state->turn_right) {
        UPDATE_AND_SEND_U8(state->hazard, 1, state);
      } else {
        UPDATE_AND_SEND_U8(state->hazard, 0, state);
      }
      return;
    }

    // Low beams -> headlights_on
    if (strcmp(name, "VCFRONT_lowBeamLeftStatus") == 0 || strcmp(name, "VCFRONT_lowBeamRightStatus") == 0) {
      UPDATE_AND_SEND_U8(state->headlights, (int)(value + 0.5f), state);
      return;
    }

    // High beams
    if (strcmp(name, "VCFRONT_highBeamLeftStatus") == 0 || strcmp(name, "VCFRONT_highBeamRightStatus") == 0) {
      UPDATE_AND_SEND_U8(state->high_beams, (int)(value + 0.5f), state);
      return;
    }

    // Feux de brouillard
    if (strcmp(name, "VCFRONT_fogLeftStatus") == 0 || strcmp(name, "VCFRONT_fogRightStatus") == 0) {
      UPDATE_AND_SEND_U8(state->fog_lights, (int)(value + 0.5f), state);
      return;
    }

    if (strcmp(name, "VCFRONT_switchLightingBrightness") == 0) {
      UPDATE_AND_SEND_FLOAT(state->brightness, value, state); // 0-127
      return;
    }

    return;
  }

  // ---------------------------------------------------------------------
  // Blind spot : ID399DAS_status
  //  - DAS_blindSpotRearLeft / Right : 0=NO_WARNING, 1/2=warning,
  //  3=SNA
  // ---------------------------------------------------------------------
  if (id == 0x399) {
    if (strcmp(name, "DAS_autopilotState") == 0) {
      // ESP_LOGI(TAG_CAN, "%s %f", name, value); // 2 nada, 3 // FSD,
      UPDATE_AND_SEND_U8(state->autopilot, value, state);
      return;
    }
    if (strcmp(name, "DAS_autopilotHandsOnState") == 0) {
      /*
        0 "LC_HANDS_ON_NOT_REQD"
        1 "LC_HANDS_ON_REQD_DETECTED"
        2 "LC_HANDS_ON_REQD_NOT_DETECTED"
        3 "LC_HANDS_ON_REQD_VISUAL"
        4 "LC_HANDS_ON_REQD_CHIME_1"
        5 "LC_HANDS_ON_REQD_CHIME_2"
        6 "LC_HANDS_ON_REQD_SLOWING"
        7 "LC_HANDS_ON_REQD_STRUCK_OUT"
        8 "LC_HANDS_ON_SUSPENDED" ;
        9 "LC_HANDS_ON_REQD_ESCALATED_CHIME_1"
        10 "LC_HANDS_ON_REQD_ESCALATED_CHIME_2"
        15 "LC_HANDS_ON_SNA"
      */
      UPDATE_AND_SEND_U8(state->autopilot_alert_lv1, value == 1 ? 1 : 0, state);
      // state->autopilot_alert_lv2 = value >= 2 && value <= 5 ? 1 : 0;
      UPDATE_AND_SEND_U8(state->autopilot_alert_lv3, value >= 6 && value <= 10 ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_laneDepartureWarning") == 0) {
      // 1 "LEFT_WARNING" 3 "LEFT_WARNING_SEVERE" 0 "NONE" 2 "RIGHT_WARNING" 4
      // "RIGHT_WARNING_SEVERE"
      UPDATE_AND_SEND_U8(state->lane_departure_left_lv1, value == 1 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->lane_departure_left_lv2, value == 3 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->lane_departure_right_lv1, value == 2 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->lane_departure_right_lv2, value == 4 ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_sideCollisionWarning") == 0) {
      // ESP_LOGI(TAG_CAN, "%s %f", name, value);
      UPDATE_AND_SEND_U8(state->side_collision_left, value == 1 || value == 3 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->side_collision_right, value == 2 || value == 3 ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_forwardCollisionWarning") == 0) {
      UPDATE_AND_SEND_U8(state->forward_collision, value == 1 ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_blindSpotRearLeft") == 0) {
      int v                       = (int)(value + 0.5f);
      UPDATE_AND_SEND_U8(state->blindspot_left, (v > 0) ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->blindspot_left_alert, (v > 1) ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_blindSpotRearRight") == 0) {
      int v                        = (int)(value + 0.5f);
      UPDATE_AND_SEND_U8(state->blindspot_right, (v > 0) ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->blindspot_right_alert, (v > 1) ? 1 : 0, state);
      return;
    }
    return;
  }
  if (id == 0x313) {
    if (strcmp(name, "UI_speedLimit") == 0) {
      UPDATE_AND_SEND_FLOAT(state->speed_threshold, value, state);
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // SOC : ID292BMS_SOC / BMS_socUI
  // ---------------------------------------------------------------------
  if (id == 0x292) {
    if (strcmp(name, "BMS_socUI") == 0) {
      UPDATE_AND_SEND_FLOAT(state->soc_percent, value, state);
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Batterie HV : ID132HVBattAmpVolt / BattVoltage132
  // ---------------------------------------------------------------------
  if (id == 0x132) {
    if (strcmp(name, "BattVoltage132") == 0) {
      UPDATE_AND_SEND_FLOAT(state->battery_voltage_HV, value, state);
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Batterie HV : ID261_12vBattStatus / v12vBattVoltage261
  // ---------------------------------------------------------------------
  if (id == 0x261) {
    if (strcmp(name, "v12vBattVoltage261") == 0) {
      UPDATE_AND_SEND_FLOAT(state->battery_voltage_LV, value, state);
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Etat BMS / charge : ID204PCS_chgStatus
  // ---------------------------------------------------------------------
  if (id == 0x204) {
    if (strcmp(name, "PCS_hvChargeStatus") == 0) {
      // 0 = not charging, 2 = charging (d'après le DBC
      // opendbc)
      int v = (int)(value + 0.5f);
      if (v == 2) {
        UPDATE_AND_SEND_U8(state->charging, 1, state);
      } else if (v == 0) {
        UPDATE_AND_SEND_U8(state->charging, 0, state);
      }
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Night mode : ID273UI_vehicleControl
  // ---------------------------------------------------------------------
  if (id == 0x273) {
    if (strcmp(name, "UI_ambientLightingEnabled") == 0) {
      UPDATE_AND_SEND_U8(state->night_mode, (value > 0.5f) ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "UI_alarmEnabled") == 0) {
      // state->sentry_mode = (value > 0.5f) ? 1 : 0;
      return;
    }
    if (strcmp(name, "UI_intrusionSensorOn") == 0) {
      // state->sentry_mode = (value > 0.5f) ? 1 : 0;
      return;
    }
    if (strcmp(name, "UI_lockRequest") == 0) {
      int v = (int)(value + 0.5f);
      // 1 = LOCK, 2 = UNLOCK (voir mapping dans JSON)
      if (v == 1) {
        UPDATE_AND_SEND_U8(state->locked, 1, state);
      } else if (v == 2) {
        UPDATE_AND_SEND_U8(state->locked, 0, state);
      }
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Sentry mode : ID284UIvehicleModes / UIsentryMode284
  // ---------------------------------------------------------------------
  if (id == 0x284) {
    if (strcmp(name, "CP_chargeCablePresent") == 0) {
      int v = (int)(value + 0.5f);
      // 1 = NOT_CONNECTED, 2 = CONNECTED (voir mapping
      // dans JSON)
      if (v == 1) {
        UPDATE_AND_SEND_U8(state->charging_cable, 0, state);
      } else if (v == 2) {
        UPDATE_AND_SEND_U8(state->charging_cable, 1, state);
      }
      return;
    }
    return;
  }

  if (id == 0x212) {
    // 2 "BMS_ABOUT_TO_CHARGE"
    // 4 "BMS_CHARGE_COMPLETE"
    // 5 "BMS_CHARGE_STOPPED"
    // 3 "BMS_CHARGING"
    // 0 "BMS_DISCONNECTED"
    // 1 "BMS_NO_POWER"
    if (strcmp(name, "BMS_uiChargeStatus") == 0) {
      UPDATE_AND_SEND_U8(state->charge_status, value, state);
      return;
    } else if (strcmp(name, "BMS_chgPowerAvailable") == 0) {
      UPDATE_AND_SEND_FLOAT(state->charge_power_kw, value > 255 ? 0 : value, state);
      return;
    }
    return;
  }

  if (id == 0x25D) {
    if (strcmp(name, "CP_chargeDoorOpen") == 0) {
      UPDATE_AND_SEND_U8(state->charging_port, (value > 0.5f) ? 1 : 0, state);
      return;
    }
    return;
  }

  // Fallback : signaux non mappés -> ignorés au niveau état haut niveau
}
