#pragma once
#include "Socket.hpp" // Reuse common includes
#include <string>

namespace FreeAI {
    namespace Network {

        class UDPSocket {
        public:
            UDPSocket();
            ~UDPSocket();

            bool Create();
            bool Bind(int port);
            
            // Send to specific address
            int SendTo(const void* data, size_t length, const std::string& ip, int port);
            
            // Receive from any address (fills out_ip, out_port)
            int ReceiveFrom(void* buffer, size_t length, std::string& out_ip, int& out_port);
            
            void Close();
            bool IsValid() const;

            // Set non-blocking mode (Critical for hole punching)
            bool SetNonBlocking(bool enable);

        private:
            SocketHandle m_handle;
        };

    }
}