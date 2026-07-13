@echo off
setlocal
rem One-time setup for building the Cranked libretro core / desktop app on Windows with MSVC.
rem Run from the repository root. Requires: git, Python 3, Visual Studio 2022+ (C++ workload).
rem See CLAUDE.md for the full build documentation.

cd /d "%~dp0.."

echo === Fetching required submodules (checkout mode; 'update = merge' in .gitmodules breaks shallow fetches) ===
git submodule update --init --checkout --force --depth 1 ^
    core/libs/zlib core/libs/nlohmann_json core/libs/magic_enum core/libs/gif-h ^
    core/libs/cpp-unicodelib core/libs/libzippp core/libs/lua54 core/libs/ffi ^
    core/libs/tracy core/libs/unicorn core/libs/asio core/libs/libzip core/libs/capstone || goto :error
rem NOTE: core/libs/dynarmic is intentionally skipped - unused and its upstream URL is dead.

echo === Fetching desktop UI submodules ===
git submodule update --init --checkout --force --depth 1 ^
    desktop/libs/imgui desktop/libs/imgui_club desktop/libs/ImGuiFileDialog desktop/libs/cxxopts || goto :error

echo === Cloning standalone asio (replaces boost::asio; headers at include/) ===
if not exist core\libs\asio_standalone (
    git clone --depth 1 https://github.com/chriskohlhoff/asio core\libs\asio_standalone || goto :error
)

echo === Applying Windows patches to submodules ===
git -C core/libs/ffi apply --check ..\..\..\patches\ffi-msvc-windows.patch 2>nul && git -C core/libs/ffi apply ..\..\..\patches\ffi-msvc-windows.patch
git -C core/libs/unicorn apply --check ..\..\..\patches\unicorn-crc32-rename.patch 2>nul && git -C core/libs/unicorn apply ..\..\..\patches\unicorn-crc32-rename.patch
git -C core/libs/lua54 apply --check ..\..\..\patches\lua54-native-exception-logging.patch 2>nul && git -C core/libs/lua54 apply ..\..\..\patches\lua54-native-exception-logging.patch

echo === Done ===
echo Next steps (from a VS x64 developer prompt, or use vcvars64.bat):
echo   1. Stage zlib:  cmake -S core/libs/zlib -B ../deps/build-zlib -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../deps/zlib ^&^& cmake --build ../deps/build-zlib --target install
echo   2. Configure:   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSKIP_TESTING=ON -DSKIP_JAVA=ON -DSKIP_DESKTOP=ON -DUSE_CAPSTONE=OFF -DZLIB_ROOT=../deps/zlib -DZLIB_USE_STATIC_LIBS=ON -DUNICORN_ARCH=arm -DCMAKE_POLICY_VERSION_MINIMUM=3.5
echo   3. Build:       cmake --build build --target cranked_libretro
echo For the desktop app add -DSKIP_DESKTOP=OFF -DSDL2_DIR=^<path-to-SDL2-devel-VC^>\cmake
exit /b 0

:error
echo Setup failed.
exit /b 1
