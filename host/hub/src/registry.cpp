#include "registry.hpp"

#include <cctype>
#include <cstddef>
#include <set>
#include <stdexcept>

namespace hub
{
namespace reflect
{
std::string humanize(std::string_view name)
{
  std::string out;
  for(std::size_t i = 0; i < name.size(); ++i)
  {
    const char c = name[i];
    const bool upper = std::isupper(static_cast<unsigned char>(c));
    if(i == 0)
      out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    else if(upper && !std::isupper(static_cast<unsigned char>(name[i - 1])))
      out += ' ', out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    else
      out += c;
  }
  return out;
}

json signal_json(const Signal &signal)
{
  json out = {{"key", signal.key}, {"label", signal.label}, {"kind", signal.kind}};
  if(!signal.unit.empty())
    out["unit"] = signal.unit;
  if(!signal.enumName.empty())
    out["enum"] = signal.enumName;
  return out;
}
} // namespace reflect

namespace
{
template <typename T>
constexpr bool present = !std::is_same_v<T, dex::meta::None>;

template <typename P>
constexpr const char *signal_root()
{
  if constexpr(requires { P::signal; })
    return P::signal;
  else
    return "value";
}

template <typename T>
T decode_exact(dex::Bytes data)
{
  if(data.size != sizeof(T))
    throw reflect::Error("payload is " + std::to_string(data.size) + " bytes, expected " +
                         std::to_string(sizeof(T)) + " (" + std::string(reflect::type_name<T>()) + ")");
  T value;
  std::memcpy(&value, data.data, sizeof(T));
  return value;
}

template <typename T>
Buffer bytes_of(const T &value)
{
  const auto *p = reinterpret_cast<const std::uint8_t *>(&value);
  return Buffer(p, p + sizeof(T));
}

template <typename T>
auto decoder()
{
  return [](dex::Bytes data) { return reflect::to_json(decode_exact<T>(data)); };
}

template <typename T>
auto encoder()
{
  return [](const json &j) { return bytes_of(reflect::parse<T>(j)); };
}

struct Built
{
  std::vector<PeripheralType> types;
  json types_schema = json::array();
};

template <typename P>
void add_type(Built &built, json &enums)
{
  using S = typename P::Status;
  using C = typename P::Config;
  using A = typename P::Action;
  using M = typename P::Monitoring;

  PeripheralType t;
  t.type = P::type;
  t.name = P::name;
  t.description = P::description;
  json entry = {{"type", P::type}, {"name", P::name}, {"description", P::description}};

  if constexpr(present<S>)
  {
    static_assert(std::is_base_of_v<dex::StatusData, S>, "Peripheral Status must derive from dex::StatusData");
    t.hasStatus = true;
    t.decodeStatus = decoder<S>();
    entry["status"] = reflect::schema<S>(enums);
  }
  if constexpr(present<C>)
  {
    t.hasConfig = true;
    t.decodeConfig = decoder<C>();
    t.encodeConfig = encoder<C>();
    entry["config"] = reflect::schema<C>(enums);
  }
  if constexpr(present<A>)
  {
    t.hasAction = true;
    t.encodeAction = encoder<A>();
    entry["action"] = reflect::schema<A>(enums);
    entry["actionDefault"] = reflect::to_json(A{});
  }
  if constexpr(present<M>)
  {
    using X = decltype(M::data);
    t.hasMonitoring = true;
    t.monitoringSize = sizeof(M);
    t.decodeMonitoring = decoder<M>();
    constexpr bool compound = std::is_class_v<X> && !reflect::is_std_array<X>::value;
    const std::string root = compound ? "" : signal_root<P>();
    reflect::collect_signals<X>(t.signals, enums, root, compound ? "" : reflect::humanize(root), {});
    t.sample = [](dex::Bytes data, std::uint32_t &timestamp, std::vector<double> &values) {
      if(data.size != sizeof(M))
        return false;
      std::memcpy(&timestamp, data.data + offsetof(M, timestamp), sizeof(timestamp));
      reflect::collect_values<X>(data.data + offsetof(M, data), {}, values);
      return true;
    };
    json signals = json::array();
    for(const auto &signal : t.signals)
      signals.push_back(reflect::signal_json(signal));
    entry["signals"] = std::move(signals);
    entry["monitoring"] = reflect::schema<M>(enums);
  }
  entry["features"] = {{"status", t.hasStatus}, {"config", t.hasConfig}, {"action", t.hasAction},
                       {"monitoring", t.hasMonitoring}};
  built.types.push_back(std::move(t));
  built.types_schema.push_back(std::move(entry));
}

template <typename... P>
void add_all(Built &built, json &enums, dex::meta::List<P...>)
{
  (add_type<P>(built, enums), ...);
}

struct Registry
{
  Built built;
  json schema;

