#include "assets.hpp"

namespace hub::assets
{
const Asset *find(std::string_view path)
{
  for(const auto &asset : all())
    if(path == asset.path)
      return &asset;
  return nullptr;
}

std::string_view mime(std::string_view path)
{
  const auto ends = [&](std::string_view suffix) { return path.ends_with(suffix); };
  if(ends(".html"))
    return "text/html; charset=utf-8";
  if(ends(".js") || ends(".mjs"))
    return "text/javascript; charset=utf-8";
  if(ends(".css"))
    return "text/css; charset=utf-8";
  if(ends(".svg"))
    return "image/svg+xml";
  if(ends(".json") || ends(".webmanifest"))
    return "application/json";
  if(ends(".png"))
    return "image/png";
  if(ends(".woff2"))
    return "font/woff2";
  return "application/octet-stream";
}
} // namespace hub::assets
