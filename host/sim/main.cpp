// iotpp-sim: simulates one or more ESP32 sentinels speaking DEX over UDP, so
// the hub and its web UI can be exercised without hardware.
//
// Each simulated device binds its own loopback address (127.0.0.10, .11, ...)
// on the DEX port, answers requests like the firmware does and publishes
// monitoring data (Pikachu power meter, Snorlax door) to the hub. It also
// raises notifications/errors from time to time and runs fake OTA updates.

#include "dex.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::system_clock;
using dex::CommandId;
using dex::Error;

constexpr std::uint8_t PikachuId = 1, MewId = 2, SnorlaxId = 3;
std::atomic<bool> running{true};

std::int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
std::uint32_t now_s() { return static_cast<std::uint32_t>(now_ms() / 1000); }

template <typename E>
E operator_or(E a, E b)
{
  using U = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}

int udp_socket(in_addr ip, std::uint16_t port)
{
  const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr = ip;
  local.sin_port = htons(port);
  if(fd < 0 || bind(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) < 0)
    throw std::system_error(errno, std::generic_category(), std::string("bind ") + inet_ntoa(ip) + ":" + std::to_string(port));
  return fd;
}

class Device
{
public:
  Device(int index, in_addr ip, std::uint16_t port, sockaddr_in hub, std::string kinds)
      : index_(index), ip_(ip), hub_(hub), rng_(1234u + index), server_(udp_socket(ip, port)), publisher_(udp_socket(ip, 0))
  {
    hasPikachu_ = kinds.find('p') != std::string::npos;
    hasSnorlax_ = kinds.find('s') != std::string::npos;
    hasMew_ = kinds.find('m') != std::string::npos;
    std::strcpy(mew_.slots[0].label, "ota_0");
    std::strcpy(mew_.slots[0].version, "1.4.2");
    mew_.slots[0].state = dex::Mew::SlotState::Valid;
    mew_.slots[0].running = mew_.slots[0].nextBoot = 1;
    std::strcpy(mew_.slots[1].label, "ota_1");
    std::strcpy(mew_.slots[1].version, "1.4.1");
    mew_.slots[1].state = dex::Mew::SlotState::Valid;
    mew_.state = dex::State::Ready;
    pikachuConfig_.monitoring = {1000, 10000, 1};
    snorlaxConfig_.monitoring = {50, 0, 1};
    nextDoor_ = now_ms() + uniform(8000, 20000);
    nextIncident_ = now_ms() + uniform(15000, 40000);
    std::cout << "Simulated device " << inet_ntoa(ip) << ':' << port << " [" << kinds << "]\n";
  }
  ~Device()
  {
    close(server_);
    close(publisher_);
  }

  void run()
  {
    std::uint8_t buffer[dex::MaxDatagramSize + 1];
    while(running)
    {
      pollfd fd{server_, POLLIN, 0};
      if(poll(&fd, 1, 50) > 0)
      {
        sockaddr_in peer{};
        socklen_t size = sizeof(peer);
        const auto received = recvfrom(server_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&peer), &size);
        if(received > 0 && now_ms() >= rebootUntil_)
          handle(buffer, static_cast<std::size_t>(received), peer);
      }
      if(now_ms() >= rebootUntil_)
        tick();
    }
  }

