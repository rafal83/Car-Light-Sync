#include "nvs_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "NVS_MANAGER";

// ============================================================================
// Helper function to open NVS handle
// ============================================================================

static esp_err_t nvs_open_handle(const char *namespace, nvs_open_mode_t open_mode, nvs_handle_t *handle)
{
    esp_err_t err = nvs_open(namespace, open_mode, handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace, esp_err_to_name(err));
    }
    return err;
}

// ============================================================================
// uint8_t operations
// ============================================================================

esp_err_t nvs_manager_set_u8(const char *namespace, const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set u8 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

uint8_t nvs_manager_get_u8(const char *namespace, const char *key, uint8_t default_value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint8_t value = default_value;
    err = nvs_get_u8(handle, key, &value);
    nvs_close(handle);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to get u8 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return value;
}

// ============================================================================
// Boolean operations
// ============================================================================

esp_err_t nvs_manager_set_bool(const char *namespace, const char *key, bool value)
{
    return nvs_manager_set_u8(namespace, key, value ? 1 : 0);
}

bool nvs_manager_get_bool(const char *namespace, const char *key, bool default_value)
{
    uint8_t value = nvs_manager_get_u8(namespace, key, default_value ? 1 : 0);
    return value != 0;
}

// ============================================================================
// uint16_t operations
// ============================================================================

esp_err_t nvs_manager_set_u16(const char *namespace, const char *key, uint16_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u16(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set u16 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

uint16_t nvs_manager_get_u16(const char *namespace, const char *key, uint16_t default_value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint16_t value = default_value;
    err = nvs_get_u16(handle, key, &value);
    nvs_close(handle);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to get u16 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return value;
}

// ============================================================================
// uint32_t operations
// ============================================================================

esp_err_t nvs_manager_set_u32(const char *namespace, const char *key, uint32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set u32 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

uint32_t nvs_manager_get_u32(const char *namespace, const char *key, uint32_t default_value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint32_t value = default_value;
    err = nvs_get_u32(handle, key, &value);
    nvs_close(handle);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to get u32 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return value;
}

// ============================================================================
// int32_t operations
// ============================================================================

esp_err_t nvs_manager_set_i32(const char *namespace, const char *key, int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set i32 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

int32_t nvs_manager_get_i32(const char *namespace, const char *key, int32_t default_value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    int32_t value = default_value;
    err = nvs_get_i32(handle, key, &value);
    nvs_close(handle);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to get i32 '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return value;
}

// ============================================================================
// String operations
// ============================================================================

esp_err_t nvs_manager_set_str(const char *namespace, const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set str '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

esp_err_t nvs_manager_get_str(const char *namespace, const char *key,
                               char *out_buffer, size_t buffer_size,
                               const char *default_value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (default_value) {
            strncpy(out_buffer, default_value, buffer_size - 1);
            out_buffer[buffer_size - 1] = '\0';
        }
        return err;
    }

    size_t required_size = buffer_size;
    err = nvs_get_str(handle, key, out_buffer, &required_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND && default_value) {
        strncpy(out_buffer, default_value, buffer_size - 1);
        out_buffer[buffer_size - 1] = '\0';
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get str '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

// ============================================================================
// Blob operations
// ============================================================================

esp_err_t nvs_manager_set_blob(const char *namespace, const char *key,
                                const void *value, size_t length)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, key, value, length);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set blob '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

esp_err_t nvs_manager_get_blob(const char *namespace, const char *key,
                                void *out_buffer, size_t *buffer_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, key, out_buffer, buffer_size);
    nvs_close(handle);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to get blob '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

// ============================================================================
// Erase operations
// ============================================================================

esp_err_t nvs_manager_erase_key(const char *namespace, const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase key '%s:%s': %s", namespace, key, esp_err_to_name(err));
    }

    return err;
}

esp_err_t nvs_manager_erase_namespace(const char *namespace)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase namespace '%s': %s", namespace, esp_err_to_name(err));
    }

    return err;
}
