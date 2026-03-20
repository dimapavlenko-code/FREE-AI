#pragma once
#include <string>

namespace FreeAI {
    namespace Crypto {

        class Identity {
        public:
            Identity();
            ~Identity();

            // Generate new Ed25519 keypair
            bool Generate();

            // Load from PEM strings (for config.ini)
            bool LoadFromPEM(const std::string& pubPEM, const std::string& privPEM);

            // Save to PEM strings
            std::string GetPublicKeyPEM() const;
            std::string GetPrivateKeyPEM() const;

            // Sign data (returns Base64 signature)
            std::string Sign(const void* data, size_t length) const;

            // Verify signature
            static bool Verify(const void* data, size_t length,
                const std::string& signatureB64,
                const std::string& pubKeyPEM);

            // Check if valid
            bool IsValid() const;

            // Get short ID (first 8 chars of pubkey hash) for display
            std::string GetShortID() const;

        private:
            void* m_pkContext; // mbedtls_pk_context*
            bool m_valid;
        };

    }
}