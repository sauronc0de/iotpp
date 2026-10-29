#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "dex_common.hpp"
#include "dex_device.hpp"
#include "dex_peripheral.hpp"
#include "dex_pikachu.hpp"
#include "dex_snorlax.hpp"

namespace dex
{

// One packet per UDP datagram. Packed native little-endian layouts.
inline constexpr std::size_t MaxDatagramSize = 1200;

#pragma pack(push, 1)

struct RequestHeader
{
  CommandId command = CommandId::Info;
};

struct ResponseHeader
{
  CommandId command = CommandId::Info;
  Error result = Error::None;
};

// Send these objects directly: udp.send(&packet, sizeof(packet)).
// DATA must itself have a packed, pointer-free layout shared by both peers.
template <typename T>
struct RequestPacket
{
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(sizeof(T) + sizeof(RequestHeader) <= MaxDatagramSize);
  CommandId command = CommandId::Info;
  T data{};
};

template <typename T>
struct ResponsePacket
{
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(sizeof(T) + sizeof(ResponseHeader) <= MaxDatagramSize);
  CommandId command = CommandId::Info;
  Error result = Error::None;
  T data{};
};

#pragma pack(pop)

inline constexpr std::size_t RequestHeaderSize = sizeof(RequestHeader);
inline constexpr std::size_t ResponseHeaderSize = sizeof(ResponseHeader);
static_assert(RequestHeaderSize == 1 && ResponseHeaderSize == 5);

struct Bytes
{
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
};

// Borrow the object's bytes without copying or converting any fields.
template <typename T>
Bytes as_bytes(const T& value)
{
  static_assert(std::is_trivially_copyable_v<T>);
  return {reinterpret_cast<const std::uint8_t*>(&value), sizeof(T)};
}

struct EncodeResult
{
  Error error = Error::None;
  std::size_t size = 0;
  explicit operator bool() const { return error == Error::None; }
};

struct Request
{
  CommandId command = CommandId::Info;
  Bytes data;
};

struct Response
{
  CommandId command = CommandId::Info;
  Error result = Error::None; // None means OK; otherwise an error bitmask.
  Bytes data;
};

namespace detail
{
inline EncodeResult encode(CommandId command, Error result, bool response, Bytes data,
                           std::uint8_t* buffer, std::size_t capacity)
{
  const std::size_t header = response ? ResponseHeaderSize : RequestHeaderSize;
  if(buffer == nullptr || (data.data == nullptr && data.size != 0))
    return {Error::InvalidParameter, 0};
  if(data.size > MaxDatagramSize - header)
    return {Error::OutOfRange, 0};
  if(capacity < header + data.size)
    return {Error::BufferOverflow, 0};

  // Move first so callers may also build a packet around an in-place payload.
  if(data.size != 0)
    std::memmove(buffer + header, data.data, data.size);
  std::memcpy(buffer, &command, sizeof(command));
  if(response)
    std::memcpy(buffer + sizeof(command), &result, sizeof(result));
  return {Error::None, header + data.size};
}

inline Error validate(Bytes packet, std::size_t header)
{
  if(packet.data == nullptr)
    return Error::InvalidParameter;
  if(packet.size > MaxDatagramSize)
    return Error::OutOfRange;
  if(packet.size < header)
    return Error::InvalidData;
  return Error::None;
}
} // namespace detail

inline EncodeResult encode_request(const Request& request, std::uint8_t* buffer, std::size_t capacity)
{
  return detail::encode(request.command, Error::None, false, request.data, buffer, capacity);
}

inline EncodeResult encode_response(const Response& response, std::uint8_t* buffer, std::size_t capacity)
{
  return detail::encode(response.command, response.result, true, response.data, buffer, capacity);
}

// Decoded payloads borrow the input buffer, which must remain valid.
inline Error decode_request(Bytes packet, Request& request)
{
  const auto error = detail::validate(packet, RequestHeaderSize);
  if(error != Error::None)
    return error;
  Request decoded;
  std::memcpy(&decoded.command, packet.data, sizeof(decoded.command));
  decoded.data = {packet.data + RequestHeaderSize, packet.size - RequestHeaderSize};
  request = decoded;
  return Error::None;
}

inline Error decode_response(Bytes packet, Response& response)
{
  const auto error = detail::validate(packet, ResponseHeaderSize);
  if(error != Error::None)
    return error;
  Response decoded;
  std::memcpy(&decoded.command, packet.data, sizeof(decoded.command));
  std::memcpy(&decoded.result, packet.data + sizeof(decoded.command), sizeof(decoded.result));
  decoded.data = {packet.data + ResponseHeaderSize, packet.size - ResponseHeaderSize};
  response = decoded;
  return Error::None;
}

// Copy DATA into an existing object; no field conversion or buffer casts.
template <typename T>
Error decode_data(Bytes data, T& value)
{
  if(data.data == nullptr && data.size != 0)
    return Error::InvalidParameter;
  static_assert(std::is_trivially_copyable_v<T>);
  if(data.size != sizeof(T))
    return Error::InvalidData;
  std::memcpy(&value, data.data, sizeof(T));
  return Error::None;
}

} // namespace dex
