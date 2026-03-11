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
    std::string model;                  // 使用的模型 (如 glm-4, gpt-4 等)
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
 * Agent 在团队中的角色
 */
enum class TeamRole {
    Leader,     // 领导者：负责分解任务、协调、汇总结果
    Worker,     // 工作者：执行具体子任务
    Reviewer,   // 审查者：审查其他 agent 的输出
    Specialist  // 专家：处理特定类型的任务
};

std::string team_role_to_string(TeamRole role);
TeamRole string_to_team_role(const std::string& str);

/**
 * 团队成员配置
 */
struct TeamMember {
    std::string agent_id;       // Agent ID
    TeamRole role;              // 角色
    std::string specialty;      // 专业领域 (如 "code", "architecture", "testing")
    int priority = 0;           // 优先级 (用于任务分配)

    static TeamMember from_json(const json& j);
    json to_json() const;
};

/**
 * 团队配置
 */
struct TeamConfig {
    std::string id;                         // 团队 ID
    std::string name;                       // 团队名称
    std::string description;                // 团队描述
    std::string leader_id;                  // 领导者 Agent ID
    std::vector<TeamMember> members;        // 团队成员
    std::string coordination_strategy;      // 协调策略: "sequential", "parallel", "hierarchical"
    bool enabled = true;

    static TeamConfig from_json(const json& j);
    json to_json() const;
    
    // 获取所有成员的 agent ID
    std::vector<std::string> get_agent_ids() const;
    // 根据专业领域获取成员
    std::vector<std::string> get_agents_by_specialty(const std::string& specialty) const;
};

/**
 * 子任务状态
 */
enum class SubTaskStatus {
    Pending,        // 等待分配
    Assigned,       // 已分配给 agent
    Running,        // 正在执行
    Completed,      // 已完成
    Failed,         // 失败
    Skipped         // 跳过
};

std::string subtask_status_to_string(SubTaskStatus status);
SubTaskStatus string_to_subtask_status(const std::string& str);

/**
 * 子任务（协作任务的组成部分）
 */
struct SubTask {
    std::string id;                     // 子任务 ID
    std::string parent_task_id;         // 父任务 ID
    std::string assigned_agent;         // 被分配的 Agent ID
    std::string description;            // 子任务描述
    std::string input;                  // 输入内容
    std::optional<std::string> output;  // 输出内容
    SubTaskStatus status = SubTaskStatus::Pending;
    std::vector<std::string> dependencies;  // 依赖的其他子任务 ID
    std::optional<std::string> error;
    int retry_count = 0;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;

    static SubTask from_json(const json& j);
    json to_json() const;
};

/**
 * 协作任务状态
 */
enum class CollabTaskStatus {
    Pending,        // 等待分解
    Decomposing,    // 正在分解
    Executing,      // 正在执行
    Aggregating,    // 正在汇总结果
    Completed,      // 已完成
    Failed,         // 失败
    Cancelled       // 已取消
};

std::string collab_task_status_to_string(CollabTaskStatus status);
CollabTaskStatus string_to_collab_task_status(const std::string& str);

/**
 * 协作任务（多 agent 协作的任务）
 */
struct CollaborationTask {
    std::string id;                         // 任务 ID
    std::string team_id;                    // 执行团队 ID
    std::string original_input;             // 原始输入
    std::string decomposition_prompt;       // 分解提示词（给 leader）
    std::vector<SubTask> subtasks;          // 子任务列表
    std::optional<std::string> final_output;  // 最终输出
    CollabTaskStatus status = CollabTaskStatus::Pending;
    std::optional<std::string> error;
    std::map<std::string, std::string> context;  // 共享上下文
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point completed_at;

    static CollaborationTask from_json(const json& j);
    json to_json() const;
    
    // 获取所有子任务状态
    size_t completed_count() const;
    size_t failed_count() const;
    bool all_subtasks_done() const;
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
    std::map<std::string, TeamConfig> teams;  // 团队配置

    static PlatformConfig from_json(const json& j);
    json to_json() const;
};

/**
 * 回调函数类型
 */
using MessageCallback = std::function<void(const Message&)>;
using TaskCallback = std::function<void(const Task&)>;
using SubTaskCallback = std::function<void(const SubTask&)>;
using CollabTaskCallback = std::function<void(const CollaborationTask&)>;

} // namespace geweinet
