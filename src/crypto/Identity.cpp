#include "crypto/Identity.hpp"
#include "network/Protocol.hpp"
#include <mbedtls/pk.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/base64.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstring>

namespace FreeAI {
    namespace Crypto {

        Identity::Identity() : m_pkContext(nullptr), m_valid(false) {
            m_pkContext = new mbedtls_pk_context;
            mbedtls_pk_init(static_cast<mbedtls_pk_context*>(m_pkContext));
        }

        Identity::~Identity() {
            if (m_pkContext) {
                mbedtls_pk_free(static_cast<mbedtls_pk_context*>(m_pkContext));
                delete static_cast<mbedtls_pk_context*>(m_pkContext);
            }
        }

        bool Identity::Generate() {
            mbedtls_entropy_context entropy;
            mbedtls_ctr_drbg_context ctr_drbg;
            mbedtls_pk_context* pk = static_cast<mbedtls_pk_context*>(m_pkContext);

            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&ctr_drbg);

            const char* pers = "FREE_AI_IDENTITY_GEN";
            int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                reinterpret_cast<const unsigned char*>(pers), strlen(pers));

            if (ret != 0) {
                std::cerr << "[CRYPTO] DRBG Seed failed: -0x" << std::hex << -ret << std::dec << std::endl;
                mbedtls_ctr_drbg_free(&ctr_drbg);
                mbedtls_entropy_free(&entropy);
                return false;
            }

            // Setup PK context for ECDSA Key (secp256k1 - same as Bitcoin)
            ret = mbedtls_pk_setup(pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
            if (ret != 0) {
                std::cerr << "[CRYPTO] PK Setup failed: -0x" << std::hex << -ret << std::dec << std::endl;
                mbedtls_ctr_drbg_free(&ctr_drbg);
                mbedtls_entropy_free(&entropy);
                return false;
            }

            // Generate ECDSA Key with secp256k1 curve (NOT Curve25519)
            ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256K1, mbedtls_pk_ec(*pk),
                mbedtls_ctr_drbg_random, &ctr_drbg);
            if (ret != 0) {
                std::cerr << "[CRYPTO] Key Gen failed: -0x" << std::hex << -ret << std::dec << std::endl;
                mbedtls_ctr_drbg_free(&ctr_drbg);
                mbedtls_entropy_free(&entropy);
                return false;
            }

