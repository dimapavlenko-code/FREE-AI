#include <iostream>
#include "network/NetworkInit.hpp"
#include "network/PeerManager.hpp"
#include "utils/Config.hpp"
#include "crypto/Identity.hpp"

#define PROJECT_NAME "FREE-AI"
#define PROJECT_VERSION "0.1.0"

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " " << PROJECT_NAME << " v" << PROJECT_VERSION << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Initialize Network
    if (!FreeAI::Network::NetworkEnvironment::Initialize()) {
        std::cerr << "[ERROR] Network init failed." << std::endl;
        return 1;
    }

    // 2. Load Configuration
    FreeAI::Utils::Config config;
    bool configLoaded = config.Load("config.ini");
    if (!configLoaded) {
        std::cout << "[CONFIG] Creating default config.ini" << std::endl;
    }

    // 3. Initialize Identity
    FreeAI::Crypto::Identity identity;
    std::string pubPEM = config.Get("security", "public_key_pem", "");
    std::string privPEM = config.Get("security", "private_key_pem", "");

    if (!identity.LoadFromPEM(pubPEM, privPEM)) {
        std::cout << "[IDENTITY] Generating new Ed25519 identity..." << std::endl;
        if (identity.Generate()) {
            config.Set("security", "public_key_pem", identity.GetPublicKeyPEM());
            config.Set("security", "private_key_pem", identity.GetPrivateKeyPEM());
            config.Save("config.ini");
            std::cout << "[IDENTITY] Identity saved to config.ini" << std::endl;
        }
    }

    std::cout << "[IDENTITY] Node ID: " << identity.GetShortID() << std::endl;

    // 4. Start Peer Manager
    // After identity is loaded:
    FreeAI::Network::PeerManager peerManager;
    peerManager.Initialize(config);
    peerManager.SetIdentity(&identity);  // NEW
    peerManager.SetSigningEnabled(config.GetBool("security", "enable_signing", true));  // NEW
    peerManager.Start();

    std::cout << "[MAIN] Running... Press Enter to stop." << std::endl;
    std::cin.get();

    // 5. Cleanup
    peerManager.Stop();
    FreeAI::Network::NetworkEnvironment::Shutdown();

    std::cout << "[OK] Shutdown Complete." << std::endl;
    return 0;
}