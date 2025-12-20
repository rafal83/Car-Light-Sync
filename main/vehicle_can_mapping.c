#include "vehicle_can_mapping.h"

#include "config_manager.h" // pour can_event_type_t + can_event_trigger
#include "esp_log.h"
#include "vehicle_can_unified.h"
#include "vehicle_can_unified_config.h"

#include <stdbool.h>
#include <string.h>

// Helper latch -> open/closed
// IRAM_ATTR: called in CAN real-time callback
static inline int IRAM_ATTR is_latch_open(float raw) {
  int v = (int)(raw + 0.5f);
  // DBC mapping (typique Tesla) :
  // 2 = LATCH_CLOSED
  // 0 = SNA, others = open / moving / fault
  if (v == 2 || v == 0)
    return 0; // closed - SNA
  return 1;
}

static uint8_t s_batterySMState = 0;

static uint16_t s_blindspotLeftCm = 0;
static uint16_t s_blindspotRightCm = 0;

static uint16_t s_frontLeftCm = 0;
static uint16_t s_frontRightCm = 0;
static uint16_t s_prev_frontLeftCm = 0;
static uint16_t s_prev_frontRightCm = 0;
static uint32_t s_prev_frontSumAvg = 0;
#ifndef FRONT_SUM_AVG_WINDOW
#define FRONT_SUM_AVG_WINDOW 50
#endif
static uint32_t s_frontSumWindow[FRONT_SUM_AVG_WINDOW] = {0};
static uint16_t s_frontSumWindowCount = 0;
static uint16_t s_frontSumWindowIndex = 0;

static volatile bool s_vehicle_state_dirty = false;

void vehicle_can_state_dirty_set(void) {
  s_vehicle_state_dirty = true;
}

bool vehicle_can_state_dirty_get(void) {
  return s_vehicle_state_dirty;
}

void vehicle_can_state_dirty_clear(void) {
  s_vehicle_state_dirty = false;
}

// Helpers to send the ESP-NOW state only when a value has changed
#define UPDATE_AND_SEND_U8(field, value, state_ptr)                \
  do {                                                             \
    uint8_t _nv = (uint8_t)(value);                                \
    if ((field) != _nv) {                                          \
      (field) = _nv;                                               \
      vehicle_can_state_dirty_set();                               \
    } else {                                                       \
      (field) = _nv;                                               \
    }                                                              \
  } while (0)

#define UPDATE_AND_SEND_I8(field, value, state_ptr)                \
  do {                                                             \
    int8_t _nv = (int8_t)(value);                                  \
    if ((field) != _nv) {                                          \
      (field) = _nv;                                               \
      vehicle_can_state_dirty_set();                               \
    } else {                                                       \
      (field) = _nv;                                               \
    }                                                              \
  } while (0)

#define UPDATE_AND_SEND_FLOAT(field, value, state_ptr)             \
  do {                                                             \
    float _nv = (float)(value);                                    \
    if ((field) != _nv) {                                          \
      (field) = _nv;                                               \
      vehicle_can_state_dirty_set();                               \
    } else {                                                       \
      (field) = _nv;                                               \
    }                                                              \
  } while (0)

static void IRAM_ATTR recompute_blindspot_alert(vehicle_state_t *state) {
  if (!state)
    return;

  if(state->blindspot_left) {
    if(state->gear == 2) { // Reverse
      UPDATE_AND_SEND_U8(state->blindspot_left_alert, s_blindspotLeftCm < 200 && s_blindspotLeftCm > 1 ? 1 : 0, state);
    } else if(state->turn_left) {
      UPDATE_AND_SEND_U8(state->blindspot_left_alert, s_blindspotLeftCm < 250 && s_blindspotLeftCm > 1 ? 1 : 0, state);
    } else {
      UPDATE_AND_SEND_U8(state->blindspot_left_alert, 0, state);
    }
  }
  if(state->blindspot_right) {
    if(state->gear == 2) { // Reverse
      UPDATE_AND_SEND_U8(state->blindspot_right_alert, s_blindspotRightCm < 200 && s_blindspotRightCm > 1 ? 1 : 0, state);
    } else if(state->turn_right) {
      UPDATE_AND_SEND_U8(state->blindspot_right_alert, s_blindspotRightCm < 250 && s_blindspotRightCm > 1 ? 1 : 0, state);
    } else {
      UPDATE_AND_SEND_U8(state->blindspot_right_alert, 0, state);
    }    
  }
}

