#ifndef BOOT_LOOP_GUARD_H
#define BOOT_LOOP_GUARD_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Nombre maximum de redémarrages consécutifs avant factory reset
 */
#define BOOT_LOOP_MAX_COUNT 10

/**
 * @brief Temps en ms après lequel le compteur est réinitialisé (boot réussi)
 */
#define BOOT_LOOP_SUCCESS_TIMEOUT_MS 30000  // 30 secondes

/**
 * @brief Initialise le système de protection contre les boot loops
 *
 * Vérifie le compteur de boot en LP SRAM et déclenche un factory reset
 * si le nombre de redémarrages consécutifs dépasse le seuil.
 *
 * @return ESP_OK si OK, ESP_FAIL si factory reset déclenché
 */
esp_err_t boot_loop_guard_init(void);

/**
 * @brief Marque le démarrage comme réussi et réinitialise le compteur
 *
 * À appeler après que tous les composants critiques ont démarré avec succès.
 */
void boot_loop_guard_mark_success(void);

/**
 * @brief Obtient le compteur actuel de boot loops
 *
 * @return Nombre de redémarrages consécutifs
 */
uint32_t boot_loop_guard_get_count(void);

#endif // BOOT_LOOP_GUARD_H
