#pragma once
#include <string>

namespace FreeAI {
    namespace Inference {

        struct ModelConfig {
            std::string model_path;           // Path to GGUF model file
            int32_t n_ctx = 2048;             // Context size (tokens)
            int32_t n_threads = 4;            // CPU threads for inference
            int32_t n_batch = 512;            // Batch size for prompt processing
            int32_t n_gpu_layers = 0;         // GPU layers to offload (0 = CPU, -1 = all)
            float temperature = 0.7f;         // Sampling temperature
            int32_t n_predict = 256;          // Max tokens to generate
            bool use_mmap = true;             // Memory-map model file
            bool use_mlock = false;           // Lock model in memory
            bool use_gpu = false;             // Enable GPU offloading

            // Memory allocation (percentages of n_ctx)
            int32_t identity_ratio = 25;      // Identity memory %
            int32_t session_ratio = 25;       // Session summary %
            int32_t conversation_ratio = 50;  // Raw conversation %

            // Archive settings
            int32_t max_archive_files = 100;  // Max archive files
            std::string archive_path;         // Archive directory

            // Thresholds (percentages)
            int32_t compression_threshold = 75;  // Compress at this %
            int32_t archive_warning_threshold = 80;  // Warn at this %

            // Default config for Qwen2.5-0.5B
            static ModelConfig Default() {
                ModelConfig cfg;
                cfg.model_path = "models/qwen2.5-0.5b-instruct-q8_0.gguf";
                cfg.n_ctx = 2048;
                cfg.n_threads = 4;
                cfg.n_batch = 512;
                cfg.n_gpu_layers = 0;
                cfg.temperature = 0.7f;
                cfg.n_predict = 256;
                cfg.use_mmap = true;
                cfg.use_mlock = false;
                cfg.use_gpu = false;
                cfg.identity_ratio = 25;
                cfg.session_ratio = 25;
                cfg.conversation_ratio = 50;
                cfg.max_archive_files = 100;
                cfg.archive_path = "models/archive";
                cfg.compression_threshold = 75;
                cfg.archive_warning_threshold = 80;
                return cfg;
            }
        };

    }
}