#ifndef COMMANDER_H
#define COMMANDER_H

#include "esp_err.h"
#include "tesla_can.h"
#include <stdbool.h>

// Protocole Panda
#define PANDA_HEADER_SIZE       4
#define PANDA_MAX_DATA_SIZE     8
#define PANDA_FRAME_SIZE        (PANDA_HEADER_SIZE + PANDA_MAX_DATA_SIZE)

// Types de messages Panda
#define PANDA_MSG_CAN_RECV      0x01
#define PANDA_MSG_CAN_SEND      0x02
#define PANDA_MSG_HEARTBEAT     0x03
#define PANDA_MSG_STATUS        0x04

// Structure d'un message Panda
typedef struct {
    uint8_t msg_type;       // Type de message
    uint8_t bus;            // Bus CAN (0=Chassis, 1=Powertrain, 2=Body)
    uint16_t length;        // Longueur des données
    can_frame_t can_frame;  // Frame CAN
} panda_message_t;

// État du Commander
typedef struct {
    bool connected;
    uint32_t last_heartbeat;
    uint32_t messages_received;
    uint32_t messages_sent;
    uint32_t errors;
    char ip_address[16];
} commander_status_t;

// Callback pour les frames CAN reçues
typedef void (*commander_can_callback_t)(const can_frame_t* frame, void* user_data);

/**
 * @brief Initialise le module Commander
 * @return ESP_OK si succès
 */
esp_err_t commander_init(void);

/**
 * @brief Démarre la connexion au Commander
 * @param ip_address Adresse IP du Commander
 * @param port Port du Commander
 * @return ESP_OK si succès
 */
esp_err_t commander_connect(const char* ip_address, uint16_t port);

/**
 * @brief Déconnecte du Commander
 * @return ESP_OK si succès
 */
esp_err_t commander_disconnect(void);

/**
 * @brief Envoie un message CAN via le Commander
 * @param frame Frame CAN à envoyer
 * @param bus Bus CAN (0, 1 ou 2)
 * @return ESP_OK si succès
 */
esp_err_t commander_send_can(const can_frame_t* frame, uint8_t bus);

/**
 * @brief Enregistre un callback pour les frames CAN reçues
 * @param callback Fonction callback
 * @param user_data Données utilisateur passées au callback
 * @return ESP_OK si succès
 */
esp_err_t commander_register_callback(commander_can_callback_t callback, void* user_data);

/**
 * @brief Obtient l'état du Commander
 * @param status Pointeur vers la structure de statut
 * @return ESP_OK si succès
 */
esp_err_t commander_get_status(commander_status_t* status);

/**
 * @brief Envoie un heartbeat au Commander
 * @return ESP_OK si succès
 */
esp_err_t commander_send_heartbeat(void);

/**
 * @brief Vérifie si le Commander est connecté
 * @return true si connecté
 */
bool commander_is_connected(void);

/**
 * @brief Arrête le module Commander
 * @return ESP_OK si succès
 */
esp_err_t commander_stop(void);

#endif // COMMANDER_H
