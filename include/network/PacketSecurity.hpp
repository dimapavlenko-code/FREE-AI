#pragma once
#include "network/Protocol.hpp"
#include "crypto/Identity.hpp"
#include <vector>
#include <cstdint>

// MiniLZO
#include "minilzo.h"

namespace FreeAI {
    namespace Network {

        class PacketSecurity {
        public:
            // Initialize compression library (call once at startup)
            static bool Initialize();

            // Cleanup compression library
            static void Shutdown();

            // Prepare outgoing packet (with optional compression + encryption + signing)
            static std::vector<uint8_t> PrepareOutgoing(
                uint8_t type,
                const void* payload,
                size_t payloadSize,
                bool sign,
                bool encrypt,
                const Crypto::Identity* identity);

            // Process incoming packet
            static bool ProcessIncoming(
                const uint8_t* data,
                size_t dataSize,
                SecurePacketHeader& outHeader,
                std::vector<uint8_t>& outPayload,
                const Crypto::Identity* identity,
                const std::string& senderPubKey);

        private:
            static uint32_t GenerateNonce();
            static void XorObfuscate(uint8_t* data, size_t size, uint32_t key);

            // Compression helpers
            static std::vector<uint8_t> CompressData(const uint8_t* input, size_t inputSize);
            static std::vector<uint8_t> DecompressData(const uint8_t* input, size_t inputSize, size_t expectedSize);

            // Work memory for compression (allocated once, reused)
            static lzo_align_t s_compressWorkMem[];
            static bool s_initialized;
        };

    }
}