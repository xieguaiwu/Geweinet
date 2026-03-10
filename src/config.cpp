#include "geweinet/config.hpp"
#include "geweinet/logger.hpp"
#include <fstream>
#include <cstdlib>

namespace geweinet {

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::load_from_file(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_COMPONENT_ERROR("Failed to open config file: " + path, "ConfigManager");
            return false;
        }
        
        json j;
        file >> j;
        config_ = PlatformConfig::from_json(j);
        
        LOG_COMPONENT_INFO("Loaded config from: " + path, "ConfigManager");
        return true;
    } catch (const std::exception& e) {
        LOG_COMPONENT_ERROR("Failed to parse config file: " + std::string(e.what()), "ConfigManager");
        return false;
    }
}

bool ConfigManager::save_to_file(const std::string& path) {
    try {
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_COMPONENT_ERROR("Failed to create config file: " + path, "ConfigManager");
            return false;
        }
        
        json j = config_.to_json();
        file << j.dump(4);
        
        LOG_COMPONENT_INFO("Saved config to: " + path, "ConfigManager");
        return true;
    } catch (const std::exception& e) {
        LOG_COMPONENT_ERROR("Failed to save config file: " + std::string(e.what()), "ConfigManager");
        return false;
    }
}

void ConfigManager::load_from_env() {
    // 从环境变量加载配置
    const char* iflow_path = std::getenv("GEWEINET_IFLOW_PATH");
    if (iflow_path) {
        config_.iflow_cli_path = iflow_path;
    }
    
    const char* log_level = std::getenv("GEWEINET_LOG_LEVEL");
    if (log_level) {
        config_.log_level = log_level;
    }
    
    const char* log_file = std::getenv("GEWEINET_LOG_FILE");
    if (log_file) {
        config_.log_file = log_file;
    }
    
    const char* max_tasks = std::getenv("GEWEINET_MAX_CONCURRENT");
    if (max_tasks) {
        config_.max_concurrent_tasks = std::stoi(max_tasks);
    }
    
    const char* socket_path = std::getenv("GEWEINET_IPC_SOCKET");
    if (socket_path) {
        config_.ipc_socket_path = socket_path;
    }
    
    LOG_COMPONENT_INFO("Loaded config from environment variables", "ConfigManager");
}

std::optional<AgentConfig> ConfigManager::get_agent_config(const std::string& agent_id) const {
    auto it = config_.agents.find(agent_id);
    if (it != config_.agents.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace geweinet
