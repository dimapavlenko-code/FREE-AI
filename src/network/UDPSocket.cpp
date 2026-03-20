#include "network/UDPSocket.hpp"
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #include <fcntl.h> // For O_NONBLOCK on some systems, but Windows uses ioctlsocket
    #include <io.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace FreeAI {
    namespace Network {

        UDPSocket::UDPSocket() : m_handle(InvalidSocket) {}

        UDPSocket::~UDPSocket() {
            Close();
        }

        bool UDPSocket::Create() {
            m_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_handle == InvalidSocket) {
                std::cerr << "[UDP] Failed to create socket: " << Socket::GetLastError() << std::endl;
                return false;
            }
            return true;
        }

        bool UDPSocket::Bind(int port) {
            if (!IsValid()) return false;

            sockaddr_in serverAddr;
            std::memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            serverAddr.sin_port = htons(static_cast<uint16_t>(port));

            if (bind(m_handle, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                std::cerr << "[UDP] Bind failed: " << Socket::GetLastError() << std::endl;
                return false;
            }
            return true;
        }

        int UDPSocket::SendTo(const void* data, size_t length, const std::string& ip, int port) {
            if (!IsValid()) return -1;

            sockaddr_in targetAddr;
            std::memset(&targetAddr, 0, sizeof(targetAddr));
            targetAddr.sin_family = AF_INET;
            targetAddr.sin_port = htons(static_cast<uint16_t>(port));
            if (inet_pton(AF_INET, ip.c_str(), &targetAddr.sin_addr) <= 0) {
                return -1;
            }

#ifdef _WIN32
            return sendto(m_handle, static_cast<const char*>(data), static_cast<int>(length), 0, (sockaddr*)&targetAddr, sizeof(targetAddr));
#else
            return sendto(m_handle, data, length, 0, (sockaddr*)&targetAddr, sizeof(targetAddr));
#endif
        }

        int UDPSocket::ReceiveFrom(void* buffer, size_t length, std::string& out_ip, int& out_port) {
            if (!IsValid()) return -1;

            sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);

#ifdef _WIN32
            int bytes = recvfrom(m_handle, static_cast<char*>(buffer), static_cast<int>(length), 0, (sockaddr*)&clientAddr, &addrLen);
#else
            int bytes = recvfrom(m_handle, buffer, length, 0, (sockaddr*)&clientAddr, &addrLen);
#endif

            if (bytes > 0) {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
                out_ip = std::string(ipStr);
                out_port = ntohs(clientAddr.sin_port);
            }
            return bytes;
        }

        void UDPSocket::Close() {
            if (IsValid()) {
#ifdef _WIN32
                closesocket(m_handle);
#else
                close(m_handle);
#endif
                m_handle = InvalidSocket;
            }
        }

        bool UDPSocket::IsValid() const {
            return m_handle != InvalidSocket;
        }

        bool UDPSocket::SetNonBlocking(bool enable) {
            if (!IsValid()) return false;
#ifdef _WIN32
            u_long mode = enable ? 1 : 0;
            return ioctlsocket(m_handle, FIONBIO, &mode) == 0;
#else
            int flags = fcntl(m_handle, F_GETFL, 0);
            if (flags == -1) return false;
            flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
            return fcntl(m_handle, F_SETFL, flags) == 0;
#endif
        }

    }
}