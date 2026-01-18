
#include "vehicle_can_unified.h"

#include "esp_log.h"
#include "vehicle_can_mapping.h"
#include "vehicle_can_unified_config.h"

#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Signal history (for managing events like RISING/FALLING EDGE)
// ---------------------------------------------------------------------------

// Simple ring via hash (id & 0xFF, idx & 0x0F) to avoid over-allocating
#define HISTORY_ID_MASK 0xFFu
#define HISTORY_SIG_MASK 0x0Fu

static float s_signal_history[HISTORY_ID_MASK + 1][HISTORY_SIG_MASK + 1];

static inline float history_get(uint32_t id, uint8_t sig_index) {
  return s_signal_history[id & HISTORY_ID_MASK][sig_index & HISTORY_SIG_MASK];
}

static inline void history_set(uint32_t id, uint8_t sig_index, float value) {
  s_signal_history[id & HISTORY_ID_MASK][sig_index & HISTORY_SIG_MASK] = value;
}

void vehicle_can_unified_init(void) {
  memset(s_signal_history, 0, sizeof(s_signal_history));
}

// ---------------------------------------------------------------------------
// DBC message lookup by ID
// ---------------------------------------------------------------------------
// IRAM_ATTR: Called for every CAN frame received (~2000 times/s)
static const can_message_def_t * IRAM_ATTR find_message_def(uint32_t id) {
  for (uint16_t i = 0; i < g_can_message_count; i++) {
    if (g_can_messages[i].id == id) {
      return &g_can_messages[i];
    }
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// Extraction de bits (Intel / Motorola) + cast / scaling
// ---------------------------------------------------------------------------

// IRAM_ATTR: Called for every CAN signal with little-endian byte order (~100k-200k times/s)
static uint64_t IRAM_ATTR extract_bits_le(const uint8_t *data, uint8_t start_bit, uint8_t length) {
  // little-endian (Intel): start_bit is the LSB bit indexed from the byte
  // 0
  uint64_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw |= ((uint64_t)data[i]) << (8 * i);
  }
  uint64_t mask = (length == 64) ? UINT64_MAX : (((uint64_t)1 << length) - 1);
  return (raw >> start_bit) & mask;
}

// IRAM_ATTR: Called for every CAN signal with big-endian byte order (~100k-200k times/s)
static uint64_t IRAM_ATTR extract_bits_be(const uint8_t *data, uint8_t start_bit, uint8_t length) {
  // big-endian (Motorola): rebuild in network order
  uint64_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw = (raw << 8) | data[i];
  }
  // start_bit is as in DBC: bit 0 = MSB of first byte
  uint8_t msb_index = 63 - start_bit;
  uint8_t shift     = msb_index - (length - 1);
  uint64_t mask     = (length == 64) ? UINT64_MAX : (((uint64_t)1 << length) - 1);
  return (raw >> shift) & mask;
}

// IRAM_ATTR: Called for every CAN signal to get raw (unscaled) value
static uint64_t IRAM_ATTR decode_signal_raw(const can_signal_def_t *sig, const uint8_t *data, uint8_t dlc) {
  (void)dlc;
  if (sig->byte_order == BYTE_ORDER_LITTLE_ENDIAN) {
    return extract_bits_le(data, sig->start_bit, sig->length);
  }
  return extract_bits_be(data, sig->start_bit, sig->length);
}

// IRAM_ATTR: Called for every CAN signal to decode and scale values (~100k-200k times/s)
static float IRAM_ATTR decode_signal_value(const can_signal_def_t *sig, const uint8_t *data, uint8_t dlc) {
  (void)dlc; // not used here but kept for future extension

  uint64_t raw = 0;
  if (sig->byte_order == BYTE_ORDER_LITTLE_ENDIAN) {
    raw = extract_bits_le(data, sig->start_bit, sig->length);
  } else {
    raw = extract_bits_be(data, sig->start_bit, sig->length);
  }

  if (sig->value_type == SIGNAL_TYPE_BOOLEAN) {
    return raw ? 1.0f : 0.0f;
  }

  if (sig->value_type == SIGNAL_TYPE_SIGNED) {
    // sign on "length" bits
    uint64_t sign_bit  = (uint64_t)1 << (sig->length - 1);
    int64_t signed_val = (raw & sign_bit) ? (int64_t)(raw | (~((sign_bit << 1) - 1))) : (int64_t)raw;
    return (float)signed_val * sig->factor + sig->offset;
  }

  // UNSIGNED
  return (float)raw * sig->factor + sig->offset;
}