private:
  double uniform(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng_); }
  bool chance(double p) { return uniform(0, 1) < p; }

  // ---- Simulation -----------------------------------------------------------------
  void tick()
  {
    const auto now = now_ms();
    if(hasPikachu_ && !pikachuPaused_ && pikachuConfig_.monitoring.enabled)
    {
      if(now >= nextSample_)
      {
        nextSample_ = now + pikachuConfig_.monitoring.samplePeriodMs;
        sample(now);
      }
      if(now >= nextPublish_)
      {
        nextPublish_ = now + pikachuConfig_.monitoring.publishPeriodMs;
        for(const auto &publication : pending_)
          publish(PikachuId, publication);
        pending_.clear();
      }
    }
    if(hasSnorlax_ && snorlaxConfig_.enabled && !snorlaxPaused_ && now >= nextDoor_)
    {
      door_ = door_ == dex::Snorlax::State::Open ? dex::Snorlax::State::Closed : dex::Snorlax::State::Open;
      nextDoor_ = now + (door_ == dex::Snorlax::State::Open ? uniform(3000, 12000) : uniform(20000, 60000));
      publish(SnorlaxId, dex::Snorlax::MonitoringPublication{now_s(), door_});
    }
    if(hasMew_)
      ota(now);
    incidents(now);
  }

  void sample(std::int64_t now)
  {
    const double t = now / 1000.0;
    dex::Pikachu::BasicSample s{};
    const double voltage = 230.0 + 3.0 * std::sin(t / 300.0 + index_) + uniform(-0.6, 0.6);
    s.frequency = static_cast<std::uint16_t>(std::lround((50.0 + 0.04 * std::sin(t / 47.0) + uniform(-0.01, 0.01)) * 100));
    s.voltage = static_cast<std::uint16_t>(std::lround(voltage * 10));
    s.peakVoltage = static_cast<std::uint16_t>(std::lround(voltage * std::sqrt(2.0) * 10));
    // Channel 1: fridge compressor cycling. Channel 2: kettle bursts. Channel 3: solar (negative power).
    const bool compressor = std::fmod(t + index_ * 97, 900.0) < 420.0;
    if(kettleUntil_ < now && chance(0.004))
      kettleUntil_ = now + static_cast<std::int64_t>(uniform(30000, 120000));
    const double currents[3] = {
        (compressor ? 1.1 : 0.25) + uniform(-0.05, 0.05),
        (kettleUntil_ > now ? 8.7 : 0.35) + uniform(-0.08, 0.08),
        std::max(0.0, 6.0 * std::sin(t / 1800.0) + uniform(-0.2, 0.2))};
    const double factors[3] = {0.82 + uniform(-0.02, 0.02), 0.97 + uniform(-0.01, 0.01), -0.99};
    for(std::size_t ch = 0; ch < 3; ++ch)
    {
      auto &c = s.channels[ch];
      const double power = voltage * currents[ch] * factors[ch];
      energy_[ch] += power * pikachuConfig_.monitoring.samplePeriodMs / 3600000.0;
      c.current = static_cast<std::uint16_t>(std::lround(currents[ch] * 100));
      c.peakCurrent = static_cast<std::uint16_t>(std::lround(currents[ch] * std::sqrt(2.0) * 100 * uniform(1.0, 1.08)));
      c.power = static_cast<std::int16_t>(std::lround(power));
      c.powerFactor = static_cast<std::int16_t>(std::lround(factors[ch] * 1000));
      c.energy = energy_[ch];
    }
    pending_.push_back({now_s(), s});
    if(pending_.size() > 900)
      pending_.erase(pending_.begin());
  }

  void ota(std::int64_t now)
  {
    using P = dex::Mew::Phase;
    if(mew_.phase == P::Idle || now < mewNext_)
      return;
    mewNext_ = now + 500;
    switch(mew_.phase)
    {
    case P::Checking:
      mew_.phase = P::Downloading;
      mew_.total = 1572864;
      mew_.downloaded = 0;
      std::strcpy(mew_.candidate, "1.5.0");
      break;
    case P::Downloading:
      mew_.downloaded = std::min(mew_.total, mew_.downloaded + 98304);
      if(mew_.downloaded == mew_.total)
        mew_.phase = P::Verifying;
      break;
    case P::Verifying:
      mew_.phase = P::Idle;
      mew_.state = dex::State::Ready;
      mew_.notifications = dex::Notification::Calibrated; // Visible "update ready" style notification.
      mewNotifyUntil_ = now + 15000;
      std::strcpy(mew_.slots[1].version, mew_.candidate);
      mew_.slots[1].state = dex::Mew::SlotState::New;
      mew_.slots[1].nextBoot = 1;
      mew_.slots[0].nextBoot = 0;
      break;
    default:
      mew_.phase = P::Idle;
    }
  }

  void incidents(std::int64_t now)
  {
    if(errorUntil_ && now >= errorUntil_)
    {
      errorUntil_ = 0;
      errors_ = Error::None;
      state_ = dex::State::Running;
    }
    if(calibratedUntil_ && now >= calibratedUntil_)
      calibratedUntil_ = 0;
    if(mewNotifyUntil_ && now >= mewNotifyUntil_)
      mewNotifyUntil_ = 0, mew_.notifications = dex::Notification::None;
    if(now < nextIncident_)
      return;
    nextIncident_ = now + static_cast<std::int64_t>(uniform(45000, 120000));
    if(chance(0.6))
      calibratedUntil_ = now + 15000; // Notification on the power meter.
    else
    {
      errors_ = chance(0.5) ? Error::OverTemperature : Error::SensorFailed;
      state_ = operator_or(dex::State::Running, dex::State::Warning);
      errorUntil_ = now + 20000;
    }
  }

  // ---- DEX --------------------------------------------------------------------------
  template <typename T>
  void publish(std::uint8_t id, const T &publication)
  {
    const dex::ResponsePacket<dex::peripheral::AddressedData<T>> packet{CommandId::PeripheralMonitoring, Error::None,
                                                                        {id, publication}};
    sendto(publisher_, &packet, sizeof(packet), 0, reinterpret_cast<const sockaddr *>(&hub_), sizeof(hub_));
  }

  void reply(const sockaddr_in &peer, CommandId command, Error result, dex::Bytes data = {})
  {
    std::uint8_t buffer[dex::MaxDatagramSize];
    const auto encoded = dex::encode_response({command, result, data}, buffer, sizeof(buffer));
    if(encoded)
      sendto(server_, buffer, encoded.size, 0, reinterpret_cast<const sockaddr *>(&peer), sizeof(peer));
  }

  template <typename T>
  void reply_addressed(const sockaddr_in &peer, CommandId command, std::uint8_t id, const T &value)
  {
    const dex::peripheral::AddressedData<T> packet{id, value};
    reply(peer, command, Error::None, dex::as_bytes(packet));
  }

  dex::StatusData peripheral_status(std::uint8_t id) const
  {
    if(id == PikachuId)
      return {pikachuPaused_ ? dex::State::Paused : !pikachuConfig_.monitoring.enabled ? dex::State::Disabled : dex::State::Running,
              Error::None, calibratedUntil_ ? dex::Notification::Calibrated : dex::Notification::None};
    if(id == SnorlaxId)
      return {snorlaxPaused_ ? dex::State::Paused : snorlaxConfig_.enabled ? dex::State::Running : dex::State::Disabled,
              Error::None, dex::Notification::None};
    return {mew_.state, mew_.errors, mew_.notifications};
  }

  void handle(const std::uint8_t *data, std::size_t size, const sockaddr_in &peer)
  {
    dex::Request request;
    const auto error = dex::decode_request({data, size}, request);
    if(size == 0)
      return;
    if(error != Error::None)
      return reply(peer, static_cast<CommandId>(data[0]), error);
    const auto command = request.command;
    switch(command)
    {
    case CommandId::Info:
    {
      std::vector<std::uint8_t> payload;
      const dex::Version firmware{1, 4, 2};
      payload.insert(payload.end(), reinterpret_cast<const std::uint8_t *>(&firmware),
                     reinterpret_cast<const std::uint8_t *>(&firmware) + sizeof(firmware));
      const auto add = [&](auto info, std::uint8_t id, dex::peripheral::PeripheralCapability caps) {
        info.id = id;
        info.version = {1, 0, 0};
        info.capabilities = caps;
        const auto *p = reinterpret_cast<const std::uint8_t *>(&info);
        payload.insert(payload.end(), p, p + sizeof(info));
      };
      using Cap = dex::peripheral::PeripheralCapability;
      if(hasPikachu_)
        add(dex::Pikachu::InfoData{}, PikachuId, operator_or(Cap::Monitoring, Cap::Config));
      if(hasMew_)
        add(dex::Mew::InfoData{}, MewId, operator_or(Cap::Actions, Cap::Config));
      if(hasSnorlax_)
        add(dex::Snorlax::InfoData{}, SnorlaxId, operator_or(operator_or(Cap::Monitoring, Cap::Events), Cap::Config));
      return reply(peer, command, Error::None, {payload.data(), payload.size()});
    }
    case CommandId::Status:
    {
      std::vector<std::uint8_t> payload(sizeof(dex::StatusData));
      const dex::StatusData device{state_, errors_, dex::Notification::None};
      std::memcpy(payload.data(), &device, sizeof(device));
      for(const auto id : {PikachuId, MewId, SnorlaxId})
        if((id == PikachuId && hasPikachu_) || (id == MewId && hasMew_) || (id == SnorlaxId && hasSnorlax_))
        {
          const auto status = peripheral_status(id);
          const auto *p = reinterpret_cast<const std::uint8_t *>(&status);
          payload.insert(payload.end(), p, p + sizeof(status));
        }
      return reply(peer, command, Error::None, {payload.data(), payload.size()});
    }
    case CommandId::Config:
      return reply(peer, command, request.data.size == 0 ? Error::None : Error::UnsupportedOperation);
    case CommandId::Reset:
      reply(peer, command, Error::None);
      rebootUntil_ = now_ms() + 3000;
      energy_[0] = energy_[1] = energy_[2] = 0;
      pending_.clear();
      return;
    default:
      break;
    }

    if(request.data.size == 0)
      return reply(peer, command, Error::InvalidParameter);
    const auto id = request.data.data[0];
    const dex::Bytes body{request.data.data + 1, request.data.size - 1};
    const bool known = (id == PikachuId && hasPikachu_) || (id == MewId && hasMew_) || (id == SnorlaxId && hasSnorlax_);
    if(!known)
      return reply(peer, command, Error::InvalidParameter);

    switch(command)
    {
    case CommandId::PeripheralStatus:
      if(id == PikachuId)
      {
        dex::Pikachu::StatusData status{};
        static_cast<dex::StatusData &>(status) = peripheral_status(id);
        return reply_addressed(peer, command, id, status);
      }
      if(id == SnorlaxId)
      {
        dex::Snorlax::StatusData status{};
        static_cast<dex::StatusData &>(status) = peripheral_status(id);
        status.doorState = door_;
        return reply_addressed(peer, command, id, status);
      }
      mew_.lastCheckUtc = mew_.lastCheckUtc ? mew_.lastCheckUtc : now_s() - 3600;
      return reply_addressed(peer, command, id, mew_);
    case CommandId::PeripheralConfig:
      return config(peer, id, request);
    case CommandId::PeripheralAction:
    {
      dex::Mew::ActionData action{};
      if(id != MewId || dex::decode_data(body, action) != Error::None || action.action != dex::Mew::Action::FirmwareCheck)
        return reply(peer, command, id == MewId ? Error::InvalidParameter : Error::UnsupportedOperation);
      if(mew_.phase != dex::Mew::Phase::Idle)
        return reply(peer, command, Error::NotReady);
      mew_.phase = dex::Mew::Phase::Checking;
      mew_.state = dex::State::Busy;
      mew_.lastCheckUtc = now_s();
      mewNext_ = now_ms() + 1500;
      return reply(peer, command, Error::None, {&id, 1});
    }
    case CommandId::PeripheralPause:
    case CommandId::PeripheralResume:
    {
      const bool pause = command == CommandId::PeripheralPause;
      if(id == PikachuId)
        pikachuPaused_ = pause;
      else if(id == SnorlaxId)
        snorlaxPaused_ = pause;
      else
        return reply(peer, command, Error::UnsupportedOperation);
      return reply(peer, command, Error::None, {&id, 1});
    }
    default:
      return reply(peer, command, Error::UnsupportedOperation);
    }
  }

  void config(const sockaddr_in &peer, std::uint8_t id, const dex::Request &request)
  {
    const auto command = request.command;
    const bool write = request.data.size > 1;
    if(id == PikachuId)
    {
      dex::peripheral::AddressedData<dex::Pikachu::ConfigData> packet{id, pikachuConfig_};
      if(write)
      {
        if(dex::decode_data(request.data, packet) != Error::None || !dex::Pikachu::valid_config(packet.data))
          return reply(peer, command, Error::InvalidParameter);
        pikachuConfig_ = packet.data;
      }
      return reply_addressed(peer, command, id, pikachuConfig_);
    }
    if(id == SnorlaxId)
    {
      dex::peripheral::AddressedData<dex::Snorlax::ConfigData> packet{id, snorlaxConfig_};
      if(write)
      {
        if(dex::decode_data(request.data, packet) != Error::None || packet.data.enabled > 1)
          return reply(peer, command, Error::InvalidParameter);
        if(std::memcmp(&packet.data.monitoring, &snorlaxConfig_.monitoring, sizeof(snorlaxConfig_.monitoring)) != 0)
          return reply(peer, command, Error::UnsupportedOperation); // Fixed acquisition profile, as in firmware.
        snorlaxConfig_ = packet.data;
      }
      return reply_addressed(peer, command, id, snorlaxConfig_);
    }
    dex::peripheral::AddressedData<dex::Mew::ConfigData> packet{id, mewConfig_};
    if(write)
    {
      if(dex::decode_data(request.data, packet) != Error::None || !dex::Mew::valid_config(packet.data))
        return reply(peer, command, Error::InvalidParameter);
      mewConfig_ = packet.data;
    }
    return reply_addressed(peer, command, id, mewConfig_);
  }

  int index_;
  in_addr ip_;
  sockaddr_in hub_;
  std::mt19937 rng_;
  int server_, publisher_;
  bool hasPikachu_ = false, hasSnorlax_ = false, hasMew_ = false;
  bool pikachuPaused_ = false, snorlaxPaused_ = false;

  dex::State state_ = dex::State::Running;
  Error errors_ = Error::None;
  std::int64_t errorUntil_ = 0, calibratedUntil_ = 0, nextIncident_ = 0, rebootUntil_ = 0;

  dex::Pikachu::ConfigData pikachuConfig_{};
  std::vector<dex::Pikachu::MonitoringPublication> pending_;
  double energy_[3]{};
  std::int64_t nextSample_ = 0, nextPublish_ = 0, kettleUntil_ = 0;

  dex::Snorlax::ConfigData snorlaxConfig_{};
  dex::Snorlax::State door_ = dex::Snorlax::State::Closed;
  std::int64_t nextDoor_ = 0;

  dex::Mew::StatusData mew_{};
  dex::Mew::ConfigData mewConfig_{};
  std::int64_t mewNext_ = 0, mewNotifyUntil_ = 0;
};

