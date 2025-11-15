// Auto-généré depuis model3_2021.json
// Véhicule: Unknown Unknown 0
// Description: Configuration CAN filtrée pour Unknown Unknown 0 (événements uniquement)

#ifndef VEHICLE_CAN_CONFIG_GENERATED_H
#define VEHICLE_CAN_CONFIG_GENERATED_H

#include "vehicle_can_config.h"

// Événements pour PCS_hvChargeStatus
static const can_event_config_t events_MSG_204PCS_chgStatus_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STARTED,
        .value = 2.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STOPPED,
        .value = 0.0f
    },
};

// Signaux pour message 0x204 - ID204PCS_chgStatus
static const can_signal_config_static_t signals_MSG_204PCS_chgStatus[] = {
    { // PCS_hvChargeStatus
        .start_bit = 4,
        .length = 2,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_204PCS_chgStatus_sig0,
        .event_count = 2
    },
};

// Événements pour UI_lockRequest
static const can_event_config_t events_MSG_273UI_vehicleControl_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_LOCKED,
        .value = 1.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_UNLOCKED,
        .value = 2.0f
    },
};

// Événements pour UI_ambientLightingEnabled
static const can_event_config_t events_MSG_273UI_vehicleControl_sig1[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_NIGHT_MODE_ON,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_NIGHT_MODE_OFF,
        .value = 0.0f
    },
};

// Signaux pour message 0x273 - ID273UI_vehicleControl
static const can_signal_config_static_t signals_MSG_273UI_vehicleControl[] = {
    { // UI_lockRequest
        .start_bit = 17,
        .length = 3,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_273UI_vehicleControl_sig0,
        .event_count = 2
    },
    { // UI_ambientLightingEnabled
        .start_bit = 40,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_273UI_vehicleControl_sig1,
        .event_count = 2
    },
};

// Événements pour CP_chargeCablePresent
static const can_event_config_t events_MSG_25DCP_status_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_CABLE_DISCONNECTED,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_CABLE_CONNECTED,
        .value = 1.0f
    },
};

// Signaux pour message 0x25D - ID25DCP_status
static const can_signal_config_static_t signals_MSG_25DCP_status[] = {
    { // CP_chargeCablePresent
        .start_bit = 3,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_25DCP_status_sig0,
        .event_count = 2
    },
};

// Événements pour DAS_blindSpotRearLeft
static const can_event_config_t events_MSG_399DAS_status_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_BLINDSPOT_WARNING,
        .value = 1.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_BLINDSPOT_WARNING,
        .value = 2.0f
    },
};

// Événements pour DAS_blindSpotRearRight
static const can_event_config_t events_MSG_399DAS_status_sig1[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_BLINDSPOT_WARNING,
        .value = 1.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_BLINDSPOT_WARNING,
        .value = 2.0f
    },
};

// Signaux pour message 0x399 - ID399DAS_status
static const can_signal_config_static_t signals_MSG_399DAS_status[] = {
    { // DAS_blindSpotRearLeft
        .start_bit = 4,
        .length = 2,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_399DAS_status_sig0,
        .event_count = 2
    },
    { // DAS_blindSpotRearRight
        .start_bit = 6,
        .length = 2,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_399DAS_status_sig1,
        .event_count = 2
    },
};

// Événements pour VCFRONT_driverDoorStatus
static const can_event_config_t events_MSG_3A1VCFRONT_vehicleStatus_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_DOOR_CLOSE,
        .value = 1.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_DOOR_OPEN,
        .value = 0.0f
    },
};

// Signaux pour message 0x3A1 - ID3A1VCFRONT_vehicleStatus
static const can_signal_config_static_t signals_MSG_3A1VCFRONT_vehicleStatus[] = {
    { // VCFRONT_driverDoorStatus
        .start_bit = 31,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3A1VCFRONT_vehicleStatus_sig0,
        .event_count = 2
    },
};

// Événements pour VCFRONT_indicatorLeftRequest
static const can_event_config_t events_MSG_3F5VCFRONT_lighting_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_TURN_LEFT,
        .value = 2.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_TURN_LEFT,
        .value = 1.0f
    },
};

// Événements pour VCFRONT_indicatorRightRequest
static const can_event_config_t events_MSG_3F5VCFRONT_lighting_sig1[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_TURN_RIGHT,
        .value = 2.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_TURN_RIGHT,
        .value = 1.0f
    },
};

// Événements pour VCFRONT_hazardLightRequest
static const can_event_config_t events_MSG_3F5VCFRONT_lighting_sig2[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_TURN_HAZARD,
        .value = 1.0f
    },
};

