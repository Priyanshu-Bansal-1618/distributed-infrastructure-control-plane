#pragma once

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include "proto/gossip.pb.h"

namespace dicp {

class TcpServer : public std::enable_shared_from_this<TcpServer> {
public:
    using RequestHandler = std::function<std::optional<proto::Envelope>(const proto::Envelope&)>;

    TcpServer(boost::asio::io_context& ioc, const std::string& bind_address, uint16_t port);
    ~TcpServer();

    void SetHandler(RequestHandler handler) { handler_ = std::move(handler); }
    void Start();
    void Stop();

    uint16_t LocalPort() const;

private:
    void StartAccept();

    boost::asio::io_context& ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    RequestHandler handler_;
    std::atomic<bool> running_{false};
};

class TcpClient {
public:
    static bool SendSync(boost::asio::io_context& ioc,
                         const std::string& host,
                         uint16_t port,
                         const proto::Envelope& request,
                         proto::Envelope& response,
                         uint32_t timeout_ms = 3000);
};

} // namespace dicp
