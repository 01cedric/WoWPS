#pragma once

// Cross-platform socket abstractions for Windows (Winsock2) and POSIX.

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif

  using socket_t  = SOCKET;
#ifndef __MINGW32__
  using ssize_t   = int;             // recv/send return int on MSVC
#endif

  inline constexpr socket_t INVALID_SOCK = INVALID_SOCKET;

#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/tcp.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <cerrno>

  using socket_t = int;

  inline constexpr socket_t INVALID_SOCK = -1;

#endif

#include <cstring>
#include <string>
#include <chrono>

#include "core/logger.hpp"
#ifdef WOWEE_PS4
#include "platform/ps4/ps4_platform.hpp"
#include "platform/ps4/socket_control.hpp"
#endif

namespace wowee {
namespace net {

// ---- Winsock lifecycle (no-op on Linux) ----

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinsockInit() { WSACleanup(); }
};
// Call once at program start (e.g. as a static in Application).
inline void ensureInit() {
    static WinsockInit instance;
}
#elif defined(WOWEE_PS4)
// sceNetInit and the resolver pool are brought up once by
// platform::ps4::initSystem, before main() reaches anything that opens a
// socket; there is nothing left for the first socket to do.
inline void ensureInit() {}
#else
inline void ensureInit() {}
#endif

// ---- Portable helpers ----

inline void closeSocket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

inline bool setNonBlocking(socket_t s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#elif defined(WOWEE_PS4)
    return platform::ps4::setSocketNonBlocking(s);
#else
    int flags = fcntl(s, F_GETFL, 0);
    return flags != -1 && fcntl(s, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

// Local realms use UDP only. Per-call nonblocking I/O avoids the socket
// FIONBIO ioctl rejected with EACCES on the tested PS4 runtime. Do not pass
// musl's Linux MSG_DONTWAIT (0x40): that means MSG_WAITALL to the BSD kernel.
inline constexpr int datagramFlags() {
#ifdef WOWEE_PS4
    return 0x80; // BSD MSG_DONTWAIT
#else
    return 0;
#endif
}

inline bool configureDatagramNonBlocking(socket_t s) {
#ifdef WOWEE_PS4
    return s != INVALID_SOCK; // Both sendto and recvfrom must use datagramFlags().
#else
    return setNonBlocking(s);
#endif
}

inline int lastError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline bool isWouldBlock(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

// Returns true for errors that mean the peer closed the connection cleanly.
// On Windows, WSAENOTCONN / WSAECONNRESET / WSAESHUTDOWN can be returned by
// recv() when the server closes the connection, rather than returning 0.
inline bool isConnectionClosed(int err) {
#ifdef _WIN32
    return err == WSAENOTCONN    ||  // socket not connected (server closed)
           err == WSAECONNRESET  ||  // connection reset by peer
           err == WSAESHUTDOWN   ||  // socket shut down
           err == WSAECONNABORTED;   // connection aborted
#else
    return err == ENOTCONN || err == ECONNRESET;
#endif
}

inline bool isInProgress(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAEALREADY || err == WSAEINPROGRESS;
#else
    return err == EINPROGRESS || err == EALREADY;
#endif
}

inline bool isInterrupted(int err) {
#ifdef _WIN32
    return err == WSAEINTR;
#else
    return err == EINTR;
#endif
}

inline const char* errorString(int err) {
#ifdef _WIN32
    // Simple thread-local buffer for FormatMessage
    thread_local char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return buf;
#else
    return strerror(err);
#endif
}

// Portable send - Windows recv/send take char*, not void*.
inline ssize_t portableSend(socket_t s, const uint8_t* data, size_t len) {
#if defined(WOWEE_PS4)
    constexpr int flags = 0x80; // BSD MSG_DONTWAIT; not musl's Linux value
#elif defined(MSG_NOSIGNAL)
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return ::send(s, reinterpret_cast<const char*>(data), static_cast<int>(len), flags);
}

inline ssize_t portableRecv(socket_t s, uint8_t* buf, size_t len) {
#ifdef WOWEE_PS4
    constexpr int flags = 0x80; // BSD MSG_DONTWAIT
#else
    constexpr int flags = 0;
#endif
    return ::recv(s, reinterpret_cast<char*>(buf), static_cast<int>(len), flags);
}

// Wait with a deadline, rebuilding select's modified sets/timeval on EINTR.
// Never pass an out-of-range POSIX descriptor to FD_SET.
inline bool waitWritable(socket_t s, std::chrono::steady_clock::time_point deadline) {
#ifndef _WIN32
    if (s < 0 || s >= FD_SETSIZE) {
        LOG_ERROR("TCP descriptor exceeds select capacity: ", s);
        return false;
    }
#endif
    for (;;) {
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            LOG_ERROR("TCP operation timed out");
            return false;
        }
        fd_set writes, errors;
        FD_ZERO(&writes); FD_ZERO(&errors);
        FD_SET(s, &writes); FD_SET(s, &errors);
        timeval tv{};
        tv.tv_sec = static_cast<long>(remaining / 1000000);
        tv.tv_usec = static_cast<long>(remaining % 1000000);
        const int result = ::select(static_cast<int>(s) + 1, nullptr, &writes, &errors, &tv);
        if (result > 0) return true;
        if (result < 0 && isInterrupted(lastError())) continue;
        if (result == 0) LOG_ERROR("TCP operation timed out");
        else LOG_ERROR("TCP select failed: ", errorString(lastError()));
        return false;
    }
}

inline bool connectTCP(socket_t s, const sockaddr_in& address, int timeoutSeconds) {
    const int result = ::connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result < 0) {
        const int error = lastError();
        if (!isInProgress(error) && !isInterrupted(error)) {
            LOG_ERROR("TCP connect failed: errno=", error, " (", errorString(error), ")");
            return false;
        }
        if (!waitWritable(s, std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds)))
            return false;
        int errorCode = 0;
        socklen_t length = sizeof(errorCode);
        if (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&errorCode), &length) != 0) {
            LOG_ERROR("TCP connection status failed: ", errorString(lastError()));
            return false;
        }
        if (errorCode != 0) {
            LOG_ERROR("TCP connection failed: errno=", errorCode, " (", errorString(errorCode), ")");
            return false;
        }
    }
    int one = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    return true;
}