// Événements pour VCFRONT_hazardSwitchBacklight
static const can_event_config_t events_MSG_3F5VCFRONT_lighting_sig3[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_TURN_HAZARD,
        .value = 0.0f
    },
};

// Signaux pour message 0x3F5 - ID3F5VCFRONT_lighting
static const can_signal_config_static_t signals_MSG_3F5VCFRONT_lighting[] = {
    { // VCFRONT_indicatorLeftRequest
        .start_bit = 0,
        .length = 2,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3F5VCFRONT_lighting_sig0,
        .event_count = 2
    },
    { // VCFRONT_indicatorRightRequest
        .start_bit = 2,
        .length = 2,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3F5VCFRONT_lighting_sig1,
        .event_count = 2
    },
    { // VCFRONT_hazardLightRequest
        .start_bit = 4,
        .length = 4,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3F5VCFRONT_lighting_sig2,
        .event_count = 1
    },
    { // VCFRONT_hazardSwitchBacklight
        .start_bit = 27,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3F5VCFRONT_lighting_sig3,
        .event_count = 1
    },
};

// Événements pour UI_fsdVisualizationEnabled
static const can_event_config_t events_MSG_3FDUI_autopilotControl_sig0[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_ENGAGED,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_DISENGAGED,
        .value = 0.0f
    },
};

// Événements pour UI_fsdStopsControlEnabled
static const can_event_config_t events_MSG_3FDUI_autopilotControl_sig1[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_ENGAGED,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_DISENGAGED,
        .value = 0.0f
    },
};

// Événements pour UI_enableAutopilotStopWarning
static const can_event_config_t events_MSG_3FDUI_autopilotControl_sig2[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_ENGAGED,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_DISENGAGED,
        .value = 0.0f
    },
};

// Signaux pour message 0x3FD - ID3FDUI_autopilotControl
static const can_signal_config_static_t signals_MSG_3FDUI_autopilotControl[] = {
    { // UI_fsdVisualizationEnabled
        .start_bit = 37,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3FDUI_autopilotControl_sig0,
        .event_count = 2
    },
    { // UI_fsdStopsControlEnabled
        .start_bit = 38,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3FDUI_autopilotControl_sig1,
        .event_count = 2
    },
    { // UI_enableAutopilotStopWarning
        .start_bit = 44,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3FDUI_autopilotControl_sig2,
        .event_count = 2
    },
};

// Événements pour CP_hvChargeStatus
static const can_event_config_t events_MSG_23DCP_chargeStatus_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STARTED,
        .value = 5.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STOPPED,
        .value = 2.0f
    },
};

// Signaux pour message 0x23D - ID23DCP_chargeStatus
static const can_signal_config_static_t signals_MSG_23DCP_chargeStatus[] = {
    { // CP_hvChargeStatus
        .start_bit = 0,
        .length = 3,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_23DCP_chargeStatus_sig0,
        .event_count = 2
    },
};

// Événements pour CP_hvChargeStatus
static const can_event_config_t events_MSG_13DCP_chargeStatus_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STARTED,
        .value = 5.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STOPPED,
        .value = 2.0f
    },
};

// Signaux pour message 0x13D - ID13DCP_chargeStatus
static const can_signal_config_static_t signals_MSG_13DCP_chargeStatus[] = {
    { // CP_hvChargeStatus
        .start_bit = 0,
        .length = 3,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_13DCP_chargeStatus_sig0,
        .event_count = 2
    },
};

// Événements pour CP_hvChargeStatus_log
static const can_event_config_t events_MSG_43DCP_chargeStatusLog_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STARTED,
        .value = 5.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_CHARGING_STOPPED,
        .value = 2.0f
    },
};

// Signaux pour message 0x43D - ID43DCP_chargeStatusLog
static const can_signal_config_static_t signals_MSG_43DCP_chargeStatusLog[] = {
    { // CP_hvChargeStatus_log
        .start_bit = 0,
        .length = 3,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_43DCP_chargeStatusLog_sig0,
        .event_count = 2
    },
};

// Événements pour DIR_brakeSwitchNO
static const can_event_config_t events_MSG_7D5DIR_debug_sig0[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_BRAKE_ON,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_BRAKE_OFF,
        .value = 0.0f
    },
};

// Événements pour DIR_brakeSwitchNC
static const can_event_config_t events_MSG_7D5DIR_debug_sig1[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_BRAKE_ON,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_BRAKE_OFF,
        .value = 0.0f
    },
};

