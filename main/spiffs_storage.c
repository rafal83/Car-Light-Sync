#include "spiffs_storage.h"

#include "esp_log.h"
#include "esp_spiffs.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool s_spiffs_initialized = false;

esp_err_t spiffs_storage_init(void) {
  if (s_spiffs_initialized) {
    ESP_LOGW(TAG_SPIFFS, "SPIFFS already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG_SPIFFS, "Initializing SPIFFS...");

  esp_vfs_spiffs_conf_t conf = {
      .base_path             = "/spiffs",
      .partition_label       = "spiffs",
      .max_files             = 10,  // Up to 10 files open simultaneously
      .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG_SPIFFS, "Failed to mount SPIFFS");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG_SPIFFS, "SPIFFS partition not found");
    } else {
      ESP_LOGE(TAG_SPIFFS, "SPIFFS initialization error (%s)", esp_err_to_name(ret));
    }
    return ret;
  }

  // Retrieve SPIFFS stats
  size_t total = 0, used = 0;
  ret          = esp_spiffs_info("spiffs", &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_SPIFFS, "Failed to read SPIFFS stats (%s)", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG_SPIFFS, "SPIFFS mounted: %d KB total, %d KB used (%d%% free)",
           total / 1024, used / 1024, ((total - used) * 100) / total);

  // Create base directories if they are missing
  mkdir("/spiffs/config", 0755);
  mkdir("/spiffs/profiles", 0755);
  mkdir("/spiffs/audio", 0755);
  mkdir("/spiffs/ble", 0755);
  mkdir("/spiffs/logs", 0755);

  s_spiffs_initialized = true;
  return ESP_OK;
}

