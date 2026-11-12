#include "http_api.hpp"

#include "assets.hpp"

#include <httplib.h>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace hub
{
namespace
{
using httplib::Request;
using httplib::Response;

void send_json(Response &res, const json &body, int status = 200)
{
  res.status = status;
  res.set_header("Cache-Control", "no-store");
  res.set_content(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), "application/json");
}

// Uniform error mapping for every handler.
template <typename F>
auto guarded(F handler)
{
  return [handler](const Request &req, Response &res) {
    try
    {
      handler(req, res);
    }
    catch(const reflect::Error &error)
    {
      send_json(res, {{"error", error.what()}}, 400);
    }
    catch(const json::exception &error)
    {
      send_json(res, {{"error", std::string("Invalid JSON: ") + error.what()}}, 400);
    }
    catch(const std::out_of_range &error)
    {
      send_json(res, {{"error", error.what()}}, 404);
    }
    catch(const std::exception &error)
    {
      send_json(res, {{"error", error.what()}}, 500);
    }
  };
}

json body_of(const Request &req) { return req.body.empty() ? json::object() : json::parse(req.body); }

std::int64_t number_param(const Request &req, const char *name, std::int64_t fallback)
{
  if(!req.has_param(name))
    return fallback;
  const auto text = req.get_param_value(name);
  std::int64_t value = fallback;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

dex::CommandId command_of(const json &value)
{
  if(value.is_number_integer())
    if(const auto command = magic_enum::enum_cast<dex::CommandId>(value.get<int>()))
      return *command;
  if(value.is_string())
    if(const auto command = magic_enum::enum_cast<dex::CommandId>(value.get<std::string>()))
      return *command;
  throw reflect::Error("Unknown DEX command " + value.dump());
}

Buffer parse_hex(const std::string &text)
{
  Buffer bytes;
  std::string digits;
  for(const char c : text)
    if(std::isxdigit(static_cast<unsigned char>(c)))
      digits += c;
    else if(c != ' ' && c != ':' && c != ',' && c != '-')
      throw reflect::Error("Invalid hex payload");
  if(digits.size() % 2 != 0)
    throw reflect::Error("Hex payload needs an even number of digits");
  for(std::size_t i = 0; i < digits.size(); i += 2)
    bytes.push_back(static_cast<std::uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
  return bytes;
}

std::int64_t now_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

HttpApi::HttpApi(DeviceManager &devices, Storage &storage, EventBus &bus, std::string webRoot)
    : devices_(devices), storage_(storage), bus_(bus), webRoot_(std::move(webRoot)),
      server_(std::make_unique<httplib::Server>())
{
  // SSE clients hold a worker each; size the pool generously.
  server_->new_task_queue = [] { return new httplib::ThreadPool(64); };
  server_->set_keep_alive_max_count(1000);
  routes();
  static_files();
}

HttpApi::~HttpApi() = default;

bool HttpApi::listen(const std::string &host, int port) { return server_->listen(host, port); }

void HttpApi::stop()
{
  bus_.close_all();
  server_->stop();
}

void HttpApi::routes()
{
  auto &s = *server_;

  s.Get("/api/schema", guarded([](const Request &, Response &res) { send_json(res, schema()); }));

  s.Get("/api/devices", guarded([this](const Request &, Response &res) { send_json(res, devices_.list()); }));

  s.Post("/api/devices", guarded([this](const Request &req, Response &res) {
           const auto body = body_of(req);
           const auto host = body.value("host", std::string());
           if(host.empty())
             throw reflect::Error("host is required");
           send_json(res, devices_.add(host, static_cast<std::uint16_t>(body.value("port", 0)), body.value("name", std::string())), 201);
         }));

  s.Get("/api/devices/:key", guarded([this](const Request &req, Response &res) {
          const auto device = devices_.get(req.path_params.at("key"));
          if(!device)
            throw std::out_of_range("Unknown device");
          send_json(res, *device);
        }));

  s.Patch("/api/devices/:key", guarded([this](const Request &req, Response &res) {
            if(!devices_.rename(req.path_params.at("key"), body_of(req).value("name", std::string())))
              throw std::out_of_range("Unknown device");
            send_json(res, *devices_.get(req.path_params.at("key")));
          }));

  s.Delete("/api/devices/:key", guarded([this](const Request &req, Response &res) {
             if(!devices_.remove(req.path_params.at("key")))
               throw std::out_of_range("Unknown device");
             send_json(res, {{"removed", true}});
           }));

  s.Post("/api/devices/:key/refresh", guarded([this](const Request &req, Response &res) {
           const auto key = req.path_params.at("key");
           devices_.refresh(key);
           send_json(res, *devices_.get(key));
         }));

  // One endpoint for the whole protocol: {command, peripheral?, data?}.
  s.Post("/api/devices/:key/command", guarded([this](const Request &req, Response &res) {
           const auto body = body_of(req);
           std::optional<std::uint8_t> peripheral;
           if(body.contains("peripheral") && !body["peripheral"].is_null())
             peripheral = body["peripheral"].get<std::uint8_t>();
           send_json(res, devices_.execute(req.path_params.at("key"), command_of(body.at("command")), peripheral,
                                           body.value("data", json())));
         }));

  s.Post("/api/devices/:key/raw", guarded([this](const Request &req, Response &res) {
           const auto body = body_of(req);
           send_json(res, devices_.execute_raw(req.path_params.at("key"), command_of(body.at("command")),
                                               parse_hex(body.value("payload", std::string()))));
         }));

  s.Post("/api/discover", guarded([this](const Request &, Response &res) {
           devices_.discover();
           send_json(res, {{"broadcast", true}});
         }));

  s.Get("/api/signals", guarded([this](const Request &req, Response &res) {
          send_json(res, storage_.signals(req.has_param("device") ? req.get_param_value("device") : ""));
        }));

  s.Get("/api/history", guarded([this](const Request &req, Response &res) {
          std::vector<std::int64_t> ids;
          std::stringstream list(req.get_param_value("ids"));
          for(std::string item; std::getline(list, item, ',');)
            if(!item.empty())
              ids.push_back(std::stoll(item));
          if(ids.empty() || ids.size() > 64)
            throw reflect::Error("ids: 1..64 signal ids required");
          const auto to = number_param(req, "to", now_ms());
          const auto from = number_param(req, "from", to - 3600 * 1000);
          send_json(res, storage_.history(ids, from, to, static_cast<int>(number_param(req, "points", 1500))));
        }));

  s.Get("/api/events", guarded([this](const Request &req, Response &res) {
          send_json(res, storage_.events(static_cast<int>(number_param(req, "limit", 200)), number_param(req, "before", 0),
                                         req.get_param_value("severity"), req.get_param_value("device"),
                                         number_param(req, "unacked", 0) != 0));
        }));

  s.Get("/api/events/counts", guarded([this](const Request &, Response &res) { send_json(res, storage_.event_counts()); }));

  s.Post("/api/events/ack", guarded([this](const Request &req, Response &res) {
           const auto body = body_of(req);
           std::vector<std::int64_t> ids;
           if(body.contains("ids"))
             ids = body["ids"].get<std::vector<std::int64_t>>();
           const auto changed = storage_.ack(ids);
           bus_.publish("ack", {{"ids", ids}, {"all", ids.empty()}});
           send_json(res, {{"acked", changed}});
         }));

  s.Get("/api/stats", guarded([this](const Request &, Response &res) {
          auto stats = devices_.stats();
          stats["storage"] = storage_.stats();
          stats["clients"] = bus_.clients();
          send_json(res, stats);
        }));

  s.Get("/api/stream", [this](const Request &, Response &res) {
    auto subscriber = bus_.subscribe();
    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider(
        "text/event-stream",
        [subscriber](std::size_t, httplib::DataSink &sink) {
          std::string frame;
          if(!EventBus::next(*subscriber, frame, std::chrono::seconds(15)))
          {
            sink.done();
            return false;
          }
          if(frame.empty())
            frame = ": keep-alive\n\n";
          return sink.write(frame.data(), frame.size());
        },
        [this, subscriber](bool) { bus_.unsubscribe(subscriber); });
  });
}

void HttpApi::static_files()
{
  server_->Get(R"(/(?!api/).*)", [this](const Request &req, Response &res) {
    std::string path = req.path == "/" ? "/index.html" : req.path;
    if(path.find("..") != std::string::npos)
    {
      res.status = 400;
      return;
    }
    res.set_header("Cache-Control", "no-cache");
    if(!webRoot_.empty())
    {
      std::ifstream file(std::filesystem::path(webRoot_) / path.substr(1), std::ios::binary);
      if(file)
      {
        std::ostringstream content;
        content << file.rdbuf();
        res.set_content(content.str(), std::string(assets::mime(path)));
        return;
      }
    }
    const auto *asset = assets::find(path);
    if(!asset)
      asset = assets::find("/index.html"), path = "/index.html"; // SPA fallback.
    if(!asset)
    {
      res.status = 404;
      return;
    }
    res.set_content(reinterpret_cast<const char *>(asset->data), asset->size, std::string(assets::mime(path)));
  });
}
} // namespace hub
