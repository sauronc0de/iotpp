#include "config_store.hpp"

#include <cstdio>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace config_store
{
namespace
{
constexpr char Namespace[] = "periph_cfg";
SemaphoreHandle_t storageMutex = nullptr;
portMUX_TYPE gate = portMUX_INITIALIZER_UNLOCKED;
unsigned transactions = 0;
bool initialized = false, sleeping = false;

struct LockedStorage
{
  LockedStorage() { xSemaphoreTake(storageMutex, portMAX_DELAY); }
  ~LockedStorage() { xSemaphoreGive(storageMutex); }
};

void key_name(PeripheralKey key, char (&name)[16])
{
  std::snprintf(name, sizeof(name), "p%04x_%02x", unsigned(key.type), unsigned(key.instance));
}

Result storage_error(esp_err_t error)
{
  if(error == ESP_OK)
    return Result::Ok;
  if(error == ESP_ERR_NVS_NOT_FOUND)
    return Result::NotFound;
  ESP_LOGE("config_store", "NVS: %s", esp_err_to_name(error));
  return Result::StorageFailed;
}
} // namespace

Result initialize()
{
  if(initialized)
    return Result::Ok;
  const auto result = storage_error(nvs_flash_init());
  if(result != Result::Ok)
    return result;
  storageMutex = xSemaphoreCreateMutex();
  if(storageMutex == nullptr)
    return Result::StorageFailed;
  portENTER_CRITICAL(&gate);
  initialized = true;
  portEXIT_CRITICAL(&gate);
  return Result::Ok;
}

namespace detail
{
bool begin_transaction()
{
  portENTER_CRITICAL(&gate);
  const bool allowed = initialized && !sleeping;
  if(allowed)
    ++transactions;
  portEXIT_CRITICAL(&gate);
  return allowed;
}

void end_transaction()
{
  portENTER_CRITICAL(&gate);
  --transactions;
  portEXIT_CRITICAL(&gate);
}

Result read_blob(PeripheralKey key, void *data, std::size_t size)
{
  Transaction transaction;
  if(!transaction)
    return Result::NotReady;
  LockedStorage lock;
  nvs_handle_t handle;
  auto error = nvs_open(Namespace, NVS_READONLY, &handle);
  if(error != ESP_OK)
    return storage_error(error);
  char name[16];
  key_name(key, name);
  std::size_t storedSize = 0;
  error = nvs_get_blob(handle, name, nullptr, &storedSize);
  if(error == ESP_OK && storedSize != size)
  {
    ESP_LOGE("config_store", "%s: stored size %u, expected %u", name, unsigned(storedSize), unsigned(size));
    nvs_close(handle);
    return Result::SizeMismatch;
  }
  if(error == ESP_OK)
    error = nvs_get_blob(handle, name, data, &storedSize);
  nvs_close(handle);
  return storage_error(error);
}

Result write_blob(PeripheralKey key, const void *data, std::size_t size)
{
  Transaction transaction;
  if(!transaction)
    return Result::NotReady;
  LockedStorage lock;
  nvs_handle_t handle;
  auto error = nvs_open(Namespace, NVS_READWRITE, &handle);
  if(error != ESP_OK)
    return storage_error(error);
  char name[16];
  key_name(key, name);
  error = nvs_set_blob(handle, name, data, size);
  if(error == ESP_OK)
    error = nvs_commit(handle);
  nvs_close(handle);
  return storage_error(error);
}
} // namespace detail

bool try_begin_sleep()
{
  portENTER_CRITICAL(&gate);
  const bool allowed = initialized && !sleeping && transactions == 0;
  if(allowed)
    sleeping = true;
  portEXIT_CRITICAL(&gate);
  return allowed;
}
} // namespace config_store
