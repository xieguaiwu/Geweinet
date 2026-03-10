#pragma once

#include "geweinet/types.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>

namespace geweinet {

/**
 * Agent 管理器
 * 负责管理所有 Agent 的注册、状态和生命周期
 */
class AgentManager {
public:
    static AgentManager& instance();
    
    /**
     * 注册 Agent
     */
    bool register_agent(const AgentConfig& config);
    
    /**
     * 注销 Agent
     */
    bool unregister_agent(const std::string& agent_id);
    
    /**
     * 获取 Agent
     */
    std::shared_ptr<Agent> get_agent(const std::string& agent_id) const;
    
    /**
     * 获取所有 Agent
     */
    std::vector<std::shared_ptr<Agent>> get_all_agents() const;
    
    /**
     * 获取可用 Agent 列表
     */
    std::vector<std::shared_ptr<Agent>> get_available_agents() const;
    
    /**
     * 更新 Agent 状态
     */
    bool update_status(const std::string& agent_id, AgentStatus status);
    
    /**
     * 检查 Agent 是否存在
     */
    bool has_agent(const std::string& agent_id) const;
    
    /**
     * 获取 Agent 数量
     */
    size_t agent_count() const;
    
    /**
     * 清空所有 Agent
     */
    void clear();

private:
    AgentManager() = default;
    ~AgentManager() = default;
    
    AgentManager(const AgentManager&) = delete;
    AgentManager& operator=(const AgentManager&) = delete;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Agent>> agents_;
};

} // namespace geweinet
