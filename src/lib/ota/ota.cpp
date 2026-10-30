#include "ota.hpp"
#include "ota_build.hpp"
#include "ota_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "cJSON.h"
#include "config_store.hpp"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "utc.hpp"
#include "wifi.h"

namespace ota
{
namespace
{
constexpr char Tag[] = "ota";
constexpr config_store::PeripheralKey ConfigKey{151, CONFIG_OTA_PERIPHERAL_ID};
constexpr std::int64_t Second = 1000000;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t configMutex = nullptr;
TaskHandle_t task = nullptr;
Config config{CONFIG_OTA_CHECK_INTERVAL_SECONDS};
Status current;
unsigned configRevision = 0;
bool busy = false, requested = false, pendingBoot = false;
bool initialCheckReserved = false;
char rejectedHash[65]{};

struct StoredConfig
{
  std::uint32_t schema = 1;
  std::uint32_t intervalSeconds = CONFIG_OTA_CHECK_INTERVAL_SECONDS;
};

void phase(Phase value, esp_err_t error = ESP_OK)
{
  portENTER_CRITICAL(&stateMux);
  current.phase = value;
  current.lastError = error;
  portEXIT_CRITICAL(&stateMux);
}

void refresh_slots()
{
  Slot slots[2]{};
  const auto *running = esp_ota_get_running_partition();
  const auto *boot = esp_ota_get_boot_partition();
  for(unsigned i = 0; i < 2; ++i)
  {
    const auto *partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0 + i), nullptr);
    if(!partition)
      continue;
    auto &slot = slots[i];
    std::snprintf(slot.label, sizeof(slot.label), "%s", partition->label);
    slot.running = running && running->address == partition->address;
    slot.nextBoot = boot && boot->address == partition->address;
    esp_app_desc_t descriptor{};
    if(esp_ota_get_partition_description(partition, &descriptor) != ESP_OK)
    {
      std::uint8_t magic = 0;
      slot.state = esp_partition_read(partition, 0, &magic, 1) == ESP_OK && magic == 0xff
          ? SlotState::Empty : SlotState::Unknown;
      continue;
    }
    std::memcpy(slot.version, descriptor.version, sizeof(descriptor.version));
    slot.state = SlotState::Unknown;
    esp_ota_img_states_t state;
    if(esp_ota_get_state_partition(partition, &state) == ESP_OK)
    {
      switch(state)
      {
      case ESP_OTA_IMG_NEW: slot.state = SlotState::New; break;
      case ESP_OTA_IMG_PENDING_VERIFY: slot.state = SlotState::PendingVerify; break;
      case ESP_OTA_IMG_VALID: slot.state = SlotState::Valid; break;
      case ESP_OTA_IMG_INVALID: slot.state = SlotState::Invalid; break;
      case ESP_OTA_IMG_ABORTED: slot.state = SlotState::Aborted; break;
      default: break;
      }
    }
  }
  portENTER_CRITICAL(&stateMux);
  std::memcpy(current.slots, slots, sizeof(slots));
  portEXIT_CRITICAL(&stateMux);
}

// Record the attempted release before boot selection, so a rollback can quarantine it.
esp_err_t metadata(const char *attemptHash = nullptr, const char *attemptVersion = nullptr)
{
  nvs_handle_t handle;
  auto error = nvs_open("iotpp_ota", NVS_READWRITE, &handle);
  if(error != ESP_OK)
    return error;
  if(attemptHash)
  {
    error = nvs_set_str(handle, "attempt_hash", attemptHash);
    if(error == ESP_OK)
      error = nvs_set_str(handle, "attempt_version", attemptVersion);
  }
  else
  {
    std::size_t length = sizeof(rejectedHash);
    error = nvs_get_str(handle, "rejected", rejectedHash, &length);
    if(error == ESP_ERR_NVS_NOT_FOUND)
      error = ESP_OK;
    char hash[65]{}, version[33]{};
    length = sizeof(hash);
    const bool hasAttempt = nvs_get_str(handle, "attempt_hash", hash, &length) == ESP_OK;
    length = sizeof(version);
    if(hasAttempt && nvs_get_str(handle, "attempt_version", version, &length) == ESP_OK)
    {
      for(const auto &slot : current.slots)
        if(!slot.running && std::strcmp(slot.version, version) == 0 &&
           (slot.state == SlotState::Aborted || slot.state == SlotState::Invalid))
        {
          std::memcpy(rejectedHash, hash, sizeof(hash));
          error = nvs_set_str(handle, "rejected", hash);
        }
      // Keep metadata until the new image is confirmed, even across resets.
      if(!pendingBoot && error == ESP_OK)
      {
        nvs_erase_key(handle, "attempt_hash");
        nvs_erase_key(handle, "attempt_version");
      }
    }
  }
  if(error == ESP_OK)
    error = nvs_commit(handle);
  nvs_close(handle);
  return error;
}

