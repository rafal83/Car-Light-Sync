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
    ESP_LOGW(TAG_SPIFFS, "SPIFFS déjà initialisé");
    return ESP_OK;
  }

  ESP_LOGI(TAG_SPIFFS, "Initialisation SPIFFS...");

  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "spiffs",
      .max_files = 10,  // Max 10 fichiers ouverts simultanément
      .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG_SPIFFS, "Échec du montage SPIFFS");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG_SPIFFS, "Partition SPIFFS introuvable");
    } else {
      ESP_LOGE(TAG_SPIFFS, "Erreur initialisation SPIFFS (%s)", esp_err_to_name(ret));
    }
    return ret;
  }

  // Vérifier les stats SPIFFS
  size_t total = 0, used = 0;
  ret = esp_spiffs_info("spiffs", &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_SPIFFS, "Erreur récupération stats SPIFFS (%s)", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG_SPIFFS, "SPIFFS monté: %d KB total, %d KB utilisés (%d%% libre)",
           total / 1024, used / 1024, ((total - used) * 100) / total);

  // Créer les répertoires de base s'ils n'existent pas
  mkdir("/spiffs/config", 0755);
  mkdir("/spiffs/profiles", 0755);
  mkdir("/spiffs/audio", 0755);
  mkdir("/spiffs/ble", 0755);

  s_spiffs_initialized = true;
  return ESP_OK;
}

esp_err_t spiffs_save_json(const char *path, const char *json_string) {
  if (!s_spiffs_initialized) {
    ESP_LOGE(TAG_SPIFFS, "SPIFFS non initialisé");
    return ESP_ERR_INVALID_STATE;
  }

  if (!path || !json_string) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t len = strlen(json_string);

  // Si le fichier existe déjà, le supprimer AVANT de vérifier l'espace
  // Car SPIFFS ne libère les blocs qu'après suppression explicite, pas juste après troncature
  bool file_existed = spiffs_file_exists(path);
  if (file_existed) {
    if (unlink(path) != 0) {
      ESP_LOGW(TAG_SPIFFS, "Erreur suppression ancien fichier %s (errno=%d)", path, errno);
      // Continuer quand même, fopen("w") va tronquer
    } else {
      ESP_LOGD(TAG_SPIFFS, "Ancien fichier supprimé pour libérer l'espace: %s", path);
    }
  }

  // Vérifier l'espace disponible maintenant (après suppression éventuelle)
  size_t total = 0, used = 0;
  spiffs_get_stats(&total, &used);
  size_t free = total - used;

  // SPIFFS a besoin de marge pour métadonnées + allocation de blocs
  const size_t SPIFFS_OVERHEAD = 8192; // 8KB de marge minimum
  if (free < len + SPIFFS_OVERHEAD) {
    ESP_LOGE(TAG_SPIFFS, "SPIFFS presque plein: besoin de %d bytes + %d overhead, seulement %d disponibles",
             len, SPIFFS_OVERHEAD, free);
    return ESP_ERR_NO_MEM;
  }

  // Maintenant créer le nouveau fichier
  FILE *f = fopen(path, "w");
  if (f == NULL) {
    int err = errno;
    const char *err_msg = "Unknown";
    if (err == ENOMEM) err_msg = "ENOMEM (out of memory)";
    else if (err == EMFILE) err_msg = "EMFILE (too many open files)";
    else if (err == ENOSPC) err_msg = "ENOSPC (no space left)";
    else if (err == ENOENT) err_msg = "ENOENT (directory not found)";

    ESP_LOGE(TAG_SPIFFS, "Erreur ouverture %s: errno=%d (%s), SPIFFS: %d/%d bytes used (%.1f%%)",
             path, err, err_msg, used, total, total > 0 ? (used * 100.0 / total) : 0);
    return ESP_FAIL;
  }

  size_t written = fwrite(json_string, 1, len, f);

  if (written != len) {
    ESP_LOGE(TAG_SPIFFS, "Erreur écriture %s: written=%d, expected=%d (ferror=%d, errno=%d)",
             path, written, len, ferror(f), errno);
    fclose(f);
    return ESP_FAIL;
  }

  fclose(f);  // fclose fait déjà un flush automatique

  ESP_LOGD(TAG_SPIFFS, "Fichier sauvegardé: %s (%d bytes)", path, written);
  return ESP_OK;
}

