#include "inference/InferenceEngine.hpp"
#include <iostream>

namespace FreeAI {
    namespace Inference {

        InferenceEngine::InferenceEngine()
            : m_context(std::make_unique<LlamaContext>())
            , m_memory_mgr(std::make_unique<MemoryManager>())
            , m_initialized(false) {
        }

        InferenceEngine::~InferenceEngine() {
            Shutdown();
        }

        bool InferenceEngine::Initialize(const ModelConfig& config) {
            if (m_initialized) {
                std::cerr << "[INFERENCE] Engine already initialized!" << std::endl;
                return false;
            }

            std::cout << "[INFERENCE] Initializing inference engine..." << std::endl;

            // Initialize memory manager first
            if (!m_memory_mgr->Initialize(config)) {
                std::cerr << "[INFERENCE] Failed to initialize memory manager" << std::endl;
                return false;
            }

            // Initialize llama context with memory manager
            if (!m_context->Initialize(config, m_memory_mgr.get())) {
                std::cerr << "[INFERENCE] Failed to load model" << std::endl;
                return false;
            }

            m_initialized = true;
            std::cout << "[INFERENCE] Engine ready!" << std::endl;
            return true;
        }

        void InferenceEngine::Shutdown() {
            if (m_initialized) {
                std::cout << "[INFERENCE] Shutting down inference engine..." << std::endl;
                m_context->UnloadModel();
                m_initialized = false;
            }
        }

        bool InferenceEngine::IsReady() const {
            return m_initialized;
        }

        std::string InferenceEngine::Infer(const std::string& prompt) {
            if (!m_initialized) {
                return "Error: Inference engine not initialized";
            }
            return m_context->Generate(prompt);
        }

        std::string InferenceEngine::GetStatus() const {
            if (!m_initialized) {
                return "Inference engine: Not initialized";
            }

            auto status = m_memory_mgr->GetMemoryStatus();
            char buffer[512];
            snprintf(buffer, sizeof(buffer),
                "Inference engine: Ready\n"
                "  Identity: %d/%d tokens (%d%%)\n"
                "  Session: %d/%d tokens (%d%%)\n"
                "  Conversation: %d/%d tokens (%d%%)\n"
                "  Archive: %d/%d files",
                status.identity_tokens, status.identity_max,
                status.identity_tokens * 100 / std::max(1, status.identity_max),
                status.session_tokens, status.session_max,
                status.session_tokens * 100 / std::max(1, status.session_max),
                status.conversation_tokens, status.conversation_max,
                status.conversation_tokens * 100 / std::max(1, status.conversation_max),
                status.archive_files, status.archive_max);

            return std::string(buffer);
        }

        MemoryManager* InferenceEngine::GetMemoryManager() {
            return m_memory_mgr.get();
        }

    }
}