static void IRAM_ATTR recompute_front_alert(vehicle_state_t *state) {
  uint32_t sum = (uint32_t)s_frontLeftCm + (uint32_t)s_frontRightCm;
  uint32_t total = 0;
  uint32_t avg = 0;

  // Simple moving average to reduce sensitivity to sensor noise.
  s_frontSumWindow[s_frontSumWindowIndex] = sum;
  s_frontSumWindowIndex = (s_frontSumWindowIndex + 1) % FRONT_SUM_AVG_WINDOW;
  if (s_frontSumWindowCount < FRONT_SUM_AVG_WINDOW)
    s_frontSumWindowCount++;
  for (uint8_t i = 0; i < s_frontSumWindowCount; i++)
    total += s_frontSumWindow[i];
  avg = total / s_frontSumWindowCount;

  // Distance is shorter than previous value and accel pedal is > 0
  if(((avg <= s_prev_frontSumAvg) && state->accel_pedal_pos > 0) && state->gear == 4 && state->speed_kph > 30){
    UPDATE_AND_SEND_U8(state->forward_collision, 1, state); 
  } else {
    UPDATE_AND_SEND_U8(state->forward_collision, 0, state); 
  }
  s_prev_frontLeftCm = s_frontLeftCm;
  s_prev_frontRightCm = s_frontRightCm;
  s_prev_frontSumAvg = avg;
}


// ============================================================================
// Mapping signaux -> vehicle_state_t
// ============================================================================

// Callback for scroll wheel events
static vehicle_wheel_scroll_callback_t s_wheel_scroll_callback = NULL;

void vehicle_can_set_wheel_scroll_callback(vehicle_wheel_scroll_callback_t callback) {
  s_wheel_scroll_callback = callback;
}

