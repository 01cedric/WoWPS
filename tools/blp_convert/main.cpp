#include "pipeline/blp_loader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using wowee::pipeline::BLPImage;
using wowee::pipeline::BLPLoader;

static std::vector<uint8_t> readFileData(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    auto sz = f.tellg();
    if (sz <= 0) return {};
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), sz);
    return data;
}

static bool convertBlpToPng(const std::string& blpPath) {
    auto data = readFileData(blpPath);
    if (data.empty()) {
        std::cerr << "Failed to read: " << blpPath << "\n";
        return false;
    }

    BLPImage img = BLPLoader::load(data);
    if (!img.isValid()) {
        std::cerr << "Failed to decode BLP: " << blpPath << "\n";
        return false;
    }

    // Output path: same name with .png
    fs::path out = fs::path(blpPath);
    out.replace_extension(".png");

    if (!stbi_write_png(out.string().c_str(), img.width, img.height, 4,
                        img.data.data(), img.width * 4)) {
        std::cerr << "Failed to write PNG: " << out << "\n";
        return false;
    }

    std::cout << blpPath << " -> " << out.string() << " (" << img.width << "x" << img.height << ")\n";
    return true;
}

static bool convertPngToBlp(const std::string& pngPath) {
    // Load PNG
    int w, h, channels;
    unsigned char* pixels = stbi_load(pngPath.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        std::cerr << "Failed to read PNG: " << pngPath << "\n";
        return false;
    }

    // Write a simple uncompressed BLP2 (ARGB8888)
    fs::path out = fs::path(pngPath);
    out.replace_extension(".blp");

    std::ofstream f(out.string(), std::ios::binary);
    if (!f.is_open()) {
        stbi_image_free(pixels);
        std::cerr << "Failed to open output: " << out << "\n";
        return false;
    }

    // BLP2 header
    f.write("BLP2", 4);
    uint32_t version = 1;
    f.write(reinterpret_cast<const char*>(&version), 4);

    uint8_t compression = 3;   // uncompressed
    uint8_t alphaDepth = 8;
    uint8_t alphaEncoding = 0;
    uint8_t hasMips = 0;
    f.write(reinterpret_cast<const char*>(&compression), 1);
    f.write(reinterpret_cast<const char*>(&alphaDepth), 1);
    f.write(reinterpret_cast<const char*>(&alphaEncoding), 1);
    f.write(reinterpret_cast<const char*>(&hasMips), 1);

    uint32_t width = static_cast<uint32_t>(w);
    uint32_t height = static_cast<uint32_t>(h);
    f.write(reinterpret_cast<const char*>(&width), 4);
    f.write(reinterpret_cast<const char*>(&height), 4);

    // Mip offsets (16 entries) - only first used
    uint32_t dataSize = width * height * 4;
    uint32_t headerSize = 4 + 4 + 4 + 4 + 16 * 4 + 16 * 4 + 256 * 4;  // magic+version+dims+mips+palette
    uint32_t mipOffsets[16] = {};
    uint32_t mipSizes[16] = {};
    mipOffsets[0] = headerSize;
    mipSizes[0] = dataSize;
    f.write(reinterpret_cast<const char*>(mipOffsets), sizeof(mipOffsets));
    f.write(reinterpret_cast<const char*>(mipSizes), sizeof(mipSizes));

    // Empty palette (256 entries)
    uint32_t palette[256] = {};
    f.write(reinterpret_cast<const char*>(palette), sizeof(palette));

    // Convert RGBA → BGRA for BLP
    std::vector<uint8_t> bgra(dataSize);
    for (int i = 0; i < w * h; ++i) {
        bgra[i * 4 + 0] = pixels[i * 4 + 2]; // B
        bgra[i * 4 + 1] = pixels[i * 4 + 1]; // G
        bgra[i * 4 + 2] = pixels[i * 4 + 0]; // R
        bgra[i * 4 + 3] = pixels[i * 4 + 3]; // A
    }
    f.write(reinterpret_cast<const char*>(bgra.data()), dataSize);

    stbi_image_free(pixels);

    std::cout << pngPath << " -> " << out.string() << " (" << w << "x" << h << ")\n";
    return true;
}

static void printUsage(const char* prog) {
    std::cout << "Usage:\n"
              << "  " << prog << " --to-png <file.blp>       Convert BLP to PNG\n"
              << "  " << prog << " --to-blp <file.png>       Convert PNG to BLP\n"
              << "  " << prog << " --batch <directory> [--recursive]  Batch convert BLP->PNG\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "--to-png") {
        return convertBlpToPng(argv[2]) ? 0 : 1;
    }

    if (mode == "--to-blp") {
        return convertPngToBlp(argv[2]) ? 0 : 1;
    }

    if (mode == "--batch") {
        std::string dir = argv[2];
        bool recursive = (argc > 3 && std::strcmp(argv[3], "--recursive") == 0);

        int count = 0, failed = 0;
        auto processEntry = [&](const fs::directory_entry& entry) {
            if (!entry.is_regular_file()) return;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".blp") {
                if (convertBlpToPng(entry.path().string())) {
                    count++;
                } else {
                    failed++;
                }
            }
        };

        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                processEntry(entry);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(dir)) {
                processEntry(entry);
            }
        }

        std::cout << "Batch complete: " << count << " converted, " << failed << " failed\n";
        return failed > 0 ? 1 : 0;
    }

    std::cerr << "Unknown mode: " << mode << "\n";
    printUsage(argv[0]);
    return 1;
}
