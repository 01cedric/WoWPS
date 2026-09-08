#include "network/world_socket.hpp"
#include "core/env_flag.hpp"
#include "network/packet.hpp"
#include "network/net_platform.hpp"
#include "game/opcode_table.hpp"
#include "auth/crypto.hpp"
#include "core/logger.hpp"
#include "core/profiler.hpp"
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

namespace {
constexpr size_t kMaxReceiveBufferBytes = 8 * 1024 * 1024;
// Per-frame packet budgets prevent a burst of server data from starving the
// render loop. Tunable via env vars for debugging heavy-traffic scenarios
// (e.g. SMSG_UPDATE_OBJECT floods on login to crowded zones).
constexpr int kDefaultMaxParsedPacketsPerUpdate = 64;
constexpr int kAbsoluteMaxParsedPacketsPerUpdate = 220;
constexpr int kMinParsedPacketsPerUpdate = 8;
constexpr int kDefaultMaxPacketCallbacksPerUpdate = 48;
constexpr int kAbsoluteMaxPacketCallbacksPerUpdate = 64;
constexpr int kMinPacketCallbacksPerUpdate = 1;
constexpr int kMaxRecvCallsPerUpdate = 64;
constexpr size_t kMaxRecvBytesPerUpdate = 512 * 1024;
constexpr size_t kMaxQueuedPacketCallbacks = 4096;
constexpr int kAsyncPumpSleepMs = 2;
constexpr size_t kRecentPacketHistoryLimit = 96;
constexpr auto kRecentPacketHistoryWindow = std::chrono::seconds(15);

inline int parsedPacketsBudgetPerUpdate() {
    static int budget = []() {
        const char* raw = std::getenv("WOWEE_NET_MAX_PARSED_PACKETS");
        if (!raw || !*raw) return kDefaultMaxParsedPacketsPerUpdate;
        char* end = nullptr;
        long parsed = std::strtol(raw, &end, 10);
        if (end == raw) return kDefaultMaxParsedPacketsPerUpdate;
        if (parsed < kMinParsedPacketsPerUpdate) return kMinParsedPacketsPerUpdate;
        if (parsed > kAbsoluteMaxParsedPacketsPerUpdate) return kAbsoluteMaxParsedPacketsPerUpdate;
        return static_cast<int>(parsed);
    }();
    return budget;
}

inline int packetCallbacksBudgetPerUpdate() {
    static int budget = []() {
        const char* raw = std::getenv("WOWEE_NET_MAX_PACKET_CALLBACKS");
        if (!raw || !*raw) return kDefaultMaxPacketCallbacksPerUpdate;
        char* end = nullptr;
        long parsed = std::strtol(raw, &end, 10);
        if (end == raw) return kDefaultMaxPacketCallbacksPerUpdate;
        if (parsed < kMinPacketCallbacksPerUpdate) return kMinPacketCallbacksPerUpdate;
        if (parsed > kAbsoluteMaxPacketCallbacksPerUpdate) return kAbsoluteMaxPacketCallbacksPerUpdate;
        return static_cast<int>(parsed);
    }();
    return budget;
}

inline bool isLoginPipelineSmsg(uint16_t opcode) {
    switch (opcode) {
        case 0x1EC: // SMSG_AUTH_CHALLENGE
        case 0x1EE: // SMSG_AUTH_RESPONSE
        case 0x03B: // SMSG_CHAR_ENUM
        case 0x03A: // SMSG_CHAR_CREATE
        case 0x03C: // SMSG_CHAR_DELETE
        case 0x4AB: // SMSG_CLIENTCACHE_VERSION
        case 0x0FD: // SMSG_TUTORIAL_FLAGS
        case 0x2E6: // SMSG_WARDEN_DATA
            return true;
        default:
            return false;
    }
}

inline bool isLoginPipelineCmsg(uint16_t opcode) {
    switch (opcode) {
        case 0x1ED: // CMSG_AUTH_SESSION
        case 0x037: // CMSG_CHAR_ENUM
        case 0x036: // CMSG_CHAR_CREATE
        case 0x038: // CMSG_CHAR_DELETE
        case 0x03D: // CMSG_PLAYER_LOGIN
            return true;
        default:
            return false;
    }
}


const char* opcodeNameForTrace(uint16_t wireOpcode) {
    const auto* table = wowee::game::getActiveOpcodeTable();
    if (!table) return "UNKNOWN";
    auto logical = table->fromWire(wireOpcode);
    if (!logical) return "UNKNOWN";
    return wowee::game::OpcodeTable::logicalToName(*logical);
}
} // namespace

