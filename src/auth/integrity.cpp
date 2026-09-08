#include "auth/integrity.hpp"
#include "auth/crypto.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace wowee {
namespace auth {

static bool readWholeFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        err = "missing: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0) size = 0;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(out.data()), size);
        if (!f) {
            err = "read failed: " + path;
            return false;
        }
    }
    return true;
}

bool computeIntegrityHashWin32WithExe(const std::array<uint8_t, 16>& checksumSalt,
                                      const std::vector<uint8_t>& clientPublicKeyA,
                                      const std::string& miscDir,
                                      const std::string& exeName,
                                      uint16_t clientBuild,
                                      std::array<uint8_t, 20>& outHash,
                                      std::string& outError) {
    // Classic 1.12.x (build <=6005) ships fmod.dll, ijl15.dll, dbghelp.dll,
    // unicows.dll alongside WoW.exe.  TBC/WotLK clients do not; hashing
    // only the .exe is sufficient (servers rarely validate the CRC for those).
    //
    // Turtle WoW ships a custom loader DLL. Some Turtle auth servers appear to validate integrity against
    // that distribution rather than a stock 1.12.1 client, so when using Turtle's executable we include
    // Turtle-specific DLLs as well.
    const bool isTurtleExe = (exeName == "TurtleWoW.exe");
    const bool isClassicBuild = (clientBuild <= 6005 || isTurtleExe);
    // Some macOS client layouts use FMOD dylib naming instead of fmod.dll.
    // We accept the first matching filename in each alias group.
    std::vector<std::vector<std::string>> fileGroups = {
        { exeName },
    };
    if (isClassicBuild) {
        fileGroups.push_back({ "fmod.dll", "fmod.dylib", "libfmod.dylib", "fmodex.dll", "fmodex.dylib", "libfmod.so" });
        fileGroups.push_back({ "ijl15.dll" });
        fileGroups.push_back({ "dbghelp.dll" });
        fileGroups.push_back({ "unicows.dll" });
    }
    if (isTurtleExe) {
        fileGroups.push_back({ "twloader.dll" });
        fileGroups.push_back({ "twdiscord.dll" });
    }

    std::vector<uint8_t> allFiles;
    for (const auto& group : fileGroups) {
        bool foundInGroup = false;
        std::string groupErr;

        for (const auto& nameStr : group) {
            std::vector<uint8_t> bytes;
            std::string path = miscDir;
            if (!path.empty() && path.back() != '/') path += '/';
            path += nameStr;

            std::string err;
            if (!readWholeFile(path, bytes, err)) {
                if (groupErr.empty()) groupErr = err;
                continue;
            }

            allFiles.insert(allFiles.end(), bytes.begin(), bytes.end());
            foundInGroup = true;
            break;
        }

        if (!foundInGroup) {
            outError = groupErr.empty() ? "missing required integrity file group" : groupErr;
            return false;
        }
    }

    // HMAC_SHA1(checksumSalt, allFiles)
    std::vector<uint8_t> key(checksumSalt.begin(), checksumSalt.end());
    const std::vector<uint8_t> checksum = Crypto::hmacSHA1(key, allFiles); // 20 bytes

    // SHA1(A || checksum)
    std::vector<uint8_t> shaIn;
    shaIn.reserve(clientPublicKeyA.size() + checksum.size());
    shaIn.insert(shaIn.end(), clientPublicKeyA.begin(), clientPublicKeyA.end());
    shaIn.insert(shaIn.end(), checksum.begin(), checksum.end());
    const std::vector<uint8_t> finalHash = Crypto::sha1(shaIn);

    if (finalHash.size() != outHash.size()) {
        outError = "unexpected sha1 size";
        return false;
    }
    std::copy(finalHash.begin(), finalHash.end(), outHash.begin());
    return true;
}

bool computeIntegrityHashWin32(const std::array<uint8_t, 16>& checksumSalt,
                               const std::vector<uint8_t>& clientPublicKeyA,
                               const std::string& miscDir,
                               std::array<uint8_t, 20>& outHash,
                               std::string& outError) {
    return computeIntegrityHashWin32WithExe(checksumSalt, clientPublicKeyA, miscDir, "WoW.exe", 5875, outHash, outError);
}

} // namespace auth
} // namespace wowee