// ---------------------------------------------------------------------------
// Main pipeline
// ---------------------------------------------------------------------------

// IRAM_ATTR declared in header for public function
void vehicle_can_process_frame_static(const can_frame_t *frame, vehicle_state_t *state) {
  if (!frame || !state)
    return;

  const can_message_def_t *msg = find_message_def(frame->id);
  if (!msg) {
    // Message not handled by the generated config
    return;
  }

  uint64_t mux_raw = 0;
  bool has_mux = false;
  for (uint8_t i = 0; i < msg->signal_count; i++) {
    const can_signal_def_t *sig = &msg->signals[i];
    if (sig->mux_type == SIGNAL_MUX_MULTIPLEXER) {
      mux_raw = decode_signal_raw(sig, frame->data, frame->dlc);
      has_mux = true;
      break;
    }
  }

  for (uint8_t i = 0; i < msg->signal_count; i++) {
    const can_signal_def_t *sig = &msg->signals[i];

    if (sig->mux_type == SIGNAL_MUX_MULTIPLEXER) {
      continue;
    }
    if (sig->mux_type == SIGNAL_MUX_MULTIPLEXED) {
      if (!has_mux || mux_raw != sig->mux_value) {
        continue;
      }
    }

    float now = decode_signal_value(sig, frame->data, frame->dlc);

    history_set(msg->id, i, now);

    vehicle_state_apply_signal(msg, sig, now, frame->bus_id, state);
  }

  state->last_update_ms = frame->timestamp_ms;
}

// ---------------------------------------------------------------------------
// Conversion to BLE CONFIG format
// ---------------------------------------------------------------------------

void vehicle_state_to_ble_config(const vehicle_state_t *src, vehicle_state_ble_config_t *dst) {
  if (!src || !dst) return;

  // Dynamique de conduite
  dst->rear_power_limit_kw_x10 = (uint16_t)(src->rear_power_limit * 10.0f);
  dst->front_power_limit_kw_x10 = (uint16_t)(src->front_power_limit * 10.0f);
  dst->max_regen_x10 = (uint16_t)(src->max_regen * 10.0f);

  // Byte 0: turn signals & brake
  dst->flags0 =
    (src->train_type    ? (1<<0) : 0);

  // Meta
  dst->last_update_ms = src->last_update_ms;
}

// ---------------------------------------------------------------------------
// Conversion to BLE DRIVE format (driving)
// ---------------------------------------------------------------------------