// Signaux pour message 0x7D5 - ID7D5DIR_debug
static const can_signal_config_static_t signals_MSG_7D5DIR_debug[] = {
    { // DIR_brakeSwitchNO
        .start_bit = 62,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_7D5DIR_debug_sig0,
        .event_count = 2
    },
    { // DIR_brakeSwitchNC
        .start_bit = 63,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_7D5DIR_debug_sig1,
        .event_count = 2
    },
};

// Événements pour DIF_brakeSwitchNO
static const can_event_config_t events_MSG_757DIF_debug_sig0[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_BRAKE_ON,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_BRAKE_OFF,
        .value = 0.0f
    },
};

// Événements pour DIF_brakeSwitchNC
static const can_event_config_t events_MSG_757DIF_debug_sig1[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_BRAKE_ON,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_BRAKE_OFF,
        .value = 0.0f
    },
};

// Signaux pour message 0x757 - ID757DIF_debug
static const can_signal_config_static_t signals_MSG_757DIF_debug[] = {
    { // DIF_brakeSwitchNO
        .start_bit = 62,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_757DIF_debug_sig0,
        .event_count = 2
    },
    { // DIF_brakeSwitchNC
        .start_bit = 63,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_757DIF_debug_sig1,
        .event_count = 2
    },
};

// Événements pour VCFRONT_autopilot1Fault
static const can_event_config_t events_MSG_2F1VCFRONT_eFuseDebugStatus_sig0[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_ENGAGED,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_DISENGAGED,
        .value = 0.0f
    },
};

// Événements pour VCFRONT_autopilot2Fault
static const can_event_config_t events_MSG_2F1VCFRONT_eFuseDebugStatus_sig1[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_ENGAGED,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_DISENGAGED,
        .value = 0.0f
    },
};

// Signaux pour message 0x2F1 - ID2F1VCFRONT_eFuseDebugStatus
static const can_signal_config_static_t signals_MSG_2F1VCFRONT_eFuseDebugStatus[] = {
    { // VCFRONT_autopilot1Fault
        .start_bit = 10,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_2F1VCFRONT_eFuseDebugStatus_sig0,
        .event_count = 2
    },
    { // VCFRONT_autopilot2Fault
        .start_bit = 10,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_2F1VCFRONT_eFuseDebugStatus_sig1,
        .event_count = 2
    },
};

// Événements pour VCLEFT_hazardButtonPressed
static const can_event_config_t events_MSG_3C2VCLEFT_switchStatus_sig0[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_TURN_HAZARD,
        .value = 0.0f
    },
};

// Événements pour VCLEFT_brakeSwitchPressed
static const can_event_config_t events_MSG_3C2VCLEFT_switchStatus_sig1[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_BRAKE_ON,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_BRAKE_OFF,
        .value = 0.0f
    },
};

// Événements pour VCLEFT_brakePressed
static const can_event_config_t events_MSG_3C2VCLEFT_switchStatus_sig2[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_BRAKE_ON,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_BRAKE_OFF,
        .value = 0.0f
    },
};

// Signaux pour message 0x3C2 - ID3C2VCLEFT_switchStatus
static const can_signal_config_static_t signals_MSG_3C2VCLEFT_switchStatus[] = {
    { // VCLEFT_hazardButtonPressed
        .start_bit = 3,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3C2VCLEFT_switchStatus_sig0,
        .event_count = 1
    },
    { // VCLEFT_brakeSwitchPressed
        .start_bit = 4,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3C2VCLEFT_switchStatus_sig1,
        .event_count = 2
    },
    { // VCLEFT_brakePressed
        .start_bit = 60,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_3C2VCLEFT_switchStatus_sig2,
        .event_count = 2
    },
};

// Événements pour UI_rebootAutopilot
static const can_event_config_t events_MSG_293UI_chassisControl_sig0[] = {
    {
        .condition = EVENT_CONDITION_RISING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_ENGAGED,
        .value = 0.0f
    },
    {
        .condition = EVENT_CONDITION_FALLING_EDGE,
        .trigger = CAN_EVENT_AUTOPILOT_DISENGAGED,
        .value = 0.0f
    },
};

// Signaux pour message 0x293 - ID293UI_chassisControl
static const can_signal_config_static_t signals_MSG_293UI_chassisControl[] = {
    { // UI_rebootAutopilot
        .start_bit = 27,
        .length = 1,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_BOOLEAN,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_293UI_chassisControl_sig0,
        .event_count = 2
    },
};

