#pragma once
#include "inference/ModelConfig.hpp"
#include "inference/MemoryManager.hpp"
#include <string>
#include <vector>
#include <memory>

// Forward declare llama.cpp types
struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;

namespace FreeAI {
    namespace Inference {

        class LlamaContext {
        public:
            LlamaContext();
            ~LlamaContext();

            // Initialize with config and memory manager
            bool Initialize(const ModelConfig& config, MemoryManager* memory_mgr);

            // Unload model (free memory)
            void UnloadModel();

            // Check if model is loaded
            bool IsLoaded() const;

            // Generate response for a prompt (with memory integration)
            std::string Generate(const std::string& user_prompt);

            // Get model info
            std::string GetModelInfo() const;

            // Execute command from AI output
            bool ExecuteCommand(const std::string& command);

        private:
            // Tokenize text to tokens
            std::vector<int> Tokenize(const std::string& text) const;

            // Detokenize tokens to text
            std::string Detokenize(const std::vector<int>& tokens) const;

            // Internal generate (called by public Generate)
            std::string GenerateInternal(const std::string& full_prompt);

            llama_model* m_model;
            llama_context* m_ctx;
            llama_sampler* m_sampler;
            const llama_vocab* m_vocab;
            ModelConfig m_config;
            MemoryManager* m_memory_mgr;
            bool m_loaded;
        };

    }
}