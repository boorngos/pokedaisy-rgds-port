cmake_minimum_required(VERSION 3.16)
project(pokedaisy-linux C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# --- mGBA 0.10.5 (same pin as PokeDaisy) ---
set(MGBA_SRC "${CMAKE_SOURCE_DIR}/third_party/mgba")
if(NOT EXISTS "${MGBA_SRC}/CMakeLists.txt")
    FetchContent_Declare(mgba
        GIT_REPOSITORY https://github.com/mgba-emu/mgba.git
        GIT_TAG 0.10.5
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(mgba)
    if(NOT mgba_POPULATED)
        FetchContent_Populate(mgba)
        set(MGBA_SRC "${mgba_SOURCE_DIR}")
    endif()
endif()

set(BUILD_STATIC        ON  CACHE BOOL "" FORCE)
set(BUILD_SHARED        OFF CACHE BOOL "" FORCE)
set(DISABLE_FRONTENDS   ON  CACHE BOOL "" FORCE)
set(DISABLE_DEPS        ON  CACHE BOOL "" FORCE)
set(SKIP_LIBRARY        OFF CACHE BOOL "" FORCE)
set(BUILD_GL            OFF CACHE BOOL "" FORCE)
set(BUILD_GLES2         OFF CACHE BOOL "" FORCE)
set(BUILD_GLES3         OFF CACHE BOOL "" FORCE)
set(BUILD_PERF          OFF CACHE BOOL "" FORCE)
set(BUILD_TEST          OFF CACHE BOOL "" FORCE)
set(BUILD_SUITE         OFF CACHE BOOL "" FORCE)
set(BUILD_LIBRETRO      OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON        OFF CACHE BOOL "" FORCE)
set(USE_DEBUGGERS       OFF CACHE BOOL "" FORCE)
set(USE_GDB_STUB        OFF CACHE BOOL "" FORCE)
set(USE_EDITLINE        OFF CACHE BOOL "" FORCE)
set(USE_FFMPEG          OFF CACHE BOOL "" FORCE)
set(USE_DISCORD_RPC     OFF CACHE BOOL "" FORCE)
set(USE_LZMA            OFF CACHE BOOL "" FORCE)
set(USE_LUA             OFF CACHE BOOL "" FORCE)
set(USE_ELF             OFF CACHE BOOL "" FORCE)
set(USE_SQLITE3         OFF CACHE BOOL "" FORCE)
set(USE_LIBZIP          OFF CACHE BOOL "" FORCE)
set(USE_MINIZIP         OFF CACHE BOOL "" FORCE)
set(USE_PNG             OFF CACHE BOOL "" FORCE)
set(USE_ZLIB            OFF CACHE BOOL "" FORCE)
set(M_CORE_GBA          ON  CACHE BOOL "" FORCE)
set(M_CORE_GB           OFF CACHE BOOL "" FORCE)
set(BUILD_LTO           OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    ON  CACHE BOOL "" FORCE)
set(USE_PTHREADS        ON  CACHE BOOL "" FORCE)

add_subdirectory("${MGBA_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/mgba" EXCLUDE_FROM_ALL)

# --- SDL2 ---
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    set(SDL2_SRC "${CMAKE_SOURCE_DIR}/third_party/SDL2")
    if(NOT EXISTS "${SDL2_SRC}/CMakeLists.txt")
        FetchContent_Declare(sdl2
            URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
            URL_HASH SHA256=8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f)
        FetchContent_GetProperties(sdl2)
        if(NOT sdl2_POPULATED)
            FetchContent_Populate(sdl2)
            set(SDL2_SRC "${sdl2_SOURCE_DIR}")
        endif()
    endif()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${SDL2_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/sdl2" EXCLUDE_FROM_ALL)
    set(PD_SDL_TARGET SDL2-static)
    set(PD_SDL_INCLUDES "${SDL2_SRC}/include")
else()
    if(TARGET SDL2::SDL2)
        set(PD_SDL_TARGET SDL2::SDL2)
    else()
        set(PD_SDL_TARGET ${SDL2_LIBRARIES})
        set(PD_SDL_INCLUDES ${SDL2_INCLUDE_DIRS})
    endif()
endif()

file(GLOB CORE_SOURCES "${CMAKE_SOURCE_DIR}/core/*.c")

add_executable(pokedaisy
    linux/host.c
    linux/pd_png.c
    ${CORE_SOURCES})

target_include_directories(pokedaisy PRIVATE
    "${CMAKE_SOURCE_DIR}/core"
    "${CMAKE_SOURCE_DIR}/linux"
    "${MGBA_SRC}/include"
    "${MGBA_SRC}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/mgba/include"
    ${PD_SDL_INCLUDES})

target_compile_definitions(pokedaisy PRIVATE
    M_CORE_GBA
    BUILD_STATIC
    USE_PTHREADS
    PD_SHARE_DIR="${CMAKE_INSTALL_PREFIX}/share/pokedaisy")

target_compile_options(pokedaisy PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(pokedaisy PRIVATE mgba ${PD_SDL_TARGET} m pthread)

if(TARGET SDL2::SDL2main)
    target_link_libraries(pokedaisy PRIVATE SDL2::SDL2main)
elseif(TARGET SDL2main)
    target_link_libraries(pokedaisy PRIVATE SDL2main)
endif()

install(TARGETS pokedaisy RUNTIME DESTINATION bin)
install(DIRECTORY share/pokedaisy/ DESTINATION share/pokedaisy)

add_executable(pokedaisy-preview
    linux/preview.c
    linux/pd_png.c
    ${CORE_SOURCES})
target_include_directories(pokedaisy-preview PRIVATE
    "${CMAKE_SOURCE_DIR}/core"
    "${CMAKE_SOURCE_DIR}/linux")
target_link_libraries(pokedaisy-preview PRIVATE m)
target_compile_options(pokedaisy-preview PRIVATE -Wall -Wextra)