esp_err_t spiffs_save_json(const char *path, const char *json_string) {
  if (!s_spiffs_initialized) {
    ESP_LOGE(TAG_SPIFFS, "SPIFFS not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!path || !json_string) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t len = strlen(json_string);

  // If the file already exists, delete it before checking free space.
  // SPIFFS only frees blocks after explicit deletion, not merely after truncation.
  bool file_existed = spiffs_file_exists(path);
  if (file_existed) {
    if (unlink(path) != 0) {
      ESP_LOGW(TAG_SPIFFS, "Failed to delete previous file %s (errno=%d)", path, errno);
      // Continue anyway; fopen("w") will truncate
    } else {
      ESP_LOGD(TAG_SPIFFS, "Previous file removed to free space: %s", path);
    }
  }

  // Check available space now (after the optional deletion)
  size_t total = 0, used = 0;
  spiffs_get_stats(&total, &used);
  size_t free = total - used;

  // SPIFFS needs headroom for metadata plus block allocation
  const size_t SPIFFS_OVERHEAD = 8192; // Minimum 8 KB margin
  if (free < len + SPIFFS_OVERHEAD) {
    ESP_LOGE(TAG_SPIFFS, "SPIFFS nearly full: need %d bytes + %d overhead, only %d available",
             len, SPIFFS_OVERHEAD, free);
    return ESP_ERR_NO_MEM;
  }

  // Create the new file
  FILE *f = fopen(path, "w");
  if (f == NULL) {
    int err            = errno;
    const char *err_msg = "Unknown";
    if (err == ENOMEM) err_msg = "ENOMEM (out of memory)";
    else if (err == EMFILE) err_msg = "EMFILE (too many open files)";
    else if (err == ENOSPC) err_msg = "ENOSPC (no space left)";
    else if (err == ENOENT) err_msg = "ENOENT (directory not found)";

    ESP_LOGE(TAG_SPIFFS, "Failed to open %s: errno=%d (%s), SPIFFS: %d/%d bytes used (%.1f%%)",
             path, err, err_msg, used, total, total > 0 ? (used * 100.0 / total) : 0);
    return ESP_FAIL;
  }

  size_t written = fwrite(json_string, 1, len, f);

  if (written != len) {
    ESP_LOGE(TAG_SPIFFS, "Write error %s: written=%d, expected=%d (ferror=%d, errno=%d)",
             path, written, len, ferror(f), errno);
    fclose(f);
    return ESP_FAIL;
  }

  fclose(f);  // fclose already performs a flush

  ESP_LOGD(TAG_SPIFFS, "File saved: %s (%d bytes)", path, written);
  return ESP_OK;
}

esp_err_t spiffs_load_json(const char *path, char *buffer, size_t buffer_size) {
  if (!s_spiffs_initialized) {
    ESP_LOGE(TAG_SPIFFS, "SPIFFS not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!path || !buffer || buffer_size == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ESP_LOGD(TAG_SPIFFS, "File not found: %s", path);
    return ESP_ERR_NOT_FOUND;
  }

  // Read the entire content
  size_t read = fread(buffer, 1, buffer_size - 1, f);
  fclose(f);

  buffer[read] = '\0';  // Null-terminator

  if (read == 0) {
    ESP_LOGW(TAG_SPIFFS, "Empty file detected: %s - removing automatically", path);
    unlink(path);  // Remove the empty file to free the slot
    return ESP_ERR_NOT_FOUND;  // Behave as if the file does not exist
  }

  ESP_LOGD(TAG_SPIFFS, "File loaded: %s (%d bytes)", path, read);
  return ESP_OK;
}

esp_err_t spiffs_delete_file(const char *path) {
  if (!s_spiffs_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (unlink(path) != 0) {
    ESP_LOGW(TAG_SPIFFS, "Failed to delete file %s", path);
    return ESP_FAIL;
  }

  ESP_LOGD(TAG_SPIFFS, "File deleted: %s", path);
  return ESP_OK;
}

bool spiffs_file_exists(const char *path) {
  if (!s_spiffs_initialized) {
    return false;
  }

  struct stat st;
  return (stat(path, &st) == 0);
}

int spiffs_get_file_size(const char *path) {
  if (!s_spiffs_initialized) {
    return -1;
  }

  struct stat st;
  if (stat(path, &st) != 0) {
    return -1;
  }

  return st.st_size;
}

esp_err_t spiffs_get_stats(size_t *total, size_t *used) {
  if (!s_spiffs_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  return esp_spiffs_info("spiffs", total, used);
}

esp_err_t spiffs_format(void) {
  ESP_LOGW(TAG_SPIFFS, "Formatting SPIFFS...");

  if (s_spiffs_initialized) {
    esp_vfs_spiffs_unregister("spiffs");
    s_spiffs_initialized = false;
  }

  esp_err_t ret = esp_spiffs_format("spiffs");
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_SPIFFS, "SPIFFS format error (%s)", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG_SPIFFS, "SPIFFS formatted successfully");
  return spiffs_storage_init();  // Mount again
}

esp_err_t spiffs_save_blob(const char *path, const void *data, size_t size) {
  if (!s_spiffs_initialized || !path || !data || size == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG_SPIFFS, "Failed to open file %s for writing", path);
    return ESP_FAIL;
  }

  size_t written = fwrite(data, 1, size, f);
  fclose(f);

  if (written != size) {
    ESP_LOGE(TAG_SPIFFS, "Write error for file %s", path);
    return ESP_FAIL;
  }

  ESP_LOGD(TAG_SPIFFS, "Blob saved: %s (%d bytes)", path, written);
  return ESP_OK;
}

esp_err_t spiffs_load_blob(const char *path, void *buffer, size_t *buffer_size) {
  if (!s_spiffs_initialized || !path || !buffer || !buffer_size) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    ESP_LOGD(TAG_SPIFFS, "File not found: %s", path);
    return ESP_ERR_NOT_FOUND;
  }

  // Read the full content
  size_t read = fread(buffer, 1, *buffer_size, f);
  fclose(f);

  if (read == 0) {
    ESP_LOGW(TAG_SPIFFS, "Empty file detected: %s - removing automatically", path);
    unlink(path);  // Remove the empty file to free the slot
    return ESP_ERR_NOT_FOUND;  // Behave as if the file does not exist
  }

  *buffer_size = read; // Return the actual size read

  ESP_LOGD(TAG_SPIFFS, "Blob loaded: %s (%d bytes)", path, read);
  return ESP_OK;
}
