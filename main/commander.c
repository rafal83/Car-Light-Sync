#include "commander.h"
#include "config.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "task_core_utils.h"

static const char *TAG = "Commander";

static int sock = -1;
static commander_status_t current_status = {0};
static commander_can_callback_t can_callback = NULL;
static void* callback_user_data = NULL;
static TaskHandle_t receive_task_handle = NULL;
static bool running = false;

// Encode un message Panda
static int encode_panda_message(const panda_message_t* msg, uint8_t* buffer, size_t buffer_size) {
    if (buffer_size < PANDA_FRAME_SIZE) {
        return -1;
    }

    buffer[0] = msg->msg_type;
    buffer[1] = msg->bus;
    buffer[2] = (msg->length >> 8) & 0xFF;
    buffer[3] = msg->length & 0xFF;

    // Copier les données CAN
    buffer[4] = (msg->can_frame.id >> 24) & 0xFF;
    buffer[5] = (msg->can_frame.id >> 16) & 0xFF;
    buffer[6] = (msg->can_frame.id >> 8) & 0xFF;
    buffer[7] = msg->can_frame.id & 0xFF;
    buffer[8] = msg->can_frame.dlc;
    
    memcpy(&buffer[9], msg->can_frame.data, msg->can_frame.dlc);

    return PANDA_HEADER_SIZE + 5 + msg->can_frame.dlc;
}

// Décode un message Panda
static int decode_panda_message(const uint8_t* buffer, size_t buffer_size, panda_message_t* msg) {
    if (buffer_size < PANDA_HEADER_SIZE) {
        return -1;
    }

    msg->msg_type = buffer[0];
    msg->bus = buffer[1];
    msg->length = (buffer[2] << 8) | buffer[3];

    if (buffer_size < PANDA_HEADER_SIZE + msg->length) {
        return -1;
    }

    // Décoder les données CAN si c'est un message CAN
    if (msg->msg_type == PANDA_MSG_CAN_RECV && msg->length >= 5) {
        msg->can_frame.id = (buffer[4] << 24) | (buffer[5] << 16) | 
                           (buffer[6] << 8) | buffer[7];
        msg->can_frame.dlc = buffer[8];
        
        if (msg->can_frame.dlc > 8) {
            msg->can_frame.dlc = 8;
        }
        
        memcpy(msg->can_frame.data, &buffer[9], msg->can_frame.dlc);
        msg->can_frame.timestamp = xTaskGetTickCount();
    }

    return PANDA_HEADER_SIZE + msg->length;
}

