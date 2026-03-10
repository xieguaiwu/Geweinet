#include "geweinet/agent_manager.hpp"
#include "geweinet/logger.hpp"

namespace geweinet {

AgentManager& AgentManager::instance() {
    static AgentManager instance;
    return instance;
}

bool AgentManager::register_agent(const AgentConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (agents_.find(config.id) != agents_.end()) {
        LOG_COMPONENT_WARNING("Agent already registered: " + config.id, "AgentManager");
        return false;
    }
    
    auto agent = std::make_shared<Agent>(config);
    agents_[config.id] = agent;
    
    LOG_COMPONENT_INFO("Registered agent: " + config.id + " (" + config.name + ")", "AgentManager");
    return true;
}

bool AgentManager::unregister_agent(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = agents_.find(agent_id);
    if (it == agents_.end()) {
        LOG_COMPONENT_WARNING("Agent not found: " + agent_id, "AgentManager");
        return false;
    }
    
    agents_.erase(it);
    LOG_COMPONENT_INFO("Unregistered agent: " + agent_id, "AgentManager");
    return true;
}

std::shared_ptr<Agent> AgentManager::get_agent(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Agent>> AgentManager::get_all_agents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<Agent>> result;
    result.reserve(agents_.size());
    for (const auto& [id, agent] : agents_) {
        result.push_back(agent);
    }
    return result;
}

std::vector<std::shared_ptr<Agent>> AgentManager::get_available_agents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<Agent>> result;
    for (const auto& [id, agent] : agents_) {
        if (agent->is_available()) {
            result.push_back(agent);
        }
    }
    return result;
}

bool AgentManager::update_status(const std::string& agent_id, AgentStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = agents_.find(agent_id);
    if (it == agents_.end()) {
        return false;
    }
    
    it->second->set_status(status);
    it->second->set_last_active(std::chrono::system_clock::now());
    
    LOG_COMPONENT_DEBUG("Agent " + agent_id + " status changed to: " + agent_status_to_string(status), "AgentManager");
    return true;
}

bool AgentManager::has_agent(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return agents_.find(agent_id) != agents_.end();
}

size_t AgentManager::agent_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return agents_.size();
}

void AgentManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    agents_.clear();
    LOG_COMPONENT_INFO("All agents cleared", "AgentManager");
}

} // namespace geweinet
