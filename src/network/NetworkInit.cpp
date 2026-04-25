#include "network/NetworkInit.hpp"
#include "network/PacketSecurity.hpp"
#include "network/HolePunchManager.hpp"
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace FreeAI {
    namespace Network {

        // Global STUN server instance (for simple use cases)
        static HolePunchManager g_stunManager;
        static bool g_stunInitialized = false;

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
            ShutdownSTUNServer();
            PacketSecurity::Shutdown();

#ifdef _WIN32
            WSACleanup();
#endif
        }

        int NetworkEnvironment::InitializeSTUNServer(int port) {
            if (g_stunInitialized) {
                std::cerr << "[NETWORK] STUN server already initialized." << std::endl;
                return g_stunManager.GetMyExternalAddress().port;
            }

            if (!g_stunManager.InitializeSTUNServer(port)) {
                std::cerr << "[NETWORK] Failed to initialize STUN server on port " << port << std::endl;
                return -1;
            }

            g_stunManager.StartSTUNServer();
            g_stunInitialized = true;

            std::cout << "[NETWORK] STUN server initialized on port " << port << std::endl;
            return port;
        }

        void NetworkEnvironment::ShutdownSTUNServer() {
            if (g_stunInitialized) {
                g_stunManager.ShutdownSTUNServer();
                g_stunInitialized = false;
                std::cout << "[NETWORK] STUN server shut down." << std::endl;
            }
        }

    }
}