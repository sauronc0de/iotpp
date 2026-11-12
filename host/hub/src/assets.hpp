#pragma once

// Web UI files compiled into the executable (generated web_assets.cpp).

#include <cstddef>
#include <span>
#include <string_view>

namespace hub::assets
{
struct Asset
{
  const char *path;
  const unsigned char *data;
  std::size_t size;
};

std::span<const Asset> all();
const Asset *find(std::string_view path);
std::string_view mime(std::string_view path);
} // namespace hub::assets
