#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <memory>
#include <functional>
#include <variant>
#include <random>
#include <nlohmann/json.hpp>

namespace geweinet {

using json = nlohmann::json;

/**
 * 生成 UUID 字符串
 */
std::string generate_uuid();

/**
 * Agent 状态
 */
enum class AgentStatus {
    Idle,       // 空闲，等待任务
    Busy,       // 忙碌，正在处理任务
    Error,      // 错误状态
    Disabled    // 已禁用
};

std::string agent_status_to_string(AgentStatus status);
AgentStatus string_to_agent_status(const std::string& str);

/**
 * 消息类型
 */
enum class MessageType {
    Task,           // 任务消息
    Response,       // 响应消息
    Error,          // 错误消息
    Control,        // 控制消息
    Heartbeat,      // 心跳消息
    Broadcast       // 广播消息
};

std::string message_type_to_string(MessageType type);
MessageType string_to_message_type(const std::string& str);

/**
 * Agent 配置
 */
struct AgentConfig {
    std::string id;                     // Agent 唯一标识
    std::string name;                   // Agent 名称
    std::string description;            // Agent 描述
    std::string working_directory;      // 工作目录
    std::string prompt_file;            // Prompt 文件路径
    std::map<std::string, std::string> env_vars;  // 环境变量
    int timeout_seconds = 300;          // 超时时间
    int max_retries = 3;                // 最大重试次数
    bool enabled = true;                // 是否启用

    static AgentConfig from_json(const json& j);
    json to_json() const;
};

/**
 * Agent 实例
 */
class Agent {
public:
    Agent(const AgentConfig& config);
    ~Agent() = default;

    const std::string& id() const { return config_.id; }
    const std::string& name() const { return config_.name; }
    const AgentConfig& config() const { return config_; }
    AgentStatus status() const { return status_; }
    
    void set_status(AgentStatus status) { status_ = status; }
    void set_last_active(std::chrono::system_clock::time_point time) { last_active_ = time; }
    
    bool is_available() const { return status_ == AgentStatus::Idle && config_.enabled; }

private:
    AgentConfig config_;
    AgentStatus status_ = AgentStatus::Idle;
    std::chrono::system_clock::time_point last_active_;
};

/**
 * 消息
 */
struct Message {
    std::string id;                     // 消息 ID
    std::string from_agent;             // 发送者 Agent ID
    std::string to_agent;               // 接收者 Agent ID (空表示广播)
    MessageType type = MessageType::Task;
    std::string content;                // 消息内容
    std::optional<std::string> parent_id;  // 父消息 ID (用于响应链)
    std::map<std::string, std::string> metadata;  // 元数据
    std::chrono::system_clock::time_point timestamp;
    std::optional<std::string> error;   // 错误信息

    Message();
    static Message from_json(const json& j);
    json to_json() const;
};

/**
 * 任务状态
 */
enum class TaskStatus {
    Pending,    // 等待处理
    Running,    // 正在运行
    Completed,  // 已完成
    Failed,     // 失败
    Cancelled   // 已取消
};

std::string task_status_to_string(TaskStatus status);
TaskStatus string_to_task_status(const std::string& str);

/**
 * 任务
 */
struct Task {
    std::string id;                     // 任务 ID
    std::string agent_id;               // 负责的 Agent ID
    std::string input;                  // 输入内容
    std::optional<std::string> output;  // 输出内容
    TaskStatus status = TaskStatus::Pending;
    std::optional<std::string> error;   // 错误信息
    int retry_count = 0;                // 重试次数
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;

    static Task from_json(const json& j);
    json to_json() const;
};

/**
 * 平台配置
 */
struct PlatformConfig {
    std::string name = "Geweinet";
    std::string version = "1.0.0";
    std::string iflow_cli_path = "iflow";  // iFlow CLI 路径
    std::string log_level = "info";
    std::string log_file = "geweinet.log";
    int max_concurrent_tasks = 10;
    int task_timeout_seconds = 600;
    std::string ipc_socket_path = "/tmp/geweinet.sock";
    std::map<std::string, AgentConfig> agents;

    static PlatformConfig from_json(const json& j);
    json to_json() const;
};

/**
 * 回调函数类型
 */
using MessageCallback = std::function<void(const Message&)>;
using TaskCallback = std::function<void(const Task&)>;

} // namespace geweinet
