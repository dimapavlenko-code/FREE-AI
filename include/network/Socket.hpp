#pragma once
#include <string>
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
    // Prevent conflict with std::min/max
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <ws2tcpip.h>
    typedef SOCKET SocketHandle;
    const SocketHandle InvalidSocket = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    typedef int SocketHandle;
    const SocketHandle InvalidSocket = -1;
#endif

namespace FreeAI {
    namespace Network {

        class Socket {
        public:
            Socket();
            ~Socket();

            // Disable copy
            Socket(const Socket&) = delete;
            Socket& operator=(const Socket&) = delete;

            bool CreateTCP();
            bool Connect(const std::string& host, int port);
            bool Bind(int port);
            int Send(const void* data, size_t length);
            int Receive(void* buffer, size_t length);
            void Close();
            bool IsValid() const;

            // Helper to get last error message (Transparent Debugging)
            static std::string GetLastError();

        private:
            SocketHandle m_handle;
        };

    }
}