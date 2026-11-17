#pragma once

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include "message_frame.hpp"
#include "proto/gossip.pb.h"

namespace dicp {

class UdpTransport : public std::enable_shared_from_this<UdpTransport> {
public:
    using MessageHandler = std::function<void(const proto::Envelope&, const boost::asio::ip::udp::endpoint&)>;

    UdpTransport(boost::asio::io_context& ioc,
                 const std::string& bind_address,
                 uint16_t port,
                 uint32_t max_message_bytes = 1200);
    ~UdpTransport();

    void SetMessageHandler(MessageHandler handler) {
        handler_ = std::move(handler);
    }

    void Start();
    void Stop();

    bool SendTo(const proto::Envelope& envelope, const boost::asio::ip::udp::endpoint& target);
    bool SendTo(const proto::Envelope& envelope, const std::string& host, uint16_t port);

    uint16_t LocalPort() const;
    boost::asio::ip::udp::endpoint LocalEndpoint() const;

    uint64_t MessagesSent() const { return messages_sent_.load(); }
    uint64_t MessagesReceived() const { return messages_received_.load(); }
    uint64_t BytesSent() const { return bytes_sent_.load(); }
    uint64_t BytesReceived() const { return bytes_received_.load(); }
    uint64_t Drops() const { return drops_.load(); }

private:
    void StartReceive();

    boost::asio::io_context& ioc_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint sender_endpoint_;
    uint32_t max_message_bytes_;
    std::vector<uint8_t> rx_buffer_;
    MessageHandler handler_;
    std::atomic<bool> running_{false};

    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> drops_{0};
};

} // namespace dicp