struct Http
{
  esp_http_client_handle_t handle = nullptr;
  ~Http() { if(handle) esp_http_client_cleanup(handle); }
  esp_err_t open(const char *url)
  {
    if(std::strncmp(url, "https://", 8) != 0)
      return ESP_ERR_INVALID_ARG;
    esp_http_client_config_t options{};
    options.url = url;
    options.timeout_ms = 10000;
    options.disable_auto_redirect = true;
    options.buffer_size = 4096;
    if(OtaCa[0])
      options.cert_pem = OtaCa;
    else
      options.crt_bundle_attach = esp_crt_bundle_attach;
    handle = esp_http_client_init(&options);
    if(!handle)
      return ESP_ERR_NO_MEM;
    auto error = esp_http_client_open(handle, 0);
    if(error != ESP_OK)
      return error;
    if(esp_http_client_fetch_headers(handle) < 0 || esp_http_client_get_status_code(handle) != 200)
      return ESP_FAIL;
    return ESP_OK;
  }
};

struct Manifest
{
  char version[33]{};
  char hash[65]{};
  char image[128]{};
  std::uint32_t size = 0;
};

const char *string_field(const cJSON *json, const char *key)
{
  const auto *value = cJSON_GetObjectItemCaseSensitive(json, key);
  return cJSON_IsString(value) ? value->valuestring : nullptr;
}

