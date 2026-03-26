#pragma once
#include "inference/LlamaContext.hpp"
#include "inference/MemoryManager.hpp"
#include <string>
#include <memory>

namespace FreeAI {
    namespace Inference {

        class InferenceEngine {
        public:
            InferenceEngine();
            ~InferenceEngine();

            // Initialize with config
            bool Initialize(const ModelConfig& config);

            // Shutdown and free resources
            void Shutdown();

            // Check if engine is ready
            bool IsReady() const;

            // Run inference on a prompt
            std::string Infer(const std::string& prompt);

            // Get engine status
            std::string GetStatus() const;

            // Get memory manager (for external access)
            MemoryManager* GetMemoryManager();

        private:
            std::unique_ptr<LlamaContext> m_context;
            std::unique_ptr<MemoryManager> m_memory_mgr;
            bool m_initialized;
        };

    }
}