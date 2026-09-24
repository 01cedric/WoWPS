#include "network/tcp_socket.hpp"
#include "network/packet.hpp"
#include "platform/ps4/socket_control.hpp"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

using wowee::network::TCPSocket;
using wowee::network::Packet;
namespace net = wowee::net;
static bool shortWrites = false, failStatus = false, zeroWrite = false;
static unsigned sendCalls = 0;
extern "C" ssize_t __real_send(int, const void*, size_t, int);
extern "C" int __real_getsockopt(int, int, int, void*, socklen_t*);
extern "C" ssize_t __wrap_send(int fd, const void* bytes, size_t length, int flags) {
    if (zeroWrite) return 0;
    if (shortWrites) {
        ++sendCalls;
        if (sendCalls % 5 == 1) { errno = EINTR; return -1; }
        if (sendCalls % 5 == 2) { errno = EAGAIN; return -1; }
        length = std::min<size_t>(length, 3);
    }
    return __real_send(fd, bytes, length, flags);
}
extern "C" int __wrap_getsockopt(int fd, int level, int option, void* data, socklen_t* size) {
    if (failStatus) { errno = EIO; return -1; }
    return __real_getsockopt(fd, level, option, data, size);
}
struct Listener {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    uint16_t port = 0;
    Listener() {
        assert(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        socklen_t size = sizeof(address);
        assert(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        port = ntohs(address.sin_port);
        assert(listen(fd, 8) == 0);
    }
    ~Listener() { close(fd); }
    int acceptPeer() {
        const int peer = accept(fd, nullptr, nullptr); assert(peer >= 0);
        int one = 1; assert(setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0);
        return peer;
    }
};
static void sendBytes(int fd, const uint8_t* data, size_t length) {
    assert(net::sendAll(fd, data, length));
}
static void pump(TCPSocket& socket) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Loopback data is already sent; several updates cover scheduler delivery.
    for (unsigned i = 0; i < 30; ++i) socket.update();
}
int main() {
    wowee::core::Logger::getInstance().setLogLevel(wowee::core::LogLevel::FATAL);
    for (const auto build : {uint16_t(12340), uint16_t(8089), uint16_t(6299), uint16_t(5875)}) {
        const size_t size = build >= 8089 ? 32 : (build >= 6299 ? 28 : 26);
        for (size_t split = 1; split < size; ++split) {
            Listener server;
            TCPSocket client;
            client.setClientBuild(build);
            std::vector<size_t> sizes;
            client.setPacketCallback([&](const Packet& p) { sizes.push_back(p.getSize()); });
            assert(client.connect("127.0.0.1", server.port));
            const int peer = server.acceptPeer();
            std::vector<uint8_t> proof(size, 0); proof[0] = 1;
            sendBytes(peer, proof.data(), split); pump(client);
            assert(sizes.empty());
            sendBytes(peer, proof.data() + split, size - split); pump(client);
            assert(sizes == std::vector<size_t>{size});
            const uint8_t realms[] = {0x10, 8, 0, 0, 0, 0, 0, 0, 0, 0x10, 0};
            for (uint8_t byte : realms) { sendBytes(peer, &byte, 1); pump(client); }
            assert((sizes == std::vector<size_t>{size, sizeof(realms)}));
            close(peer);
        }
    }
    std::cout << "PASS proof framing at every split for 4 client builds; fragmented realm list\n";
    {
        Listener server;
        TCPSocket client;
        assert(client.connect("localhost", server.port));
        const int peer = server.acceptPeer();
        Packet outgoing(0x10); std::vector<uint8_t> bytes(512, 0x5a);
        outgoing.writeBytes(bytes.data(), bytes.size());
        shortWrites = true; client.send(outgoing); shortWrites = false;
        std::vector<uint8_t> received(bytes.size() + 1);
        assert(recv(peer, received.data(), received.size(), MSG_WAITALL) == static_cast<ssize_t>(received.size()));
        assert(received[0] == 0x10 && std::equal(bytes.begin(), bytes.end(), received.begin() + 1));
        assert(sendCalls > bytes.size() / 3);
        size_t failures = 0;
        client.setPacketCallback([&](const Packet& p) { assert(p.getSize() == 2); ++failures; });
        const uint8_t failure[] = {1, 4}; sendBytes(peer, failure, sizeof(failure));
        shutdown(peer, SHUT_WR); pump(client);
        assert(failures == 1 && !client.isConnected()); close(peer);
    }
    std::cout << "PASS short writes, EINTR, EAGAIN, failure response before FIN\n";
    {
        Listener server; TCPSocket client;
        failStatus = true;
        assert(!client.connect("127.0.0.1", server.port));
        failStatus = false;
        assert(!client.isConnected());
        assert(client.connect("127.0.0.1", server.port));
        zeroWrite = true; client.send(Packet(0)); zeroWrite = false;
        assert(!client.isConnected());
    }
    {
        Listener server;
        sockaddr_in address{};
        const int fd = net::openResolvedSocket("127.0.0.1", server.port, address);
        assert(fd >= 0);
        const int original = fcntl(fd, F_GETFL, 0);
        assert(wowee::platform::ps4::setSocketNonBlocking(fd));
        assert((fcntl(fd, F_GETFL, 0) & (original | O_NONBLOCK)) == (original | O_NONBLOCK));
        const uint8_t byte = 0;
        assert(!net::sendAll(fd, &byte, 1, 0));
        assert(!net::waitWritable(FD_SETSIZE, std::chrono::steady_clock::now() + std::chrono::seconds(1)));
        close(fd);
        assert(!wowee::platform::ps4::setSocketNonBlocking(-1));
    }
    std::cout << "PASS connection-status failure, reconnect, zero send, deadline, fd bounds, nonblocking flags\n";
}
