#include "tcp_transport.hpp"
#include "message_frame.hpp"
#include "core/common/logger.hpp"
#include <arpa/inet.h>

namespace dicp {

TcpServer::TcpServer(boost::asio::io_context& ioc, const std::string& bind_address, uint16_t port)
    : ioc_(ioc),
      acceptor_(ioc) {
    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::make_address(bind_address), port);
    acceptor_.open(ep.protocol());
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();
}

TcpServer::~TcpServer() {
    Stop();
}

void TcpServer::Start() {
    if (running_.exchange(true)) return;
    StartAccept();
}

void TcpServer::Stop() {
    if (!running_.exchange(false)) return;
    boost::system::error_code ec;
    acceptor_.close(ec);
}

uint16_t TcpServer::LocalPort() const {
    boost::system::error_code ec;
    return acceptor_.local_endpoint(ec).port();
}

void TcpServer::StartAccept() {
    if (!running_) return;

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioc_);
    acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec) {
        if (!ec) {
            // Read 8-byte header: 4-byte Magic + 4-byte Length
            auto header_buf = std::make_shared<std::vector<uint8_t>>(8);
            boost::asio::async_read(*socket, boost::asio::buffer(*header_buf),
                [this, socket, header_buf](boost::system::error_code ec, std::size_t) {
                    if (ec) return;
                    uint32_t magic_net = 0, len_net = 0;
                    std::memcpy(&magic_net, header_buf->data(), 4);
                    std::memcpy(&len_net, header_buf->data() + 4, 4);
                    uint32_t magic = ntohl(magic_net);
                    uint32_t len = ntohl(len_net);

                    if (magic != MessageFrame::MAGIC || len > 10 * 1024 * 1024) {
                        return; // Invalid framing
                    }

                    auto payload = std::make_shared<std::vector<uint8_t>>(len);
                    boost::asio::async_read(*socket, boost::asio::buffer(*payload),
                        [this, socket, payload, len](boost::system::error_code ec, std::size_t) {
                            if (ec) return;
                            proto::Envelope req;
                            if (!req.ParseFromArray(payload->data(), static_cast<int>(len))) {
                                return;
                            }

                            if (handler_) {
                                auto resp_opt = handler_(req);
                                if (resp_opt) {
                                    size_t resp_len = resp_opt->ByteSizeLong();
                                    auto out_buf = std::make_shared<std::vector<uint8_t>>(8 + resp_len);
                                    uint32_t m_net = htonl(MessageFrame::MAGIC);
                                    uint32_t l_net = htonl(static_cast<uint32_t>(resp_len));
                                    std::memcpy(out_buf->data(), &m_net, 4);
                                    std::memcpy(out_buf->data() + 4, &l_net, 4);
                                    resp_opt->SerializeToArray(out_buf->data() + 8, static_cast<int>(resp_len));

                                    boost::asio::async_write(*socket, boost::asio::buffer(*out_buf),
                                        [socket, out_buf](boost::system::error_code, std::size_t) {});
                                }
                            }
                        });
                });
        }
        if (running_) {
            StartAccept();
        }
    });
}

bool TcpClient::SendSync(boost::asio::io_context& ioc,
                         const std::string& host,
                         uint16_t port,
                         const proto::Envelope& request,
                         proto::Envelope& response,
                         uint32_t /*timeout_ms*/) {
    try {
        boost::asio::ip::tcp::socket socket(ioc);
        boost::asio::ip::tcp::resolver resolver(ioc);
        boost::system::error_code ec;

        auto endpoints = resolver.resolve(host, std::to_string(port), ec);
        if (ec) return false;

        boost::asio::connect(socket, endpoints, ec);
        if (ec) return false;

        size_t req_len = request.ByteSizeLong();
        std::vector<uint8_t> out_buf(8 + req_len);
        uint32_t m_net = htonl(MessageFrame::MAGIC);
        uint32_t l_net = htonl(static_cast<uint32_t>(req_len));
        std::memcpy(out_buf.data(), &m_net, 4);
        std::memcpy(out_buf.data() + 4, &l_net, 4);
        request.SerializeToArray(out_buf.data() + 8, static_cast<int>(req_len));

        boost::asio::write(socket, boost::asio::buffer(out_buf), ec);
        if (ec) return false;

        std::vector<uint8_t> header_buf(8);
        boost::asio::read(socket, boost::asio::buffer(header_buf), ec);
        if (ec) return false;

        uint32_t resp_magic_net = 0, resp_len_net = 0;
        std::memcpy(&resp_magic_net, header_buf.data(), 4);
        std::memcpy(&resp_len_net, header_buf.data() + 4, 4);
        uint32_t resp_magic = ntohl(resp_magic_net);
        uint32_t resp_len = ntohl(resp_len_net);

        if (resp_magic != MessageFrame::MAGIC || resp_len > 10 * 1024 * 1024) return false;

        std::vector<uint8_t> payload_buf(resp_len);
        boost::asio::read(socket, boost::asio::buffer(payload_buf), ec);
        if (ec) return false;

        return response.ParseFromArray(payload_buf.data(), static_cast<int>(resp_len));
    } catch (...) {
        return false;
    }
}

} // namespace dicp
