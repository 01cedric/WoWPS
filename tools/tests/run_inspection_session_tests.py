#!/usr/bin/env python3
"""Execute real CVar disk I/O and settings debug branches across a restart."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
lua = (root / 'src/addons/lua_system_api.cpp').read_text()
settings = (root / 'src/ui/game_screen_minimap.cpp').read_text()

def block(source, marker):
    start = source.index(marker)
    begin = source.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

functions = '\n'.join(block(lua, marker) for marker in (
    'static void loadStoredCVars() {',
    'static void saveStoredCVars() {',
    'std::string storedCVarValue('))
load_branch = block(settings, 'else if (key == "volumetric_debug")')[5:]
save_line = re.search(r'    out << "volumetric_debug[^\n]+', settings).group()
member = re.search(r'    int pendingVolumetricDebug = [^\n]+',
    (root / 'include/ui/settings_panel.hpp').read_text()).group()
code = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
namespace ui { constexpr int kInterfaceLayoutVersion = 99; }
static std::string testPath;
static std::string cvarStorePath() { return testPath; }
static std::unordered_map<std::string, std::string>& cvarStore() {
    static std::unordered_map<std::string, std::string> store; return store;
}
static void toLowerInPlace(std::string& text) {
    for (char& c : text) c = std::tolower(static_cast<unsigned char>(c));
}
static void saveStoredCVars();
''' + functions + '\nstruct Panel {\n' + member + r'''
};
static void readSettingsDebug(Panel& settingsPanel_, const std::string& val) {
    const std::string key = "volumetric_debug";
    (void)settingsPanel_; (void)val;
''' + load_branch + r'''
}
static std::string writeSettingsDebug(const Panel& settingsPanel_) {
    (void)settingsPanel_;
    std::ostringstream out;
''' + save_line + r'''
    return out.str();
}
int main(int argc, char** argv) {
    assert(argc == 2); testPath = argv[1];
    int cases = 0;
    for (const char* legacy : {"0", "1", "2", "3", "4", "-99", "999", "bad", ""}) {
        // Simulate first launch of the new build with legacy files in place.
        cvarStore().clear();
        { std::ofstream out(testPath);
          out << "ExtVolumetricDebug=" << legacy << "\nextvolumetricquality=2\n"
              << "wowps_interface_layout_version=99\n"; }
        assert(storedCVarValue("extVolumetricDebug", "4") == "0"); // pre-Lua renderer
        assert(storedCVarValue("extVolumetricQuality", "1") == "2");
        loadStoredCVars();
        assert(!cvarStore().count("extvolumetricdebug"));
        assert(storedCVarValue("EXTVOLUMETRICDEBUG", "4") == "0");
        Panel panel;
        readSettingsDebug(panel, legacy);
        assert(panel.pendingVolumetricDebug == 0);

        // Explicit choices stay live across saves and settings/Lua reloads.
        for (int live = 0; live <= 4; ++live) {
            panel.pendingVolumetricDebug = live;
            cvarStore()["extvolumetricdebug"] = std::to_string(live);
            saveStoredCVars();
            assert(storedCVarValue("extVolumetricDebug", "0") == std::to_string(live));
            loadStoredCVars();
            readSettingsDebug(panel, legacy);
            assert(panel.pendingVolumetricDebug == live);
            assert(storedCVarValue("extVolumetricDebug", "0") == std::to_string(live));
            assert(writeSettingsDebug(panel) == "volumetric_debug=0\n");
            std::ifstream in(testPath);
            std::string disk((std::istreambuf_iterator<char>(in)), {});
            assert(disk.find("extvolumetricdebug") == std::string::npos);
            assert(disk.find("extvolumetricquality=2") != std::string::npos);
            ++cases;
        }
        // Restart drops only diagnostic state, retaining normal quality settings.
        cvarStore().clear();
        loadStoredCVars();
        assert(storedCVarValue("extVolumetricDebug", "4") == "0");
        assert(storedCVarValue("extVolumetricQuality", "1") == "2");
    }
    std::printf("PASS %d legacy/live combinations: pre-Lua startup, CVar load/save, settings reload, restart recovery; quality retained\n", cases);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-inspection-') as temp:
    out = Path(temp)
    (out / 'test.cpp').write_text(code)
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror']
    if os.getenv('SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run([os.getenv('CXX', 'c++'), *flags, str(out / 'test.cpp'), '-o', str(out / 'test')], check=True)
    subprocess.run([str(out / 'test'), str(out / 'cvars.cfg')], check=True,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
