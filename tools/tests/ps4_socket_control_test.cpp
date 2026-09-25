// Exercise the production PS4 mode switch against a host socket-option adapter.
// This reproduces denied descriptor-control calls, not console kernel behavior.
#include "network/net_platform.hpp"
#define WOWEE_PS4 1
#include "platform/ps4/socket_control.hpp"
#undef WOWEE_PS4
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/ioctl.h>

namespace net = wowee::net;
namespace ps4 = wowee::platform::ps4;
static unsigned optionCalls = 0, deniedCalls = 0;
static int optionFailure = 0;

extern "C" int __real_fcntl(int, int, ...);
extern "C" int __real_setsockopt(int, int, int, const void*, socklen_t);
extern "C" int __wrap_fcntl(int, int, ...) {
    ++deniedCalls;
    errno = EACCES;
    return -1;
}
extern "C" int __wrap_ioctl(int, unsigned long, ...) {
    ++deniedCalls;
    errno = EACCES;
    return -1;
}
extern "C" int __wrap_setsockopt(int fd, int level, int option, const void* data, socklen_t length) {
    if (level != 0xffff || option != 0x1200)
        return __real_setsockopt(fd, level, option, data, length);
    ++optionCalls;
    assert(length == sizeof(int) && data);
    int enabled = 0;
    std::memcpy(&enabled, data, sizeof(enabled));
    assert(enabled == 1);
    if (optionFailure) { errno = optionFailure; return -1; }
    const int flags = __real_fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : __real_fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    wowee::core::Logger::getInstance().setLogLevel(wowee::core::LogLevel::FATAL);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t length = sizeof(address);
    assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    assert(listen(listener, 1) == 0);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    assert(client >= 0);
    const int original = __real_fcntl(client, F_GETFL, 0);
    int enabled = 1;
    assert(fcntl(client, F_GETFL, 0) == -1 && errno == EACCES);
    assert(ioctl(client, FIONBIO, &enabled) == -1 && errno == EACCES);
    assert(deniedCalls == 2);
    assert(ps4::setSocketNonBlocking(client));
    assert(optionCalls == 1 && deniedCalls == 2);
    assert(__real_fcntl(client, F_GETFL, 0) == (original | O_NONBLOCK));
    assert(net::connectTCP(client, address, 2));
    const int peer = accept(listener, nullptr, nullptr);
    assert(peer >= 0);
    uint8_t received = 0;
    assert(net::portableRecv(client, &received, 1) == -1 && net::isWouldBlock(errno));
    const uint8_t request = 0x5a, reply = 0xa5;
    assert(net::sendAll(client, &request, 1));
    assert(recv(peer, &received, 1, 0) == 1 && received == request);
    assert(send(peer, &reply, 1, 0) == 1);
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto count = net::portableRecv(client, &received, 1);
        if (count == 1) break;
        assert(count == -1 && net::isWouldBlock(errno));
        usleep(1000);
    }
    assert(received == reply);
    close(peer); close(client); close(listener);
    std::cout << "PASS PS4 SO_NBIO path connects and exchanges bytes with fcntl/ioctl denied\n";

    for (const int failure : {EACCES, ENOPROTOOPT}) {
        client = socket(AF_INET, SOCK_STREAM, 0);
        assert(client >= 0);
        optionFailure = failure;
        assert(!ps4::setSocketNonBlocking(client) && errno == failure);
        assert((__real_fcntl(client, F_GETFL, 0) & O_NONBLOCK) == 0);
        close(client);
    }
    optionFailure = 0;
    assert(!ps4::setSocketNonBlocking(-1) && errno == EBADF);
    assert(deniedCalls == 2); // No fallback to fcntl/ioctl on any path.
    std::cout << "PASS PS4 socket-option failure propagates without unsafe blocking fallback\n";
}
