#include "udp_transport.hpp"
#include "core/common/logger.hpp"

namespace dicp {

UdpTransport::UdpTransport(boost::asio::io_context& ioc,
                           const std::string& bind_address,
                           uint16_t port,
                           uint32_t max_message_bytes)
    : ioc_(ioc),
      socket_(ioc),
      max_message_bytes_(max_message_bytes),
      rx_buffer_(max_message_bytes + 64) {
    boost::asio::ip::udp::endpoint ep(boost::asio::ip::make_address(bind_address), port);
    socket_.open(ep.protocol());
    socket_.set_option(boost::asio::socket_base::reuse_address(true));
    socket_.bind(ep);
}

UdpTransport::~UdpTransport() {
    Stop();
}

void UdpTransport::Start() {
    if (running_.exchange(true)) {
        return;
    }
    StartReceive();
}

void UdpTransport::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    boost::system::error_code ec;
    socket_.cancel(ec);
    socket_.close(ec);
}

uint16_t UdpTransport::LocalPort() const {
    boost::system::error_code ec;
    return socket_.local_endpoint(ec).port();
}

boost::asio::ip::udp::endpoint UdpTransport::LocalEndpoint() const {
    boost::system::error_code ec;
    return socket_.local_endpoint(ec);
}

void UdpTransport::StartReceive() {
    if (!running_) return;

    auto self = shared_from_this();
    socket_.async_receive_from(
        boost::asio::buffer(rx_buffer_),
        sender_endpoint_,
        [this, self](boost::system::error_code ec, std::size_t bytes_recvd) {
            if (!ec && bytes_recvd > 0) {
                messages_received_++;
                bytes_received_ += bytes_recvd;

                auto envelope_opt = MessageFrame::Decode(rx_buffer_.data(), bytes_recvd, max_message_bytes_);
                if (envelope_opt) {
                    if (handler_) {
                        handler_(*envelope_opt, sender_endpoint_);
                    }
                } else {
                    drops_++;
                    DICP_LOG_WARN("UdpTransport", "Dropped malformed or oversized packet from " +
                                  sender_endpoint_.address().to_string() + ":" +
                                  std::to_string(sender_endpoint_.port()));
                }
            } else if (ec && ec != boost::asio::error::operation_aborted) {
                DICP_LOG_ERROR("UdpTransport", "Receive error: " + ec.message());
            }

            if (running_) {
                StartReceive();
            }
        });
}

bool UdpTransport::SendTo(const proto::Envelope& envelope, const boost::asio::ip::udp::endpoint& target) {
    if (!socket_.is_open()) return false;

    auto frame = MessageFrame::Encode(envelope, max_message_bytes_);
    if (frame.empty()) {
        drops_++;
        return false;
    }

    auto buf_ptr = std::make_shared<std::vector<uint8_t>>(std::move(frame));
    auto self = shared_from_this();

    socket_.async_send_to(
        boost::asio::buffer(*buf_ptr),
        target,
        [this, self, buf_ptr](boost::system::error_code ec, std::size_t bytes_sent) {
            if (!ec) {
                messages_sent_++;
                bytes_sent_ += bytes_sent;
            } else if (ec != boost::asio::error::operation_aborted) {
                drops_++;
                DICP_LOG_WARN("UdpTransport", "Send error: " + ec.message());
            }
        });
    return true;
}

bool UdpTransport::SendTo(const proto::Envelope& envelope, const std::string& host, uint16_t port) {
    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(host, ec);
    if (!ec) {
        return SendTo(envelope, boost::asio::ip::udp::endpoint(addr, port));
    }

    boost::asio::ip::udp::resolver resolver(ioc_);
    auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec || endpoints.empty()) {
        DICP_LOG_WARN("UdpTransport", "Resolve failed for " + host + ":" + std::to_string(port) + " : " + ec.message());
        return false;
    }

    return SendTo(envelope, *endpoints.begin());
}

} // namespace dicp