// Tâche de réception des messages
static void receive_task(void* pvParameters) {
    uint8_t rx_buffer[256];
    
    ESP_LOGI(TAG, "Tâche de réception démarrée");
    
    while (running) {
        int len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
        
        if (len < 0) {
            ESP_LOGE(TAG, "Erreur réception: errno %d", errno);
            current_status.errors++;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connexion fermée par le Commander");
            current_status.connected = false;
            break;
        }

        // Traiter le message reçu
        panda_message_t msg;
        int decoded = decode_panda_message(rx_buffer, len, &msg);
        
        if (decoded > 0) {
            current_status.messages_received++;
            
            switch (msg.msg_type) {
                case PANDA_MSG_CAN_RECV:
                    // Appeler le callback si enregistré
                    if (can_callback != NULL) {
                        can_callback(&msg.can_frame, callback_user_data);
                    }
                    break;
                    
                case PANDA_MSG_HEARTBEAT:
                    current_status.last_heartbeat = xTaskGetTickCount();
                    ESP_LOGD(TAG, "Heartbeat reçu");
                    break;
                    
                default:
                    ESP_LOGD(TAG, "Message type %d reçu", msg.msg_type);
                    break;
            }
        } else {
            ESP_LOGW(TAG, "Message invalide reçu");
            current_status.errors++;
        }
    }
    
    ESP_LOGI(TAG, "Tâche de réception terminée");
    receive_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t commander_init(void) {
    memset(&current_status, 0, sizeof(current_status));
    ESP_LOGI(TAG, "Commander initialisé");
    return ESP_OK;
}

esp_err_t commander_connect(const char* ip_address, uint16_t port) {
    if (current_status.connected) {
        ESP_LOGW(TAG, "Déjà connecté");
        return ESP_OK;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(ip_address);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Erreur création socket: errno %d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connexion à %s:%d...", ip_address, port);
    
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Erreur connexion: errno %d", errno);
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }

    // Configurer timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    strncpy(current_status.ip_address, ip_address, sizeof(current_status.ip_address) - 1);
    current_status.connected = true;
    current_status.last_heartbeat = xTaskGetTickCount();

    // Envoyer le handshake "ehllo" pour démarrer le stream CAN
    const char* handshake = "ehllo";
    int sent = send(sock, handshake, strlen(handshake), 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Erreur envoi handshake: errno %d", errno);
        close(sock);
        sock = -1;
        current_status.connected = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Handshake 'ehllo' envoyé");

    // Démarrer la tâche de réception
    running = true;
    create_task_on_general_core(receive_task, "commander_rx", 4096, NULL, 5, &receive_task_handle);

    ESP_LOGI(TAG, "Connecté au Commander");
    return ESP_OK;
}

esp_err_t commander_disconnect(void) {
    if (!current_status.connected) {
        return ESP_OK;
    }

    running = false;

    if (sock >= 0) {
        // Envoyer "bye" pour fermer proprement la connexion
        const char* goodbye = "bye";
        int sent = send(sock, goodbye, strlen(goodbye), 0);
        if (sent < 0) {
            ESP_LOGW(TAG, "Erreur envoi 'bye': errno %d", errno);
        } else {
            ESP_LOGI(TAG, "Message 'bye' envoyé");
        }

        // Petit délai pour laisser le message partir
        vTaskDelay(pdMS_TO_TICKS(50));

        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = -1;
    }

    // Attendre la fin de la tâche
    if (receive_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    current_status.connected = false;
    ESP_LOGI(TAG, "Déconnecté du Commander");
    return ESP_OK;
}

esp_err_t commander_send_can(const can_frame_t* frame, uint8_t bus) {
    if (!current_status.connected || sock < 0) {
        return ESP_FAIL;
    }

    panda_message_t msg;
    msg.msg_type = PANDA_MSG_CAN_SEND;
    msg.bus = bus;
    msg.length = 5 + frame->dlc;
    memcpy(&msg.can_frame, frame, sizeof(can_frame_t));

    uint8_t buffer[PANDA_FRAME_SIZE];
    int encoded = encode_panda_message(&msg, buffer, sizeof(buffer));
    
    if (encoded < 0) {
        ESP_LOGE(TAG, "Erreur encodage message");
        return ESP_FAIL;
    }

    int sent = send(sock, buffer, encoded, 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Erreur envoi: errno %d", errno);
        current_status.errors++;
        return ESP_FAIL;
    }

    current_status.messages_sent++;
    return ESP_OK;
}

esp_err_t commander_register_callback(commander_can_callback_t callback, void* user_data) {
    can_callback = callback;
    callback_user_data = user_data;
    return ESP_OK;
}

esp_err_t commander_get_status(commander_status_t* status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(status, &current_status, sizeof(commander_status_t));
    return ESP_OK;
}

esp_err_t commander_send_heartbeat(void) {
    if (!current_status.connected || sock < 0) {
        return ESP_FAIL;
    }

    panda_message_t msg;
    msg.msg_type = PANDA_MSG_HEARTBEAT;
    msg.bus = 0;
    msg.length = 0;

    uint8_t buffer[PANDA_HEADER_SIZE];
    buffer[0] = msg.msg_type;
    buffer[1] = msg.bus;
    buffer[2] = 0;
    buffer[3] = 0;

    int sent = send(sock, buffer, PANDA_HEADER_SIZE, 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Erreur envoi heartbeat: errno %d", errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool commander_is_connected(void) {
    return current_status.connected;
}

esp_err_t commander_stop(void) {
    return commander_disconnect();
}
