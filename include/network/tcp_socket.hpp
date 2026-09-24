#pragma once

#include "network/socket.hpp"
#include "network/net_platform.hpp"

namespace wowee {
namespace network {

class TCPSocket : public Socket {
public:
    TCPSocket();
    ~TCPSocket() override;

    bool connect(const std::string& host, uint16_t port) override;
    void disconnect() override;
    [[nodiscard]] bool isConnected() const override { return connected; }

    void send(const Packet& packet) override;
    void update() override;

    // Proof framing is selected by the build announced in LOGON_CHALLENGE,
    // never by how many bytes happened to arrive in one TCP segment.
    void setClientBuild(uint16_t build) { clientBuild = build; }

private:
    void tryParsePackets();
    size_t getExpectedPacketSize(uint8_t opcode);

    socket_t sockfd = INVALID_SOCK;
    bool connected = false;
    uint16_t clientBuild = 12340;
    std::vector<uint8_t> receiveBuffer;
};

} // namespace network
} // namespace wowee
