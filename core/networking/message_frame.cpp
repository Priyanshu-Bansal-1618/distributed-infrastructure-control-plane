#include "message_frame.hpp"
#include "core/common/logger.hpp"
#include <arpa/inet.h>
#include <cstring>

namespace dicp {

std::optional<MessageFrame::Header> MessageFrame::ParseHeader(const uint8_t* data, size_t length) {
    if (length < HEADER_SIZE) {
        return std::nullopt;
    }

    Header header;
    uint32_t magic_net = 0;
    std::memcpy(&magic_net, data, 4);
    header.magic = ntohl(magic_net);

    uint16_t ver_net = 0;
    std::memcpy(&ver_net, data + 4, 2);
    header.protocol_version = ntohs(ver_net);

    uint16_t type_net = 0;
    std::memcpy(&type_net, data + 6, 2);
    header.message_type = ntohs(type_net);

    uint32_t len_net = 0;
    std::memcpy(&len_net, data + 8, 4);
    header.payload_length = ntohl(len_net);

    return header;
}

std::vector<uint8_t> MessageFrame::Encode(const proto::Envelope& envelope, uint32_t max_bytes) {
    size_t payload_size = envelope.ByteSizeLong();
    size_t total_size = HEADER_SIZE + payload_size;

    if (total_size > max_bytes) {
        DICP_LOG_WARN("MessageFrame", "Encoded message exceeds max_bytes limit: " + 
                      std::to_string(total_size) + " > " + std::to_string(max_bytes));
        return {};
    }

    std::vector<uint8_t> buffer(total_size);

    uint32_t magic_net = htonl(MAGIC);
    std::memcpy(buffer.data(), &magic_net, 4);

    uint16_t ver_net = htons(CURRENT_PROTOCOL_VERSION);
    std::memcpy(buffer.data() + 4, &ver_net, 2);

    uint16_t type_net = htons(static_cast<uint16_t>(envelope.type()));
    std::memcpy(buffer.data() + 6, &type_net, 2);

    uint32_t len_net = htonl(static_cast<uint32_t>(payload_size));
    std::memcpy(buffer.data() + 8, &len_net, 4);

    if (payload_size > 0) {
        envelope.SerializeToArray(buffer.data() + HEADER_SIZE, static_cast<int>(payload_size));
    }

    return buffer;
}

std::optional<proto::Envelope> MessageFrame::Decode(const uint8_t* data, size_t length, uint32_t max_bytes) {
    auto header_opt = ParseHeader(data, length);
    if (!header_opt) {
        DICP_LOG_WARN("MessageFrame", "Packet too short for header: length=" + std::to_string(length));
        return std::nullopt;
    }

    const auto& header = *header_opt;
    if (header.magic != MAGIC) {
        DICP_LOG_WARN("MessageFrame", "Invalid magic header: 0x" + std::to_string(header.magic));
        return std::nullopt;
    }

    if (header.protocol_version != CURRENT_PROTOCOL_VERSION) {
        DICP_LOG_WARN("MessageFrame", "Unsupported protocol version: " + std::to_string(header.protocol_version));
        return std::nullopt;
    }

    if (header.payload_length > max_bytes) {
        DICP_LOG_WARN("MessageFrame", "Payload length exceeds limit: " + std::to_string(header.payload_length));
        return std::nullopt;
    }

    if (length < HEADER_SIZE + header.payload_length) {
        DICP_LOG_WARN("MessageFrame", "Truncated packet: buffer length " + std::to_string(length) +
                                      " < required " + std::to_string(HEADER_SIZE + header.payload_length));
        return std::nullopt;
    }

    proto::Envelope envelope;
    if (!envelope.ParseFromArray(data + HEADER_SIZE, static_cast<int>(header.payload_length))) {
        DICP_LOG_WARN("MessageFrame", "Failed to parse protobuf Envelope from payload");
        return std::nullopt;
    }

    return envelope;
}

} // namespace dicp
