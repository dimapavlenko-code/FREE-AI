cmake_minimum_required(VERSION 3.20)
project(FREE_AI VERSION 0.1.0 LANGUAGES CXX)

# --- Core Settings ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Helpful for IDEs

# --- Options ---
option(BUILD_SHARED_LIBS "Build shared libraries" OFF) # Prefer static for distribution
option(ENABLE_TESTS "Build unit tests" ON)

# --- Platform Specifics ---
if(WIN32)
    # Windows needs Winsock2 and Ws2_32 for networking
    set(PLATFORM_LIBS ws2_32 wsock32)
    add_definitions(-D_WIN32_WINNT=0x0A00) # Windows 10 API
    add_definitions(-DNOMINMAX) # Prevent min/max macros conflict
else()
    # Linux/Unix needs pthread for threading
    set(PLATFORM_LIBS pthread)
endif()

# --- mbedTLS Integration (Version 3.6.x) ---
set(USE_SHARED_MBEDTLS_LIBRARY OFF)
set(ENABLE_TESTING OFF)
set(ENABLE_PROGRAMS OFF)

add_subdirectory(src/crypto/mbedtls)

# --- Source Files ---
set(SOURCES
    src/main.cpp
    src/network/NetworkInit.cpp
    src/network/Socket.cpp
    src/network/UDPSocket.cpp
    src/network/PeerManager.cpp
    src/network/PacketSecurity.cpp
    src/network/HolePunchManager.cpp
    src/network/DHT.cpp              # NEW
    src/crypto/Identity.cpp
    src/utils/Config.cpp
)

set(HEADERS
    include/network/NetworkInit.hpp
    include/network/Socket.hpp
    include/network/UDPSocket.hpp
    include/network/Protocol.hpp
    include/network/PeerManager.hpp
    include/network/PacketSecurity.hpp
    include/network/HolePunchManager.hpp
    include/network/DHT.hpp          # NEW
    include/crypto/Identity.hpp
    include/utils/Config.hpp
    include/utils/ThreadUtils.hpp
)


# --- Executable ---
add_executable(freeai ${SOURCES} ${HEADERS})

# --- Link Libraries ---
# mbedTLS 3.6 requires linking all three libs
target_link_libraries(freeai PRIVATE mbedcrypto mbedx509 mbedtls ${PLATFORM_LIBS})
# Add threading library explicitly for Linux/macOS
if(UNIX)
    target_link_libraries(freeai PRIVATE pthread)
endif()

# --- Include Directories ---
target_include_directories(freeai PRIVATE ${CMAKE_SOURCE_DIR}/include)

# --- Compiler Warnings (Transparency & Quality) ---
if(MSVC)
    target_compile_options(freeai PRIVATE /W4 /WX-) # High warning level
else()
    target_compile_options(freeai PRIVATE -Wall -Wextra -pedantic)
endif()

# --- Test App ---
if(ENABLE_TESTS)
    add_executable(freeai_test tests/test_main.cpp)
    target_link_libraries(freeai_test PRIVATE ${PLATFORM_LIBS})
endif()