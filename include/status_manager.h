#ifndef STATUS_MANAGER_H
#define STATUS_MANAGER_H

/**
 * @brief Force la mise à jour immédiate de la LED de statut
 *
 * Cette fonction met à jour la LED selon l'état actuel du système
 * (BLE, CAN, WiFi, etc.) sauf si la LED est en mode FACTORY_RESET
 */
void status_manager_update_led_now(void);

#endif // STATUS_MANAGER_H
