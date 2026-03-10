#pragma once

#include "geweinet/types.hpp"
#include <string>
#include <optional>

namespace geweinet {

/**
 * 配置管理器
 * 负责加载和管理平台配置
 */
class ConfigManager {
public:
    static ConfigManager& instance();
    
    /**
     * 从文件加载配置
     */
    bool load_from_file(const std::string& path);
    
    /**
     * 保存配置到文件
     */
    bool save_to_file(const std::string& path);
    
    /**
     * 从环境变量加载配置
     */
    void load_from_env();
    
    /**
     * 获取平台配置
     */
    const PlatformConfig& config() const { return config_; }
    PlatformConfig& config() { return config_; }
    
    /**
     * 获取特定 Agent 配置
     */
    std::optional<AgentConfig> get_agent_config(const std::string& agent_id) const;
    
    /**
     * 设置平台配置
     */
    void set_config(const PlatformConfig& config) { config_ = config; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    PlatformConfig config_;
};

} // namespace geweinet
