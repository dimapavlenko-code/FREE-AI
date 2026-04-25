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
#include <shellapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "advapi32.lib")
#define PROJECT_NAME "FREE-AI"
#define PROJECT_VERSION "0.1.0"

// Debug: start test instances
void StartTestInstances()
{
	// Get exe path
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::string exeDir = exePath;
	if (exeDir.find("x64-Debug\\freeai.exe") == std::string::npos)
		return;
	size_t lastSlash = exeDir.find_last_of("\\/");
	if (lastSlash != std::string::npos)
		exeDir = exeDir.substr(0, lastSlash);
	std::string projectRoot = exeDir;
	size_t pos = projectRoot.find("\\out\\build\\x64-Debug");
	if (pos != std::string::npos)
		projectRoot = projectRoot.substr(0, pos);
	std::string scriptPath = projectRoot + "\\start_test.cmd";
	if (GetFileAttributesA(scriptPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		printf("[WARN] start_test.cmd not found: %s\n", scriptPath.c_str());
		return;
	}
	printf("[INFO] Starting test instances: %s\n", scriptPath.c_str());
	SHELLEXECUTEINFOA sei = { 0 };
	std::string params = ("/c \"" + scriptPath + "\"");
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = "open";
	sei.lpFile = "cmd.exe";
	sei.lpParameters = params.c_str();
	sei.lpDirectory = projectRoot.c_str();
	sei.nShow = SW_NORMAL;
	ShellExecuteExA(&sei);
	Sleep(2000);
	printf("[INFO] Test instances started\n");
}


FreeAI::Inference::InferenceEngine g_inferenceEngine;

int main() {
	std::cout << "========================================" << std::endl;
	std::cout << " " << PROJECT_NAME << " v" << PROJECT_VERSION << std::endl;
	std::cout << "========================================" << std::endl;
	StartTestInstances();
	if (!FreeAI::Network::NetworkEnvironment::Initialize()) {
		std::cerr << "[ERR] Network init failed." << std::endl;
		return 1;
	}
	FreeAI::Utils::Config config;
	bool configLoaded = config.Load("config.ini");
	if (!configLoaded)
		std::cout << "[CONFIG] Creating default config.ini" << std::endl;
	FreeAI::Crypto::Identity identity;
	std::string pubPEM = config.Get("security", "public_key_pem", "");
	std::string privPEM = config.Get("security", "private_key_pem", "");
	if (!identity.LoadFromPEM(pubPEM, privPEM)) {
		std::cout << "[IDENTITY] Generating new Ed25519 identity..." << std::endl;
		if (identity.Generate()) {
			config.Set("security", "public_key_pem", identity.GetPublicKeyPEM());
			config.Set("security", "private_key_pem", identity.GetPrivateKeyPEM());
			config.Save("config.ini");
			std::cout << "[IDENTITY] Identity saved" << std::endl;
		}
	}
	std::cout << "[IDENTITY] Node ID: " << identity.GetShortID() << std::endl;
	FreeAI::Network::PeerManager peerManager;
	peerManager.Initialize(config);
	peerManager.SetIdentity(&identity);
	peerManager.SetSigningEnabled(config.GetBool("security", "enable_signing", true));
	peerManager.Start();
	std::cout << "[MAIN] Loading inference config..." << std::endl;
	FreeAI::Inference::ModelConfig modelConfig = config.GetInferenceConfig();
	g_inferenceEngine.Initialize(modelConfig);
	std::cout << "[MAIN] Running... Press x to stop, infer to talk with LLM, status for LLAMA, cancel to abort infer. " << std::endl;
	std::string input;
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
					std::cout << "[DHT] Nodes: " << curnodecnt << std::endl;
				}
			}
		}
		else {
			if (input == "infer") {
				std::cout << "[MAIN] Enter prompt (or 'cancel' to abort):" << std::endl;
				std::string prompt;
				std::getline(std::cin, prompt);
				if (prompt != "cancel") {
					std::cout << "[INF] Processing..." << std::endl;
					std::string response = g_inferenceEngine.Infer(prompt);
					std::cout << "[INF] Response: " << response << std::endl;
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
					for (const auto& f : mem->GetArchiveIndex())
						std::cout << "  " << f << std::endl;
				}
			}
			else {
				std::cout << "Incorrect input: " << input << std::endl;
				input.clear();
			}
		}
	}

	peerManager.Stop();
	FreeAI::Network::NetworkEnvironment::Shutdown();
	std::cout << "[OK] Shutdown Complete." << std::endl;
	return 0;
}