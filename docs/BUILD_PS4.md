# Build WoWPS 2.00 for PS4

## Requirements

Use a Linux host with CMake, Ninja, a compatible clang/clang++ and ld.lld, Python 3, and the OpenOrbis PS4 toolchain. The toolchain must include its headers, libraries, startup objects, linker script and Linux packaging tools. The vendored PS4 renderer archives and dependency sources in this repository are required. Original MPQ/DBC data is not needed to compile the release.

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
./tools/ps4/build_ps4_pkg.sh --jobs 3
```

Run `./tools/ps4/build_ps4_pkg.sh --help` for supported options. The supplied shader binaries are reused only after `tools/ps4/verify_shaders.py` checks the manifest. Editing listed GLSL requires regeneration with the supported shader tools; do not bypass that guard.

## Explicit configure/build/package commands

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
cmake -S . -B build-ps4 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/ps4.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ps4 --parallel 3
./tools/ps4/package.sh "$PWD/build-ps4"
```

The package is produced under `build-ps4/pkg/` with content ID `IV0000-WOWE00001_00-WOWEEPS4CLIENT00`. `BUILD_VERSION` supplies `02.00` to both the generated client identity and SFO package version. CMake's project version is `2.0.0`. Do not change save/network versions to match the display version.

The packaging script creates the fself executable, stages runtime data/shaders/addons, generates `param.sfo` and builds the homebrew PKG. It includes the existing compatibility handling for packaging hosts with OpenSSL 3. No original client MPQs/DBCs should be staged.

## Reproducible checks

```bash
python3 tools/ps4/verify_shaders.py
python3 tools/tests/run_release_checks.py build-test-results
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
export LD_LIBRARY_PATH="/tmp/wowee-openssl11-shim${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$OO_PS4_TOOLCHAIN/bin/linux/PkgTool.Core" pkg_validate --verbose \
  build-ps4/pkg/IV0000-WOWE00001_00-WOWEEPS4CLIENT00.pkg
```

The host suite runner reports missing inputs as skipped rather than successful. Additional source-data-dependent suites require the matching extracted inputs. Keep host results separate from hardware tests. Consult [BUILD_VALIDATION.md](BUILD_VALIDATION.md) for the checks actually performed on the delivered release.

## Installation

Back up `/data/wow_ps/saves/local_realm/` before updating. Install the PKG and provide your own client `Data` tree under `/data/wow_ps/Data/`. A source build and valid package do not certify installation, visual correctness or sustained performance on the console.
