#include "network/world_socket.hpp"
#include "core/logger.hpp"
#include <chrono>
#include <thread>
#include <iostream>

// Simulate preemption after the auth bytes enter the kernel. The independent
// peer replies during this delay while the production receive thread runs.
extern "C" ssize_t __real_send(int, const void*, size_t, int);
extern "C" ssize_t __wrap_send(int fd, const void* data, size_t size, int flags) {
    const auto sent = __real_send(fd, data, size, flags);
    if (sent > 0) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    return sent;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    wowee::core::Logger::getInstance().setLogLevel(wowee::core::LogLevel::FATAL);
    wowee::network::WorldSocket client;
    std::vector<uint8_t> key(40);
    for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i);
    int stage = 0;
    bool failed = false;
    client.setPacketCallback([&](const wowee::network::Packet& packet) {
        if (stage == 0 && packet.getOpcode() == 0x1ec && packet.getSize() == 40) {
            wowee::network::Packet auth(0x1ed);
            auth.writeUInt32(12340); // Transport fixture, not a full game session.
            failed = !client.sendAuthSession(auth, key, 12340);
            stage = 1;
        } else if (stage == 1 && packet.getOpcode() == 0x1ee && packet.getData() == std::vector<uint8_t>{0x0c}) {
            client.send(wowee::network::Packet(0x37));
            stage = 2;
        } else if (stage == 2 && packet.getOpcode() == 0x3b && packet.getData() == std::vector<uint8_t>{0}) {
            stage = 3;
        } else failed = true;
    });
    if (!client.connect("127.0.0.1", static_cast<uint16_t>(std::stoi(argv[1])))) return 3;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end && !failed && stage != 3) {
        client.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    client.disconnect();
    if (failed || stage != 3) { std::cerr << "world transport stage=" << stage << '\n'; return 4; }
    return 0;
}
