#include "dex.hpp"

#include <magic_enum/magic_enum.hpp>
#include <magic_enum/magic_enum_flags.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace magic_enum::customize
{
template <>
struct enum_range<dex::State>
{
  static constexpr bool is_flags = true;
};
template <>
struct enum_range<dex::Error>
{
  static constexpr bool is_flags = true;
};
template <>
struct enum_range<dex::Notification>
{
  static constexpr bool is_flags = true;
};
template <>
struct enum_range<dex::peripheral::PeripheralCapability>
{
  static constexpr bool is_flags = true;
};
} // namespace magic_enum::customize

namespace
{
struct Options
{
  std::string command = "all";
  std::string host = "192.168.41.252";
  std::string port = "4444";
};

void usage()
{
  std::cout << "Usage: dex-client [info|status|config|all] [host] [--port PORT]\n"
               "Defaults: all, 192.168.41.252:4444; timeout: 5 seconds per query.\n";
}

Options parse_options(int argc, char **argv)
{
  Options options;
  int positional = 0;
  for(int i = 1; i < argc; ++i)
  {
    const std::string_view argument = argv[i];
    if(argument == "--port")
    {
      if(++i == argc)
        throw std::runtime_error("--port requires a value");
      options.port = argv[i];
    }
    else if(argument.empty() || argument.front() == '-')
      throw std::runtime_error("Unknown option: " + std::string(argument));
    else if(positional == 0)
    {
      options.command = argument;
      ++positional;
    }
    else if(positional == 1)
    {
      options.host = argument;
      ++positional;
    }
    else
      throw std::runtime_error("Too many arguments");
  }

  if(options.command != "all" && options.command != "info" && options.command != "status" && options.command != "config")
    throw std::runtime_error("Unknown query: " + options.command);
  unsigned port = 0;
  const auto parsed = std::from_chars(options.port.data(), options.port.data() + options.port.size(), port);
  if(parsed.ec != std::errc{} || parsed.ptr != options.port.data() + options.port.size() || port == 0 || port > 65535)
    throw std::runtime_error("Invalid UDP port: " + options.port);
  return options;
}

template <typename E>
void print_enum(std::string_view label, E value)
{
  const auto name = magic_enum::enum_name(value);
  std::cout << label << ": " << (name.empty() ? "Unknown" : name) << " (0x" << std::hex
            << static_cast<std::uint64_t>(value) << std::dec << ")\n";
}

template <typename E>
void print_flags(std::string_view label, E value)
{
  // Zero is not a flag; print its regular enum name instead.
  const auto name = value == E{} ? std::string(magic_enum::enum_name<E, magic_enum::as_common<>>(value)) : magic_enum::enum_flags_name(value);
  std::cout << label << ": " << (name.empty() ? "Unknown" : name) << " (0x" << std::hex
            << static_cast<std::uint64_t>(value) << std::dec << ")\n";
}

template <typename T>
T decode(dex::Bytes bytes)
{
  T value{};
  const auto error = dex::decode_data(bytes, value);
  if(error != dex::Error::None)
    throw std::runtime_error("Invalid response payload: " + std::string(magic_enum::enum_flags_name(error)));
  return value;
}

void print_version(std::string_view label, dex::Version version)
{
  std::cout << label << ": " << unsigned(version.major) << '.' << unsigned(version.minor) << '.' << unsigned(version.patch) << '\n';
}

void print_status(const dex::StatusData &status)
{
  print_flags("State", status.state);
  print_flags("Errors", status.errors);
  print_flags("Notifications", status.notifications);
}

void print_data(const dex::Response &response)
{
  const auto data = response.data;
  switch(response.command)
  {
  case dex::CommandId::Info: {
    // std::array<T, 0> still occupies one byte in device::InfoData<0>.
    if(data.size == sizeof(dex::device::InfoData<0>))
    {
      print_version("Firmware version", decode<dex::device::InfoData<0>>(data).firmwareVersion);
      std::cout << "Peripherals: 0\n";
      break;
    }
    if(data.size < sizeof(dex::Version) || (data.size - sizeof(dex::Version)) % sizeof(dex::peripheral::InfoData) != 0)
      throw std::runtime_error("Invalid Info payload size");
    print_version("Firmware version", decode<dex::Version>({data.data, sizeof(dex::Version)}));
    const auto count = (data.size - sizeof(dex::Version)) / sizeof(dex::peripheral::InfoData);
    std::cout << "Peripherals: " << count << '\n';
    for(std::size_t i = 0; i < count; ++i)
    {
      const auto peripheral = decode<dex::peripheral::InfoData>({data.data + sizeof(dex::Version) + i * sizeof(dex::peripheral::InfoData), sizeof(dex::peripheral::InfoData)});
      std::cout << "Peripheral: " << unsigned(peripheral.id) << '\n';
      print_version("Version", peripheral.version);
      print_flags("Capabilities", peripheral.capabilities);
    }
    break;
  }
  case dex::CommandId::Status:
    if(data.size < sizeof(dex::StatusData) || (data.size - sizeof(dex::StatusData)) % sizeof(dex::peripheral::StatusData) != 0)
      throw std::runtime_error("Invalid Status payload size");
    print_status(decode<dex::StatusData>({data.data, sizeof(dex::StatusData)}));
    for(std::size_t offset = sizeof(dex::StatusData); offset < data.size; offset += sizeof(dex::peripheral::StatusData))
    {
      std::cout << "Peripheral index: " << (offset - sizeof(dex::StatusData)) / sizeof(dex::peripheral::StatusData) << '\n';
      print_status(decode<dex::peripheral::StatusData>({data.data + offset, sizeof(dex::peripheral::StatusData)}));
    }
    break;
  case dex::CommandId::Config:
    // ConfigData currently has no fields; accept a header-only read response too.
    if(data.size != 0)
      decode<dex::device::ConfigData>(data);
    std::cout << "Configuration: no fields defined\n";
    break;
  default:
    throw std::runtime_error("Unexpected response command");
  }
}

class Socket
{
public:
  Socket() : fd_(socket(AF_INET, SOCK_DGRAM, 0))
  {
    if(fd_ < 0)
      throw std::system_error(errno, std::generic_category(), "socket");
  }
  ~Socket() { close(fd_); }
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
  int get() const { return fd_; }

private:
  int fd_;
};

bool request(const sockaddr_in &target, dex::CommandId command)
{
  std::cout << "\n=== " << magic_enum::enum_name(command) << " ===\n"
            << std::flush;
  try
  {
    // Isolate each query so late responses cannot be mistaken for the next one.
    Socket udp;
    const timeval timeout{5, 0};
    if(setsockopt(udp.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
       connect(udp.get(), reinterpret_cast<const sockaddr *>(&target), sizeof(target)) < 0)
      throw std::system_error(errno, std::generic_category(), "UDP setup");

    const dex::RequestHeader packet{command};
    const auto bytes = dex::as_bytes(packet);
    if(send(udp.get(), bytes.data, bytes.size, 0) != static_cast<ssize_t>(bytes.size))
      throw std::system_error(errno, std::generic_category(), "send");

    std::array<std::uint8_t, dex::MaxDatagramSize + 1> buffer{};
    const auto received = recv(udp.get(), buffer.data(), buffer.size(), 0);
    if(received < 0)
    {
      if(errno == EAGAIN || errno == EWOULDBLOCK)
        throw std::runtime_error("No response within 5 seconds");
      throw std::system_error(errno, std::generic_category(), "recv");
    }
    std::cout << "Response:";
    for(ssize_t i = 0; i < received; ++i)
      std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0') << unsigned(buffer[i]);
    std::cout << std::dec << std::setfill(' ') << '\n';

    dex::Response response;
    const auto error = dex::decode_response({buffer.data(), static_cast<std::size_t>(received)}, response);
    if(error != dex::Error::None)
    {
      print_flags("Decode error", error);
      return false;
    }
    print_enum("Command", response.command);
    print_flags("Result", response.result);
    if(response.command != command)
      throw std::runtime_error("Response command does not match request");
    if(response.result != dex::Error::None)
      return false;
    print_data(response);
    return true;
  }
  catch(const std::exception &error)
  {
    std::cerr << "DEX query failed: " << error.what() << '\n';
    return false;
  }
}
} // namespace

int main(int argc, char **argv)
{
  for(int i = 1; i < argc; ++i)
    if(std::string_view(argv[i]) == "--help" || std::string_view(argv[i]) == "-h")
    {
      usage();
      return 0;
    }
  try
  {
    // DEX uses native little-endian packed layouts.
    const std::uint16_t endian = 1;
    if(*reinterpret_cast<const std::uint8_t *>(&endian) != 1)
      throw std::runtime_error("DEX requires a little-endian host");
    const auto options = parse_options(argc, argv);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICSERV;
    addrinfo *addresses = nullptr;
    const auto error = getaddrinfo(options.host.c_str(), options.port.c_str(), &hints, &addresses);
    if(error != 0)
      throw std::runtime_error(gai_strerror(error));
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resolved(addresses, freeaddrinfo);
    const auto target = *reinterpret_cast<const sockaddr_in *>(addresses->ai_addr);
    std::cout << "Target: " << options.host << ':' << options.port << '\n';

    std::vector<dex::CommandId> commands;
    if(options.command == "all" || options.command == "info") commands.push_back(dex::CommandId::Info);
    if(options.command == "all" || options.command == "status") commands.push_back(dex::CommandId::Status);
    if(options.command == "all" || options.command == "config") commands.push_back(dex::CommandId::Config);
    bool success = true;
    for(const auto command : commands)
      if(!request(target, command)) success = false;
    return success ? 0 : 1;
  }
  catch(const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    usage();
    return 1;
  }
}
