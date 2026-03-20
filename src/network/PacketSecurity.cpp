#include "network/PacketSecurity.hpp"
#include <cstring>
#include <random>
#include <iostream>

#ifdef MBEDTLS_CHACHA20_C
    #include <mbedtls/chacha20.h>
#endif

namespace FreeAI {
    namespace Network {

        uint32_t PacketSecurity::GenerateNonce() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uint32_t> dist;
            return dist(gen);
        }

        void PacketSecurity::XorObfuscate(uint8_t* data, size_t size, uint32_t key) {
            for (size_t i = 0; i < size; ++i) {
                data[i] ^= static_cast<uint8_t>((key >> ((i % 4) * 8)) & 0xFF);
            }
        }

        std::vector<uint8_t> PacketSecurity::PrepareOutgoing(
            uint8_t type,
            const void* payload,
            size_t payloadSize,
            bool sign,
            bool encrypt,
            const Crypto::Identity* identity)
        {
            SecurePacketHeader header;
            header.nonce = GenerateNonce();
            header.magic_xor = MAGIC_NUMBER ^ header.nonce;
            header.type = type;
            header.flags = 0;
            header.payload_size = static_cast<uint16_t>(payloadSize);
            std::memset(header.reserved, 0, sizeof(header.reserved));

            if (sign && identity && identity->IsValid()) {
                header.flags |= FLAG_SIGNED;
            }
            if (encrypt) {
                header.flags |= FLAG_ENCRYPTED;
            }

            // Calculate total size
            size_t totalSize = sizeof(SecurePacketHeader);
            if (header.flags & FLAG_SIGNED) {
                totalSize += SIGNATURE_SIZE;
            }
            if (header.flags & FLAG_ENCRYPTED) {
                totalSize += CHACHA20_IV_SIZE;
            }
            totalSize += payloadSize;

            std::vector<uint8_t> packet(totalSize);
            uint8_t* ptr = packet.data();

            // Write header (XOR obfuscate the magic)
            std::memcpy(ptr, &header, sizeof(SecurePacketHeader));
            ptr += sizeof(SecurePacketHeader);

            // Prepare data to sign (header + payload)
            std::vector<uint8_t> signData;
            if (header.flags & FLAG_SIGNED) {
                signData.insert(signData.end(), packet.data(), packet.data() + sizeof(SecurePacketHeader));
                signData.insert(signData.end(), static_cast<const uint8_t*>(payload), 
                               static_cast<const uint8_t*>(payload) + payloadSize);
            }

            // Handle encryption
            const void* finalPayload = payload;
            size_t finalPayloadSize = payloadSize;
            std::vector<uint8_t> encryptedPayload;

            if (header.flags & FLAG_ENCRYPTED) {
                // Generate IV
                uint8_t iv[CHACHA20_IV_SIZE];
                for (int i = 0; i < CHACHA20_IV_SIZE; ++i) {
                    iv[i] = static_cast<uint8_t>(GenerateNonce() & 0xFF);
                }
                std::memcpy(ptr, iv, CHACHA20_IV_SIZE);
                ptr += CHACHA20_IV_SIZE;

                // Encrypt (ChaCha20 if available, else XOR)
                #ifdef MBEDTLS_CHACHA20_C
                    mbedtls_chacha20_context ctx;
                    mbedtls_chacha20_init(&ctx);
                    uint8_t key[32]; // Should derive from handshake
                    std::memset(key, 0x42, 32); // Placeholder
                    mbedtls_chacha20_setkey(&ctx, key);
                    mbedtls_chacha20_starts(&ctx, iv, 0);
                    
                    encryptedPayload.resize(payloadSize);
                    mbedtls_chacha20_update(&ctx, payloadSize, 
                                           static_cast<const uint8_t*>(payload),
                                           encryptedPayload.data());
                    mbedtls_chacha20_free(&ctx);
                    
                    finalPayload = encryptedPayload.data();
                    finalPayloadSize = encryptedPayload.size();
                #else
                    // Fallback to XOR
                    encryptedPayload.assign(static_cast<const uint8_t*>(payload),
                                           static_cast<const uint8_t*>(payload) + payloadSize);
                    XorObfuscate(encryptedPayload.data(), encryptedPayload.size(), header.nonce);
                    finalPayload = encryptedPayload.data();
                    finalPayloadSize = encryptedPayload.size();
                #endif
            }

            // Write payload
            std::memcpy(ptr, finalPayload, finalPayloadSize);
            ptr += finalPayloadSize;

            // Write signature (at the end, after header but before payload in actual transmission)
            // Actually, let's put signature right after header for easier parsing
            if (header.flags & FLAG_SIGNED) {
                // Reorganize: Header | Signature | IV | Payload
                // For now, signature at end for simplicity
                std::string signature = identity->Sign(signData.data(), signData.size());
                if (signature.empty()) {
                    header.flags &= ~FLAG_SIGNED; // Remove flag if signing failed
                } else {
                    // Write signature at the position after header
                    uint8_t* sigPos = packet.data() + sizeof(SecurePacketHeader);
                    std::memset(sigPos, 0, SIGNATURE_SIZE);
                    std::memcpy(sigPos, signature.c_str(), signature.size());
                }
            }

            // Final XOR obfuscation on entire packet (except nonce)
            XorObfuscate(packet.data() + sizeof(uint32_t), packet.size() - sizeof(uint32_t), header.nonce);

            return packet;
        }

        bool PacketSecurity::ProcessIncoming(
            const uint8_t* data,
            size_t dataSize,
            SecurePacketHeader& outHeader,
            std::vector<uint8_t>& outPayload,
            const Crypto::Identity* identity)
        {
            if (dataSize < sizeof(SecurePacketHeader)) {
                return false;
            }

            // De-obfuscate
            std::vector<uint8_t> packet(data, data + dataSize);
            uint32_t nonce;
            std::memcpy(&nonce, packet.data() + sizeof(uint32_t), sizeof(uint32_t));
            XorObfuscate(packet.data() + sizeof(uint32_t), packet.size() - sizeof(uint32_t), nonce);

            // Read header
            std::memcpy(&outHeader, packet.data(), sizeof(SecurePacketHeader));

            // Verify magic
            if ((outHeader.magic_xor ^ outHeader.nonce) != MAGIC_NUMBER) {
                return false; // Invalid packet
            }

            // Parse payload
            size_t offset = sizeof(SecurePacketHeader);
            
            // Skip signature if present
            if (outHeader.flags & FLAG_SIGNED) {
                // Verify signature here (would need sender's public key)
                offset += SIGNATURE_SIZE;
            }

            // Skip IV if encrypted
            if (outHeader.flags & FLAG_ENCRYPTED) {
                offset += CHACHA20_IV_SIZE;
            }

            // Extract payload
            size_t payloadSize = outHeader.payload_size;
            if (offset + payloadSize > dataSize) {
                return false;
            }

            outPayload.assign(packet.data() + offset, packet.data() + offset + payloadSize);

            // Decrypt if needed
            if (outHeader.flags & FLAG_ENCRYPTED) {
                // Reverse encryption (same as encrypt for XOR/ChaCha20)
                // Would need proper key management
            }

            return true;
        }

    }
}