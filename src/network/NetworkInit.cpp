#include "network/NetworkInit.hpp"
#include "network/PacketSecurity.hpp"  
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace FreeAI {
    namespace Network {

        bool NetworkEnvironment::Initialize() {
#ifdef _WIN32
            WSADATA wsaData;
            int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (ret != 0) {
                return false;
            }
#endif

            // Initialize MiniLZO compression
            if (!PacketSecurity::Initialize()) {
                std::cerr << "[NETWORK] Failed to initialize compression!" << std::endl;
                return false;
            }

            return true;
        }

        void NetworkEnvironment::Shutdown() {
            PacketSecurity::Shutdown();

#ifdef _WIN32
            WSACleanup();
#endif
        }

    }
}