namespace wowee {
namespace network {

// WoW 3.3.5a RC4 encryption keys (hardcoded in client)
static constexpr uint8_t ENCRYPT_KEY[] = {
    0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5,
    0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE
};

static constexpr uint8_t DECRYPT_KEY[] = {
    0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA,
    0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57
};

WorldSocket::WorldSocket() {
    net::ensureInit();
    // Always reserve baseline receive capacity (safe, behavior-preserving).
    receiveBuffer.reserve(64 * 1024);
    useFastRecvAppend_ = core::envFlagEnabled("WOWEE_NET_FAST_RECV_APPEND", true);
    useParseScratchQueue_ = core::envFlagEnabled("WOWEE_NET_PARSE_SCRATCH", false);
    useAsyncPump_ = core::envFlagEnabled("WOWEE_NET_ASYNC_PUMP", true);
    if (useParseScratchQueue_) {
        LOG_WARNING("WOWEE_NET_PARSE_SCRATCH is temporarily disabled (known unstable); forcing off");
        useParseScratchQueue_ = false;
    }
    if (useParseScratchQueue_) {
        parsedPacketsScratch_.reserve(64);
    }
    LOG_INFO("WorldSocket net opts: fast_recv_append=", useFastRecvAppend_ ? "on" : "off",
             " async_pump=", useAsyncPump_ ? "on" : "off",
             " parse_scratch=", useParseScratchQueue_ ? "on" : "off",
             " max_parsed_packets=", parsedPacketsBudgetPerUpdate(),
             " max_packet_callbacks=", packetCallbacksBudgetPerUpdate());
}

WorldSocket::~WorldSocket() {
    WorldSocket::disconnect();  // qualified call: virtual dispatch is bypassed in destructors
}

bool WorldSocket::connect(const std::string& host, uint16_t port) {
    LOG_INFO("Connecting to world server: ", host, ":", port);

    stopAsyncPump();

    // Socket open, non-blocking, and the address resolved.
    struct sockaddr_in serverAddr;
    sockfd = net::openResolvedSocket(host, port, serverAddr);
    if (sockfd == INVALID_SOCK) return false;

    int result = ::connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result < 0) {
        int err = net::lastError();
        if (!net::isInProgress(err)) {
            LOG_ERROR("Failed to connect: ", net::errorString(err));
            net::closeSocket(sockfd);
            sockfd = INVALID_SOCK;
            return false;
        }

        // Non-blocking connect in progress - wait up to 10s for completion.
        // On Windows, calling recv() before the connect completes returns
        // WSAENOTCONN; we must poll writability before declaring connected.
        fd_set writefds, errfds;
        FD_ZERO(&writefds);
        FD_ZERO(&errfds);
        FD_SET(sockfd, &writefds);
        FD_SET(sockfd, &errfds);

        struct timeval tv;
        tv.tv_sec  = 10;
        tv.tv_usec = 0;

        int sel = ::select(static_cast<int>(sockfd) + 1, nullptr, &writefds, &errfds, &tv);
        if (sel <= 0) {
            LOG_ERROR("World server connection timed out (", host, ":", port, ")");
            net::closeSocket(sockfd);
            sockfd = INVALID_SOCK;
            return false;
        }

        // Verify the socket error code - writeable doesn't guarantee success on all platforms
        int sockErr = 0;
        socklen_t errLen = sizeof(sockErr);
        getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char*>(&sockErr), &errLen);
        if (sockErr != 0) {
            LOG_ERROR("Failed to connect to world server: ", net::errorString(sockErr));
            net::closeSocket(sockfd);
            sockfd = INVALID_SOCK;
            return false;
        }
    }

    // Disable Nagle's algorithm - send small packets immediately.
    int one = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&one), sizeof(one));

    connected = true;
    LOG_INFO("Connected to world server: ", host, ":", port);
    startAsyncPump();
    return true;
}

void WorldSocket::disconnect() {
    stopAsyncPump();
    {
        std::lock_guard<std::mutex> lock(ioMutex_);
        closeSocketNoJoin();
        encryptionEnabled = false;
        useVanillaCrypt = false;
        receiveBuffer.clear();
        receiveReadOffset_ = 0;
        parsedPacketsScratch_.clear();
        headerBytesDecrypted = 0;
        packetTraceStart_ = {};
        packetTraceUntil_ = {};
        packetTraceReason_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        pendingPacketCallbacks_.clear();
    }
    LOG_INFO("Disconnected from world server");
}

void WorldSocket::tracePacketsFor(std::chrono::milliseconds duration, const std::string& reason) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    packetTraceStart_ = std::chrono::steady_clock::now();
    packetTraceUntil_ = packetTraceStart_ + duration;
    packetTraceReason_ = reason;
    LOG_DEBUG("WS TRACE enabled: reason='", packetTraceReason_,
              "' durationMs=", duration.count());
}

bool WorldSocket::isConnected() const {
    return connected.load(std::memory_order_acquire);
}

