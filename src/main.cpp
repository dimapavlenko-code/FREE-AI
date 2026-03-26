#include <iostream>
#include "network/NetworkInit.hpp"
#include "network/PeerManager.hpp"
#include "utils/Config.hpp"
#include "crypto/Identity.hpp"
#include "inference/InferenceEngine.hpp"
#include "inference/ModelConfig.hpp"

#include <Windows.h>
#include <lmcons.h>
#include <conio.h>
#pragma comment(lib, "advapi32.lib")

#define PROJECT_NAME "FREE-AI"
#define PROJECT_VERSION "0.1.0"

FreeAI::Inference::InferenceEngine g_inferenceEngine;

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
	//peerManager.Start();

	// In main(), after network initialization:
	std::cout << "[MAIN] Loading inference config..." << std::endl;
	FreeAI::Inference::ModelConfig modelConfig = config.GetInferenceConfig();
	g_inferenceEngine.Initialize(modelConfig);

	std::cout << "[MAIN] Running... Press x to stop, infer to talk with LLM, status to get LLAMA status, cancel to stop infer inference hamdling. " << std::endl;

	std::string input;
	// for Qwen: this is windows-specific, but needed only for debugging, so do not worry
	while (true) {

		if (input.empty()) {
			if (_kbhit()) {
				auto firstchar = (char)_getch();
				if (firstchar == 'x')
					break;
				
				std::cout << "\n\n>> " << firstchar;
				std::getline(std::cin, input);
				input = firstchar + input;
			}
			else {
				Sleep(500);
				static size_t prevnodecnt(0);
				auto curnodecnt = peerManager.GetDHTNodeCount();
				if (curnodecnt != prevnodecnt) {
					prevnodecnt = curnodecnt;
					std::cout << "[DHT] Known nodes: " << curnodecnt << std::endl;
				}
			}
		}
		else {

			// In the console command loop (where you handle 'x' to exit):
			if (input == "infer") { // ToDo: input must be read from console?
				std::cout << "[MAIN] Enter prompt (or 'cancel' to abort):" << std::endl;
				std::string prompt;
				std::getline(std::cin, prompt);

				if (prompt != "cancel") {
					std::cout << "[INFERENCE] Processing..." << std::endl;
					std::string response = g_inferenceEngine.Infer(prompt);
					std::cout << "[INFERENCE] Response: " << response << std::endl;
				}
				else
					input.clear();
			}
			else if (input == "status") {
				std::cout << g_inferenceEngine.GetStatus() << std::endl;
				input.clear();
			}
			else if (input == "/memory") {
				auto* mem = g_inferenceEngine.GetMemoryManager();
				if (mem) {
					std::cout << "Identity:\n" << mem->GetIdentity() << std::endl;
					std::cout << "Session:\n" << mem->GetSessionSummary() << std::endl;
					std::cout << "Archive files:\n";
					for (const auto& f : mem->GetArchiveIndex()) {
						std::cout << "  " << f << std::endl;
					}
				}
			}
			else {
				std::cout << "Incorrect input: " << input << std::endl;
				input.clear();
			}
		}


	}

	// 5. Cleanup
	peerManager.Stop();
	FreeAI::Network::NetworkEnvironment::Shutdown();

	std::cout << "[OK] Shutdown Complete." << std::endl;
	return 0;
}