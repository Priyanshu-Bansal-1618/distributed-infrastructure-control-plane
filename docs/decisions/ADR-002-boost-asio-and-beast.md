# ADR-002: Boost.Asio and Boost.Beast for Asynchronous Networking and HTTP

## Status
Accepted

## Context
The Node Agent and Load Balancer require high-concurrency, non-blocking asynchronous I/O across UDP, TCP, and HTTP protocols without introducing unnecessary external runtime dependencies.

## Decision
We select **Boost.Asio** for event loop orchestration, asynchronous UDP/TCP sockets, and timers, paired with **Boost.Beast** for HTTP/1.1 client and server processing.

## Advantages:
- Standardized, zero-copy, highly optimized async networking.
- Single unified `boost::asio::io_context` model for sockets, timers, and HTTP dispatch.
- Header-only or lightweight linking without sprawling runtime dependencies.
- Native integration with modern C++20 move semantics and lambda completion handlers.

## Consequences:
- All networking handlers must remain strictly non-blocking. Any disk I/O (SQLite) or CPU-intensive tasks must be delegated to background worker threads.
