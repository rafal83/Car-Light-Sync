#include "vehicle_can_mapping.h"

#include "config_manager.h" // pour can_event_type_t + can_event_trigger
#include "esp_log.h"
#include "vehicle_can_unified.h"
#include "vehicle_can_unified_config.h"

#include <string.h>

// Helper latch -> ouvert/fermé
static inline int is_latch_open(float raw) {
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
static uint8_t s_door_front_left_open   = 0;
static uint8_t s_door_rear_left_open    = 0;
static uint8_t s_door_front_right_open  = 0;
static uint8_t s_door_rear_right_open   = 0;

// Listes d'IDs CAN utilisés par le mapping (standard 11 bits)
// Commun à tous les bus pour l'instant; les tableaux BODY/CHASSIS sont prêts pour
// des spécialisations futures.
static const uint32_t CAN_IDS_COMMON[]  = {0x102, 0x103, 0x118, 0x132, 0x204, 0x212, 0x257, 0x25D, 0x261, 0x273, 0x284, 0x292, 0x2E1, 0x334, 0x399, 0x3C2, 0x3F3, 0x3F5};
static const uint32_t CAN_IDS_BODY[]    = {0}; // à remplir si nécessaire
static const uint32_t CAN_IDS_CHASSIS[] = {0}; // à remplir si nécessaire

// Calcule un mask/code pour filtrage TWAI standard: seuls les bits communs à tous les IDs
// restent à 1 dans le mask; code contient ces bits. Cela peut laisser passer plus large que la
// liste exacte, mais réduit nettement le flux matériel.
bool vehicle_can_get_twai_filter(can_bus_type_t bus, uint32_t *code_out, uint32_t *mask_out) {
  uint32_t local_ids[32];
  size_t count = 0;

  // Collecter les IDs communs
  for (size_t i = 0; i < sizeof(CAN_IDS_COMMON) / sizeof(CAN_IDS_COMMON[0]); i++) {
    if (CAN_IDS_COMMON[i] != 0) {
      local_ids[count++] = CAN_IDS_COMMON[i];
    }
  }
  // Ajouter les IDs spécifiques bus (si présents)
  const uint32_t *bus_list = NULL;
  size_t bus_list_len      = 0;
  if (bus == CAN_BUS_BODY) {
    bus_list     = CAN_IDS_BODY;
    bus_list_len = sizeof(CAN_IDS_BODY) / sizeof(CAN_IDS_BODY[0]);
  } else if (bus == CAN_BUS_CHASSIS) {
    bus_list     = CAN_IDS_CHASSIS;
    bus_list_len = sizeof(CAN_IDS_CHASSIS) / sizeof(CAN_IDS_CHASSIS[0]);
  }
  for (size_t i = 0; i < bus_list_len; i++) {
    if (bus_list[i] != 0 && count < sizeof(local_ids) / sizeof(local_ids[0])) {
      local_ids[count++] = bus_list[i];
    }
  }

  if (count == 0) {
    return false;
  }

  uint32_t code = local_ids[0] & 0x7FF;
  uint32_t mask = 0x7FF;
  for (size_t i = 1; i < count; i++) {
    uint32_t id = local_ids[i] & 0x7FF;
    mask &= ~(code ^ id); // gardez seulement les bits communs
    code &= mask;
  }

  if (code_out)
    *code_out = code;
  if (mask_out)
    *mask_out = mask;
  return true;
}

static void recompute_doors_open(vehicle_state_t *state) {
  if (!state)
    return;
  uint8_t c = 0;
  c += s_door_front_left_open ? 1 : 0;
  c += s_door_rear_left_open ? 1 : 0;
  c += s_door_front_right_open ? 1 : 0;
  c += s_door_rear_right_open ? 1 : 0;
  state->doors_open_count = c;
}

// ============================================================================
// Mapping signaux -> vehicle_state_t
// ============================================================================

void vehicle_state_apply_signal(const can_message_def_t *msg, const can_signal_def_t *sig, float value, vehicle_state_t *state) {
  if (!msg || !sig || !state)
    return;

  uint32_t id      = msg->id;
  const char *name = sig->name ? sig->name : "";

  if (id == 0x3C2) {
    if (strcmp(name, "VCLEFT_swcLeftScrollTicks") == 0) {
      if (value != 21 && value != 0) {
        state->left_btn_scroll_up   = value > 0 ? 1 : 0;
        state->left_btn_scroll_down = value < 0 ? 1 : 0;
      }
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftDoublePress") == 0) {
      state->left_btn_dbl_press = value > 0.5f ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftPressed") == 0) {
      state->left_btn_press = value == 2 ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftTiltLeft") == 0) {
      state->left_btn_tilt_left = value == 2 ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_swcLeftTiltRight") == 0) {
      state->left_btn_tilt_right = value == 2 ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_swcRightScrollTicks") == 0) {
      if (value != 21 && value != 0) {
        state->right_btn_scroll_up   = value > 0 ? 1 : 0;
        state->right_btn_scroll_down = value < 0 ? 1 : 0;
      }
      return;
    } else if (strcmp(name, "VCLEFT_swcRightDoublePress") == 0) {
      state->right_btn_dbl_press = value > 0.5f ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_swcRightPressed") == 0) {
      state->right_btn_press = value == 2 ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_swcRightTiltLeft") == 0) {
      state->right_btn_tilt_left = value == 2 ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_swcRightTiltRight") == 0) {
      state->right_btn_tilt_right = value == 2 ? 1 : 0;
      return;
    } else if (strcmp(name, "VCLEFT_screenDarkMode") == 0) {
      ESP_LOGI(TAG_CAN, "%s %f", name, value);
      state->night_mode = value > 0.5f;
      return;
    }
    return;
  }
  // ---------------------------------------------------------------------
  // Vitesse véhicule : ID257DIspeed / DI_vehicleSpeed (kph)
  // ---------------------------------------------------------------------
  if (id == 0x257) {
    if (strcmp(name, "DI_vehicleSpeed") == 0) {
      state->speed_kph = value; // déjà en kph
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
      state->gear = (int8_t)(value + 0.5f);
      return;
    }
    if (strcmp(name, "DI_accelPedalPos") == 0) {
      state->accel_pedal_pos = (int8_t)(value + 0.5f);
      return;
    }
    return;
  }
  if (id == 0x39D) {
    if (strcmp(name, "IBST_driverBrakeApply") == 0) {
      // 0=OFF, 1=ON
      state->brake_pressed = (value == 2) ? 1 : 0;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Odomètre
  // ---------------------------------------------------------------------
  if (id == 0x3F3) {
    if (strcmp(name, "UI_odometer") == 0) {
      state->odometer_km = value;
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
      state->door_front_left_open = s_door_front_left_open;
      recompute_doors_open(state);
      return;
    }
    if (strcmp(name, "VCLEFT_rearLatchStatus") == 0) {
      s_door_rear_left_open      = is_latch_open(value);
      state->door_rear_left_open = s_door_rear_left_open;
      recompute_doors_open(state);
      return;
    }
    return;
  }

  if (id == 0x103) { // droite + trunk
    if (strcmp(name, "VCRIGHT_frontLatchStatus") == 0) {
      s_door_front_right_open      = is_latch_open(value);
      state->door_front_right_open = s_door_front_right_open;
      recompute_doors_open(state);
      return;
    }
    if (strcmp(name, "VCRIGHT_rearLatchStatus") == 0) {
      s_door_rear_right_open      = is_latch_open(value);
      state->door_rear_right_open = s_door_rear_right_open;
      recompute_doors_open(state);
      return;
    }
    if (strcmp(name, "VCRIGHT_trunkLatchStatus") == 0) {
      state->trunk_open = is_latch_open(value) ? 1 : 0;
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
        state->frunk_open = (value == 1 /* || value == 5*/) ? 1 : 0;
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
      state->turn_left = (v >= 1) ? 1 : 0;
      if (state->turn_left && state->turn_right) {
        state->hazard = 1;
      } else {
        state->hazard = 0;
      }
      return;
    }
    if (strcmp(name, "VCFRONT_indicatorRightRequest") == 0) {
      int v             = (int)(value + 0.5f);
      state->turn_right = (v >= 1) ? 1 : 0;
      if (state->turn_left && state->turn_right) {
        state->hazard = 1;
      } else {
        state->hazard = 0;
      }
      return;
    }

    // Low beams -> headlights_on
    if (strcmp(name, "VCFRONT_lowBeamLeftStatus") == 0 || strcmp(name, "VCFRONT_lowBeamRightStatus") == 0) {
      state->headlights = (int)(value + 0.5f);
      return;
    }

    // High beams
    if (strcmp(name, "VCFRONT_highBeamLeftStatus") == 0 || strcmp(name, "VCFRONT_highBeamRightStatus") == 0) {
      state->high_beams = (int)(value + 0.5f);
      return;
    }

    // Feux de brouillard
    if (strcmp(name, "VCFRONT_fogLeftStatus") == 0 || strcmp(name, "VCFRONT_fogRightStatus") == 0) {
      state->fog_lights = (int)(value + 0.5f);
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
      state->autopilot = value;
      return;
    }
    if (strcmp(name, "DAS_laneDepartureWarning") == 0) {
      // 1 "LEFT_WARNING" 3 "LEFT_WARNING_SEVERE" 0 "NONE" 2 "RIGHT_WARNING" 4
      // "RIGHT_WARNING_SEVERE"
      state->lane_departure_left_lv1  = value == 1 ? 1 : 0;
      state->lane_departure_left_lv2  = value == 3 ? 1 : 0;
      state->lane_departure_right_lv1 = value == 2 ? 1 : 0;
      state->lane_departure_right_lv2 = value == 4 ? 1 : 0;
      return;
    }
    if (strcmp(name, "DAS_sideCollisionWarning") == 0) {
      ESP_LOGI(TAG_CAN, "%s %f", name, value);
      state->side_collision_left  = value == 1 || value == 3 ? 1 : 0;
      state->side_collision_right = value == 2 || value == 3 ? 1 : 0;
      return;
    }
    if (strcmp(name, "DAS_forwardCollisionWarning") == 0) {
      state->forward_collision = value == 1 ? 1 : 0;
      return;
    }
    if (strcmp(name, "DAS_blindSpotRearLeft") == 0) {
      int v                 = (int)(value + 0.5f);
      state->blindspot_left = (v >= 1) ? 1 : 0;
      return;
    }
    if (strcmp(name, "DAS_blindSpotRearRight") == 0) {
      int v                  = (int)(value + 0.5f);
      state->blindspot_right = (v >= 1) ? 1 : 0;
      return;
    }
    return;
  }
  if (id == 0x334) {
    if (strcmp(name, "UI_speedLimit") == 0) {
      state->speed_threshold = value;
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // SOC : ID292BMS_SOC / SOCUI292
  // ---------------------------------------------------------------------
  if (id == 0x292) {
    if (strcmp(name, "SOCUI292") == 0) {
      state->soc_percent = value / 1.023;
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Batterie HV : ID132HVBattAmpVolt / BattVoltage132
  // ---------------------------------------------------------------------
  if (id == 0x132) {
    if (strcmp(name, "BattVoltage132") == 0) {
      state->battery_voltage_HV = value;
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Batterie HV : ID261_12vBattStatus / v12vBattVoltage261
  // ---------------------------------------------------------------------
  if (id == 0x261) {
    if (strcmp(name, "v12vBattVoltage261") == 0) {
      state->battery_voltage_LV = value;
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
        state->charging = 1;
      } else if (v == 0) {
        state->charging = 0;
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
      state->night_mode = (value > 0.5f) ? 1 : 0;
      return;
    }
    if (strcmp(name, "UI_displayBrightnessLevel") == 0) {
      state->brightness = (uint8_t)(value / 1.27f); // 0-127
      return;
    }
    if (strcmp(name, "UI_alarmEnabled") == 0) {
      // state->sentry_mode = (value > 0.5f) ? 1 : 0;
      return;
    }
    if (strcmp(name, "UI_intrusionSensorOn") == 0) {
      state->sentry_mode = (value > 0.5f) ? 1 : 0;
      return;
    }
    if (strcmp(name, "UI_lockRequest") == 0) {
      int v = (int)(value + 0.5f);
      // 1 = LOCK, 2 = UNLOCK (voir mapping dans JSON)
      if (v == 1) {
        state->locked = 1;
      } else if (v == 2) {
        state->locked = 0;
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
        state->charging_cable = 0;
      } else if (v == 2) {
        state->charging_cable = 1;
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
      state->charge_status = value;
      return;
    } else if (strcmp(name, "BMS_chgPowerAvailable") == 0) {
      state->charge_power_kw = value > 255 ? 0 : value;
      return;
    }
    return;
  }

  if (id == 0x25D) {
    if (strcmp(name, "CP_chargeDoorOpen") == 0) {
      state->charging_port = (value > 0.5f) ? 1 : 0;
      return;
    }
    return;
  }

  // Fallback : signaux non mappés -> ignorés au niveau état haut niveau
}
