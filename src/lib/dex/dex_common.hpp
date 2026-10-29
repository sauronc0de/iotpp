#pragma once

#include <cstdint>

namespace dex
{

#pragma pack(push, 1)

struct Version
{
  std::uint8_t major;
  std::uint8_t minor;
  std::uint8_t patch;
};

template <typename T>
struct MonitoringPublication
{
  std::uint32_t timestamp; // Unix UTC seconds at acquisition/event time.
  T data;
};

enum class Event : std::uint8_t
{
  Wakeup,
};

enum class CommandId : std::uint8_t
{
  Info = 0x00,          //* Return device Info
  Status,               //* Return device Status
  Config,               //* Set/Get device configuration
  PeripheralCmd = 0x10, //* Peripheral command
  Reset = 0xFF          //* Reset the device
};

enum class State : std::uint16_t
{
  None = 0,

  // Lifecycle
  Initializing = 1UL << 0,
  Ready = 1UL << 1,
  Running = 1UL << 2,
  Stopping = 1UL << 3,
  Stopped = 1UL << 4,

  // Operation
  Busy = 1UL << 5,
  Paused = 1UL << 6,
  Sleeping = 1UL << 7,
  Suspended = 1UL << 8,
  Waiting = 1UL << 9,

  // Conditions
  Degraded = 1UL << 10,
  Warning = 1UL << 11,
  Fault = 1UL << 12,

  // Availability
  Disabled = 1UL << 13,
  Offline = 1UL << 14,

  Reserved = 1UL << 15
};

enum class Error : std::uint32_t
{
  None = 0,

  // General
  Undefined = 1UL << 0,
  InitializationFailed = 1UL << 1,
  OperationFailed = 1UL << 2,
  NotReady = 1UL << 3,
  Timeout = 1UL << 4,
  ResourceUnavailable = 1UL << 5,

  // Configuration
  InvalidConfiguration = 1UL << 6,
  InvalidParameter = 1UL << 7,
  OutOfRange = 1UL << 8,
  UnsupportedOperation = 1UL << 9,

  // Communication
  CommunicationFailed = 1UL << 10,
  ConnectionLost = 1UL << 11,
  InvalidData = 1UL << 12,
  BufferOverflow = 1UL << 13,
  TransferFailed = 1UL << 14,

  // Hardware
  HardwareFailed = 1UL << 15,
  SensorFailed = 1UL << 16,
  ActuatorFailed = 1UL << 17,
  PeripheralFailed = 1UL << 18,

  // Memory & Storage
  MemoryFailed = 1UL << 19,
  StorageFailed = 1UL << 20,
  DataCorrupted = 1UL << 21,

  // Power & Environment
  PowerFailed = 1UL << 22,
  UnderVoltage = 1UL << 23,
  OverVoltage = 1UL << 24,
  OverCurrent = 1UL << 25,
  OverTemperature = 1UL << 26,

  // Firmware & System
  FirmwareFailed = 1UL << 27,
  WatchdogReset = 1UL << 28,
  ClockFailed = 1UL << 29,
  SecurityFailed = 1UL << 30,

  // Reserved
  Reserved = 1UL << 31
};

enum class Notification : std::uint32_t
{
  None = 0,
  Calibrated
};

struct StatusData
{
  State state;
  Error errors;
  Notification notifications;
};

#pragma pack(pop)

} // namespace dex