void WorldSocket::closeSocketNoJoin() {
    if (sockfd != INVALID_SOCK) {
        net::closeSocket(sockfd);
        sockfd = INVALID_SOCK;
    }
    connected = false;
}

void WorldSocket::recordRecentPacket(bool outbound, uint16_t opcode, uint16_t payloadLen) {
    const auto now = std::chrono::steady_clock::now();
    recentPacketHistory_.push_back(RecentPacketTrace{now, outbound, opcode, payloadLen});
    while (!recentPacketHistory_.empty() &&
           (recentPacketHistory_.size() > kRecentPacketHistoryLimit ||
            (now - recentPacketHistory_.front().when) > kRecentPacketHistoryWindow)) {
        recentPacketHistory_.pop_front();
    }
}

void WorldSocket::dumpRecentPacketHistoryLocked(const char* reason, size_t bufferedBytes) {
    if (recentPacketHistory_.empty()) {
        LOG_WARNING("WS CLOSE TRACE reason='", reason, "' buffered=", bufferedBytes,
                  " no recent packet history");
        return;
    }

    const auto lastWhen = recentPacketHistory_.back().when;
    LOG_WARNING("WS CLOSE TRACE reason='", reason, "' buffered=", bufferedBytes,
              " recentPackets=", recentPacketHistory_.size());
    for (const auto& entry : recentPacketHistory_) {
        const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            lastWhen - entry.when).count();
        LOG_WARNING("WS CLOSE TRACE ", entry.outbound ? "TX" : "RX",
                  " -", ageMs, "ms opcode=0x",
                  std::hex, entry.opcode, std::dec,
                  " logical=", opcodeNameForTrace(entry.opcode),
                  " payload=", entry.payloadLen);
    }
}

