#pragma once

// Generic DEX payload reflection, driven only by the metadata declared in the
// DEX headers (dex_describe / dex_flags, see libs/dex/dex_meta.hpp) and by
// magic_enum for enum names. Nothing in here knows any concrete peripheral.
//
//   to_json<T>     packed payload -> JSON (engineering units, enum names)
//   parse<T>   JSON -> packed payload (range/enum validation)
//   schema<T>      JSON type description consumed by the web UI
//   signals<T>     flattened numeric/enum leaves (monitoring signals)

#include "dex.hpp"

#include <magic_enum/magic_enum.hpp>
#include <magic_enum/magic_enum_flags.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace hub::reflect
{
using json = nlohmann::ordered_json;
using dex::meta::FieldInfo;

template <typename T>
concept Described = requires(const T *p) { dex_describe(p); };

template <typename E>
concept FlagsEnum = std::is_enum_v<E> && requires(E e) { dex_flags(e); };
} // namespace hub::reflect

// Teach magic_enum about DEX enums once, generically: bitmask enums are
// tagged with dex_flags(); one-byte enums use their whole 0..255 range.
namespace magic_enum::customize
{
template <typename E>
  requires hub::reflect::FlagsEnum<E>
struct enum_range<E>
{
  static constexpr bool is_flags = true;
};

template <typename E>
  requires(std::is_enum_v<E> && !hub::reflect::FlagsEnum<E> && sizeof(E) == 1 &&
           std::is_unsigned_v<std::underlying_type_t<E>>)
struct enum_range<E>
{
  static constexpr int min = 0;
  static constexpr int max = 255;
};
} // namespace magic_enum::customize

