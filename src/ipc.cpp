#include "geweinet/ipc.hpp"
#include "geweinet/logger.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <thread>

namespace geweinet {

// IPCServer 实现
IPCServer& IPCServer::instance() {
    static IPCServer instance;
    return instance;
}

IPCServer::~IPCServer() {
    stop();
}

bool IPCServer::start(const std::string& socket_path) {
    if (running_) return true;
    
    socket_path_ = socket_path;
    
    // 创建 Socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        LOG_COMPONENT_ERROR("Failed to create socket: " + std::string(strerror(errno)), "IPCServer");
        return false;
    }
    
    // 绑定地址
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';  // 确保null终止
    
    // 删除已存在的 socket 文件
    unlink(socket_path.c_str());
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_COMPONENT_ERROR("Failed to bind socket: " + std::string(strerror(errno)), "IPCServer");
        close(server_fd_);
        return false;
    }
    
    // 监听
    if (listen(server_fd_, 5) < 0) {
        LOG_COMPONENT_ERROR("Failed to listen: " + std::string(strerror(errno)), "IPCServer");
        close(server_fd_);
        unlink(socket_path.c_str());
        return false;
    }
    
    running_ = true;
    accept_thread_ = std::make_unique<std::thread>(&IPCServer::accept_loop, this);
    
    LOG_COMPONENT_INFO("IPC Server started at: " + socket_path, "IPCServer");
    return true;
}

void IPCServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }
    
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }
    
    LOG_COMPONENT_INFO("IPC Server stopped", "IPCServer");
}

void IPCServer::set_handler(MessageHandler handler) {
    handler_ = std::move(handler);
}

void IPCServer::accept_loop() {
    while (running_) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                LOG_COMPONENT_ERROR("Failed to accept connection: " + std::string(strerror(errno)), "IPCServer");
            }
            continue;
        }
        
        // 在新线程中处理客户端
        std::thread(&IPCServer::handle_client, this, client_fd).detach();
    }
}

void IPCServer::handle_client(int client_fd) {
    char buffer[65536];
    std::string request;
    
    // 读取请求
    while (true) {
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) break;
        
        buffer[n] = '\0';
        request += buffer;
        
        // 检查是否接收完整（以换行符结束）
        if (request.back() == '\n') {
            request.pop_back();
            break;
        }
    }
    
    LOG_COMPONENT_DEBUG("Received IPC request: " + request.substr(0, 200), "IPCServer");
    
    // 处理请求
    std::string response;
    if (handler_) {
        response = handler_(request);
    } else {
        response = R"({"error": "No handler registered"})";
    }
    
    // 发送响应
    response += '\n';
    write(client_fd, response.c_str(), response.size());
    
    close(client_fd);
}

// IPCClient 实现
bool IPCClient::connect(const std::string& socket_path) {
    if (connected_) return true;
    
    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';  // 确保null终止
    
        if (::connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    connected_ = true;
    return true;
}

void IPCClient::disconnect() {
    if (connected_) {
        close(socket_fd_);
        socket_fd_ = -1;
        connected_ = false;
    }
}

std::optional<std::string> IPCClient::send(const std::string& message) {
    if (!connected_) return std::nullopt;
    
    // 发送消息
    std::string msg = message + '\n';
    if (write(socket_fd_, msg.c_str(), msg.size()) < 0) {
        disconnect();
        return std::nullopt;
    }
    
    // 接收响应
    char buffer[65536];
    std::string response;
    
    while (true) {
        ssize_t n = read(socket_fd_, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            disconnect();
            return std::nullopt;
        }
        
        buffer[n] = '\0';
        response += buffer;
        
        if (response.back() == '\n') {
            response.pop_back();
            break;
        }
    }
    
    return response;
}

} // namespace geweinet
