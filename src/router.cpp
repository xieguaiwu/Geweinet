#include "geweinet/router.hpp"
#include "geweinet/logger.hpp"
#include <future>

namespace geweinet {

Router& Router::instance() {
    static Router instance;
    return instance;
}

Router::~Router() {
    stop();
}

void Router::start() {
    if (running_) return;
    
    running_ = true;
    worker_ = std::thread(&Router::worker_thread, this);
    LOG_COMPONENT_INFO("Router started", "Router");
}

void Router::stop() {
    if (!running_) return;
    
    running_ = false;
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    LOG_COMPONENT_INFO("Router stopped", "Router");
}

bool Router::send_message(const Message& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        message_queue_.push(message);
    }
    cv_.notify_one();
    
    LOG_COMPONENT_DEBUG("Message queued: " + message.id + " to " + message.to_agent, "Router");
    return true;
}

std::optional<Message> Router::send_and_wait(const Message& message, int timeout_ms) {
    // 创建 promise 用于等待响应
    std::promise<Message> promise;
    auto future = promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_responses_[message.id] = std::move(promise);
        message_queue_.push(message);
    }
    cv_.notify_one();
    
    // 等待响应
    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
    if (status == std::future_status::timeout) {
        LOG_COMPONENT_WARNING("Message timeout: " + message.id, "Router");
        std::lock_guard<std::mutex> lock(mutex_);
        pending_responses_.erase(message.id);
        return std::nullopt;
    }
    
    return future.get();
}

void Router::broadcast(const Message& message) {
    auto agents = AgentManager::instance().get_all_agents();
    
    for (const auto& agent : agents) {
        Message broadcast_msg = message;
        broadcast_msg.id = generate_uuid();
        broadcast_msg.to_agent = agent->id();
        broadcast_msg.type = MessageType::Broadcast;
        
        send_message(broadcast_msg);
    }
    
    LOG_COMPONENT_DEBUG("Broadcast message to " + std::to_string(agents.size()) + " agents", "Router");
}

void Router::register_handler(const std::string& agent_id, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[agent_id] = std::move(handler);
    LOG_COMPONENT_DEBUG("Handler registered for agent: " + agent_id, "Router");
}

void Router::unregister_handler(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(agent_id);
    LOG_COMPONENT_DEBUG("Handler unregistered for agent: " + agent_id, "Router");
}

void Router::set_default_handler(MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    default_handler_ = std::move(handler);
}

void Router::process_messages() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    while (!message_queue_.empty()) {
        Message message = message_queue_.front();
        message_queue_.pop();
        
        lock.unlock();
        route_message(message);
        lock.lock();
    }
}

size_t Router::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return message_queue_.size();
}

void Router::worker_thread() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        cv_.wait(lock, [this] { return !message_queue_.empty() || !running_; });
        
        if (!running_) break;
        
        while (!message_queue_.empty()) {
            Message message = message_queue_.front();
            message_queue_.pop();
            
            lock.unlock();
            route_message(message);
            lock.lock();
        }
    }
}

void Router::route_message(const Message& message) {
    // 检查是否是响应消息
    if (message.type == MessageType::Response && message.parent_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_responses_.find(*message.parent_id);
        if (it != pending_responses_.end()) {
            it->second.set_value(message);
            pending_responses_.erase(it);
            return;
        }
    }
    
    // 查找目标处理器
    MessageHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(message.to_agent);
        if (it != handlers_.end()) {
            handler = it->second;
        } else if (default_handler_) {
            handler = default_handler_;
        }
    }
    
    if (handler) {
        try {
            handler(message);
            LOG_COMPONENT_DEBUG("Message routed to: " + message.to_agent, "Router");
        } catch (const std::exception& e) {
            LOG_COMPONENT_ERROR("Handler error for " + message.to_agent + ": " + e.what(), "Router");
        }
    } else {
        LOG_COMPONENT_WARNING("No handler for agent: " + message.to_agent, "Router");
    }
}

} // namespace geweinet