            m_valid = true;
            mbedtls_ctr_drbg_free(&ctr_drbg);
            mbedtls_entropy_free(&entropy);
            return true;
        }

        bool Identity::LoadFromPEM(const std::string& pubPEM, const std::string& privPEM) {
            if (pubPEM.empty() || privPEM.empty()) {
                return false;
            }

            mbedtls_pk_context* pk = static_cast<mbedtls_pk_context*>(m_pkContext);

            // Initialize temporary RNG for key parsing (required by mbedTLS 3.6.x)
            mbedtls_entropy_context entropy;
            mbedtls_ctr_drbg_context ctr_drbg;
            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&ctr_drbg);

            int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                reinterpret_cast<const unsigned char*>("FREE_AI_KEY_LOAD"), 16);
            if (ret != 0) {
                mbedtls_ctr_drbg_free(&ctr_drbg);
                mbedtls_entropy_free(&entropy);
                std::cerr << "[CRYPTO] DRBG Seed for key load failed: -0x" << std::hex << -ret << std::dec << std::endl;
                return false;
            }

            // Parse private key (includes public key)
            ret = mbedtls_pk_parse_key(pk,
                reinterpret_cast<const unsigned char*>(privPEM.c_str()),
                privPEM.size() + 1,
                nullptr, 0,
                mbedtls_ctr_drbg_random,
                &ctr_drbg);

            mbedtls_ctr_drbg_free(&ctr_drbg);
            mbedtls_entropy_free(&entropy);

            if (ret != 0) {
                std::cerr << "[CRYPTO] Key Parse failed: -0x" << std::hex << -ret << std::dec << std::endl;
                return false;
            }

            m_valid = true;
            return true;
        }

        std::string Identity::GetPublicKeyPEM() const {
            if (!m_valid) return "";

            mbedtls_pk_context* pk = static_cast<mbedtls_pk_context*>(m_pkContext);
            unsigned char pem_buf[512];

            int ret = mbedtls_pk_write_pubkey_pem(pk, pem_buf, sizeof(pem_buf));
            if (ret != 0) {
                return "";
            }

            return std::string(reinterpret_cast<char*>(pem_buf));
        }

        std::string Identity::GetPrivateKeyPEM() const {
            if (!m_valid) return "";

            mbedtls_pk_context* pk = static_cast<mbedtls_pk_context*>(m_pkContext);
            unsigned char pem_buf[1024];

            int ret = mbedtls_pk_write_key_pem(pk, pem_buf, sizeof(pem_buf));
            if (ret != 0) {
                return "";
            }

            return std::string(reinterpret_cast<char*>(pem_buf));
        }

        std::string Identity::Sign(const void* data, size_t length) const {
            if (!m_valid) return "";

            mbedtls_pk_context* pk = static_cast<mbedtls_pk_context*>(m_pkContext);
            unsigned char sig[72]; // ECDSA sig is up to 72 bytes (DER encoded)
            size_t sig_len = 0;

            // Initialize temporary RNG for signing
            mbedtls_entropy_context entropy;
            mbedtls_ctr_drbg_context ctr_drbg;
            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&ctr_drbg);

            int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                reinterpret_cast<const unsigned char*>("FREE_AI_SIGN"), 12);
            if (ret != 0) {
                mbedtls_ctr_drbg_free(&ctr_drbg);
                mbedtls_entropy_free(&entropy);
                return "";
            }

            // ECDSA Sign with SHA256 hash (CORRECT for secp256k1)
            ret = mbedtls_pk_sign(pk,
                MBEDTLS_MD_SHA256,  // <-- SHA256 for ECDSA (not NONE)
                static_cast<const unsigned char*>(data),
                length,
                sig,
                sizeof(sig),
                &sig_len,
                mbedtls_ctr_drbg_random,
                &ctr_drbg);

            mbedtls_ctr_drbg_free(&ctr_drbg);
            mbedtls_entropy_free(&entropy);

            if (ret != 0) {
                std::cerr << "[CRYPTO] Sign failed: -0x" << std::hex << -ret << std::dec << std::endl;
                return "";
            }

            // Encode to Base64
            unsigned char b64[128];
            size_t b64_len = 0;
            mbedtls_base64_encode(b64, sizeof(b64), &b64_len, sig, sig_len);

            return std::string(reinterpret_cast<char*>(b64));
        }

        bool Identity::Verify(const void* data, size_t length,
            const std::string& signatureB64,
            const std::string& pubKeyPEM) {
           
            // Decode Base64
            unsigned char sig[Network::SIGNATURE_SIZE];
            size_t sig_len = 0;
            int ret = mbedtls_base64_decode(sig, sizeof(sig), &sig_len,
                reinterpret_cast<const unsigned char*>(signatureB64.c_str()),
                signatureB64.size());

            if (ret != 0) {
                char error_buf[100];
                mbedtls_strerror(ret, error_buf, sizeof(error_buf));
                std::cerr << "[ERROR] Base64 decode failed: " << error_buf
                    << " (code: " << ret << ")" << std::endl;
                return false;
            }           

            // Parse public key
            mbedtls_pk_context pk;
            mbedtls_pk_init(&pk);
            ret = mbedtls_pk_parse_public_key(&pk,
                reinterpret_cast<const unsigned char*>(pubKeyPEM.c_str()),
                pubKeyPEM.size() + 1);
            if (ret != 0) {
                mbedtls_pk_free(&pk);
                return false;
            }

            // Verify with SHA256 (CORRECT for ECDSA)
            ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256,
                static_cast<const unsigned char*>(data), length,
                sig, sig_len);

            mbedtls_pk_free(&pk);
            return (ret == 0);
        }

        bool Identity::IsValid() const {
            return m_valid;
        }

        std::string Identity::GetShortID() const {
            if (!m_valid) return "INVALID";
            std::string pem = GetPublicKeyPEM();
            auto pemsz = pem.size();
            if (pemsz < 20) return "ERROR";
            std::stringstream ss;

            auto pemsz_2 = pemsz / 2;
            for (size_t i = 0; i < 8 && i < pemsz_2; ++i) {
                ss << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(pem[i + pemsz_2]);
            }
            return ss.str().substr(0, 8);
        }

    }
}