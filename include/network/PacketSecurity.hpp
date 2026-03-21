#pragma once
#include "network/Protocol.hpp"
#include "crypto/Identity.hpp"
#include <vector>
#include <string>

namespace FreeAI {
    namespace Network {

        class PacketSecurity {
        public:
            // Sign and optionally encrypt a packet
            static std::vector<uint8_t> PrepareOutgoing(
                uint8_t type,
                const void* payload,
                size_t payloadSize,
                bool sign,
                bool encrypt,
                const Crypto::Identity* identity = nullptr);

            // Verify and optionally decrypt a packet
            static bool ProcessIncoming(
                const uint8_t* data,
                size_t dataSize,
                SecurePacketHeader& outHeader,
                std::vector<uint8_t>& outPayload,
                const Crypto::Identity* identity = nullptr,
                const std::string& senderPubKey = "");

            // Generate random nonce
            static uint32_t GenerateNonce();

            // XOR obfuscation (lightweight DPI resistance)
            static void XorObfuscate(uint8_t* data, size_t size, uint32_t key);
        };

    }
}