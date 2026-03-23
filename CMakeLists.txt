cmake_minimum_required(VERSION 3.20)
project(FREE_AI VERSION 0.1.0 LANGUAGES CXX C)  # ← Added C language

# --- Core Settings ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options ---
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(ENABLE_TESTS "Build unit tests" ON)

# --- Platform Specifics ---
if(WIN32)
    set(PLATFORM_LIBS ws2_32 wsock32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DNOMINMAX)
else()
    set(PLATFORM_LIBS pthread)
endif()

# --- mbedTLS Integration (Version 3.6.x) ---
set(USE_SHARED_MBEDTLS_LIBRARY OFF)
set(ENABLE_TESTING OFF)
set(ENABLE_PROGRAMS OFF)
add_subdirectory(src/crypto/mbedtls)

# --- MiniLZO Compression ---
set(MINILZO_SOURCES
    src/compression/minilzo/minilzo.c
)

# --- Source Files ---
set(SOURCES
    src/main.cpp
    src/network/NetworkInit.cpp
    src/network/Socket.cpp
    src/network/UDPSocket.cpp
    src/network/PeerManager.cpp
    src/network/PacketSecurity.cpp
    src/network/HolePunchManager.cpp
    src/network/DHT.cpp
    src/crypto/Identity.cpp
    src/utils/Config.cpp
    src/utils/Helpers.cpp
    ${MINILZO_SOURCES}
)

set(HEADERS
    include/network/NetworkInit.hpp
    include/network/Socket.hpp
    include/network/UDPSocket.hpp
    include/network/Protocol.hpp
    include/network/PeerManager.hpp
    include/network/PacketSecurity.hpp
    include/network/HolePunchManager.hpp
    include/network/DHT.hpp
    include/crypto/Identity.hpp
    include/utils/Config.hpp
    include/utils/ThreadUtils.hpp
    include/utils/Helpers.hpp
)

# --- Executable ---
add_executable(freeai ${SOURCES} ${HEADERS})

# --- Link Libraries ---
target_link_libraries(freeai PRIVATE mbedcrypto mbedx509 mbedtls ${PLATFORM_LIBS})
if(UNIX)
    target_link_libraries(freeai PRIVATE pthread)
endif()

# --- Include Directories ---
target_include_directories(freeai PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/compression/minilzo
)

# --- Compiler Warnings ---
if(MSVC)
    target_compile_options(freeai PRIVATE /W4 /WX-)
    # Disable warnings for third-party MiniLZO
    set_source_files_properties(${MINILZO_SOURCES} PROPERTIES COMPILE_FLAGS "/W0")
else()
    target_compile_options(freeai PRIVATE -Wall -Wextra -pedantic)
    set_source_files_properties(${MINILZO_SOURCES} PROPERTIES COMPILE_FLAGS "-w")
endif()

# --- MiniLZO Defines ---
target_compile_definitions(freeai PRIVATE MINILZO_CFG_USE_INTERNAL_LZODEFS)

# --- Test App ---
if(ENABLE_TESTS)
    add_executable(freeai_test tests/test_main.cpp)
    target_link_libraries(freeai_test PRIVATE ${PLATFORM_LIBS})
endif()