void usage()
{
  std::cout << "Usage: iotpp-sim [--devices N] [--base IP] [--port PORT] [--hub HOST:PORT] [--layout p,pm,sm]\n"
               "  --devices N     Number of simulated sentinels (default 3)\n"
               "  --base IP       First device address; next ones increment (default 127.0.0.10)\n"
               "  --port PORT     DEX server port on each device (default 4444)\n"
               "  --hub HOST:PORT Monitoring receiver (default 127.0.0.1:4444)\n"
               "  --layout LIST   Peripherals per device, cycled: p=Pikachu s=Snorlax m=Mew (default pms,pm,sm)\n";
}
} // namespace

int main(int argc, char **argv)
{
  int count = 3;
  std::string base = "127.0.0.10", hub = "127.0.0.1:4444", layout = "pms,pm,sm";
  std::uint16_t port = 4444;
  for(int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if(arg == "-h" || arg == "--help")
      return usage(), 0;
    if(i + 1 == argc)
      return usage(), 1;
    const std::string value = argv[++i];
    if(arg == "--devices")
      count = std::max(1, std::stoi(value));
    else if(arg == "--base")
      base = value;
    else if(arg == "--port")
      port = static_cast<std::uint16_t>(std::stoi(value));
    else if(arg == "--hub")
      hub = value;
    else if(arg == "--layout")
      layout = value;
    else
      return usage(), 1;
  }

  sockaddr_in hubAddress{};
  hubAddress.sin_family = AF_INET;
  const auto colon = hub.rfind(':');
  hubAddress.sin_port = htons(static_cast<std::uint16_t>(std::stoi(hub.substr(colon + 1))));
  in_addr first{};
  if(colon == std::string::npos || inet_pton(AF_INET, hub.substr(0, colon).c_str(), &hubAddress.sin_addr) != 1 ||
     inet_pton(AF_INET, base.c_str(), &first) != 1)
    return usage(), 1;

  std::vector<std::string> kinds;
  for(std::size_t start = 0; start <= layout.size();)
  {
    const auto end = std::min(layout.find(',', start), layout.size());
    kinds.push_back(layout.substr(start, end - start));
    start = end + 1;
  }

  signal(SIGINT, [](int) { running = false; });
  signal(SIGTERM, [](int) { running = false; });
  std::vector<std::unique_ptr<Device>> devices;
  std::vector<std::thread> threads;
  try
  {
    for(int i = 0; i < count; ++i)
    {
      in_addr ip{htonl(ntohl(first.s_addr) + static_cast<std::uint32_t>(i))};
      devices.push_back(std::make_unique<Device>(i, ip, port, hubAddress, kinds[static_cast<std::size_t>(i) % kinds.size()]));
    }
  }
  catch(const std::exception &error)
  {
    std::cerr << "iotpp-sim: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Publishing monitoring to " << hub << " (Ctrl+C to stop)" << std::endl;
  for(auto &device : devices)
    threads.emplace_back([&device] { device->run(); });
  for(auto &thread : threads)
    thread.join();
  return 0;
}