  Registry()
  {
    json enums = json::object();
    add_all(built, enums, dex::Peripherals{});
    std::set<std::uint8_t> seen;
    for(const auto &t : built.types)
      if(!seen.insert(t.type).second)
        throw std::logic_error("Duplicate DEX peripheral type " + std::to_string(t.type));

    json commands = json::array();
    for(const auto command : magic_enum::enum_values<dex::CommandId>())
    {
      const char *payload = command == dex::CommandId::Config || command == dex::CommandId::PeripheralConfig ? "config"
                            : command == dex::CommandId::PeripheralAction                                    ? "action"
                                                                                                             : "none";
      commands.push_back({{"name", magic_enum::enum_name(command)},
                          {"value", static_cast<int>(command)},
                          {"peripheral", is_peripheral_command(command)},
                          {"payload", payload}});
    }
    schema = {
        {"protocol", "DEX"},
        {"maxDatagram", dex::MaxDatagramSize},
        {"commands", std::move(commands)},
        {"device",
         {{"status", reflect::schema<dex::StatusData>(enums)},
          {"config", reflect::schema<dex::device::ConfigData>(enums)},
          {"peripheralInfo", reflect::schema<dex::peripheral::InfoData>(enums)}}},
        {"peripheralStatus", reflect::schema<dex::peripheral::StatusData>(enums)},
        {"capabilities", reflect::enum_schema<dex::peripheral::PeripheralCapability>(enums)},
        {"results", reflect::enum_schema<dex::Error>(enums)},
        {"types", built.types_schema},
    };
    schema["enums"] = std::move(enums);
  }
};

const Registry &registry()
{
  static const Registry instance;
  return instance;
}
} // namespace

bool is_peripheral_command(dex::CommandId command)
{
  return magic_enum::enum_name(command).starts_with("Peripheral");
}

const std::vector<PeripheralType> &peripheral_types() { return registry().built.types; }

const PeripheralType *find_type(std::uint8_t type)
{
  for(const auto &t : peripheral_types())
    if(t.type == type)
      return &t;
  return nullptr;
}

const json &schema() { return registry().schema; }

DeviceInfo decode_device_info(dex::Bytes data)
{
  DeviceInfo info;
  // std::array<T, 0> still occupies one byte in device::InfoData<0>.
  if(data.size == sizeof(dex::device::InfoData<0>))
  {
    info.firmware = decode_exact<dex::device::InfoData<0>>(data).firmwareVersion;
    return info;
  }
  if(data.size < sizeof(dex::Version))
    throw reflect::Error("Info payload too short");
  info.firmware = decode_exact<dex::Version>({data.data, sizeof(dex::Version)});
  const auto bytes = data.size - sizeof(dex::Version);
  // Typed entries (InfoData + type byte) are preferred; legacy entries have no type.
  constexpr auto typedSize = sizeof(dex::peripheral::InfoData) + 1;
  const bool typed = bytes % typedSize == 0;
  const auto stride = typed ? typedSize : sizeof(dex::peripheral::InfoData);
  if(bytes % stride != 0)
    throw reflect::Error("Invalid Info payload size " + std::to_string(data.size));
  for(std::size_t offset = sizeof(dex::Version); offset < data.size; offset += stride)
  {
    const auto entry = decode_exact<dex::peripheral::InfoData>({data.data + offset, sizeof(dex::peripheral::InfoData)});
    DeviceInfo::Entry e{entry.id, std::nullopt, entry.version, entry.capabilities};
    if(typed)
      e.type = data.data[offset + sizeof(dex::peripheral::InfoData)];
    info.peripherals.push_back(e);
  }
  return info;
}

json device_info_json(const DeviceInfo &info)
{
  json peripherals = json::array();
  for(const auto &p : info.peripherals)
  {
    json entry = {{"id", p.id},
                  {"version", reflect::to_json(p.version)},
                  {"capabilities", reflect::enum_to_json(p.capabilities)}};
    entry["type"] = p.type ? json(*p.type) : json();
    peripherals.push_back(std::move(entry));
  }
  return {{"firmware", reflect::to_json(info.firmware)}, {"peripherals", std::move(peripherals)}};
}

json decode_device_status(dex::Bytes data, dex::StatusData &summary)
{
  constexpr auto head = sizeof(dex::StatusData), each = sizeof(dex::peripheral::StatusData);
  if(data.size < head || (data.size - head) % each != 0)
    throw reflect::Error("Invalid Status payload size " + std::to_string(data.size));
  summary = decode_exact<dex::StatusData>({data.data, head});
  json out = reflect::to_json(summary);
  json peripherals = json::array();
  for(std::size_t offset = head; offset < data.size; offset += each)
    peripherals.push_back(reflect::to_json(decode_exact<dex::peripheral::StatusData>({data.data + offset, each})));
  out["peripherals"] = std::move(peripherals);
  return out;
}

json decode_generic_status(dex::Bytes data)
{
  if(data.size < sizeof(dex::StatusData))
    throw reflect::Error("Status payload too short");
  return reflect::to_json(decode_exact<dex::StatusData>({data.data, sizeof(dex::StatusData)}));
}
} // namespace hub
