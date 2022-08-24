#include <string>

#ifndef __TYPES_CPP__
#define __TYPES_CPP__

enum class PACKET_TYPE {
  PING,
  INFO,
  SET,
  GET,

  UNKNOWN
};

std::string PACKET_TYPES(const PACKET_TYPE& command) {
  switch (command) {
    case PACKET_TYPE::SET:
      return "SET";
    case PACKET_TYPE::GET:
      return "GET";
    case PACKET_TYPE::PING:
      return "PING";
    case PACKET_TYPE::INFO:
      return "INFO";
    default:
      return "";
  }
}

PACKET_TYPE packet_type_from_string(const std::string& value) {
  if (value == "SET") {
    return PACKET_TYPE::SET;
  }

  if (value == "GET") {
    return PACKET_TYPE::GET;
  }

  if (value == "PING") {
    return PACKET_TYPE::PING;
  }

  if (value == "INFO") {
    return PACKET_TYPE::INFO;
  }

  return PACKET_TYPE::UNKNOWN;
}

#endif