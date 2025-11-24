
#include <string.h>
#include "vehicle_can_unified.h"
#include "vehicle_can_unified_config.h"
#include "vehicle_can_mapping.h"
#include "esp_log.h"

// ---------------------------------------------------------------------------
// Historique de signaux (pour gestion d'events type RISING/FALLING EDGE)
// ---------------------------------------------------------------------------

// Simple ring via hash (id & 0xFF, idx & 0x0F) pour éviter d'allouer trop
#define HISTORY_ID_MASK 0xFFu
#define HISTORY_SIG_MASK 0x0Fu

static float s_signal_history[HISTORY_ID_MASK + 1][HISTORY_SIG_MASK + 1];

static inline float history_get(uint32_t id, uint8_t sig_index)
{
    return s_signal_history[id & HISTORY_ID_MASK][sig_index & HISTORY_SIG_MASK];
}

static inline void history_set(uint32_t id, uint8_t sig_index, float value)
{
    s_signal_history[id & HISTORY_ID_MASK][sig_index & HISTORY_SIG_MASK] = value;
}

void vehicle_can_unified_init(void)
{
    memset(s_signal_history, 0, sizeof(s_signal_history));
}

// ---------------------------------------------------------------------------
// Recherche de message DBC par ID
// ---------------------------------------------------------------------------
static const can_message_def_t* find_message_def(uint32_t id)
{
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

static uint64_t extract_bits_le(const uint8_t* data, uint8_t start_bit, uint8_t length)
{
    // little-endian (Intel) : start_bit est le bit LSB indexé à partir de l’octet 0
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) {
        raw |= ((uint64_t) data[i]) << (8 * i);
    }
    uint64_t mask = (length == 64) ? UINT64_MAX : (((uint64_t) 1 << length) - 1);
    return (raw >> start_bit) & mask;
}

static uint64_t extract_bits_be(const uint8_t* data, uint8_t start_bit, uint8_t length)
{
    // big-endian (Motorola) : on reconstruit en ordre réseau
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) {
        raw = (raw << 8) | data[i];
    }
    // start_bit est comme en DBC : bit 0 = MSB du premier octet
    uint8_t msb_index = 63 - start_bit;
    uint8_t shift = msb_index - (length - 1);
    uint64_t mask = (length == 64) ? UINT64_MAX : (((uint64_t) 1 << length) - 1);
    return (raw >> shift) & mask;
}

static float decode_signal_value(const can_signal_def_t* sig, const uint8_t* data, uint8_t dlc)
{
    (void) dlc;  // non utilisé ici mais conservé pour extension future

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
        // signe sur "length" bits
        uint64_t sign_bit = (uint64_t) 1 << (sig->length - 1);
        int64_t signed_val = (raw & sign_bit) ? (int64_t) (raw | (~((sign_bit << 1) - 1))) : (int64_t) raw;
        return (float) signed_val * sig->factor + sig->offset;
    }

    // UNSIGNED
    return (float) raw * sig->factor + sig->offset;
}

// ---------------------------------------------------------------------------
// Pipeline principal
// ---------------------------------------------------------------------------

void vehicle_can_process_frame_static(const can_frame_t* frame, vehicle_state_t* state)
{
    if (!frame || !state)
        return;

    const can_message_def_t* msg = find_message_def(frame->id);
    if (!msg) {
        // Message non géré par la config générée
        return;
    }

    for (uint8_t i = 0; i < msg->signal_count; i++) {
        const can_signal_def_t* sig = &msg->signals[i];

        float now = decode_signal_value(sig, frame->data, frame->dlc);

        history_set(msg->id, i, now);

        vehicle_state_apply_signal(msg, sig, now, state);
    }

    state->last_update_ms = frame->timestamp_ms;
}