// Événements pour DI_gear
static const can_event_config_t events_MSG_118DriveSystemStatus_sig0[] = {
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_GEAR_DRIVE,
        .value = 4.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_GEAR_PARK,
        .value = 1.0f
    },
    {
        .condition = EVENT_CONDITION_VALUE_EQUALS,
        .trigger = CAN_EVENT_GEAR_REVERSE,
        .value = 2.0f
    },
};

// Signaux pour message 0x118 - ID118DriveSystemStatus
static const can_signal_config_static_t signals_MSG_118DriveSystemStatus[] = {
    { // DI_gear
        .start_bit = 21,
        .length = 3,
        .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
        .value_type = SIGNAL_TYPE_UNSIGNED,
        .factor = 1.0f,
        .offset = 0.0f,
        .events = events_MSG_118DriveSystemStatus_sig0,
        .event_count = 3
    },
};

// Tableau principal des messages CAN
static const can_message_config_static_t can_messages[] = {
    { // 0x204 - ID204PCS_chgStatus
        .message_id = 0x204,
        .bus = 1,
        .signals = signals_MSG_204PCS_chgStatus,
        .signal_count = 1
    },
    { // 0x273 - ID273UI_vehicleControl
        .message_id = 0x273,
        .bus = 1,
        .signals = signals_MSG_273UI_vehicleControl,
        .signal_count = 2
    },
    { // 0x25D - ID25DCP_status
        .message_id = 0x25D,
        .bus = 1,
        .signals = signals_MSG_25DCP_status,
        .signal_count = 1
    },
    { // 0x399 - ID399DAS_status
        .message_id = 0x399,
        .bus = 2,
        .signals = signals_MSG_399DAS_status,
        .signal_count = 2
    },
    { // 0x3A1 - ID3A1VCFRONT_vehicleStatus
        .message_id = 0x3A1,
        .bus = 2,
        .signals = signals_MSG_3A1VCFRONT_vehicleStatus,
        .signal_count = 1
    },
    { // 0x3F5 - ID3F5VCFRONT_lighting
        .message_id = 0x3F5,
        .bus = 2,
        .signals = signals_MSG_3F5VCFRONT_lighting,
        .signal_count = 4
    },
    { // 0x3FD - ID3FDUI_autopilotControl
        .message_id = 0x3FD,
        .bus = 2,
        .signals = signals_MSG_3FDUI_autopilotControl,
        .signal_count = 3
    },
    { // 0x23D - ID23DCP_chargeStatus
        .message_id = 0x23D,
        .bus = 1,
        .signals = signals_MSG_23DCP_chargeStatus,
        .signal_count = 1
    },
    { // 0x13D - ID13DCP_chargeStatus
        .message_id = 0x13D,
        .bus = 0,
        .signals = signals_MSG_13DCP_chargeStatus,
        .signal_count = 1
    },
    { // 0x43D - ID43DCP_chargeStatusLog
        .message_id = 0x43D,
        .bus = 2,
        .signals = signals_MSG_43DCP_chargeStatusLog,
        .signal_count = 1
    },
    { // 0x7D5 - ID7D5DIR_debug
        .message_id = 0x7D5,
        .bus = 2,
        .signals = signals_MSG_7D5DIR_debug,
        .signal_count = 2
    },
    { // 0x757 - ID757DIF_debug
        .message_id = 0x757,
        .bus = 2,
        .signals = signals_MSG_757DIF_debug,
        .signal_count = 2
    },
    { // 0x2F1 - ID2F1VCFRONT_eFuseDebugStatus
        .message_id = 0x2F1,
        .bus = 1,
        .signals = signals_MSG_2F1VCFRONT_eFuseDebugStatus,
        .signal_count = 2
    },
    { // 0x3C2 - ID3C2VCLEFT_switchStatus
        .message_id = 0x3C2,
        .bus = 2,
        .signals = signals_MSG_3C2VCLEFT_switchStatus,
        .signal_count = 3
    },
    { // 0x293 - ID293UI_chassisControl
        .message_id = 0x293,
        .bus = 1,
        .signals = signals_MSG_293UI_chassisControl,
        .signal_count = 1
    },
    { // 0x118 - ID118DriveSystemStatus
        .message_id = 0x118,
        .bus = 0,
        .signals = signals_MSG_118DriveSystemStatus,
        .signal_count = 1
    },
};

// Configuration globale du véhicule
static const vehicle_can_config_static_t vehicle_config = {
    .messages = can_messages,
    .message_count = 16
};

// Fonction pour obtenir la configuration
static inline const vehicle_can_config_static_t* get_vehicle_can_config(void) {
    return &vehicle_config;
}

#endif // VEHICLE_CAN_CONFIG_GENERATED_H