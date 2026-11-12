#pragma once

// REST + Server-Sent Events API and the embedded web UI.

#include "devices.hpp"

#include <memory>
#include <string>

namespace httplib
{
class Server;
}

namespace hub
{
class HttpApi
{
public:
  HttpApi(DeviceManager &devices, Storage &storage, EventBus &bus, std::string webRoot);
  ~HttpApi();
  bool listen(const std::string &host, int port); // Blocks until stop().
  void stop();

private:
  void routes();
  void static_files();

  DeviceManager &devices_;
  Storage &storage_;
  EventBus &bus_;
  std::string webRoot_;
  std::unique_ptr<httplib::Server> server_;
};
} // namespace hub
