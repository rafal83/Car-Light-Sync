#ifndef TESLA_CAN_H
#define TESLA_CAN_H

#include <stdint.h>
#include <stdbool.h>

// IDs des messages CAN pour Tesla Model 3 (2021)
// Bus CAN: Chassis (500 kbps)

// Statut du véhicule
#define CAN_ID_VEHICLE_STATUS       0x118  // État général du véhicule
#define CAN_ID_GEAR_STATUS          0x118  // Position du sélecteur (P/R/N/D)
#define CAN_ID_SPEED                0x257  // Vitesse du véhicule
#define CAN_ID_DOOR_STATUS          0x2B4  // État des portes
#define CAN_ID_LOCK_STATUS          0x2B5  // État de verrouillage
#define CAN_ID_WINDOW_STATUS        0x2C4  // État des fenêtres
#define CAN_ID_TRUNK_STATUS         0x2E5  // État coffre/frunk
#define CAN_ID_LIGHT_STATUS         0x3E5  // État des lumières
#define CAN_ID_BRAKE_STATUS         0x2C3  // État des freins
#define CAN_ID_TURN_SIGNAL          0x3F5  // Clignotants
#define CAN_ID_CHARGE_STATUS        0x3D2  // État de charge
#define CAN_ID_BATTERY_VOLTAGE      0x392  // Tension batterie
#define CAN_ID_ODOMETER             0x3F2  // Odomètre
#define CAN_ID_TIRE_PRESSURE        0x2E4  // Pression des pneus
#define CAN_ID_BLINDSPOT            0x2A5  // Détection angle mort
#define CAN_ID_NIGHT_MODE           0x3C8  // Mode nuit automatique

// Structure pour les données CAN
typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t timestamp;
} can_frame_t;

// Structure pour l'état du véhicule
typedef struct {
    // État de conduite
    bool ignition_on;           // Contact mis
    uint8_t gear;               // P=0, R=1, N=2, D=3
    float speed_kmh;            // Vitesse en km/h
    bool brake_pressed;         // Frein appuyé
    
    // Portes et verrouillage
    bool door_fl;               // Porte avant gauche
    bool door_fr;               // Porte avant droite
    bool door_rl;               // Porte arrière gauche
    bool door_rr;               // Porte arrière droite
    bool trunk_open;            // Coffre ouvert
    bool frunk_open;            // Frunk ouvert
    bool locked;                // Véhicule verrouillé
    
    // Fenêtres
    uint8_t window_fl;          // 0=fermée, 100=ouverte
    uint8_t window_fr;
    uint8_t window_rl;
    uint8_t window_rr;
    
    // Lumières et signaux
    bool headlights_on;         // Phares allumés
    bool high_beams_on;         // Feux de route
    bool fog_lights_on;         // Feux de brouillard
    uint8_t turn_signal;        // 0=off, 1=left, 2=right, 3=hazard
    
    // Charge
    bool charging;              // En charge
    uint8_t charge_percent;     // Pourcentage de charge
    float charge_power_kw;      // Puissance de charge en kW
    
    // Autres
    float battery_voltage;      // Tension batterie 12V
    uint32_t odometer_km;       // Odomètre en km
    
    // Détection angle mort
    bool blindspot_left;        // Détection angle mort gauche
    bool blindspot_right;       // Détection angle mort droite
    bool blindspot_warning;     // Détection angle mort warning
    
    // Mode nuit
    bool night_mode;            // Mode nuit actif (via capteur lumière)
    
    // Timestamp de la dernière mise à jour
    uint32_t last_update;
} vehicle_state_t;

// Fonctions de décodage des frames CAN
void decode_vehicle_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_gear_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_speed(const can_frame_t* frame, vehicle_state_t* state);
void decode_door_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_lock_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_window_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_trunk_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_light_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_brake_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_turn_signal(const can_frame_t* frame, vehicle_state_t* state);
void decode_charge_status(const can_frame_t* frame, vehicle_state_t* state);
void decode_battery_voltage(const can_frame_t* frame, vehicle_state_t* state);
void decode_blindspot(const can_frame_t* frame, vehicle_state_t* state);
void decode_night_mode(const can_frame_t* frame, vehicle_state_t* state);

// Fonction principale de décodage
void process_can_frame(const can_frame_t* frame, vehicle_state_t* state);

#endif // TESLA_CAN_H
