#pragma once

#include <cstdint>

// Warden anti-cheat protocol constants for WoW 3.3.5a (12340).
// Server-to-Client (SMSG) and Client-to-Server (CMSG) sub-opcodes,
// memory region boundaries, check sizes, and result codes.

namespace wowee {
namespace game {

// ---------------------------------------------------------------------------
// Warden sub-opcodes (inside SMSG_WARDEN_DATA / CMSG_WARDEN_DATA)
// ---------------------------------------------------------------------------
// Server → Client
constexpr uint8_t WARDEN_SMSG_MODULE_USE           = 0x00;
constexpr uint8_t WARDEN_SMSG_MODULE_CACHE          = 0x01;
constexpr uint8_t WARDEN_SMSG_CHEAT_CHECKS_REQUEST  = 0x02;
constexpr uint8_t WARDEN_SMSG_MODULE_INITIALIZE     = 0x03;
constexpr uint8_t WARDEN_SMSG_HASH_REQUEST          = 0x05;

// Client → Server
constexpr uint8_t WARDEN_CMSG_MODULE_MISSING        = 0x00;
constexpr uint8_t WARDEN_CMSG_MODULE_OK             = 0x01;
constexpr uint8_t WARDEN_CMSG_CHEAT_CHECKS_RESULT   = 0x02;
constexpr uint8_t WARDEN_CMSG_HASH_RESULT           = 0x04;

// ---------------------------------------------------------------------------
// PE section boundaries (Wow.exe 3.3.5a 12340, default base 0x400000)
// ---------------------------------------------------------------------------
constexpr uint32_t PE_TEXT_SECTION_BASE     = 0x400000;
constexpr uint32_t PE_TEXT_SECTION_END      = 0x800000;
constexpr uint32_t PE_RDATA_SECTION_BASE    = 0x7FF000;
constexpr uint32_t PE_DATA_RAW_SECTION_BASE = 0x827000;
constexpr uint32_t PE_BSS_SECTION_BASE      = 0x883000;
constexpr uint32_t PE_BSS_SECTION_END       = 0xD06000;

// Windows KUSER_SHARED_DATA page (read-only, always mapped)
constexpr uint32_t KUSER_SHARED_DATA_BASE   = 0x7FFE0000;
constexpr uint32_t KUSER_SHARED_DATA_END    = 0x7FFF0000;

// ---------------------------------------------------------------------------
// Well-known memory addresses
// ---------------------------------------------------------------------------
constexpr uint32_t WARDEN_TICKCOUNT_ADDRESS     = 0x00CF0BC8;
constexpr uint32_t WARDEN_WIN_VERSION_ADDRESS   = 0x7FFE026C;

// ---------------------------------------------------------------------------
// Check sizes (bytes)
// ---------------------------------------------------------------------------
constexpr uint32_t WARDEN_CR_HEADER_SIZE      = 17;
constexpr uint32_t WARDEN_CR_ENTRY_SIZE       = 68;
constexpr uint32_t WARDEN_PAGE_CHECK_SIZE     = 29;
constexpr uint32_t WARDEN_PAGE_A_SHORT_SIZE   = 24;
constexpr uint32_t WARDEN_KNOWN_CODE_SCAN_OFFSET = 13856;

// ---------------------------------------------------------------------------
// Memory-check result codes
// ---------------------------------------------------------------------------
constexpr uint8_t WARDEN_MEM_CHECK_SUCCESS   = 0x00;
constexpr uint8_t WARDEN_MEM_CHECK_UNMAPPED  = 0xE9;
constexpr uint8_t WARDEN_PAGE_CHECK_FOUND    = 0x4A;

} // namespace game
} // namespace wowee
