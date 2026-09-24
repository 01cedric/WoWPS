#include "network/tcp_socket.hpp"
#include "network/packet.hpp"
#include "network/net_platform.hpp"
#include "core/logger.hpp"

namespace wowee {
namespace network {

TCPSocket::TCPSocket() {
    net::ensureInit();
}

TCPSocket::~TCPSocket() {
    TCPSocket::disconnect();  // qualified call: virtual dispatch is bypassed in destructors
}

bool TCPSocket::connect(const std::string& host, uint16_t port) {
    disconnect();
    LOG_INFO("Connecting to ", host, ":", port);

    // Socket open, non-blocking, and the address resolved.
    struct sockaddr_in serverAddr;
    sockfd = net::openResolvedSocket(host, port, serverAddr);
    if (sockfd == INVALID_SOCK) return false;

    if (!net::connectTCP(sockfd, serverAddr, 5)) {
        LOG_ERROR("Unable to connect to auth endpoint ", host, ":", port);
        disconnect();
        return false;
    }

    connected = true;
    LOG_INFO("Connected to ", host, ":", port);
    return true;
}

void TCPSocket::disconnect() {
    if (sockfd != INVALID_SOCK) {
        net::closeSocket(sockfd);
        sockfd = INVALID_SOCK;
    }
    connected = false;
    receiveBuffer.clear();
}

void TCPSocket::send(const Packet& packet) {
    if (!connected) return;

    // Build complete packet with opcode
    std::vector<uint8_t> sendData;

    // Add opcode (1 byte) - always little-endian, but it's just 1 byte so doesn't matter
    sendData.push_back(static_cast<uint8_t>(packet.getOpcode() & 0xFF));

    // Add packet data
    const auto& data = packet.getData();
    sendData.insert(sendData.end(), data.begin(), data.end());

    LOG_DEBUG("Sending packet: opcode=0x", std::hex, packet.getOpcode(), std::dec,
              " size=", sendData.size(), " bytes");

    // Send complete packet
    if (!net::sendAll(sockfd, sendData.data(), sendData.size())) disconnect();
}

void TCPSocket::update() {
    if (!connected) return;

    // Drain the socket. Some servers send small packets and close immediately; a single recv()
    // can return a partial packet, and the next recv() may return 0 (FIN) which would otherwise
    // make us drop the buffered bytes without parsing.
    bool sawClose = false;
    bool receivedAny = false;
    for (;;) {
        // 4 KB per recv() call - large enough for any single game packet while keeping
        // stack usage reasonable. Typical WoW packets are 20-500 bytes; UPDATE_OBJECT
        // can reach ~2 KB in crowded zones.
        uint8_t buffer[4096];
        ssize_t received = net::portableRecv(sockfd, buffer, sizeof(buffer));

        if (received > 0) {
            receivedAny = true;
            LOG_DEBUG("Received ", received, " bytes from server");
            receiveBuffer.insert(receiveBuffer.end(), buffer, buffer + received);
            continue; // keep draining
        }

        if (received == 0) {
            sawClose = true;
            break;
        }

        int err = net::lastError();
        if (net::isInterrupted(err)) continue;
        if (net::isWouldBlock(err)) {
            break;
        }
        if (net::isConnectionClosed(err)) {
            // Peer closed the connection - treat the same as recv() returning 0
            sawClose = true;
            break;
        }

        LOG_ERROR("Receive failed: ", net::errorString(err));
        disconnect();
        return;
    }

    if (receivedAny) {
        tryParsePackets();
    }

    if (sawClose) {
        LOG_INFO("Connection closed by server");
        disconnect();
    }
}

void TCPSocket::tryParsePackets() {
    // For auth packets, we need at least 1 byte (opcode)
    while (!receiveBuffer.empty()) {
        uint8_t opcode = receiveBuffer[0];

        // Determine expected packet size based on opcode
        // This is specific to authentication protocol
        size_t expectedSize = getExpectedPacketSize(opcode);

        if (expectedSize == 0) {
            // Unknown opcode or need more data to determine size
            LOG_WARNING("Unknown opcode or indeterminate size: 0x", std::hex, static_cast<int>(opcode), std::dec);
            break;
        }

        if (receiveBuffer.size() < expectedSize) {
            // Not enough data yet
            LOG_DEBUG("Waiting for more data: have ", receiveBuffer.size(),
                     " bytes, need ", expectedSize);
            break;
        }

        // We have a complete packet!
        LOG_DEBUG("Parsing packet: opcode=0x", std::hex, static_cast<int>(opcode), std::dec,
                 " size=", expectedSize, " bytes");

        // Create packet from buffer data
        std::vector<uint8_t> packetData(receiveBuffer.begin(),
                                        receiveBuffer.begin() + expectedSize);

        Packet packet(opcode, packetData);

        // Remove parsed data from buffer
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + expectedSize);

        // Call callback if set
        if (packetCallback) {
            packetCallback(packet);
        }
    }
}

size_t TCPSocket::getExpectedPacketSize(uint8_t opcode) {
    // Authentication packet sizes (WoW 3.3.5a)
    // Note: These are minimum sizes. Some packets are variable length.

    switch (opcode) {
        case 0x00:  // LOGON_CHALLENGE response
            // Need to read status byte to determine success/failure
            if (receiveBuffer.size() >= 3) {
                uint8_t status = receiveBuffer[2];
                if (status == 0x00) {
                    // Success: opcode(1) + unk(1) + status(1) + B(32) + gLen(1) + g(gLen) +
                    //          nLen(1) + N(nLen) + salt(32) + crcHash(16) + securityFlags(1)
                    //          + optional security flag data
                    if (receiveBuffer.size() >= 36) {  // enough to read g_len
                        uint8_t gLen = receiveBuffer[35];
                        size_t minSize = 36 + gLen + 1;  // up to N_len
                        if (receiveBuffer.size() >= minSize) {
                            uint8_t nLen = receiveBuffer[36 + gLen];
                            size_t baseSize = 36 + gLen + 1 + nLen + 32 + 16 + 1;
                            // Need to read securityFlags to account for extra data
                            if (receiveBuffer.size() >= baseSize) {
                                uint8_t secFlags = receiveBuffer[baseSize - 1];
                                size_t extra = 0;
                                if (secFlags & 0x01) extra += 20;  // PIN: seed(4) + salt(16)
                                if (secFlags & 0x02) extra += 12;  // Matrix: w(1)+h(1)+digits(1)+challenges(1)+seed(8)
                                if (secFlags & 0x04) extra += 1;   // Authenticator: required(1)
                                return baseSize + extra;
                            }
                        }
                    }
                    return 0;  // Need more data
                }                     // Failure - just opcode + unknown + status
                    return 3;
               
            }
            return 0;  // Need more data to determine

        case 0x01:  // LOGON_PROOF response
            // Success response varies by server build (determined by client build sent in challenge):
            //   Build >= 8089: cmd(1)+error(1)+M2(20)+accountFlags(4)+surveyId(4)+loginFlags(2) = 32
            //   Build 6299-8088: cmd(1)+error(1)+M2(20)+surveyId(4)+loginFlags(2) = 28
            //   Build < 6299: cmd(1)+error(1)+M2(20)+surveyId(4) = 26
            // Failure: varies by server - minimum 2 bytes (opcode + status), some send 4
            if (receiveBuffer.size() >= 2) {
                uint8_t status = receiveBuffer[1];
                if (status == 0x00) {
                    return clientBuild >= 8089 ? 32 : (clientBuild >= 6299 ? 28 : 26);
                }                     // Consume up to 4 bytes if available, minimum 2
                    return (receiveBuffer.size() >= 4) ? 4 : 2;
               
            }
            return 0;  // Need more data

        case 0x10:  // REALM_LIST response
            // Variable length - format: opcode(1) + size(2) + payload(size)
            // Need to read size field (little-endian uint16 at offset 1-2)
            if (receiveBuffer.size() >= 3) {
                uint16_t size = receiveBuffer[1] | (receiveBuffer[2] << 8);
                // Total packet size is: opcode(1) + size field(2) + payload(size)
                return 1 + 2 + size;
            }
            return 0;  // Need more data to read size field

        default:
            LOG_WARNING("Unknown auth packet opcode: 0x", std::hex, static_cast<int>(opcode), std::dec);
            return 0;
    }
}

} // namespace network
} // namespace wowee