void WorldSocket::send(const Packet& packet) {
    static const bool kLogCharCreatePayload = core::envFlagEnabled("WOWEE_NET_LOG_CHAR_CREATE", false);
    static const bool kLogSwapItemPackets = core::envFlagEnabled("WOWEE_NET_LOG_SWAP_ITEM", false);
    std::lock_guard<std::mutex> lock(ioMutex_);
    if (!connected || sockfd == INVALID_SOCK) return;

    const auto& data = packet.getData();
    uint16_t opcode = packet.getOpcode();
    // CMSG header uses a 16-bit size field, so payloads > 64KB are unsupported.
    // Guard here rather than silently truncating via cast, which would write a
    // wrong size to the header while still appending all bytes.
    if (data.size() > 0xFFFF) {
        LOG_ERROR("Packet payload too large for CMSG header: ", data.size(),
                  " bytes (opcode=0x", std::hex, opcode, std::dec, "). Dropping.");
        return;
    }
    uint16_t payloadLen = static_cast<uint16_t>(data.size());

    // Debug: parse and log character-create payload fields (helps diagnose appearance issues).
    if (kLogCharCreatePayload && opcode == 0x036) { // CMSG_CHAR_CREATE
        size_t pos = 0;
        std::string name;
        while (pos < data.size()) {
            uint8_t c = data[pos++];
            if (c == 0) break;
            name.push_back(static_cast<char>(c));
        }
        auto rd8 = [&](uint8_t& out) -> bool {
            if (pos >= data.size()) return false;
            out = data[pos++];
            return true;
        };
        uint8_t race = 0, cls = 0, gender = 0;
        uint8_t skin = 0, face = 0, hairStyle = 0, hairColor = 0, facial = 0, outfit = 0;
        bool ok =
            rd8(race) && rd8(cls) && rd8(gender) &&
            rd8(skin) && rd8(face) && rd8(hairStyle) && rd8(hairColor) && rd8(facial) && rd8(outfit);
        if (ok) {
            LOG_INFO("CMSG_CHAR_CREATE payload: name='", name,
                     "' race=", static_cast<int>(race), " class=", static_cast<int>(cls), " gender=", static_cast<int>(gender),
                     " skin=", static_cast<int>(skin), " face=", static_cast<int>(face),
                     " hairStyle=", static_cast<int>(hairStyle), " hairColor=", static_cast<int>(hairColor),
                     " facial=", static_cast<int>(facial), " outfit=", static_cast<int>(outfit),
                     " payloadLen=", payloadLen);
            // Persist to disk so we can compare TX vs DB even if the console scrolls away.
            std::ofstream f("charcreate_payload.log", std::ios::app);
            if (f.is_open()) {
                f << "name='" << name << "'"
                  << " race=" << static_cast<int>(race)
                  << " class=" << static_cast<int>(cls)
                  << " gender=" << static_cast<int>(gender)
                  << " skin=" << static_cast<int>(skin)
                  << " face=" << static_cast<int>(face)
                  << " hairStyle=" << static_cast<int>(hairStyle)
                  << " hairColor=" << static_cast<int>(hairColor)
                  << " facial=" << static_cast<int>(facial)
                  << " outfit=" << static_cast<int>(outfit)
                  << " payloadLen=" << payloadLen
                  << "\n";
            }
        } else {
            LOG_WARNING("CMSG_CHAR_CREATE payload too short to parse (name='", name,
                        "' payloadLen=", payloadLen, " pos=", pos, ")");
        }
    }

    if (kLogSwapItemPackets && (opcode == 0x10C || opcode == 0x10D)) { // CMSG_SWAP_ITEM / CMSG_SWAP_INV_ITEM
        LOG_INFO("WS TX opcode=0x", std::hex, opcode, std::dec, " payloadLen=", payloadLen,
                 " data=[", core::toHexString(data.data(), data.size(), true), "]");
    }

    const auto traceNow = std::chrono::steady_clock::now();
    recordRecentPacket(true, opcode, payloadLen);
    if (packetTraceUntil_ > traceNow) {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            traceNow - packetTraceStart_).count();
        LOG_DEBUG("WS TRACE TX +", elapsedMs, "ms opcode=0x",
                  std::hex, opcode, std::dec,
                  " logical=", opcodeNameForTrace(opcode),
                  " payload=", payloadLen,
                  " reason='", packetTraceReason_, "'");
    }

    // WotLK 3.3.5 CMSG header (6 bytes total):
    // - size (2 bytes, big-endian) = payloadLen + 4 (opcode is 4 bytes for CMSG)
    // - opcode (4 bytes, little-endian)
    // Note: Client-to-server uses 4-byte opcode, server-to-client uses 2-byte
    uint16_t sizeField = payloadLen + 4;

    std::vector<uint8_t> sendData;
    sendData.reserve(6 + payloadLen);

    // Size (2 bytes, big-endian)
    uint8_t size_hi = (sizeField >> 8) & 0xFF;
    uint8_t size_lo = sizeField & 0xFF;
    sendData.push_back(size_hi);
    sendData.push_back(size_lo);

    // Opcode (4 bytes, little-endian)
    sendData.push_back(opcode & 0xFF);
    sendData.push_back((opcode >> 8) & 0xFF);
    sendData.push_back(0);  // High bytes are 0 for all WoW opcodes
    sendData.push_back(0);

    // Debug logging disabled - too spammy

    // Encrypt header if encryption is enabled (all 6 bytes)
    if (encryptionEnabled) {
        if (useVanillaCrypt) {
            vanillaCrypt.encrypt(sendData.data(), 6);
        } else {
            encryptCipher.process(sendData.data(), 6);
        }
    }

    // Add payload (unencrypted)
    sendData.insert(sendData.end(), data.begin(), data.end());

    // Debug: dump packet bytes for AUTH_SESSION
    if (opcode == 0x1ED) {
        LOG_DEBUG("AUTH_SESSION raw bytes: ",
                  core::toHexString(sendData.data(), sendData.size(), true));
    }
    if (isLoginPipelineCmsg(opcode)) {
        LOG_INFO("WS TX LOGIN opcode=0x", std::hex, opcode, std::dec,
                 " payload=", payloadLen, " enc=", encryptionEnabled ? "yes" : "no");
    }

    // Send complete packet, retrying on partial sends. Non-blocking sockets
    // can return fewer bytes than requested when the kernel buffer is full.
    // Without a retry loop, the server receives a truncated packet and the
    // TCP stream permanently desyncs (next header lands mid-payload).
    size_t totalSent = 0;
    while (totalSent < sendData.size()) {
        ssize_t sent = net::portableSend(sockfd, sendData.data() + totalSent,
                                          sendData.size() - totalSent);
        if (sent < 0) {
            int err = net::lastError();
            if (net::isWouldBlock(err)) {
                // Kernel buffer full - yield briefly and retry.
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            LOG_ERROR("Send failed: ", net::errorString(err));
            break;
        }
        if (sent == 0) break;  // connection closed
        totalSent += static_cast<size_t>(sent);
    }
    if (totalSent != sendData.size()) {
        LOG_WARNING("Incomplete send: ", totalSent, " of ", sendData.size(), " bytes");
    }
}

void WorldSocket::update() {
    ZoneScopedN("WorldSocket::update");
    if (!useAsyncPump_) {
        pumpNetworkIO();
    }
    dispatchQueuedPackets();
}

void WorldSocket::startAsyncPump() {
    if (!useAsyncPump_ || asyncPumpRunning_.load(std::memory_order_acquire)) {
        return;
    }
    asyncPumpStop_.store(false, std::memory_order_release);
    asyncPumpThread_ = std::thread(&WorldSocket::asyncPumpLoop, this);
}

void WorldSocket::stopAsyncPump() {
    asyncPumpStop_.store(true, std::memory_order_release);
    if (asyncPumpThread_.joinable()) {
        asyncPumpThread_.join();
    }
    asyncPumpRunning_.store(false, std::memory_order_release);
}

