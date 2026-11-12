// iotpp-hub: home IoT hub. Talks DEX over UDP with every ESP32 on the LAN,
// stores monitoring data in SQLite and serves the web UI + REST/SSE API.

#include "dex_link.hpp"
#include "devices.hpp"
#include "http_api.hpp"
#include "storage.hpp"

#include <pthread.h>
#include <signal.h>

#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace
{
struct Options
{
  std::string httpHost = "0.0.0.0";
  int httpPort = 8080;
  std::uint16_t udpPort = 4444;
  std::uint16_t devicePort = 4444;
  std::string database = "iotpp-hub.db";
  std::string webRoot;
  int pollSeconds = 5;
  int timeoutMs = 1500;
  int retentionDays = 90;
  bool discover = true;
  std::vector<std::string> devices;
};

void usage()
{
  std::cout << "Usage: iotpp-hub [options]\n"
               "  --http HOST:PORT      Web UI / API address (default 0.0.0.0:8080)\n"
               "  --udp-port PORT       Local DEX port: requests + monitoring reception (default 4444)\n"
               "  --device-port PORT    DEX server port on devices (default 4444)\n"
               "  --db FILE             SQLite database (default iotpp-hub.db)\n"
               "  --device HOST[:PORT]  Add a device at startup (repeatable)\n"
               "  --poll SECONDS        Status polling interval (default 5)\n"
               "  --timeout MS          DEX request timeout (default 1500)\n"
               "  --retention DAYS      Keep samples this long (default 90, 0 = forever)\n"
               "  --no-discover         Do not broadcast a DEX Info discovery at startup\n"
               "  --web-root DIR        Serve the UI from DIR instead of the embedded copy (development)\n";
}

int number(std::string_view text, int lo, int hi, std::string_view option)
{
  int value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if(parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < lo || value > hi)
    throw std::runtime_error("Invalid value for " + std::string(option) + ": " + std::string(text));
  return value;
}

Options parse(int argc, char **argv)
{
  Options options;
  for(int i = 1; i < argc; ++i)
  {
    const std::string_view arg = argv[i];
    const auto value = [&]() -> std::string_view {
      if(++i == argc)
        throw std::runtime_error(std::string(arg) + " requires a value");
      return argv[i];
    };
    if(arg == "-h" || arg == "--help")
      usage(), std::exit(0);
    else if(arg == "--http")
    {
      const auto text = value();
      const auto colon = text.rfind(':');
      if(colon == std::string_view::npos)
        options.httpPort = number(text, 1, 65535, arg);
      else
        options.httpHost = std::string(text.substr(0, colon)), options.httpPort = number(text.substr(colon + 1), 1, 65535, arg);
    }
    else if(arg == "--udp-port")
      options.udpPort = static_cast<std::uint16_t>(number(value(), 1, 65535, arg));
    else if(arg == "--device-port")
      options.devicePort = static_cast<std::uint16_t>(number(value(), 1, 65535, arg));
    else if(arg == "--db")
      options.database = value();
    else if(arg == "--device")
      options.devices.emplace_back(value());
    else if(arg == "--poll")
      options.pollSeconds = number(value(), 1, 3600, arg);
    else if(arg == "--timeout")
      options.timeoutMs = number(value(), 50, 30000, arg);
    else if(arg == "--retention")
      options.retentionDays = number(value(), 0, 36500, arg);
    else if(arg == "--no-discover")
      options.discover = false;
    else if(arg == "--web-root")
      options.webRoot = value();
    else
      throw std::runtime_error("Unknown option: " + std::string(arg));
  }
  return options;
}
} // namespace

int main(int argc, char **argv)
{
  try
  {
    const auto options = parse(argc, argv);

    // Handle SIGINT/SIGTERM on a dedicated thread; block them everywhere else.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    signal(SIGPIPE, SIG_IGN);

    hub::schema(); // Build the registry early: fail fast on DEX description errors.
    hub::Storage storage(options.database);
    hub::EventBus bus;
    hub::DeviceManager devices(storage, bus,
                               {options.devicePort, std::chrono::milliseconds(options.timeoutMs),
                                std::chrono::seconds(options.pollSeconds)});
    hub::DexLink link(options.udpPort, [&](const hub::Endpoint &from, dex::CommandId command, const hub::Reply &reply) {
      devices.on_datagram(from, command, reply);
    });
    devices.attach(link);
    for(const auto &device : options.devices)
    {
      const auto colon = device.rfind(':');
      const auto host = colon == std::string::npos ? device : device.substr(0, colon);
      const auto port = colon == std::string::npos ? options.devicePort : number(std::string_view(device).substr(colon + 1), 1, 65535, "--device");
      devices.add(host, static_cast<std::uint16_t>(port), "");
    }
    if(options.discover)
      devices.discover();

    hub::HttpApi api(devices, storage, bus, options.webRoot);

    std::atomic<bool> running{true};
    std::thread maintenance([&] {
      // Hourly retention pruning, interruptible at shutdown.
      for(int tick = 0; running; ++tick, std::this_thread::sleep_for(std::chrono::seconds(1)))
        if(options.retentionDays > 0 && tick % 3600 == 0)
        {
          const auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * options.retentionDays);
          storage.prune(std::chrono::duration_cast<std::chrono::milliseconds>(cutoff.time_since_epoch()).count());
        }
    });
    std::thread signalThread([&] {
      int received = 0;
      sigwait(&signals, &received);
      std::cout << "\nStopping iotpp-hub..." << std::endl;
      running = false;
      api.stop();
    });

    std::cout << "iotpp-hub\n"
              << "  Web UI : http://" << (options.httpHost == "0.0.0.0" ? "localhost" : options.httpHost) << ':'
              << options.httpPort << "/\n"
              << "  DEX    : UDP 0.0.0.0:" << options.udpPort << " (devices on port " << options.devicePort << ")\n"
              << "  DB     : " << options.database << '\n'
              << "  Types  : ";
    for(const auto &type : hub::peripheral_types())
      std::cout << type.name << '(' << int(type.type) << ") ";
    std::cout << std::endl;

    if(!api.listen(options.httpHost, options.httpPort))
    {
      std::cerr << "Cannot listen on " << options.httpHost << ':' << options.httpPort << '\n';
      pthread_kill(signalThread.native_handle(), SIGTERM);
    }
    signalThread.join();
    running = false;
    maintenance.join();
    devices.stop();
    return 0;
  }
  catch(const std::exception &error)
  {
    std::cerr << "iotpp-hub: " << error.what() << '\n';
    return 1;
  }
}
