#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <optional>
#include "proto/gossip.pb.h"

namespace dicp {

class MessageFrame {
public:
    static constexpr uint32_t MAGIC = 0x44494350; // "DICP" in ASCII
    static constexpr uint16_t CURRENT_PROTOCOL_VERSION = 1;
    static constexpr size_t HEADER_SIZE = 12; // 4 (magic) + 2 (ver) + 2 (type) + 4 (len)

    struct Header {
        uint32_t magic;
        uint16_t protocol_version;
        uint16_t message_type;
        uint32_t payload_length;
    };

    static std::vector<uint8_t> Encode(const proto::Envelope& envelope, uint32_t max_bytes = 1200);
    static std::optional<proto::Envelope> Decode(const uint8_t* data, size_t length, uint32_t max_bytes = 1200);
    static std::optional<proto::Envelope> Decode(const std::vector<uint8_t>& buffer, uint32_t max_bytes = 1200) {
        return Decode(buffer.data(), buffer.size(), max_bytes);
    }

    static std::optional<Header> ParseHeader(const uint8_t* data, size_t length);
};

} // namespace dicp
