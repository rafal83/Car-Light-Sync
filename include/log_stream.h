#ifndef LOG_STREAM_H
#define LOG_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize log streaming system
 *
 * Sets up internal buffers and prepares for log streaming.
 * Must be called before any other log_stream functions.
 */
esp_err_t log_stream_init(void);

/**
 * @brief Send a log message to all connected SSE clients
 *
 * @param message Log message string (null-terminated)
 * @param level Log level string ("INFO", "WARN", "ERROR", etc.)
 * @param tag Tag/module name
 */
void log_stream_send(const char *message, const char *level, const char *tag);

/**
 * @brief Register an SSE client file descriptor and send initial headers
 *
 * @param fd Socket file descriptor for the SSE client
 * @return ESP_OK on success
 */
esp_err_t log_stream_register_client(int fd);

/**
 * @brief Send initial SSE headers to a client socket
 *
 * @param fd Socket file descriptor
 * @return ESP_OK on success
 */
esp_err_t log_stream_send_headers(int fd);

/**
 * @brief Unregister an SSE client file descriptor
 *
 * @param fd Socket file descriptor to remove
 */
void log_stream_unregister_client(int fd);

/**
 * @brief Get number of active log stream clients
 *
 * @return Number of connected SSE clients
 */
int log_stream_get_client_count(void);

#ifdef __cplusplus
}
#endif

#endif // LOG_STREAM_H
