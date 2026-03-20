#include "network/NetworkInit.hpp"
#include <iostream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") // Link Winsock library automatically on MSVC
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
#endif

namespace FreeAI {
    namespace Network {

        bool NetworkEnvironment::Initialize() {
#ifdef _WIN32
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0) {
                std::cerr << "[NET] Winsock Startup Failed: " << result << std::endl;
                return false;
            }
            // Optional: Print Winsock version for transparency
            // std::cout << "[NET] Winsock Initialized (Version: " << LOBYTE(wsaData.wVersion) << ")" << std::endl;
#endif
            return true;
        }

        void NetworkEnvironment::Shutdown() {
#ifdef _WIN32
            WSACleanup();
            // std::cout << "[NET] Winsock Cleanup Complete" << std::endl;
#endif
        }

    }
}