#pragma once

// DEX metadata: a dependency-free, compile-time description of every DEX
// payload. Each struct is described once, next to its definition, with
// `dex_describe` (found through ADL). Hosts such as the hub walk these
// descriptions to decode, encode, validate, store and render any payload
// without per-type code. Firmware never instantiates them: zero cost.
//
//   constexpr auto dex_describe(const Sample *)
//   {
//     using Self = Sample;
//     return dex::meta::fields(DEX_FIELD(current).unit("A").scale(0.01),
//                              DEX_FIELD(enabled).boolean());
//   }
//
// Derived structs list `dex::meta::base<Base>()` first. A struct without its
// own description inherits its base's one (pointer conversion), which is
// exactly right for derivations that add no fields. Hosts static_assert that
// a description covers sizeof(T), so a forgotten field fails to compile.
//
// Flag enums (bitmasks) are marked with `constexpr bool dex_flags(E) { return true; }`.

#include <cstdint>
#include <limits>
#include <tuple>

namespace dex::meta
{

struct FieldInfo
{
  const char *name = "";
  const char *label = nullptr; // Human label; hosts derive one from `name` when null.
  const char *unit = "";
  const char *doc = nullptr;
  double scale = 1.0; // Engineering value = raw * scale.
  double min = -std::numeric_limits<double>::infinity(); // Engineering units.
  double max = std::numeric_limits<double>::infinity();
  bool readOnly = false;
  bool isBoolean = false; // Integer used as 0/1 switch.
};

template <typename C, typename M>
struct Field
{
  using Class = C;
  using Member = M;
  M C::*member;
  FieldInfo info;

  constexpr Field label(const char *value) const { return with([&](FieldInfo &i) { i.label = value; }); }
  constexpr Field unit(const char *value) const { return with([&](FieldInfo &i) { i.unit = value; }); }
  constexpr Field doc(const char *value) const { return with([&](FieldInfo &i) { i.doc = value; }); }
  constexpr Field scale(double value) const { return with([&](FieldInfo &i) { i.scale = value; }); }
  constexpr Field range(double lo, double hi) const { return with([&](FieldInfo &i) { i.min = lo; i.max = hi; }); }
  constexpr Field readonly() const { return with([](FieldInfo &i) { i.readOnly = true; }); }
  constexpr Field boolean() const { return with([](FieldInfo &i) { i.isBoolean = true; i.min = 0; i.max = 1; }); }

private:
  template <typename F>
  constexpr Field with(F f) const
  {
    Field copy = *this;
    f(copy.info);
    return copy;
  }
};

// Marker: include every field described for Base.
template <typename Base>
struct BaseFields
{
};

template <typename C, typename M>
constexpr Field<C, M> field(const char *name, M C::*member)
{
  return {member, FieldInfo{name}};
}

template <typename Base>
constexpr BaseFields<Base> base()
{
  return {};
}

template <typename... F>
constexpr std::tuple<F...> fields(F... f)
{
  return {f...};
}

// Adjust the metadata of a nested field by dotted path from the described
// struct, e.g. `at("monitoring.samplePeriodMs").range(1000, 60000)`. Lets a
// peripheral tighten or freeze a shared sub-struct without redefining it.
// A read-only path freezes its whole subtree. Overrides add no bytes.
struct Override
{
  const char *path = "";
  double min = 0, max = 0;
  bool hasRange = false;
  bool readOnly = false;
  const char *doc = nullptr;

  constexpr Override range(double lo, double hi) const
  {
    Override copy = *this;
    copy.min = lo;
    copy.max = hi;
    copy.hasRange = true;
    return copy;
  }
  constexpr Override readonly() const
  {
    Override copy = *this;
    copy.readOnly = true;
    return copy;
  }
  constexpr Override describe(const char *text) const
  {
    Override copy = *this;
    copy.doc = text;
    return copy;
  }
};

constexpr Override at(const char *path)
{
  Override o{};
  o.path = path;
  return o;
}

// Peripheral traits may also declare `static bool valid(const Config &)`:
// the cross-field rule shared by firmware and hosts (checked before sending).

// Feature placeholder in peripheral traits (e.g. `using Action = None;`).
struct None
{
};

template <typename... T>
struct List
{
};

} // namespace dex::meta

// Requires `using Self = <described struct>;` in scope.
#define DEX_FIELD(member) ::dex::meta::field(#member, &Self::member)
