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
