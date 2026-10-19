"""Cross-compile pokedaisy for the RG DS (aarch64) with the SimpleOS Zig toolchain."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIMPLE = Path(r"D:\Documenti\Hobbistica\RetroGaming\Firmware\RGDS\simple-os-for-rgds-1")
ZIG = SIMPLE / "build" / "toolchain" / "zig-windows-x86_64-0.14.0" / "zig.exe"
CMAKE = SIMPLE / "build" / "toolchain" / "cmake-3.30.5-windows-x86_64" / "bin" / "cmake.exe"
NINJA = SIMPLE / "build" / "toolchain" / "ninja.exe"
SDL_INC = ROOT / "build-aarch64" / "SDL2-2.32.10" / "include"
SDL_LIB = SIMPLE / "build" / "sd-sdl" / "libSDL2.so"
TARGET = "aarch64-linux-gnu.2.34"
BUILD = ROOT / "build-aarch64"
WRAP = BUILD / "wrap"


def write_wrappers() -> None:
    WRAP.mkdir(parents=True, exist_ok=True)
    inc = str(SDL_INC)
    zig = str(ZIG)
    (WRAP / "zig-cc.cmd").write_text(
        f'@echo off\r\n"{zig}" cc -target {TARGET} -I "{inc}" %*\r\n',
        encoding="utf-8",
    )
    (WRAP / "zig-cxx.cmd").write_text(
        f'@echo off\r\n"{zig}" c++ -target {TARGET} -I "{inc}" %*\r\n',
        encoding="utf-8",
    )
    (WRAP / "zig-ar.cmd").write_text(
        f'@echo off\r\n"{zig}" ar %*\r\n', encoding="utf-8"
    )
    (WRAP / "zig-ranlib.cmd").write_text(
        f'@echo off\r\n"{zig}" ranlib %*\r\n', encoding="utf-8"
    )
    prefix = BUILD / "sdl-prefix"
    cmake_dir = prefix / "lib" / "cmake" / "SDL2"
    cmake_dir.mkdir(parents=True, exist_ok=True)
    (cmake_dir / "SDL2Config.cmake").write_text(
        "set(SDL2_FOUND TRUE)\n"
        f'set(SDL2_INCLUDE_DIRS "{SDL_INC.as_posix()}")\n'
        f'set(SDL2_LIBRARIES "{SDL_LIB.as_posix()}")\n'
        "if(NOT TARGET SDL2::SDL2)\n"
        "  add_library(SDL2::SDL2 SHARED IMPORTED)\n"
        "  set_target_properties(SDL2::SDL2 PROPERTIES\n"
        f'    IMPORTED_LOCATION "{SDL_LIB.as_posix()}"\n'
        f'    INTERFACE_INCLUDE_DIRECTORIES "{SDL_INC.as_posix()}")\n'
        "endif()\n",
        encoding="utf-8",
    )
    cc = (WRAP / "zig-cc.cmd").as_posix()
    cxx = (WRAP / "zig-cxx.cmd").as_posix()
    ar = (WRAP / "zig-ar.cmd").as_posix()
    ranlib = (WRAP / "zig-ranlib.cmd").as_posix()
    (WRAP / "toolchain.cmake").write_text(
        "set(CMAKE_SYSTEM_NAME Linux)\n"
        "set(CMAKE_SYSTEM_PROCESSOR aarch64)\n"
        f'set(CMAKE_C_COMPILER "{cc}")\n'
        f'set(CMAKE_CXX_COMPILER "{cxx}")\n'
        f'set(CMAKE_AR "{ar}" CACHE FILEPATH "" FORCE)\n'
        f'set(CMAKE_RANLIB "{ranlib}" CACHE FILEPATH "" FORCE)\n'
        "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n"
        "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n"
        "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n"
        "set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)\n"
        "set(CMAKE_C_COMPILER_WORKS TRUE)\n"
        "set(CMAKE_CXX_COMPILER_WORKS TRUE)\n",
        encoding="utf-8",
    )


def run(args, **kw):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.check_call(args, **kw)


def main():
    for p in (ZIG, CMAKE, NINJA, SDL_INC / "SDL.h", SDL_LIB):
        if not p.exists():
            sys.exit(f"missing {p}")
    if not (ROOT / "third_party" / "mgba" / "CMakeLists.txt").exists():
        sys.exit("third_party/mgba missing")
    write_wrappers()
    prefix = BUILD / "sdl-prefix"
    if (BUILD / "CMakeCache.txt").exists() and "--fresh" in sys.argv:
        shutil.rmtree(BUILD)
        write_wrappers()
    run(
        [
            str(CMAKE),
            "-S",
            str(ROOT),
            "-B",
            str(BUILD),
            "-G",
            "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={NINJA}",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_TOOLCHAIN_FILE={WRAP / 'toolchain.cmake'}",
            f"-DCMAKE_PREFIX_PATH={prefix}",
            "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF",
            "-DCMAKE_INSTALL_PREFIX=/usr",
        ]
    )
    run([str(CMAKE), "--build", str(BUILD), "-j", "8"])
    exe = BUILD / "pokedaisy"
    print("built", exe, exe.stat().st_size)


if __name__ == "__main__":
    main()