namespace hub::reflect
{
class Error : public std::invalid_argument
{
public:
  using std::invalid_argument::invalid_argument;
};

// ---- Type traits --------------------------------------------------------------
template <typename T>
struct is_std_array : std::false_type
{
};
template <typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type
{
};
template <typename T>
inline constexpr bool is_char_array_v = std::is_array_v<T> && std::is_same_v<std::remove_extent_t<T>, char>;

template <typename T>
struct base_of_marker
{
};
template <typename B>
struct base_of_marker<dex::meta::BaseFields<B>>
{
  using type = B;
};
template <typename T>
concept BaseMarker = requires { typename base_of_marker<T>::type; };

template <typename T>
constexpr std::string_view type_name()
{
  std::string_view name = __PRETTY_FUNCTION__;
  const auto start = name.find("T = ") + 4;
  name = name.substr(start, name.find_first_of(";]", start) - start);
  if(name.substr(0, 5) == "dex::")
    name.remove_prefix(5);
  return name;
}

std::string humanize(std::string_view name); // "peakCurrent" -> "Peak current"

inline std::string label_of(const FieldInfo &info)
{
  return info.label ? std::string(info.label) : humanize(info.name);
}

// ---- Field iteration ------------------------------------------------------------
template <typename T, typename F>
constexpr void for_each_field(F &&f);

template <typename T, typename Item, typename F>
constexpr void visit_item(const Item &item, F &f)
{
  if constexpr(BaseMarker<Item>)
    for_each_field<typename base_of_marker<Item>::type>(f);
  else
    f(item);
}

// Calls f(field) for every described field of T, bases first.
template <typename T, typename F>
constexpr void for_each_field(F &&f)
{
  if constexpr(Described<T>)
    std::apply([&](const auto &...items) { (visit_item<T>(items, f), ...); },
               dex_describe(static_cast<const T *>(nullptr)));
}

template <typename T>
constexpr std::size_t described_size()
{
  std::size_t size = 0;
  for_each_field<T>([&](const auto &field) { size += sizeof(typename std::decay_t<decltype(field)>::Member); });
  return size;
}

// Packed DEX payloads: the description must account for every byte.
template <typename T>
constexpr bool fully_described()
{
  if constexpr(std::is_class_v<T> && !is_std_array<T>::value)
    return std::is_empty_v<T> || described_size<T>() == sizeof(T);
  return true;
}

// Address of a (possibly packed, possibly inherited) member; accessed via memcpy only.
template <typename T, typename Field>
const std::uint8_t *member_address(const void *object, const Field &field)
{
  using C = typename Field::Class;
  const auto &base = static_cast<const C &>(*static_cast<const T *>(object));
  return reinterpret_cast<const std::uint8_t *>(&(base.*field.member));
}

template <typename M>
M load(const void *address)
{
  M value;
  std::memcpy(&value, address, sizeof(M));
  return value;
}

// ---- Enums ----------------------------------------------------------------------
template <typename E>
json enum_to_json(E value)
{
  if constexpr(FlagsEnum<E>)
  {
    using U = std::underlying_type_t<E>;
    json names = json::array();
    U rest = static_cast<U>(value);
    for(const auto flag : magic_enum::enum_values<E>())
    {
      const auto bits = static_cast<U>(flag);
      if(bits != 0 && (static_cast<U>(value) & bits) == bits)
      {
        names.push_back(magic_enum::enum_name(flag));
        rest &= static_cast<U>(~bits);
      }
    }
    if(rest != 0)
      names.push_back(static_cast<std::uint64_t>(rest));
    return names;
  }
  else
  {
    const auto name = magic_enum::enum_name(value);
    if(name.empty())
      return static_cast<std::int64_t>(magic_enum::enum_integer(value));
    return std::string(name);
  }
}

template <typename E>
E enum_from_json(const json &j, const std::string &path)
{
  using U = std::underlying_type_t<E>;
  const auto one = [&](const json &item) -> U {
    if(item.is_number_integer())
      return static_cast<U>(item.get<std::int64_t>());
    if(item.is_string())
      if(const auto value = magic_enum::enum_cast<E>(item.get<std::string>()))
        return static_cast<U>(*value);
    throw Error(path + ": invalid " + std::string(type_name<E>()) + " value " + item.dump());
  };
  if constexpr(FlagsEnum<E>)
    if(j.is_array())
    {
      U bits = 0;
      for(const auto &item : j)
        bits |= one(item);
      return static_cast<E>(bits);
    }
  return static_cast<E>(one(j));
}

template <typename E>
std::string enum_schema(json &enums)
{
  const std::string name(type_name<E>());
  if(!enums.contains(name))
  {
    json values = json::array();
    for(const auto value : magic_enum::enum_values<E>())
      values.push_back({{"name", magic_enum::enum_name(value)},
                        {"value", static_cast<std::int64_t>(magic_enum::enum_integer(value))}});
    enums[name] = {{"flags", FlagsEnum<E>}, {"values", std::move(values)}};
  }
  return name;
}

// ---- Numbers --------------------------------------------------------------------
inline double scaled(double raw, double scale)
{
  if(scale == 1.0)
    return raw;
  // Round to the precision implied by the scale (0.01 -> 2 decimals).
  const double digits = std::pow(10.0, std::max(0.0, std::ceil(-std::log10(scale))));
  return std::round(raw * scale * digits) / digits;
}

template <typename M>
constexpr const char *number_type()
{
  if constexpr(std::is_floating_point_v<M>)
    return sizeof(M) == 4 ? "f32" : "f64";
  else if constexpr(std::is_signed_v<M>)
    return sizeof(M) == 1 ? "i8" : sizeof(M) == 2 ? "i16" : sizeof(M) == 4 ? "i32" : "i64";
  else
    return sizeof(M) == 1 ? "u8" : sizeof(M) == 2 ? "u16" : sizeof(M) == 4 ? "u32" : "u64";
}

// ---- to_json --------------------------------------------------------------------
template <typename T>
json to_json(const void *address, const FieldInfo &info = {})
{
  static_assert(fully_described<T>(), "DEX description does not cover every byte of the struct: "
                                      "list every field in its dex_describe()");
  if constexpr(is_char_array_v<T>)
  {
    const auto *text = static_cast<const char *>(address);
    return std::string(text, strnlen(text, sizeof(T)));
  }
  else if constexpr(is_std_array<T>::value)
  {
    using E = typename T::value_type;
    json items = json::array();
    for(std::size_t i = 0; i < std::tuple_size_v<T>; ++i)
      items.push_back(to_json<E>(static_cast<const std::uint8_t *>(address) + i * sizeof(E), info));
    return items;
  }
  else if constexpr(std::is_enum_v<T>)
    return enum_to_json(load<T>(address));
  else if constexpr(std::is_arithmetic_v<T>)
  {
    const auto raw = load<T>(address);
    if(info.isBoolean)
      return raw != 0;
    if(info.scale != 1.0 || std::is_floating_point_v<T>)
      return scaled(static_cast<double>(raw), info.scale);
    return raw;
  }
  else
  {
    json object = json::object();
    for_each_field<T>([&](const auto &field) {
      using M = typename std::decay_t<decltype(field)>::Member;
      object[field.info.name] = to_json<M>(member_address<T>(address, field), field.info);
    });
    return object;
  }
}

template <typename T>
json to_json(const T &value)
{
  return to_json<T>(&value);
}

// ---- parse ------------------------------------------------------------------
// Missing object keys keep the current value: callers start from a GET result.
template <typename T>
void parse(const json &j, void *address, const FieldInfo &info = {}, const std::string &path = "$")
{
  if constexpr(is_char_array_v<T>)
  {
    if(!j.is_string())
      throw Error(path + ": expected a string");
    const auto text = j.get<std::string>();
    if(text.size() >= sizeof(T))
      throw Error(path + ": longer than " + std::to_string(sizeof(T) - 1) + " characters");
    std::memset(address, 0, sizeof(T));
    std::memcpy(address, text.data(), text.size());
  }
  else if constexpr(is_std_array<T>::value)
  {
    using E = typename T::value_type;
    if(!j.is_array() || j.size() > std::tuple_size_v<T>)
      throw Error(path + ": expected an array of up to " + std::to_string(std::tuple_size_v<T>) + " items");
    for(std::size_t i = 0; i < j.size(); ++i)
      parse<E>(j[i], static_cast<std::uint8_t *>(address) + i * sizeof(E), info, path + "[" + std::to_string(i) + "]");
  }
  else if constexpr(std::is_enum_v<T>)
  {
    const auto value = enum_from_json<T>(j, path);
    std::memcpy(address, &value, sizeof(T));
  }
  else if constexpr(std::is_arithmetic_v<T>)
  {
    if(!j.is_number() && !j.is_boolean())
      throw Error(path + ": expected a number");
    const double value = j.is_boolean() ? (j.get<bool>() ? 1.0 : 0.0) : j.get<double>();
    if(!std::isfinite(value) || value < info.min || value > info.max)
      throw Error(path + ": " + std::to_string(value) + " outside [" + std::to_string(info.min) + ", " +
                  std::to_string(info.max) + "]");
    const double raw = std::is_floating_point_v<T> ? value / info.scale : std::round(value / info.scale);
    if(raw < static_cast<double>(std::numeric_limits<T>::lowest()) || raw > static_cast<double>(std::numeric_limits<T>::max()))
      throw Error(path + ": does not fit in " + number_type<T>());
    const auto stored = static_cast<T>(raw);
    std::memcpy(address, &stored, sizeof(T));
  }
  else
  {
    if(!j.is_object())
      throw Error(path + ": expected an object");
    for_each_field<T>([&](const auto &field) {
      using M = typename std::decay_t<decltype(field)>::Member;
      if(const auto it = j.find(field.info.name); it != j.end())
        parse<M>(*it, const_cast<std::uint8_t *>(member_address<T>(address, field)), field.info,
                     path + "." + field.info.name);
    });
  }
}

template <typename T>
T parse(const json &j, T value = {})
{
  parse<T>(j, &value);
  return value;
}

// ---- schema ---------------------------------------------------------------------
inline void add_info(json &out, const FieldInfo &info)
{
  out["name"] = info.name;
  out["label"] = label_of(info);
  if(*info.unit)
    out["unit"] = info.unit;
  if(info.scale != 1.0)
    out["scale"] = info.scale;
  if(std::isfinite(info.min))
    out["min"] = info.min;
  if(std::isfinite(info.max))
    out["max"] = info.max;
  if(info.doc)
    out["doc"] = info.doc;
  if(info.readOnly)
    out["readonly"] = true;
}

template <typename T>
json schema(json &enums, const FieldInfo &info = {})
{
  if constexpr(is_char_array_v<T>)
    return {{"kind", "string"}, {"maxLength", sizeof(T) - 1}};
  else if constexpr(is_std_array<T>::value)
    return {{"kind", "array"}, {"size", std::tuple_size_v<T>}, {"items", schema<typename T::value_type>(enums, info)}};
  else if constexpr(std::is_enum_v<T>)
    return {{"kind", FlagsEnum<T> ? "flags" : "enum"}, {"enum", enum_schema<T>(enums)}};
  else if constexpr(std::is_arithmetic_v<T>)
  {
    if(info.isBoolean)
      return {{"kind", "bool"}};
    json number = {{"kind", "number"}, {"type", number_type<T>()}};
    if constexpr(std::is_integral_v<T>)
    {
      number["min"] = std::max(info.min, static_cast<double>(std::numeric_limits<T>::lowest()) * info.scale);
      number["max"] = std::min(info.max, static_cast<double>(std::numeric_limits<T>::max()) * info.scale);
      number["step"] = info.scale;
    }
    return number;
  }
  else
  {
    json fields = json::array();
    for_each_field<T>([&](const auto &field) {
      using M = typename std::decay_t<decltype(field)>::Member;
      json entry;
      add_info(entry, field.info);
      entry["type"] = schema<M>(enums, field.info);
      fields.push_back(std::move(entry));
    });
    return {{"kind", "struct"}, {"name", type_name<T>()}, {"size", sizeof(T)}, {"fields", std::move(fields)}};
  }
}

// ---- signals --------------------------------------------------------------------
struct Signal
{
  std::string key;   // "channels.0.current"
  std::string label; // "Channel 1 › Current"
  std::string unit;
  std::string kind;  // number | enum | flags | bool
  std::string enumName;
};

inline std::string join(const std::string &prefix, const std::string &part, const char *separator)
{
  return prefix.empty() ? part : prefix + separator + part;
}

// Flatten every numeric/enum leaf. Strings are not signals.
template <typename T>
void collect_signals(std::vector<Signal> &out, json &enums, const std::string &key, const std::string &label,
                     const FieldInfo &info)
{
  if constexpr(is_char_array_v<T>)
    return;
  else if constexpr(is_std_array<T>::value)
  {
    for(std::size_t i = 0; i < std::tuple_size_v<T>; ++i)
      collect_signals<typename T::value_type>(out, enums, join(key, std::to_string(i), "."),
                                              label + " " + std::to_string(i + 1), info);
  }
  else if constexpr(std::is_enum_v<T>)
    out.push_back({key, label, "", FlagsEnum<T> ? "flags" : "enum", enum_schema<T>(enums)});
  else if constexpr(std::is_arithmetic_v<T>)
    out.push_back({key, label, info.unit, info.isBoolean ? "bool" : "number", ""});
  else
    for_each_field<T>([&](const auto &field) {
      using M = typename std::decay_t<decltype(field)>::Member;
      collect_signals<M>(out, enums, join(key, field.info.name, "."), join(label, label_of(field.info), " › "),
                         field.info);
    });
}

// Values in exactly the order produced by collect_signals<T>.
template <typename T>
void collect_values(const void *address, const FieldInfo &info, std::vector<double> &out)
{
  if constexpr(is_char_array_v<T>)
    return;
  else if constexpr(is_std_array<T>::value)
  {
    using E = typename T::value_type;
    for(std::size_t i = 0; i < std::tuple_size_v<T>; ++i)
      collect_values<E>(static_cast<const std::uint8_t *>(address) + i * sizeof(E), info, out);
  }
  else if constexpr(std::is_enum_v<T>)
    out.push_back(static_cast<double>(magic_enum::enum_integer(load<T>(address))));
  else if constexpr(std::is_arithmetic_v<T>)
    out.push_back(scaled(static_cast<double>(load<T>(address)), info.scale));
  else
    for_each_field<T>([&](const auto &field) {
      using M = typename std::decay_t<decltype(field)>::Member;
      collect_values<M>(member_address<T>(address, field), field.info, out);
    });
}

json signal_json(const Signal &signal);

} // namespace hub::reflect
