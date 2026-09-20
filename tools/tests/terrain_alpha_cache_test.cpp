#include "rendering/terrain_alpha_cache.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using wowee::rendering::TerrainAlphaCache;
struct Texture { static inline int destroyed = 0; ~Texture() { ++destroyed; } };
int main() {
    using Cache = TerrainAlphaCache<Texture>;
    Cache cache;
    Texture a, b;
    assert(cache.allocatedBytes() == 0);
    std::vector<uint8_t> bytes(4096);
    for (size_t i=0; i<bytes.size(); ++i) bytes[i] = uint8_t(i*17);
    const auto mask=Cache::normalize(bytes);
    assert(!Cache::opaque(mask));
    assert(!cache.find(mask));
    cache.remember(mask, &a);
    assert(cache.find(mask)==&a);
    assert(cache.allocatedBytes() <= 512*1024 + 128*sizeof(void*));
    bytes.push_back(123); // Upload ignores bytes beyond 64x64, as before.
    assert(cache.find(Cache::normalize(bytes))==&a);
    auto changed=mask; changed[2050]^=1;
    assert(!cache.find(changed));
    auto empty=Cache::normalize({});
    assert(Cache::opaque(empty));
    const std::array<uint8_t, 2> shortInput{0, 128};
    auto padded=Cache::normalize(shortInput);
    assert(padded[0]==0 && padded[1]==128 && padded[2]==255 && padded[4095]==255);
    // Force a slot collision: equality must prevent reuse of a wrong mask.
    TerrainAlphaCache<Texture, 1> colliding;
    colliding.remember(mask, &a);
    assert(!colliding.find(changed));
    colliding.remember(changed, &b);
    assert(!colliding.find(mask));
    assert(colliding.find(changed)==&b && colliding.replacements==1);
    colliding.clear(); cache.clear();
    assert(!cache.find(mask) && !colliding.find(changed));
    assert(cache.allocatedBytes()==0 && Texture::destroyed==0);
    cache.remember(mask, &b);
    assert(cache.find(mask)==&b); // New scene owners can be inserted after clear.
    std::puts("PASS alpha equality, forced hash-slot collision, padding/truncation, opaque, bounded allocation, clear and non-owning lifetime");
}
