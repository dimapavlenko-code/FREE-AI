#include "network/Socket.hpp"
#include <cstring>
#include <iostream>

namespace FreeAI {
    namespace Network {

        Socket::Socket() : m_handle(InvalidSocket) {}

        Socket::~Socket() {
            Close();
        }

        bool Socket::CreateTCP() {
            m_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_handle == InvalidSocket) {
                std::cerr << "[SOCKET] Failed to create socket: " << GetLastError() << std::endl;
                return false;
            }
            return true;
        }

        bool Socket::Connect(const std::string& host, int port) {
            if (!IsValid()) return false;

            sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(static_cast<uint16_t>(port));

            // Convert IP string to binary
            if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
                std::cerr << "[SOCKET] Invalid address: " << host << std::endl;
                return false;
            }

            int result = connect(m_handle, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
            if (result < 0) {
                std::cerr << "[SOCKET] Connection failed: " << GetLastError() << std::endl;
                return false;
            }
            return true;
        }

        bool Socket::Bind(int port) {
            if (!IsValid()) return false;

            sockaddr_in serverAddr;
            std::memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
            serverAddr.sin_port = htons(static_cast<uint16_t>(port));

            int result = bind(m_handle, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
            if (result < 0) {
                std::cerr << "[SOCKET] Bind failed on port " << port << ": " << GetLastError() << std::endl;
                return false;
            }
            return true;
        }

        int Socket::Send(const void* data, size_t length) {
            if (!IsValid()) return -1;
#ifdef _WIN32
            return send(m_handle, static_cast<const char*>(data), static_cast<int>(length), 0);
#else
            return send(m_handle, data, length, 0);
#endif
        }

        int Socket::Receive(void* buffer, size_t length) {
            if (!IsValid()) return -1;
#ifdef _WIN32
            return recv(m_handle, static_cast<char*>(buffer), static_cast<int>(length), 0);
#else
            return recv(m_handle, buffer, length, 0);
#endif
        }

        void Socket::Close() {
            if (IsValid()) {
#ifdef _WIN32
                closesocket(m_handle);
#else
                close(m_handle);
#endif
                m_handle = InvalidSocket;
            }
        }

        bool Socket::IsValid() const {
            return m_handle != InvalidSocket;
        }

        std::string Socket::GetLastError() {
#ifdef _WIN32
            int err = WSAGetLastError();
            return "Winsock Error " + std::to_string(err);
#else
            return "POSIX Error " + std::to_string(errno);
#endif
        }

    }
}