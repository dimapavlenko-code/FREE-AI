#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>

namespace FreeAI {
    namespace Utils {

        struct ConfigEntry {
            std::string key;
            std::string value;
            std::string comment; // Comment before this key
        };

        struct ConfigSection {
            std::string name;
            std::string comment; // Comment before this section
            std::vector<ConfigEntry> entries; // Use vector to preserve order
        };

        class Config {
        public:
            Config() = default;

            // Load from file
            bool Load(const std::string& filename);

            // Save to file (preserves comments and multiline)
            bool Save(const std::string& filename) const;

            // Get value (with default)
            std::string Get(const std::string& section, const std::string& key, const std::string& defaultVal = "") const;
            int GetInt(const std::string& section, const std::string& key, int defaultVal = 0) const;
            bool GetBool(const std::string& section, const std::string& key, bool defaultVal = false) const;

            // Set value
            void Set(const std::string& section, const std::string& key, const std::string& value);
            void SetInt(const std::string& section, const std::string& key, int value);
            void SetBool(const std::string& section, const std::string& key, bool value);

            // Set comment for a key (for documentation)
            void SetComment(const std::string& section, const std::string& key, const std::string& comment);
            void SetSectionComment(const std::string& section, const std::string& comment);

            static std::string Trim(const std::string& str);

        private:
            std::vector<ConfigSection> m_sections; // Preserve order

            // Helpers            
            ConfigSection* FindSection(const std::string& name);
            const ConfigSection* FindSection(const std::string& name) const;
            ConfigEntry* FindEntry(ConfigSection& section, const std::string& key);
            const ConfigEntry* FindEntry(const ConfigSection& section, const std::string& key) const;
        };

    }
}