esp_err_t spiffs_load_json(const char *path, char *buffer, size_t buffer_size) {
  if (!s_spiffs_initialized) {
    ESP_LOGE(TAG_SPIFFS, "SPIFFS non initialisé");
    return ESP_ERR_INVALID_STATE;
  }

  if (!path || !buffer || buffer_size == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ESP_LOGD(TAG_SPIFFS, "Fichier non trouvé: %s", path);
    return ESP_ERR_NOT_FOUND;
  }

  // Lire tout le contenu
  size_t read = fread(buffer, 1, buffer_size - 1, f);
  fclose(f);

  buffer[read] = '\0';  // Null-terminator

  if (read == 0) {
    ESP_LOGW(TAG_SPIFFS, "Fichier vide détecté: %s - suppression automatique", path);
    unlink(path);  // Supprimer le fichier vide pour libérer le slot
    return ESP_ERR_NOT_FOUND;  // Retourner comme si le fichier n'existait pas
  }

  ESP_LOGD(TAG_SPIFFS, "Fichier chargé: %s (%d bytes)", path, read);
  return ESP_OK;
}

esp_err_t spiffs_delete_file(const char *path) {
  if (!s_spiffs_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (unlink(path) != 0) {
    ESP_LOGW(TAG_SPIFFS, "Erreur suppression fichier %s", path);
    return ESP_FAIL;
  }

  ESP_LOGD(TAG_SPIFFS, "Fichier supprimé: %s", path);
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
  ESP_LOGW(TAG_SPIFFS, "Formatage SPIFFS...");

  if (s_spiffs_initialized) {
    esp_vfs_spiffs_unregister("spiffs");
    s_spiffs_initialized = false;
  }

  esp_err_t ret = esp_spiffs_format("spiffs");
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_SPIFFS, "Erreur formatage SPIFFS (%s)", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG_SPIFFS, "SPIFFS formaté avec succès");
  return spiffs_storage_init();  // Remonter
}

esp_err_t spiffs_save_blob(const char *path, const void *data, size_t size) {
  if (!s_spiffs_initialized || !path || !data || size == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG_SPIFFS, "Erreur ouverture fichier %s en écriture", path);
    return ESP_FAIL;
  }

  size_t written = fwrite(data, 1, size, f);
  fclose(f);

  if (written != size) {
    ESP_LOGE(TAG_SPIFFS, "Erreur écriture fichier %s", path);
    return ESP_FAIL;
  }

  ESP_LOGD(TAG_SPIFFS, "Blob sauvegardé: %s (%d bytes)", path, written);
  return ESP_OK;
}

esp_err_t spiffs_load_blob(const char *path, void *buffer, size_t *buffer_size) {
  if (!s_spiffs_initialized || !path || !buffer || !buffer_size) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    ESP_LOGD(TAG_SPIFFS, "Fichier non trouvé: %s", path);
    return ESP_ERR_NOT_FOUND;
  }

  // Lire tout le contenu
  size_t read = fread(buffer, 1, *buffer_size, f);
  fclose(f);

  if (read == 0) {
    ESP_LOGW(TAG_SPIFFS, "Fichier vide détecté: %s - suppression automatique", path);
    unlink(path);  // Supprimer le fichier vide pour libérer le slot
    return ESP_ERR_NOT_FOUND;  // Retourner comme si le fichier n'existait pas
  }

  *buffer_size = read; // Retourner la taille réelle lue

  ESP_LOGD(TAG_SPIFFS, "Blob chargé: %s (%d bytes)", path, read);
  return ESP_OK;
}