void vehicle_state_to_ble_drive(const vehicle_state_t *src, vehicle_state_ble_drive_t *dst) {
  if (!src || !dst) return;

  // Dynamique de conduite
  float speed_kph_abs = fabsf(src->speed_kph);
  dst->speed_kph = (uint8_t)(speed_kph_abs);
  dst->rear_power_kw_x10 = (int16_t)(src->rear_power * 10.0f);  // Can be negative (regen)
  dst->front_power_kw_x10 = (int16_t)(src->front_power * 10.0f); // Can be negative (regen)
  dst->soc_percent = (uint8_t)(src->soc_percent);
  dst->odometer_km = (uint32_t)src->odometer_km;

  // Valeurs uint8
  dst->gear = src->gear;
  dst->pedal_map = src->pedal_map;
  dst->accel_pedal_pos = src->accel_pedal_pos;
  dst->brightness = (uint8_t)(src->brightness);
  dst->autopilot = src->autopilot;

  // Byte 0: turn signals & brake
  dst->flags0 =
    (src->turn_left    ? (1<<0) : 0) |
    (src->turn_right   ? (1<<1) : 0) |
    (src->hazard       ? (1<<2) : 0) |
    (src->brake_pressed ? (1<<3) : 0) |
    (src->high_beams   ? (1<<4) : 0) |
    (src->headlights   ? (1<<5) : 0) |
    (src->fog_lights   ? (1<<6) : 0);

  // Byte 1: blindspots & collisions
  dst->flags1 =
    (src->blindspot_left        ? (1<<0) : 0) |
    (src->blindspot_right       ? (1<<1) : 0) |
    (src->blindspot_left_alert  ? (1<<2) : 0) |
    (src->blindspot_right_alert ? (1<<3) : 0) |
    (src->side_collision_left   ? (1<<4) : 0) |
    (src->side_collision_right  ? (1<<5) : 0) |
    (src->forward_collision     ? (1<<6) : 0) |
    (src->night_mode            ? (1<<7) : 0);

  // Byte 2: autopilot alerts & lane departure
  dst->flags2 =
    (src->lane_departure_left_lv1  ? (1<<0) : 0) |
    (src->lane_departure_left_lv2  ? (1<<1) : 0) |
    (src->lane_departure_right_lv1 ? (1<<2) : 0) |
    (src->lane_departure_right_lv2 ? (1<<3) : 0) |
    (src->autopilot_alert_lv1      ? (1<<4) : 0) |
    (src->autopilot_alert_lv2      ? (1<<5) : 0);

  // Meta
  dst->last_update_ms = src->last_update_ms;
}

// ---------------------------------------------------------------------------
// Conversion to BLE PARK format (parking)
// ---------------------------------------------------------------------------

void vehicle_state_to_ble_park(const vehicle_state_t *src, vehicle_state_ble_park_t *dst) {
  if (!src || !dst) return;

  // Energy
  dst->soc_percent = (uint8_t)(src->soc_percent);
  dst->charge_power_kw_x10 = (int16_t)(src->charge_power_kw * 10.0f);
  dst->battery_voltage_LV_x10 = (uint8_t)(src->battery_voltage_LV * 10.0f);
  dst->battery_voltage_HV_x10 = (int16_t)(src->battery_voltage_HV * 10.0f);
  dst->odometer_km = (uint32_t)src->odometer_km;

  // Valeurs uint8
  dst->charge_status = src->charge_status;
  dst->brightness = (uint8_t)(src->brightness);

  // Byte 0: doors & locks
  dst->flags0 =
    (src->locked                ? (1<<0) : 0) |
    (src->door_front_left_open  ? (1<<1) : 0) |
    (src->door_rear_left_open   ? (1<<2) : 0) |
    (src->door_front_right_open ? (1<<3) : 0) |
    (src->door_rear_right_open  ? (1<<4) : 0) |
    (src->frunk_open            ? (1<<5) : 0) |
    (src->trunk_open            ? (1<<6) : 0) |
    (src->brake_pressed         ? (1<<7) : 0);

  // Byte 1: lights
  dst->flags1 =
    (src->turn_left    ? (1<<0) : 0) |
    (src->turn_right   ? (1<<1) : 0) |
    (src->hazard       ? (1<<2) : 0) |
    (src->headlights   ? (1<<3) : 0) |
    (src->high_beams   ? (1<<4) : 0) |
    (src->fog_lights   ? (1<<5) : 0);

  // Byte 2: charging & sentry
  dst->flags2 =
    (src->charging_cable ? (1<<0) : 0) |
    (src->charging       ? (1<<1) : 0) |
    (src->charging_port  ? (1<<2) : 0) |
    (src->sentry_mode    ? (1<<3) : 0) |
    (src->sentry_alert   ? (1<<4) : 0) |
    (src->night_mode     ? (1<<5) : 0);

  // Meta
  dst->last_update_ms = src->last_update_ms;
}