// A partially transmitted packet cannot be discarded on a TCP stream. The
// caller must disconnect on failure, including timeout, before sending again.
inline bool sendAll(socket_t s, const uint8_t* data, size_t size, int timeoutSeconds = 5) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    size_t offset = 0;
    while (offset < size) {
        if (std::chrono::steady_clock::now() >= deadline) {
            LOG_ERROR("TCP send timed out after ", offset, " of ", size, " bytes");
            return false;
        }
        const ssize_t sent = portableSend(s, data + offset, size - offset);
        if (sent > 0) { offset += static_cast<size_t>(sent); continue; }
        if (sent == 0) { LOG_ERROR("TCP closed during send"); return false; }
        const int error = lastError();
        if (isInterrupted(error)) continue;
        if (isWouldBlock(error)) {
            if (waitWritable(s, deadline)) continue;
            return false;
        }
        LOG_ERROR("TCP send failed: ", errorString(error));
        return false;
    }
    return true;
}

/// Open a non-blocking TCP socket and resolve host into an address to connect
/// to. Answers INVALID_SOCK when either step fails, having cleaned up after
/// itself.
///
/// Call connectTCP afterwards with the auth/world connection deadline.
inline socket_t openResolvedSocket(const std::string& host, uint16_t port,
                                   struct sockaddr_in& addr) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) {
        LOG_ERROR("Failed to create socket");
        return INVALID_SOCK;
    }
    if (!setNonBlocking(fd)) {
        const int error = lastError();
#ifdef WOWEE_PS4
        LOG_ERROR("TCP setsockopt(SO_NBIO) failed: fd=", fd,
#else
        LOG_ERROR("Failed to make TCP socket nonblocking: fd=", fd,
#endif
                  " errno=", error, " (", errorString(error), ")");
        closeSocket(fd);
        return INVALID_SOCK;
    }

#ifdef WOWEE_PS4
    LOG_DEBUG("TCP nonblocking mode enabled with SO_NBIO: fd=", fd);
#endif

#ifdef WOWEE_PS4
    // The console's libc has no DNS behind getaddrinfo; the system resolver
    // (sceNetResolver) does the lookup. Same failure handling as below.
    const uint32_t ip = wowee::platform::ps4::resolveIPv4(host);
    if (ip == 0) {
        LOG_ERROR("Failed to resolve host: ", host);
        closeSocket(fd);
        return INVALID_SOCK;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    return fd;
#else
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
        LOG_ERROR("Failed to resolve host: ", host);
        closeSocket(fd);
        return INVALID_SOCK;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr;
    addr.sin_port = htons(port);
    freeaddrinfo(res);
    return fd;
#endif
}

} // namespace net
} // namespace wowee