void WorldSocket::asyncPumpLoop() {
    asyncPumpRunning_.store(true, std::memory_order_release);
    while (!asyncPumpStop_.load(std::memory_order_acquire)) {
        pumpNetworkIO();
        {
            std::lock_guard<std::mutex> lock(ioMutex_);
            if (!connected) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kAsyncPumpSleepMs));
    }
    asyncPumpRunning_.store(false, std::memory_order_release);
}

void WorldSocket::pumpNetworkIO() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    if (!connected || sockfd == INVALID_SOCK) return;
    auto bufferedBytes = [&]() -> size_t {
        return (receiveBuffer.size() >= receiveReadOffset_)
            ? (receiveBuffer.size() - receiveReadOffset_)
            : 0;
    };
    auto compactReceiveBuffer = [&]() {
        if (receiveReadOffset_ == 0) return;
        if (receiveReadOffset_ >= receiveBuffer.size()) {
            receiveBuffer.clear();
            receiveReadOffset_ = 0;
            return;
        }
        const size_t remaining = receiveBuffer.size() - receiveReadOffset_;
        std::memmove(receiveBuffer.data(), receiveBuffer.data() + receiveReadOffset_, remaining);
        receiveBuffer.resize(remaining);
        receiveReadOffset_ = 0;
    };

    // Drain the socket. Some servers send an auth response and immediately close; a single recv()
    // may read the response, and a subsequent recv() can return 0 (FIN). If we disconnect right
    // away we lose the buffered response and the UI ends up with a generic "no characters" symptom.
    bool sawClose = false;
    bool receivedAny = false;
    size_t bytesReadThisTick = 0;
    int readOps = 0;
    while (connected && readOps < kMaxRecvCallsPerUpdate &&
           bytesReadThisTick < kMaxRecvBytesPerUpdate) {
        uint8_t buffer[4096];
        ssize_t received = net::portableRecv(sockfd, buffer, sizeof(buffer));

        if (received > 0) {
            receivedAny = true;
            ++readOps;
            size_t receivedSize = static_cast<size_t>(received);
            bytesReadThisTick += receivedSize;
            if (useFastRecvAppend_) {
                size_t liveBytes = bufferedBytes();
                if (liveBytes > kMaxReceiveBufferBytes || receivedSize > (kMaxReceiveBufferBytes - liveBytes)) {
                    compactReceiveBuffer();
                    liveBytes = bufferedBytes();
                }
                if (liveBytes > kMaxReceiveBufferBytes || receivedSize > (kMaxReceiveBufferBytes - liveBytes)) {
                    LOG_ERROR("World socket receive buffer would overflow (buffered=", liveBytes,
                              " incoming=", receivedSize, " max=", kMaxReceiveBufferBytes,
                              "). Disconnecting to recover framing.");
                    closeSocketNoJoin();
                    return;
                }
                const size_t oldSize = receiveBuffer.size();
                const size_t needed = oldSize + receivedSize;
                if (receiveBuffer.capacity() < needed) {
                    size_t newCap = receiveBuffer.capacity() ? receiveBuffer.capacity() : 64 * 1024;
                    while (newCap < needed && newCap < kMaxReceiveBufferBytes) {
                        newCap = std::min(kMaxReceiveBufferBytes, newCap * 2);
                    }
                    if (newCap < needed) {
                        LOG_ERROR("World socket receive buffer capacity growth failed (needed=", needed,
                                  " max=", kMaxReceiveBufferBytes, "). Disconnecting to recover framing.");
                        closeSocketNoJoin();
                        return;
                    }
                    receiveBuffer.reserve(newCap);
                }
                receiveBuffer.insert(receiveBuffer.end(), buffer, buffer + receivedSize);
            } else {
                // Non-fast path: same overflow pre-check as fast path to prevent
                // unbounded buffer growth before the post-check below.
                size_t liveBytes = bufferedBytes();
                if (liveBytes > kMaxReceiveBufferBytes || receivedSize > (kMaxReceiveBufferBytes - liveBytes)) {
                    compactReceiveBuffer();
                    liveBytes = bufferedBytes();
                }
                if (liveBytes > kMaxReceiveBufferBytes || receivedSize > (kMaxReceiveBufferBytes - liveBytes)) {
                    LOG_ERROR("World socket receive buffer would overflow (buffered=", liveBytes,
                              " incoming=", receivedSize, " max=", kMaxReceiveBufferBytes,
                              "). Disconnecting to recover framing.");
                    closeSocketNoJoin();
                    return;
                }
                receiveBuffer.insert(receiveBuffer.end(), buffer, buffer + receivedSize);
            }
            if (bufferedBytes() > kMaxReceiveBufferBytes) {
                LOG_ERROR("World socket receive buffer overflow (", bufferedBytes(),
                          " bytes). Disconnecting to recover framing.");
                closeSocketNoJoin();
                return;
            }
            continue;
        }

        if (received == 0) {
            sawClose = true;
            break;
        }

        int err = net::lastError();
        if (net::isWouldBlock(err)) {
            break;
        }
        if (net::isConnectionClosed(err)) {
            // Peer closed the connection - treat the same as recv() returning 0
            sawClose = true;
            break;
        }

        LOG_ERROR("Receive failed: ", net::errorString(err));
        closeSocketNoJoin();
        return;
    }

    if (receivedAny) {
        const bool debugLog = core::Logger::getInstance().shouldLog(core::LogLevel::DEBUG);
        if (debugLog) {
            LOG_DEBUG("World socket read ", bytesReadThisTick, " bytes in ", readOps,
                      " recv call(s), buffered=", bufferedBytes());
        }
        // Hex dump received bytes for auth debugging (debug-only to avoid per-frame string work)
        if (debugLog && bytesReadThisTick <= 128) {
            LOG_DEBUG("World socket raw bytes: ",
                      core::toHexString(receiveBuffer.data() + receiveReadOffset_,
                                        receiveBuffer.size() - receiveReadOffset_, true));
        }
        tryParsePackets();
        if (debugLog && connected && bufferedBytes() > 0) {
            LOG_DEBUG("World socket parse left ", bufferedBytes(),
                     " bytes buffered (awaiting complete packet)");
        }
    }

    if (connected && (readOps >= kMaxRecvCallsPerUpdate || bytesReadThisTick >= kMaxRecvBytesPerUpdate)) {
        LOG_DEBUG("World socket recv budget reached (calls=", readOps,
                  ", bytes=", bytesReadThisTick, "), deferring remaining socket drain");
    }

    if (sawClose) {
        dumpRecentPacketHistoryLocked("peer_closed", bufferedBytes());
        LOG_WARNING("World server connection closed by peer (receivedAny=", receivedAny,
                 " buffered=", bufferedBytes(), ")");
        closeSocketNoJoin();
        return;
    }
}

