#pragma once

#include "geweinet/types.hpp"
#include "geweinet/agent_manager.hpp"
#include <functional>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <future>

namespace geweinet {

/**
 * 消息路由器
 * 负责消息的分发和路由
 */
class Router {
public:
    using MessageHandler = std::function<void(const Message&)>;
    
    static Router& instance();
    
    /**
     * 启动路由器
     */
    void start();
    
    /**
     * 停止路由器
     */
    void stop();
    
    /**
     * 发送消息
     */
    bool send_message(const Message& message);
    
    /**
     * 发送消息并等待响应
     */
    std::optional<Message> send_and_wait(const Message& message, int timeout_ms = 30000);
    
    /**
     * 广播消息给所有 Agent
     */
    void broadcast(const Message& message);
    
    /**
     * 注册消息处理器
     */
    void register_handler(const std::string& agent_id, MessageHandler handler);
    
    /**
     * 注销消息处理器
     */
    void unregister_handler(const std::string& agent_id);
    
    /**
     * 注册默认处理器（处理未知目标的消息）
     */
    void set_default_handler(MessageHandler handler);
    
    /**
     * 处理消息队列
     */
    void process_messages();
    
    /**
     * 获取待处理消息数量
     */
    size_t pending_count() const;

private:
    Router() = default;
    ~Router();
    
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    
    void route_message(const Message& message);
    void worker_thread();
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, MessageHandler> handlers_;
    MessageHandler default_handler_;
    
    std::queue<Message> message_queue_;
    std::condition_variable cv_;
    std::thread worker_;
    bool running_ = false;
    
    // 响应等待机制
    std::unordered_map<std::string, std::promise<Message>> pending_responses_;
    std::unordered_map<std::string, Message> response_cache_;
};

} // namespace geweinet