// IRAM_ATTR defined in the header
void vehicle_state_apply_signal(const can_message_def_t *msg, const can_signal_def_t *sig, float value, uint8_t bus_id, vehicle_state_t *state) {
  if (!msg || !sig || !state)
    return;

  uint32_t id      = msg->id;
  const char *name = sig->name ? sig->name : "";

  if ((id == 0x3C2) && (bus_id == CAN_BUS_BODY)) {
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
        // Call the callback immediately with the scroll value
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
  // Vehicle speed: ID257DIspeed / DI_vehicleSpeed (kph)
  // ---------------------------------------------------------------------
  if (id == 0x257) {
    if (strcmp(name, "DI_vehicleSpeed") == 0) {
      UPDATE_AND_SEND_FLOAT(state->speed_kph, value, state); // already in kph
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
  // Odometer
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
      UPDATE_AND_SEND_U8(state->door_front_left_open, is_latch_open(value), state);
      return;
    }
    if (strcmp(name, "VCLEFT_rearLatchStatus") == 0) {
      UPDATE_AND_SEND_U8(state->door_rear_left_open, is_latch_open(value), state);
      return;
    }
    return;
  }

  if (id == 0x103) { // droite + trunk
    if (strcmp(name, "VCRIGHT_frontLatchStatus") == 0) {
      UPDATE_AND_SEND_U8(state->door_front_right_open, is_latch_open(value), state);
      return;
    }
    if (strcmp(name, "VCRIGHT_rearLatchStatus") == 0) {
      UPDATE_AND_SEND_U8(state->door_rear_right_open, is_latch_open(value), state);
      return;
    }
    if (strcmp(name, "VCRIGHT_trunkLatchStatus") == 0) {
      UPDATE_AND_SEND_U8(state->trunk_open, is_latch_open(value), state);
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Frunk : ID2E1VCFRONT_status / VCFRONT_frunkLatchStatus
  // ---------------------------------------------------------------------
  if (id == 0x2E1) {
    if (strcmp(name, "VCFRONT_frunkLatchStatus") == 0) {
      if ((value >= 1) && (value <= 2)) {
        UPDATE_AND_SEND_U8(state->frunk_open, is_latch_open(value) ? 1 : 0, state);
      }
      return;
    }
    return;
  }

  // ---------------------------------------------------------------------
  // Lights + turn signals + fog: ID3F5VCFRONT_lighting
  // ---------------------------------------------------------------------
  if (id == 0x3F5) {
    // Turn signals: store only the binary state, events
    // are handled below
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
    if (strcmp(name, "VCFRONT_lowBeamLeftStatus") == 0) { //  || strcmp(name, "VCFRONT_lowBeamRightStatus") == 0) {
      UPDATE_AND_SEND_U8(state->headlights, (uint8_t)(value + 0.5f) == 1 ? 1 : 0, state);
      return;
    }

    // High beams
    if (strcmp(name, "VCFRONT_highBeamLeftStatus") == 0) { //  || strcmp(name, "VCFRONT_highBeamRightStatus") == 0) {
      UPDATE_AND_SEND_U8(state->high_beams, (uint8_t)(value + 0.5f) == 1 ? 1 : 0, state);
      return;
    }

    // Feux de brouillard
    if (strcmp(name, "VCFRONT_fogLeftStatus") == 0) { // || strcmp(name, "VCFRONT_fogRightStatus") == 0) {
      UPDATE_AND_SEND_U8(state->fog_lights, (uint8_t)(value + 0.5f) == 1 ? 1 : 0 , state);
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
      /*
        0 "DISABLED" 
        1 "UNAVAILABLE"
        2 "AVAILABLE" 
        3 "ACTIVE_NOMINAL" 
        4 "ACTIVE_RESTRICTED" 
        5 "ACTIVE_NAV" 
        8 "ABORTING" 
        9 "ABORTED" 
        14 "FAULT" 
        15 "SNA" 
      */
      int v                       = (int)(value + 0.5f);
      // ESP_LOGI(TAG_CAN, "%s %f", name, value); // 2 nada, 3 // FSD,
      v = (v >= 3) && (v <= 9) ? v : 0;
      UPDATE_AND_SEND_U8(state->autopilot, v, state);
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
      // ESP_LOGI(TAG_CAN, "%s %f", name, value);
      int v                       = (int)(value + 0.5f);
      // UPDATE_AND_SEND_U8(state->autopilot_alert_lv1, v == 1 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->autopilot_alert_lv1, v >= 3 && v <= 5 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->autopilot_alert_lv2, v >= 6 && v <= 10 ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_laneDepartureWarning") == 0) {
      // 1 "LEFT_WARNING" 3 "LEFT_WARNING_SEVERE" 0 "NONE" 2 "RIGHT_WARNING" 4
      // "RIGHT_WARNING_SEVERE"
      int v                       = (int)(value + 0.5f);
      UPDATE_AND_SEND_U8(state->lane_departure_left_lv1, v == 1 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->lane_departure_left_lv2, v == 3 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->lane_departure_right_lv1, v == 2 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->lane_departure_right_lv2, v == 4 ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_sideCollisionWarning") == 0) {
      // ESP_LOGI(TAG_CAN, "%s %f", name, value);
      int v                       = (int)(value + 0.5f);
      UPDATE_AND_SEND_U8(state->side_collision_left, v == 1 || v == 3 ? 1 : 0, state);
      UPDATE_AND_SEND_U8(state->side_collision_right, v == 2 || v == 3 ? 1 : 0, state);
      return;
    }
    // if (strcmp(name, "DAS_forwardCollisionWarning") == 0) {
    //   int v                       = (int)(value + 0.5f);
    //   UPDATE_AND_SEND_U8(state->forward_collision, v == 1 ? 1 : 0, state);
    //   return;
    // }
    if (strcmp(name, "DAS_blindSpotRearLeft") == 0) {
      UPDATE_AND_SEND_U8(state->blindspot_left, (value > 0) ? 1 : 0, state);
      return;
    }
    if (strcmp(name, "DAS_blindSpotRearRight") == 0) {
      UPDATE_AND_SEND_U8(state->blindspot_right, (value > 0) ? 1 : 0, state);
      return;
    }
    return;
  }
  
  if (id == 0x22E) {
    if (strcmp(name, "PARK_sdiSensor12RawDistData") == 0) {
      s_blindspotLeftCm = (uint16_t)(value + 0.5f);
      recompute_blindspot_alert(state);
      return;
    }
    if (strcmp(name, "PARK_sdiSensor7RawDistData") == 0) {
      s_blindspotRightCm = (uint16_t)(value + 0.5f);
      recompute_blindspot_alert(state);
      return;
    }
    return;
  }

  if (id == 0x20E) {
    if (strcmp(name, "PARK_sdiSensor3RawDistData") == 0) {
      value = value > 1 && value <= 500 ? value : 0;
      s_frontLeftCm = (uint16_t)(value + 0.5f);
      recompute_front_alert(state);
      return;
    }
    if (strcmp(name, "PARK_sdiSensor4RawDistData") == 0) {
      value = value > 1 && value <= 500 ? value : 0;
      s_frontRightCm = (uint16_t)(value + 0.5f);
      recompute_front_alert(state);
      return;
    }
    return;
  }

  if (id == 0x334 && bus_id == CAN_BUS_CHASSIS) {
    if (strcmp(name, "UI_pedalMap") == 0) {
      UPDATE_AND_SEND_U8(state->pedal_map, (int)(value + 0.5f), state);
      return;
    }
    if (strcmp(name, "UI_speedLimit") == 0) {
      UPDATE_AND_SEND_FLOAT(state->speed_limit, (int)(value + 0.5f), state);
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

  if (id == 0x2e5) {
    if (strcmp(name, "FrontPower2E5") == 0) {
      UPDATE_AND_SEND_FLOAT(state->front_power, value, state);
      return;
    }
    return;
  }
  if (id == 0x266) {
    if (strcmp(name, "RearPower266") == 0) {
      UPDATE_AND_SEND_FLOAT(state->rear_power, value, state);
      return;
    }
    return;
  }

  // if (id == 0x126) {
  //   if (strcmp(name, "RearHighVoltage126") == 0) {
  //     s_rearHighVoltage126 = value;
  //     recompute_power(state);
  //     return;
  //   }
  //   if (strcmp(name, "RearMotorCurrent126") == 0) {
  //     s_rearMotorCurrent126 = value;
  //     recompute_power(state);
  //     return;
  //   }
  //   return;
  // }
  // if (id == 0x126) {
  //   if (strcmp(name, "FrontHighVoltage1A5") == 0) {
  //     s_frontHighVoltage1A5 = value;
  //     recompute_power(state);
  //     return;
  //   }
  //   if (strcmp(name, "FrontMotorCurrent1A5") == 0) {
  //     s_frontMotorCurrent1A5 = value;
  //     recompute_power(state);
  //     return;
  //   }
  //   return;
  // }

  // ---------------------------------------------------------------------
  // Batterie HV : ID261_12vBattStatus / v12vBattVoltage261
  // ---------------------------------------------------------------------
  if (id == 0x261) {
    if (strcmp(name, "VCFRONT_batterySMState") == 0) {
      s_batterySMState = (int)(value + 0.5f);; // 3 Standby
    }
    if ((strcmp(name, "v12vBattVoltage261") == 0) && (s_batterySMState == 3)) {
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
      // 0 = not charging, 2 = charging (per the DBC
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

  if (id == 0x7FF) {
    if (strcmp(name, "GTW_drivetrainType") == 0) {
      UPDATE_AND_SEND_U8(state->train_type, (value > 0.5f), state);
      return;
    }
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

  // Fallback: unmapped signals -> ignored at high-level state
}
