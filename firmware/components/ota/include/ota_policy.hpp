#pragma once

#include <cstdint>
#include <cstring>

namespace ota
{
// Release versions are exactly three decimal components, each fitting DEX Version.
inline bool parse_version(const char *text, std::uint32_t &number)
{
  if(text == nullptr)
    return false;
  number = 0;
  for(unsigned component = 0; component != 3; ++component)
  {
    if(*text < '0' || *text > '9')
      return false;
    unsigned value = 0;
    do
    {
      value = value * 10 + unsigned(*text++ - '0');
      if(value > 255)
        return false;
    } while(*text >= '0' && *text <= '9');
    number = (number << 8) | value;
    if(component != 2 && *text++ != '.')
      return false;
  }
  return *text == '\0';
}

inline bool valid_image_name(const char *name)
{
  if(name == nullptr || *name == '\0' || std::strlen(name) > 127 || std::strstr(name, ".."))
    return false;
  for(const char *p = name; *p; ++p)
    if(!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
         (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.'))
      return false;
  return true; // Relative filename only: never another origin or a traversal.
}
} // namespace ota
