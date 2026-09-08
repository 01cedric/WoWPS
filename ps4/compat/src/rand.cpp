/*
 * WoWee PS4 crypto shim - RAND_bytes.
 *
 * PS4:   libSceRandom's sceRandomGetRandomNumber, resolved at runtime with
 *        sceKernelLoadStartModule + sceKernelDlsym so nothing extra has to be
 *        linked; then /dev/urandom through the libc open/read that OpenOrbis
 *        routes to libkernel.
 * Linux: getrandom(2), then /dev/urandom.
 * If every source fails, a SHA-256 based generator seeded from clocks and
 * addresses fills the buffer, a warning is logged once and 0 is returned.
 */
#include "shim_internal.hpp"
#include "openssl/rand.h"
#include "openssl/sha.h"

#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

#if defined(__ORBIS__) || defined(__PS4__) || defined(WOWEE_PS4)
#  define WOWEE_SHIM_PS4 1
#elif defined(__FreeBSD__) && defined(__has_include)
#  if __has_include(<orbis/libkernel.h>)
#    define WOWEE_SHIM_PS4 1
#  endif
#endif

#if defined(WOWEE_SHIM_PS4)
#  include <orbis/libkernel.h>
#elif defined(__linux__)
#  include <sys/random.h>
#  include <errno.h>
#endif

WOWEE_SHIM_BEGIN

namespace {

#if defined(WOWEE_SHIM_PS4)
typedef int (*SceRandomFn)(void *buf, size_t size);

SceRandomFn resolveSceRandom() {
    static SceRandomFn fn = nullptr;
    static bool tried = false;
    if (tried) return fn;
    tried = true;
    int32_t handle = static_cast<int32_t>(sceKernelLoadStartModule("libSceRandom.sprx", 0, nullptr, 0, nullptr, nullptr));
    if (handle <= 0) return nullptr;
    void *sym = nullptr;
    if (sceKernelDlsym(handle, "sceRandomGetRandomNumber", &sym) != 0 || !sym) return nullptr;
    fn = reinterpret_cast<SceRandomFn>(sym);
    return fn;
}

bool fillFromSceRandom(unsigned char *buf, size_t n) {
    SceRandomFn fn = resolveSceRandom();
    if (!fn) return false;
    while (n) {
        size_t chunk = n > 64 ? 64 : n; /* SCE_RANDOM_MAX_SIZE */
        if (fn(buf, chunk) != 0) return false;
        buf += chunk;
        n -= chunk;
    }
    return true;
}
#endif

bool fillFromUrandom(unsigned char *buf, size_t n) {
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    bool ok = true;
    while (n) {
        ssize_t r = ::read(fd, buf, n);
        if (r <= 0) {
            ok = false;
            break;
        }
        buf += static_cast<size_t>(r);
        n -= static_cast<size_t>(r);
    }
    ::close(fd);
    return ok;
}

#if defined(__linux__) && !defined(WOWEE_SHIM_PS4)
bool fillFromGetrandom(unsigned char *buf, size_t n) {
    while (n) {
        ssize_t r = ::getrandom(buf, n, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        buf += static_cast<size_t>(r);
        n -= static_cast<size_t>(r);
    }
    return true;
}
#endif

/* Last resort: hash-chain over whatever varies between runs. Not a CSPRNG. */
void fillFromFallback(unsigned char *buf, size_t n) {
    static bool warned = false;
    static uint64_t counter = 0;
    static unsigned char chain[SHA256_DIGEST_LENGTH];
    if (!warned) {
        warned = true;
        wowee_shim_log("RAND_bytes: no system entropy source available; using weak fallback generator");
    }
    while (n) {
        SHA256_CTX c;
        SHA256_Init(&c);
        SHA256_Update(&c, chain, sizeof chain);
        uint64_t t = static_cast<uint64_t>(std::time(nullptr));
        uint64_t clk = static_cast<uint64_t>(std::clock());
        uintptr_t sp = reinterpret_cast<uintptr_t>(&c);
        uintptr_t fp = reinterpret_cast<uintptr_t>(&fillFromFallback);
        ++counter;
        SHA256_Update(&c, &t, sizeof t);
        SHA256_Update(&c, &clk, sizeof clk);
        SHA256_Update(&c, &sp, sizeof sp);
        SHA256_Update(&c, &fp, sizeof fp);
        SHA256_Update(&c, &counter, sizeof counter);
        SHA256_Final(chain, &c);
        size_t take = n < sizeof chain ? n : sizeof chain;
        std::memcpy(buf, chain, take);
        buf += take;
        n -= take;
    }
}

} // namespace

int RAND_bytes(unsigned char *buf, int num) {
    if (num < 0) return 0;
    if (num == 0) return 1;
    if (!buf) return 0;
    const size_t n = static_cast<size_t>(num);
#if defined(WOWEE_SHIM_PS4)
    if (fillFromSceRandom(buf, n)) return 1;
#elif defined(__linux__)
    if (fillFromGetrandom(buf, n)) return 1;
#endif
    if (fillFromUrandom(buf, n)) return 1;
    fillFromFallback(buf, n);
    return 0;
}

int RAND_priv_bytes(unsigned char *buf, int num) { return RAND_bytes(buf, num); }

int RAND_status(void) {
    unsigned char probe[4];
#if defined(WOWEE_SHIM_PS4)
    if (fillFromSceRandom(probe, sizeof probe)) return 1;
#elif defined(__linux__)
    if (fillFromGetrandom(probe, sizeof probe)) return 1;
#endif
    return fillFromUrandom(probe, sizeof probe) ? 1 : 0;
}

int RAND_poll(void) { return RAND_status(); }

WOWEE_SHIM_END
