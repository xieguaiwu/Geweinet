#pragma once

#include "geweinet/types.hpp"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

namespace geweinet {

/**
 * IPC 服务器
 * 提供 Unix Socket 通信接口
 */
class IPCServer {
public:
    using MessageHandler = std::function<std::string(const std::string&)>;
    
    static IPCServer& instance();
    
    /**
     * 启动服务器
     * @param socket_path Unix Socket 路径
     */
    bool start(const std::string& socket_path = "/tmp/geweinet.sock");
    
    /**
     * 停止服务器
     */
    void stop();
    
    /**
     * 设置消息处理器
     */
    void set_handler(MessageHandler handler);
    
    /**
     * 检查是否正在运行
     */
    bool is_running() const { return running_; }
    
    /**
     * 获取 Socket 路径
     */
    std::string socket_path() const { return socket_path_; }

private:
    IPCServer() = default;
    ~IPCServer();
    
    IPCServer(const IPCServer&) = delete;
    IPCServer& operator=(const IPCServer&) = delete;
    
    void accept_loop();
    void handle_client(int client_fd);
    
    std::string socket_path_;
    int server_fd_ = -1;
    std::atomic<bool> running_{false};
    MessageHandler handler_;
    std::unique_ptr<std::thread> accept_thread_;
};

/**
 * IPC 客户端
 */
class IPCClient {
public:
    /**
     * 连接到服务器
     */
    bool connect(const std::string& socket_path = "/tmp/geweinet.sock");
    
    /**
     * 断开连接
     */
    void disconnect();
    
    /**
     * 发送消息并接收响应
     */
    std::optional<std::string> send(const std::string& message);
    
    /**
     * 检查是否已连接
     */
    bool is_connected() const { return connected_; }

private:
    int socket_fd_ = -1;
    bool connected_ = false;
};

} // namespace geweinet
