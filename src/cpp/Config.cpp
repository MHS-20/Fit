#include "Config.hpp"
#include <ini.h>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <iostream> // Added for safety diagnostics

namespace fs = std::filesystem;

// Clean extraction utility to aggressively strip ALL spaces, tabs, and line breaks
static std::string clean_key_string(const std::string& str) {
    if (str.empty()) return "";
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    std::string out = str.substr(first, (last - first + 1));
    // Convert keys to lower-case to mimic Python configparser behavior safely
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return std::tolower(c); });
    return out;
}

static std::string clean_value_string(const std::string& str) {
    if (str.empty()) return "";
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::map<std::string, std::map<std::string, std::string>> fitconfig_read() {
    std::map<std::string, std::map<std::string, std::string>> conf;
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path home = std::getenv("HOME") ? std::getenv("HOME") : "";
    fs::path p1 = xdg ? fs::path(xdg) / "fit/config" : home / ".config/fit/config";
    fs::path p2 = home / ".fitconfig";
    
    auto handler = [](void* user, const char* section, const char* name, const char* value) -> int {
        auto* configuration = static_cast<std::map<std::string, std::map<std::string, std::string>>*>(user);
        
        std::string clean_section = clean_key_string(section ? section : "");
        std::string clean_name = clean_key_string(name ? name : "");
        std::string clean_value = clean_value_string(value ? value : "");
        
        if (!clean_section.empty() && !clean_name.empty()) {
            (*configuration)[clean_section][clean_name] = clean_value;
        }
        return 1;
    };
    
    for (auto& p : {p1, p2}) {
        if (fs::exists(p)) {
            ini_parse(p.string().c_str(), handler, &conf);
        }
    }
    return conf;
}

std::string fitconfig_user_get(const std::map<std::string, std::map<std::string, std::string>>& config) {
    // Debug helper: Let's dump out what maps actually exist in memory right now
    if (config.empty()) {
        std::cout << "[DEBUG] Configuration map is completely empty!\n";
    }
    for (auto const& [section, keys] : config) {
        std::cout << "[DEBUG] Found Section: [" << section << "]\n";
        for (auto const& [key, val] : keys) {
            std::cout << "        Key: '" << key << "' => Value: '" << val << "'\n";
        }
    }

    if (config.count("user") && config.at("user").count("name") && config.at("user").count("email")) {
        return config.at("user").at("name") + " <" + config.at("user").at("email") + ">";
    }
    return "";
}