void WorldSocket::tryParsePackets() {
    // World server packets have 4-byte incoming header: size(2) + opcode(2)
    int parsedThisTick = 0;
    size_t parseOffset = receiveReadOffset_;
    size_t localHeaderBytesDecrypted = headerBytesDecrypted;
    std::vector<Packet> parsedPacketsLocal;
    std::vector<Packet>* parsedPackets = &parsedPacketsLocal;
    if (useParseScratchQueue_) {
        parsedPacketsScratch_.clear();
        // Keep a warm queue to reduce steady-state allocations, but avoid
        // retaining pathological capacity after burst/misaligned streams.
        if (parsedPacketsScratch_.capacity() > 1024) {
            std::vector<Packet>().swap(parsedPacketsScratch_);
        } else if (parsedPacketsScratch_.capacity() < 64) {
            parsedPacketsScratch_.reserve(64);
        }
        parsedPackets = &parsedPacketsScratch_;
    } else {
        parsedPacketsLocal.reserve(32);
    }
    const int maxParsedThisTick = parsedPacketsBudgetPerUpdate();
    while ((receiveBuffer.size() - parseOffset) >= 4 && parsedThisTick < maxParsedThisTick) {
        uint8_t rawHeader[4] = {0, 0, 0, 0};
        std::memcpy(rawHeader, receiveBuffer.data() + parseOffset, 4);

        // Decrypt header bytes in-place if encryption is enabled
        // Only decrypt bytes we haven't already decrypted
        if (encryptionEnabled && localHeaderBytesDecrypted < 4) {
            size_t toDecrypt = 4 - localHeaderBytesDecrypted;
            if (useVanillaCrypt) {
                vanillaCrypt.decrypt(receiveBuffer.data() + parseOffset + localHeaderBytesDecrypted, toDecrypt);
            } else {
                decryptCipher.process(receiveBuffer.data() + parseOffset + localHeaderBytesDecrypted, toDecrypt);
            }
            localHeaderBytesDecrypted = 4;
        }

        // Parse header (now decrypted in-place).
        // Size: 2 bytes big-endian. For world packets, this includes opcode bytes.
        uint16_t size = (receiveBuffer[parseOffset + 0] << 8) | receiveBuffer[parseOffset + 1];
        // Opcode: 2 bytes little-endian.
        uint16_t opcode = receiveBuffer[parseOffset + 2] | (receiveBuffer[parseOffset + 3] << 8);
        if (size < 2) {
            LOG_ERROR("World packet framing desync: invalid size=", size,
                      " rawHdr=", std::hex,
                      static_cast<int>(rawHeader[0]), " ",
                      static_cast<int>(rawHeader[1]), " ",
                      static_cast<int>(rawHeader[2]), " ",
                      static_cast<int>(rawHeader[3]), std::dec,
                      " enc=", encryptionEnabled, ". Disconnecting to recover stream.");
            closeSocketNoJoin();
            return;
        }
        constexpr uint16_t kMaxWorldPacketSize = 0x8000;  // 32KB - allows large guild rosters, auction lists
        if (size > kMaxWorldPacketSize) {
            LOG_ERROR("World packet framing desync: oversized packet size=", size,
                      " rawHdr=", std::hex,
                      static_cast<int>(rawHeader[0]), " ",
                      static_cast<int>(rawHeader[1]), " ",
                      static_cast<int>(rawHeader[2]), " ",
                      static_cast<int>(rawHeader[3]), std::dec,
                      " enc=", encryptionEnabled, ". Disconnecting to recover stream.");
            closeSocketNoJoin();
            return;
        }

        const uint16_t payloadLen = size - 2;
        const size_t totalSize = 4 + payloadLen;

        if (headerTracePacketsLeft > 0) {
            LOG_INFO("WS HDR TRACE raw=",
                     std::hex,
                     static_cast<int>(rawHeader[0]), " ",
                     static_cast<int>(rawHeader[1]), " ",
                     static_cast<int>(rawHeader[2]), " ",
                     static_cast<int>(rawHeader[3]),
                     " dec=",
                     static_cast<int>(receiveBuffer[parseOffset + 0]), " ",
                     static_cast<int>(receiveBuffer[parseOffset + 1]), " ",
                     static_cast<int>(receiveBuffer[parseOffset + 2]), " ",
                     static_cast<int>(receiveBuffer[parseOffset + 3]),
                     std::dec,
                     " size=", size,
                     " payload=", payloadLen,
                     " opcode=0x", std::hex, opcode, std::dec,
                     " buffered=", (receiveBuffer.size() - parseOffset));
            --headerTracePacketsLeft;
        }
        if (isLoginPipelineSmsg(opcode)) {
            LOG_INFO("WS RX LOGIN opcode=0x", std::hex, opcode, std::dec,
                     " size=", size, " payload=", payloadLen,
                     " buffered=", (receiveBuffer.size() - parseOffset),
                     " enc=", encryptionEnabled ? "yes" : "no");
        }
        recordRecentPacket(false, opcode, payloadLen);
        const auto traceNow = std::chrono::steady_clock::now();
        if (packetTraceUntil_ > traceNow) {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                traceNow - packetTraceStart_).count();
            LOG_DEBUG("WS TRACE RX +", elapsedMs, "ms opcode=0x",
                      std::hex, opcode, std::dec,
                      " logical=", opcodeNameForTrace(opcode),
                      " payload=", payloadLen,
                      " reason='", packetTraceReason_, "'");
        }

        if ((receiveBuffer.size() - parseOffset) < totalSize) {
            // Not enough data yet - header stays decrypted in buffer
            break;
        }

        // Extract payload (skip header). Guard allocation failures so malformed
        // streams cannot unwind into application-level OOM crashes.
        try {
            std::vector<uint8_t> packetData(payloadLen);
            if (payloadLen > 0) {
                std::memcpy(packetData.data(), receiveBuffer.data() + parseOffset + 4, payloadLen);
            }
            // Queue packet; callbacks run after buffer state is finalized.
            parsedPackets->emplace_back(opcode, std::move(packetData));
        } catch (const std::bad_alloc& e) {
            LOG_ERROR("OOM while queuing world packet opcode=0x", std::hex, opcode, std::dec,
                      " payload=", payloadLen, " buffered=", receiveBuffer.size(),
                      " parseOffset=", parseOffset, " what=", e.what(),
                      ". Disconnecting to recover.");
            closeSocketNoJoin();
            return;
        }
        parseOffset += totalSize;
        localHeaderBytesDecrypted = 0;
        ++parsedThisTick;
    }

    if (parseOffset > receiveReadOffset_) {
        receiveReadOffset_ = parseOffset;
        // Compact lazily to avoid front-erase memmove every update.
        if (receiveReadOffset_ >= receiveBuffer.size()) {
            receiveBuffer.clear();
            receiveReadOffset_ = 0;
        } else if (receiveReadOffset_ >= 64 * 1024 || receiveReadOffset_ * 2 >= receiveBuffer.size()) {
            const size_t remaining = receiveBuffer.size() - receiveReadOffset_;
            std::memmove(receiveBuffer.data(), receiveBuffer.data() + receiveReadOffset_, remaining);
            receiveBuffer.resize(remaining);
            receiveReadOffset_ = 0;
        }
    }
    headerBytesDecrypted = localHeaderBytesDecrypted;

    // Queue parsed packets for main-thread dispatch.
    if (!parsedPackets->empty()) {
        std::lock_guard<std::mutex> callbackLock(callbackMutex_);
        for (auto& packet : *parsedPackets) {
            pendingPacketCallbacks_.push_back(std::move(packet));
        }
        if (pendingPacketCallbacks_.size() > kMaxQueuedPacketCallbacks) {
            LOG_ERROR("World socket callback queue overflow (", pendingPacketCallbacks_.size(),
                      " packets). Disconnecting to recover.");
            pendingPacketCallbacks_.clear();
            closeSocketNoJoin();
            return;
        }
    }

    const size_t buffered = (receiveBuffer.size() >= receiveReadOffset_)
        ? (receiveBuffer.size() - receiveReadOffset_)
        : 0;
    if (parsedThisTick >= maxParsedThisTick && buffered >= 4) {
        LOG_DEBUG("World socket parse budget reached (", parsedThisTick,
                 " packets); deferring remaining buffered data=", buffered, " bytes");
    }
}

