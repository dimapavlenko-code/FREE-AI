#pragma once
#include "network/HolePunchManager.hpp"
#include <vector>
#include <cstring>

namespace FreeAI {
    namespace Network {

        // =====================================================================
        // STUN Response Parser - Extracted from HolePunchManager
        // =====================================================================
        // Handles parsing of STUN Binding Response messages and extraction
        // of XOR-MAPPED-ADDRESS attributes according to RFC 5389.
        // =====================================================================

        class STUNParser {
        public:
            static constexpr uint32_t MAGIC_COOKIE = 0x2112A442;
            static constexpr size_t HEADER_SIZE = 20;
            static constexpr uint16_t XOR_MAPPED_ADDRESS_TYPE = 0x8027;

            // Parse XOR-MAPPED-ADDRESS from STUN Binding Response
            // Returns ExternalAddress with discovered flag set on success
            static ExternalAddress ParseMappedAddress(const char* data, int length);

            // Validate STUN Binding Response message
            static bool IsValidBindingResponse(const char* data, int length);

            // Build STUN Binding Response with XOR-MAPPED-ADDRESS
            static std::vector<uint8_t> BuildBindingResponse(
                const sockaddr_in& clientAddr,
                const char* transactionId);

            // XOR a port value with the magic cookie
            static uint16_t XORPort(uint16_t rawPort);

            // XOR an address with the magic cookie
            static uint32_t XORAddress(uint32_t xoredAddr);

            // Un-XOR a port value (same operation due to XOR symmetry)
            static uint16_t UnXORPort(uint16_t xoredPort);

            // Un-XOR an address value (same operation due to XOR symmetry)
            static uint32_t UnXORAddress(uint32_t xoredAddr);

        private:
            // Parse a single STUN attribute
            static bool ParseAttribute(const uint8_t*& ptr, int& remaining,
                                       uint16_t& type, uint16_t& len);

            // XOR IPv4 address from STUN response
            static uint32_t UnXORIPv4(uint32_t xoredAddr);
        };

    }
}