esp_err_t read_manifest(Manifest &manifest)
{
  Http http;
  auto error = http.open(CONFIG_OTA_MANIFEST_URL);
  if(error != ESP_OK)
    return error;
  char body[2049]{};
  std::size_t used = 0;
  const auto deadline = esp_timer_get_time() + 60 * Second;
  while(used < sizeof(body) - 1)
  {
    if(esp_timer_get_time() > deadline)
      return ESP_ERR_TIMEOUT;
    const int count = esp_http_client_read(http.handle, body + used, sizeof(body) - 1 - used);
    if(count < 0)
      return ESP_FAIL;
    if(count == 0)
      break;
    used += count;
  }
  if(used == sizeof(body) - 1 || !esp_http_client_is_complete_data_received(http.handle))
    return ESP_ERR_INVALID_SIZE;
  const char *end = nullptr;
  cJSON *json = cJSON_ParseWithLengthOpts(body, used + 1, &end, true);
  if(!json)
    return ESP_ERR_INVALID_RESPONSE;
  const char *application = string_field(json, "application");
  const char *profile = string_field(json, "device_profile");
  const char *chip = string_field(json, "chip");
  const char *version = string_field(json, "version");
  const char *image = string_field(json, "image");
  const char *hash = string_field(json, "sha256");
  const auto *size = cJSON_GetObjectItemCaseSensitive(json, "size");
  const auto *schema = cJSON_GetObjectItemCaseSensitive(json, "schema");
  std::uint32_t parsed = 0;
  bool valid = cJSON_IsNumber(schema) && schema->valuedouble == 1 &&
      application && std::strcmp(application, OtaApplication) == 0 &&
      profile && std::strcmp(profile, OtaProfile) == 0 &&
      chip && std::strcmp(chip, CONFIG_IDF_TARGET) == 0 &&
      parse_version(version, parsed) && std::strlen(version) < sizeof(manifest.version) &&
      valid_image_name(image) && hash && std::strlen(hash) == 64 &&
      cJSON_IsNumber(size) && std::isfinite(size->valuedouble) && size->valuedouble > 0 &&
      size->valuedouble <= UINT32_MAX && std::floor(size->valuedouble) == size->valuedouble;
  if(valid)
  {
    for(unsigned i = 0; i < 64; ++i)
      valid = valid && ((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f'));
  }
  if(valid)
  {
    std::strcpy(manifest.version, version);
    std::strcpy(manifest.image, image);
    std::strcpy(manifest.hash, hash);
    manifest.size = static_cast<std::uint32_t>(size->valuedouble);
  }
  cJSON_Delete(json);
  return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t install(const Manifest &manifest)
{
  const auto *destination = esp_ota_get_next_update_partition(nullptr);
  if(!destination || manifest.size > destination->size)
    return ESP_ERR_INVALID_SIZE;
  char url[768];
  const char *slash = std::strrchr(CONFIG_OTA_MANIFEST_URL, '/');
  if(!slash || std::snprintf(url, sizeof(url), "%.*s/%s",
      int(slash - CONFIG_OTA_MANIFEST_URL), CONFIG_OTA_MANIFEST_URL, manifest.image) >= int(sizeof(url)))
    return ESP_ERR_INVALID_ARG;
  Http http;
  auto error = http.open(url);
  if(error != ESP_OK)
    return error;
  const auto length = esp_http_client_get_content_length(http.handle);
  if(length > 0 && length != manifest.size)
    return ESP_ERR_INVALID_SIZE;
  esp_ota_handle_t handle = 0;
  error = esp_ota_begin(destination, manifest.size, &handle);
  if(error != ESP_OK)
    return error;
  phase(Phase::Downloading);
  psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
  auto hashError = psa_crypto_init();
  if(hashError == PSA_SUCCESS)
    hashError = psa_hash_setup(&sha, PSA_ALG_SHA_256);
  char buffer[4096];
  std::uint32_t downloaded = 0;
  const auto deadline = esp_timer_get_time() + 10 * 60 * Second;
  while(error == ESP_OK && hashError == 0)
  {
    if(esp_timer_get_time() > deadline)
    {
      error = ESP_ERR_TIMEOUT;
      break;
    }
    const int count = esp_http_client_read(http.handle, buffer, sizeof(buffer));
    if(count < 0)
      error = ESP_FAIL;
    else if(count == 0)
      break;
    else if(std::uint32_t(count) > manifest.size - downloaded)
      error = ESP_ERR_INVALID_SIZE;
    else
    {
      hashError = psa_hash_update(&sha, reinterpret_cast<unsigned char *>(buffer), count);
      error = esp_ota_write(handle, buffer, count);
      downloaded += count;
      portENTER_CRITICAL(&stateMux);
      current.downloaded = downloaded;
      portEXIT_CRITICAL(&stateMux);
      vTaskDelay(1); // Let lower-priority application/idle tasks run during flash writes.
    }
  }
  unsigned char digest[32]{};
  std::size_t digestSize = 0;
  if(hashError == PSA_SUCCESS)
    hashError = psa_hash_finish(&sha, digest, sizeof(digest), &digestSize);
  psa_hash_abort(&sha);
  char hex[65]{};
  for(unsigned i = 0; i < 32; ++i)
    std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  if(error == ESP_OK && (hashError != 0 || downloaded != manifest.size ||
      !esp_http_client_is_complete_data_received(http.handle) || std::strcmp(hex, manifest.hash) != 0))
    error = ESP_ERR_OTA_VALIDATE_FAILED;
  phase(Phase::Verifying);
  if(error != ESP_OK)
  {
    esp_ota_abort(handle);
    return error;
  }
  error = esp_ota_end(handle); // Consumes handle, including on failure; validates IDF image/chip/signature.
  if(error != ESP_OK)
    return error;
  esp_app_desc_t descriptor{};
  error = esp_ota_get_partition_description(destination, &descriptor);
  if(error != ESP_OK || std::strncmp(descriptor.version, manifest.version, sizeof(descriptor.version)) != 0 ||
     std::strncmp(descriptor.project_name, OtaApplication, sizeof(descriptor.project_name)) != 0)
    return ESP_ERR_OTA_VALIDATE_FAILED;
  error = metadata(manifest.hash, manifest.version);
  if(error == ESP_OK)
    error = esp_ota_set_boot_partition(destination);
  if(error != ESP_OK)
    return error;
  refresh_slots();
  phase(Phase::RebootPending);
  ESP_LOGI(Tag, "Installed %s; restarting", manifest.version);
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

esp_err_t check()
{
  portENTER_CRITICAL(&stateMux);
  current.downloaded = current.total = 0;
  current.candidate[0] = '\0';
  current.lastCheckUtc = static_cast<std::uint32_t>(std::time(nullptr));
  portEXIT_CRITICAL(&stateMux);
  phase(Phase::Checking);
  Manifest manifest;
  auto error = read_manifest(manifest);
  if(error != ESP_OK)
    return error;
  std::uint32_t offered, running;
  if(!parse_version(esp_app_get_description()->version, running) || !parse_version(manifest.version, offered))
    return ESP_ERR_INVALID_VERSION;
  portENTER_CRITICAL(&stateMux);
  std::memcpy(current.candidate, manifest.version, sizeof(current.candidate));
  current.total = manifest.size;
  portEXIT_CRITICAL(&stateMux);
  if(offered <= running)
    return ESP_OK;
  if(std::strcmp(rejectedHash, manifest.hash) == 0)
    return ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
  return install(manifest);
}

std::int64_t next_period(std::uint32_t seconds)
{
  return esp_timer_get_time() + std::int64_t(seconds) * Second +
      (seconds ? esp_random() % (std::max<std::uint32_t>(1, seconds / 10)) : 0) * Second;
}

void OtaTask(void *)
{
  // Confirmation is entirely local, before any dependency on time/server/network.
  if(pendingBoot)
  {
    phase(Phase::PendingValidation);
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    const auto deadline = esp_timer_get_time() + CONFIG_OTA_BOOT_VALIDATION_SECONDS * Second;
    while(esp_timer_get_time() < deadline)
    {
      ESP_ERROR_CHECK(esp_task_wdt_reset());
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    if(esp_get_free_heap_size() < 16384 || esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
    {
      esp_ota_mark_app_invalid_rollback_and_reboot();
      esp_restart();
    }
    ESP_ERROR_CHECK(esp_task_wdt_delete(nullptr));
    portENTER_CRITICAL(&stateMux);
    pendingBoot = false;
    portEXIT_CRITICAL(&stateMux);
    metadata();
    config_store::detail::end_transaction();
    phase(Phase::Idle);
  }
  refresh_slots();
  unsigned revision = 0;
  std::uint32_t interval = configuration().intervalSeconds;
  std::int64_t next = next_period(30); // Initial check, staggered after startup.
  unsigned retrySeconds = 60;
  while(true)
  {
    portENTER_CRITICAL(&stateMux);
    const bool manual = requested;
    const auto latestRevision = configRevision;
    interval = config.intervalSeconds;
    portEXIT_CRITICAL(&stateMux);
    if(revision != latestRevision)
    {
      revision = latestRevision;
      next = next_period(initialCheckReserved ? 30 : interval);
    }
    if(initialCheckReserved && interval == 0)
    {
      initialCheckReserved = false;
      config_store::detail::end_transaction();
    }
    const bool automatic = interval != 0 && CONFIG_OTA_MANIFEST_URL[0] && esp_timer_get_time() >= next;
    if(manual || automatic)
    {
      if(!manual)
      {
        if(!config_store::detail::begin_transaction())
          break; // Sleep has already committed.
        portENTER_CRITICAL(&stateMux);
        // A manual request may have raced with the automatic deadline.
        const bool alreadyReserved = busy;
        busy = true;
        portEXIT_CRITICAL(&stateMux);
        if(alreadyReserved)
          config_store::detail::end_transaction();
      }
      // A bounded network wait keeps accepted requests observable and prevents
      // a sleeping device from waiting indefinitely for an unavailable server.
      phase(Phase::WaitingNetwork);
      const auto deadline = esp_timer_get_time() + 60 * Second;
      while((!wifi::is_connected() || std::time(nullptr) < 1700000000) && esp_timer_get_time() < deadline)
        vTaskDelay(pdMS_TO_TICKS(250));
      const auto error = !wifi::is_connected() || std::time(nullptr) < 1700000000 ? ESP_ERR_TIMEOUT : check();
      refresh_slots();
      phase(error == ESP_OK ? Phase::Idle : Phase::Error, error);
      if(error != ESP_OK)
        ESP_LOGW(Tag, "Check failed: %s", esp_err_to_name(error));
      next = next_period(error == ESP_OK ? interval : std::min<std::uint32_t>(interval, retrySeconds));
      retrySeconds = error == ESP_OK ? 60 : std::min(3600u, retrySeconds * 2);
      portENTER_CRITICAL(&stateMux);
      busy = requested = false;
      portEXIT_CRITICAL(&stateMux);
      config_store::detail::end_transaction();
      if(initialCheckReserved)
      {
        initialCheckReserved = false;
        config_store::detail::end_transaction();
      }
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
  }
  vTaskDelete(nullptr);
}
} // namespace

esp_err_t initialize()
{
#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  ESP_LOGE(Tag, "OTA requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE");
  return ESP_ERR_INVALID_STATE;
#endif
#if !CONFIG_ESP_TASK_WDT_EN || !CONFIG_ESP_TASK_WDT_INIT || !CONFIG_ESP_TASK_WDT_PANIC
  ESP_LOGE(Tag, "OTA requires an initialized task watchdog with panic/reset enabled");
  return ESP_ERR_INVALID_STATE;
#endif
  if(configMutex)
    return ESP_ERR_INVALID_STATE;
  configMutex = xSemaphoreCreateMutex();
  if(!configMutex)
    return ESP_ERR_NO_MEM;
  const auto *running = esp_ota_get_running_partition();
  if(!running || !esp_ota_get_next_update_partition(nullptr) ||
     !esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr) ||
     !esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr) ||
     !esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr))
    return ESP_ERR_NOT_FOUND;
  StoredConfig stored;
  const auto result = config_store::load_config(ConfigKey, stored);
  if(result != config_store::Result::Ok && result != config_store::Result::NotFound)
    return ESP_FAIL;
  if(stored.schema != 1 || stored.intervalSeconds > 2592000)
    return ESP_ERR_INVALID_ARG;
  config.intervalSeconds = stored.intervalSeconds;
  esp_ota_img_states_t state;
  pendingBoot = esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  if(pendingBoot && !config_store::detail::begin_transaction())
    return ESP_ERR_INVALID_STATE;
  if(pendingBoot)
    phase(Phase::PendingValidation);
  // Sleeping profiles must get a first automatic check before the door can
  // commit chip-wide sleep. Later periodic checks don't keep an idle chip awake.
  initialCheckReserved = config.intervalSeconds != 0 && CONFIG_OTA_MANIFEST_URL[0];
  if(initialCheckReserved && !config_store::detail::begin_transaction())
    return ESP_ERR_INVALID_STATE;
  refresh_slots();
  return metadata();
}

esp_err_t start()
{
  if(!configMutex || task)
    return ESP_ERR_INVALID_STATE;
  auto error = utc::start();
  if(error != ESP_OK)
    return error;
  return xTaskCreate(OtaTask, "ota", 12288, nullptr, 3, &task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t firmware_check()
{
  if(!task || !CONFIG_OTA_MANIFEST_URL[0])
    return ESP_ERR_INVALID_STATE;
  if(!config_store::detail::begin_transaction())
    return ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&stateMux);
  const bool accepted = !busy && !pendingBoot;
  if(accepted)
    busy = requested = true;
  portEXIT_CRITICAL(&stateMux);
  if(!accepted)
  {
    config_store::detail::end_transaction();
    return ESP_ERR_INVALID_STATE;
  }
  xTaskNotifyGive(task);
  return ESP_OK;
}

esp_err_t configure(Config candidate)
{
  if(!configMutex || candidate.intervalSeconds > 2592000)
    return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(configMutex, portMAX_DELAY);
  const StoredConfig stored{1, candidate.intervalSeconds};
  const auto result = config_store::save_config(ConfigKey, stored);
  if(result == config_store::Result::Ok)
  {
    portENTER_CRITICAL(&stateMux);
    config = candidate;
    ++configRevision;
    portEXIT_CRITICAL(&stateMux);
    if(task)
      xTaskNotifyGive(task);
  }
  xSemaphoreGive(configMutex);
  return result == config_store::Result::Ok ? ESP_OK : ESP_FAIL;
}

Config configuration()
{
  portENTER_CRITICAL(&stateMux);
  const auto snapshot = config;
  portEXIT_CRITICAL(&stateMux);
  return snapshot;
}

Status status()
{
  portENTER_CRITICAL(&stateMux);
  const auto snapshot = current;
  portEXIT_CRITICAL(&stateMux);
  return snapshot;
}
} // namespace ota