void WorldSocket::dispatchQueuedPackets() {
    std::deque<Packet> localPackets;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (!packetCallback || pendingPacketCallbacks_.empty()) {
            return;
        }
        const int maxCallbacksThisTick = packetCallbacksBudgetPerUpdate();
        for (int i = 0; i < maxCallbacksThisTick && !pendingPacketCallbacks_.empty(); ++i) {
            localPackets.push_back(std::move(pendingPacketCallbacks_.front()));
            pendingPacketCallbacks_.pop_front();
        }
        if (!pendingPacketCallbacks_.empty()) {
            LOG_DEBUG("World socket callback budget reached (", localPackets.size(),
                      " callbacks); deferring ", pendingPacketCallbacks_.size(),
                      " queued packet callbacks");
        }
    }

    while (!localPackets.empty()) {
        packetCallback(localPackets.front());
        localPackets.pop_front();
    }
}

void WorldSocket::initEncryption(const std::vector<uint8_t>& sessionKey, uint32_t build) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    if (sessionKey.size() != 40) {
        LOG_ERROR("Invalid session key size: ", sessionKey.size(), " (expected 40)");
        return;
    }

    const bool useRawVanillaCrypt = (build <= 5875);
    const bool useCmangosTbcCrypt = (build > 5875 && build <= 8606);
    useVanillaCrypt = useRawVanillaCrypt || useCmangosTbcCrypt;

    LOG_INFO(">>> ENABLING ENCRYPTION (",
             useRawVanillaCrypt ? "vanilla XOR" : (useCmangosTbcCrypt ? "CMaNGOS TBC HMAC-XOR" : "WotLK RC4"),
             ") build=", build, " <<<");

    if (useRawVanillaCrypt) {
        vanillaCrypt.init(sessionKey);
    } else if (useCmangosTbcCrypt) {
        // CMaNGOS TBC AuthCrypt::Init:
        //   key = HMAC_SHA1({38 A7 83 15 F8 92 25 30 71 98 67 B1 8C 04 E2 AA}, K)
        // Then the vanilla XOR+addition header cipher is used with that 20-byte key.
        static constexpr uint8_t kCmangosTbcSeed[] = {
            0x38, 0xA7, 0x83, 0x15, 0xF8, 0x92, 0x25, 0x30,
            0x71, 0x98, 0x67, 0xB1, 0x8C, 0x04, 0xE2, 0xAA
        };
        std::vector<uint8_t> seed(kCmangosTbcSeed, kCmangosTbcSeed + sizeof(kCmangosTbcSeed));
        std::vector<uint8_t> headerKey = auth::Crypto::hmacSHA1(seed, sessionKey);
        vanillaCrypt.init(headerKey);
    } else {
        // WotLK: HMAC-SHA1(hardcoded seed, sessionKey) -> RC4 key
        std::vector<uint8_t> encryptKey(ENCRYPT_KEY, ENCRYPT_KEY + 16);
        std::vector<uint8_t> decryptKey(DECRYPT_KEY, DECRYPT_KEY + 16);

        std::vector<uint8_t> encryptHash = auth::Crypto::hmacSHA1(encryptKey, sessionKey);
        std::vector<uint8_t> decryptHash = auth::Crypto::hmacSHA1(decryptKey, sessionKey);

        // WoW WotLK world-header stream cipher is protocol-defined RC4.
        // Replacing it would break interoperability with target servers.
        encryptCipher.init(encryptHash); // codeql[cpp/weak-cryptographic-algorithm]
        decryptCipher.init(decryptHash); // codeql[cpp/weak-cryptographic-algorithm]

        // Drop first 1024 bytes of keystream (WoW WotLK protocol requirement)
        encryptCipher.drop(1024);
        decryptCipher.drop(1024);
    }

    encryptionEnabled = true;
    headerTracePacketsLeft = 24;
    LOG_INFO("World server encryption initialized successfully");
}

} // namespace network
} // namespace wowee
