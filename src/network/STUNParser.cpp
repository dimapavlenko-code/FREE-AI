#include "network/STUNParser.hpp"
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace FreeAI {
    namespace Network {

        // =====================================================================
        // STUNParser Implementation
        // =====================================================================

        bool STUNParser::IsValidBindingResponse(const char* data, int length) {
            if (length < static_cast<int>(HEADER_SIZE)) {
                return false;
            }

            // Check magic cookie (bytes 0-3)
            uint32_t magicCookie;
            std::memcpy(&magicCookie, data, sizeof(magicCookie));
            magicCookie = ntohl(magicCookie);

            if (magicCookie != MAGIC_COOKIE) {
                return false;
            }

            // Check message type (Binding Response = 0x0101)
            uint16_t messageType;
            std::memcpy(&messageType, data + 2, sizeof(messageType));
            messageType = ntohs(messageType);

            return (messageType == PT_STUN_BINDING_RESPONSE);
        }

        ExternalAddress STUNParser::ParseMappedAddress(const char* data, int length) {
            ExternalAddress addr{"", 0, false};

            // Skip STUN header (20 bytes)
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data) + HEADER_SIZE;
            int remaining = length - static_cast<int>(HEADER_SIZE);

            while (remaining > 0) {
                uint16_t attrType, attrLen;
                if (!ParseAttribute(ptr, remaining, attrType, attrLen)) {
                    break;
                }

                // Check for XOR-MAPPED-ADDRESS (0x8027)
                if (attrType == XOR_MAPPED_ADDRESS_TYPE && attrLen >= 8) {
                    // Family
                    uint16_t family;
                    std::memcpy(&family, ptr, sizeof(family));
                    family = ntohs(family);

                    if (family == 1) { // IPv4
                        // Port (XOR with magic cookie)
                        uint16_t xoredPort;
                        std::memcpy(&xoredPort, ptr + 4, sizeof(xoredPort));
                        xoredPort = ntohs(xoredPort);
                        uint16_t realPort = UnXORPort(xoredPort);

                        // Address (XOR with magic cookie)
                        uint32_t xoredAddr;
                        std::memcpy(&xoredAddr, ptr + 6, sizeof(xoredAddr));
                        uint32_t realAddr = UnXORIPv4(xoredAddr);

                        char ipStr[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &realAddr, ipStr, INET_ADDRSTRLEN);

                        addr.ip = std::string(ipStr);
                        addr.port = static_cast<int>(realPort);
                        addr.discovered = true;
                    }
                    break;
                }

                ptr += attrLen;
                remaining -= attrLen;
            }

            return addr;
        }

        std::vector<uint8_t> STUNParser::BuildBindingResponse(
            const sockaddr_in& clientAddr,
            const char* transactionId) {

            std::vector<uint8_t> response(HEADER_SIZE + 8); // Header + XOR-MAPPED-ADDRESS
            std::memset(response.data(), 0, response.size());

            // Message Type: Binding Response (0x0101)
            uint16_t msgType = htons(PT_STUN_BINDING_RESPONSE);
            std::memcpy(response.data(), &msgType, sizeof(msgType));

            // Length: 8 (XOR-MAPPED-ADDRESS value: family(2) + port(2) + addr(4))
            uint16_t attrLen = htons(8);
            std::memcpy(response.data() + 2, &attrLen, sizeof(attrLen));

            // Transaction ID (bytes 4-19)
            std::memcpy(response.data() + 4, transactionId, STUN_TRANSACTION_ID_SIZE);

            // XOR-MAPPED-ADDRESS attribute (starts at byte 20)
            size_t attrOffset = HEADER_SIZE;

            // Type: XOR-MAPPED-ADDRESS (0x8027)
            uint16_t attrType = htons(XOR_MAPPED_ADDRESS_TYPE);
            std::memcpy(response.data() + attrOffset, &attrType, sizeof(attrType));
            attrOffset += 2;

            // Value: family(2) + port(2) + addr(4)
            // Family: 1 (IPv4)
            uint16_t family = 1;
            std::memcpy(response.data() + attrOffset, &family, sizeof(family));
            attrOffset += 2;

            // Port: XOR the real port with magic cookie
            uint16_t rawPort = ntohs(clientAddr.sin_port);
            uint16_t xoredPort = htons(XORPort(rawPort));
            std::memcpy(response.data() + attrOffset, &xoredPort, sizeof(xoredPort));
            attrOffset += 2;

            // Address: XOR with magic cookie
            uint32_t xoredAddr = clientAddr.sin_addr.s_addr ^ MAGIC_COOKIE;
            std::memcpy(response.data() + attrOffset, &xoredAddr, sizeof(xoredAddr));

            return response;
        }

        uint16_t STUNParser::XORPort(uint16_t rawPort) {
            uint16_t magicPort = static_cast<uint16_t>((MAGIC_COOKIE >> 16) & 0xFFFF);
            return rawPort ^ magicPort;
        }

        uint32_t STUNParser::XORAddress(uint32_t xoredAddr) {
            return xoredAddr ^ MAGIC_COOKIE;
        }

        uint16_t STUNParser::UnXORPort(uint16_t xoredPort) {
            // XOR is symmetric: unXOR(x) = XOR(x)
            return XORPort(xoredPort);
        }

        uint32_t STUNParser::UnXORAddress(uint32_t xoredAddr) {
            // XOR is symmetric: unXOR(x) = XOR(x)
            return XORAddress(xoredAddr);
        }

        bool STUNParser::ParseAttribute(const uint8_t*& ptr, int& remaining,
                                         uint16_t& type, uint16_t& len) {
            if (remaining < 4) {
                return false;
            }

            std::memcpy(&type, ptr, sizeof(type));
            std::memcpy(&len, ptr + 2, sizeof(len));
            type = ntohs(type);
            len = ntohs(len);

            ptr += 4;
            remaining -= 4;

            if (len > remaining) {
                return false;
            }

            return true;
        }

        uint32_t STUNParser::UnXORIPv4(uint32_t xoredAddr) {
            return xoredAddr ^ MAGIC_COOKIE;
        }

    }
}
