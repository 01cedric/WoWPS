#include "auth/auth_handler.hpp"
#include "auth/crypto.hpp"
#include "core/logger.hpp"
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <iostream>

// Deterministic entropy is linked into this test executable ONLY. Small A
// deliberately exercises wire-padding errors; production uses RAND_bytes.
extern "C" int __real_RAND_bytes(unsigned char*, int);
extern "C" int __wrap_RAND_bytes(unsigned char* output, int size) {
    if (!std::getenv("WOWPS_TEST_SMALL_A")) return __real_RAND_bytes(output, size);
    std::memset(output, 0, size);
    if (size) output[0] = 1;
    return 1;
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    wowee::core::Logger::getInstance().setLogLevel(wowee::core::LogLevel::FATAL);
    wowee::auth::AuthHandler auth;
    const std::string mode = argv[2];
    std::string failure;
    auth.setOnFailure([&](const std::string& reason) { failure = reason; });
    if (!auth.connect("127.0.0.1", static_cast<uint16_t>(std::stoi(argv[1])))) return 3;
    if (mode == "hash") auth.authenticateWithHash("testaccount", wowee::auth::Crypto::sha1("TESTACCOUNT:TESTPASSWORD"));
    else auth.authenticate("testaccount", "testpassword");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        auth.update(0.001f);
        if (auth.getState() == wowee::auth::AuthState::FAILED) {
            if (mode == "reject" && !auth.lastFailureWasProtocol()) return 0;
            if (mode == "bad_m2" && failure.find("verification failed") != std::string::npos) return 0;
            std::cerr << failure << '\n'; return 4;
        }
        if (auth.getState() == wowee::auth::AuthState::AUTHENTICATED) auth.requestRealmList();
        if (auth.getState() == wowee::auth::AuthState::REALM_LIST_RECEIVED) {
            const auto& realms = auth.getRealms();
            if (mode == "reject" || mode == "bad_m2") return 5;
            return realms.size() == 1 && realms[0].name == "Test Realm" &&
                   realms[0].address == "127.0.0.1:8085" && auth.getSessionKey().size() == 40 ? 0 : 6;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cerr << "Auth test timed out\n";
    return 7;
}
