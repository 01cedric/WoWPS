#pragma once
#include <cstdint>
namespace wowee::game {
constexpr uint8_t localWardIdProfile(uint32_t id) {
    switch(id) {
        case 543:case 8457:case 8458:case 10223:case 10225:case 27128:case 43010:return 1;
        case 6143:case 8461:case 8462:case 10177:case 28609:case 32796:case 43012:return 2;
        default:return 0;
    }
}
}
