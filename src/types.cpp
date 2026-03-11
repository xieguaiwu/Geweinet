#include "geweinet/types.hpp"
#include <random>
#include <sstream>
#include <iomanip>

namespace geweinet {

// 生成 UUID v4 格式字符串
std::string generate_uuid() {
    // 使用 thread_local 保证线程安全
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_int_distribution<> dis(0, 15);
    thread_local std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    ss << std::hex;
    
    for (int i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";  // UUID v4
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);  // 8, 9, a, or b
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    
    return ss.str();
}

// Agent 状态转换
std::string agent_status_to_string(AgentStatus status) {
    switch (status) {
        case AgentStatus::Idle: return "idle";
        case AgentStatus::Busy: return "busy";
        case AgentStatus::Error: return "error";
        case AgentStatus::Disabled: return "disabled";
        default: return "unknown";
    }
}

AgentStatus string_to_agent_status(const std::string& str) {
    if (str == "idle") return AgentStatus::Idle;
    if (str == "busy") return AgentStatus::Busy;
    if (str == "error") return AgentStatus::Error;
    if (str == "disabled") return AgentStatus::Disabled;
    return AgentStatus::Idle;
}

// 消息类型转换
std::string message_type_to_string(MessageType type) {
    switch (type) {
        case MessageType::Task: return "task";
        case MessageType::Response: return "response";
        case MessageType::Error: return "error";
        case MessageType::Control: return "control";
        case MessageType::Heartbeat: return "heartbeat";
        case MessageType::Broadcast: return "broadcast";
        default: return "unknown";
    }
}

MessageType string_to_message_type(const std::string& str) {
    if (str == "task") return MessageType::Task;
    if (str == "response") return MessageType::Response;
    if (str == "error") return MessageType::Error;
    if (str == "control") return MessageType::Control;
    if (str == "heartbeat") return MessageType::Heartbeat;
    if (str == "broadcast") return MessageType::Broadcast;
    return MessageType::Task;
}

// AgentConfig 实现
AgentConfig AgentConfig::from_json(const json& j) {
    AgentConfig config;
    config.id = j.value("id", generate_uuid());
    config.name = j.value("name", "unnamed");
    config.description = j.value("description", "");
    config.working_directory = j.value("working_directory", ".");
    config.prompt_file = j.value("prompt_file", "");
    config.model = j.value("model", "");  // 可选，为空则使用默认模型
    config.timeout_seconds = j.value("timeout_seconds", 300);
    config.max_retries = j.value("max_retries", 3);
    config.enabled = j.value("enabled", true);
    
    if (j.contains("env_vars") && j["env_vars"].is_object()) {
        for (auto& [key, value] : j["env_vars"].items()) {
            config.env_vars[key] = value.get<std::string>();
        }
    }
    
    return config;
}

json AgentConfig::to_json() const {
    json j;
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["working_directory"] = working_directory;
    j["prompt_file"] = prompt_file;
    j["model"] = model;
    j["timeout_seconds"] = timeout_seconds;
    j["max_retries"] = max_retries;
    j["enabled"] = enabled;
    j["env_vars"] = env_vars;
    return j;
}

// Agent 实现
Agent::Agent(const AgentConfig& config) 
    : config_(config), 
      status_(config.enabled ? AgentStatus::Idle : AgentStatus::Disabled),
      last_active_(std::chrono::system_clock::now()) {}

// Message 实现
Message::Message() 
    : id(generate_uuid()),
      type(MessageType::Task),
      timestamp(std::chrono::system_clock::now()) {}

Message Message::from_json(const json& j) {
    Message msg;
    msg.id = j.value("id", generate_uuid());
    msg.from_agent = j.value("from_agent", "");
    msg.to_agent = j.value("to_agent", "");
    msg.type = string_to_message_type(j.value("type", "task"));
    msg.content = j.value("content", "");
    
    if (j.contains("parent_id") && !j["parent_id"].is_null()) {
        msg.parent_id = j["parent_id"].get<std::string>();
    }
    
    if (j.contains("metadata") && j["metadata"].is_object()) {
        for (auto& [key, value] : j["metadata"].items()) {
            msg.metadata[key] = value.get<std::string>();
        }
    }
    
    if (j.contains("error") && !j["error"].is_null()) {
        msg.error = j["error"].get<std::string>();
    }
    
    return msg;
}

json Message::to_json() const {
    json j;
    j["id"] = id;
    j["from_agent"] = from_agent;
    j["to_agent"] = to_agent;
    j["type"] = message_type_to_string(type);
    j["content"] = content;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    
    if (parent_id) {
        j["parent_id"] = *parent_id;
    }
    
    j["metadata"] = metadata;
    
    if (error) {
        j["error"] = *error;
    }
    
    return j;
}

// Task 状态转换
std::string task_status_to_string(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending: return "pending";
        case TaskStatus::Running: return "running";
        case TaskStatus::Completed: return "completed";
        case TaskStatus::Failed: return "failed";
        case TaskStatus::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

TaskStatus string_to_task_status(const std::string& str) {
    if (str == "pending") return TaskStatus::Pending;
    if (str == "running") return TaskStatus::Running;
    if (str == "completed") return TaskStatus::Completed;
    if (str == "failed") return TaskStatus::Failed;
    if (str == "cancelled") return TaskStatus::Cancelled;
    return TaskStatus::Pending;
}

// Task 实现
Task Task::from_json(const json& j) {
    Task task;
    task.id = j.value("id", generate_uuid());
    task.agent_id = j.value("agent_id", "");
    task.input = j.value("input", "");
    
    if (j.contains("output") && !j["output"].is_null()) {
        task.output = j["output"].get<std::string>();
    }
    
    task.status = string_to_task_status(j.value("status", "pending"));
    
    if (j.contains("error") && !j["error"].is_null()) {
        task.error = j["error"].get<std::string>();
    }
    
    task.retry_count = j.value("retry_count", 0);
    
    return task;
}

json Task::to_json() const {
    json j;
    j["id"] = id;
    j["agent_id"] = agent_id;
    j["input"] = input;
    j["status"] = task_status_to_string(status);
    j["retry_count"] = retry_count;
    
    if (output) {
        j["output"] = *output;
    }
    
    if (error) {
        j["error"] = *error;
    }
    
    return j;
}

// PlatformConfig 实现
PlatformConfig PlatformConfig::from_json(const json& j) {
    PlatformConfig config;
    config.name = j.value("name", "Geweinet");
    config.version = j.value("version", "1.0.0");
    config.iflow_cli_path = j.value("iflow_cli_path", "iflow");
    config.log_level = j.value("log_level", "info");
    config.log_file = j.value("log_file", "geweinet.log");
    config.max_concurrent_tasks = j.value("max_concurrent_tasks", 10);
    config.task_timeout_seconds = j.value("task_timeout_seconds", 600);
    config.ipc_socket_path = j.value("ipc_socket_path", "/tmp/geweinet.sock");
    
    if (j.contains("agents") && j["agents"].is_object()) {
        for (auto& [id, agent_j] : j["agents"].items()) {
            AgentConfig agent_config = AgentConfig::from_json(agent_j);
            agent_config.id = id;  // 使用 key 作为 ID
            config.agents[id] = agent_config;
        }
    }
    
    if (j.contains("teams") && j["teams"].is_object()) {
        for (auto& [id, team_j] : j["teams"].items()) {
            TeamConfig team_config = TeamConfig::from_json(team_j);
            team_config.id = id;  // 使用 key 作为 ID
            config.teams[id] = team_config;
        }
    }
    
    return config;
}

json PlatformConfig::to_json() const {
    json j;
    j["name"] = name;
    j["version"] = version;
    j["iflow_cli_path"] = iflow_cli_path;
    j["log_level"] = log_level;
    j["log_file"] = log_file;
    j["max_concurrent_tasks"] = max_concurrent_tasks;
    j["task_timeout_seconds"] = task_timeout_seconds;
    j["ipc_socket_path"] = ipc_socket_path;
    
    json agents_json;
    for (const auto& [id, agent] : agents) {
        agents_json[id] = agent.to_json();
    }
    j["agents"] = agents_json;
    
    json teams_json;
    for (const auto& [id, team] : teams) {
        teams_json[id] = team.to_json();
    }
    j["teams"] = teams_json;
    
    return j;
}

// TeamRole 转换
std::string team_role_to_string(TeamRole role) {
    switch (role) {
        case TeamRole::Leader: return "leader";
        case TeamRole::Worker: return "worker";
        case TeamRole::Reviewer: return "reviewer";
        case TeamRole::Specialist: return "specialist";
        default: return "worker";
    }
}

TeamRole string_to_team_role(const std::string& str) {
    if (str == "leader") return TeamRole::Leader;
    if (str == "reviewer") return TeamRole::Reviewer;
    if (str == "specialist") return TeamRole::Specialist;
    return TeamRole::Worker;
}

// TeamMember 实现
TeamMember TeamMember::from_json(const json& j) {
    TeamMember member;
    member.agent_id = j.value("agent_id", "");
    member.role = string_to_team_role(j.value("role", "worker"));
    member.specialty = j.value("specialty", "");
    member.priority = j.value("priority", 0);
    return member;
}

json TeamMember::to_json() const {
    json j;
    j["agent_id"] = agent_id;
    j["role"] = team_role_to_string(role);
    j["specialty"] = specialty;
    j["priority"] = priority;
    return j;
}

// TeamConfig 实现
TeamConfig TeamConfig::from_json(const json& j) {
    TeamConfig config;
    config.id = j.value("id", generate_uuid());
    config.name = j.value("name", "unnamed team");
    config.description = j.value("description", "");
    config.leader_id = j.value("leader_id", "");
    config.coordination_strategy = j.value("coordination_strategy", "sequential");
    config.enabled = j.value("enabled", true);
    
    if (j.contains("members") && j["members"].is_array()) {
        for (const auto& member_j : j["members"]) {
            config.members.push_back(TeamMember::from_json(member_j));
        }
    }
    
    return config;
}

json TeamConfig::to_json() const {
    json j;
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["leader_id"] = leader_id;
    j["coordination_strategy"] = coordination_strategy;
    j["enabled"] = enabled;
    
    json members_array = json::array();
    for (const auto& member : members) {
        members_array.push_back(member.to_json());
    }
    j["members"] = members_array;
    
    return j;
}

std::vector<std::string> TeamConfig::get_agent_ids() const {
    std::vector<std::string> ids;
    for (const auto& member : members) {
        ids.push_back(member.agent_id);
    }
    return ids;
}

std::vector<std::string> TeamConfig::get_agents_by_specialty(const std::string& specialty) const {
    std::vector<std::string> ids;
    for (const auto& member : members) {
        if (member.specialty == specialty) {
            ids.push_back(member.agent_id);
        }
    }
    return ids;
}

// SubTaskStatus 转换
std::string subtask_status_to_string(SubTaskStatus status) {
    switch (status) {
        case SubTaskStatus::Pending: return "pending";
        case SubTaskStatus::Assigned: return "assigned";
        case SubTaskStatus::Running: return "running";
        case SubTaskStatus::Completed: return "completed";
        case SubTaskStatus::Failed: return "failed";
        case SubTaskStatus::Skipped: return "skipped";
        default: return "unknown";
    }
}

SubTaskStatus string_to_subtask_status(const std::string& str) {
    if (str == "assigned") return SubTaskStatus::Assigned;
    if (str == "running") return SubTaskStatus::Running;
    if (str == "completed") return SubTaskStatus::Completed;
    if (str == "failed") return SubTaskStatus::Failed;
    if (str == "skipped") return SubTaskStatus::Skipped;
    return SubTaskStatus::Pending;
}

// SubTask 实现
SubTask SubTask::from_json(const json& j) {
    SubTask task;
    task.id = j.value("id", generate_uuid());
    task.parent_task_id = j.value("parent_task_id", "");
    task.assigned_agent = j.value("assigned_agent", "");
    task.description = j.value("description", "");
    task.input = j.value("input", "");
    
    if (j.contains("output") && !j["output"].is_null()) {
        task.output = j["output"].get<std::string>();
    }
    
    task.status = string_to_subtask_status(j.value("status", "pending"));
    
    if (j.contains("dependencies") && j["dependencies"].is_array()) {
        for (const auto& dep : j["dependencies"]) {
            task.dependencies.push_back(dep.get<std::string>());
        }
    }
    
    if (j.contains("error") && !j["error"].is_null()) {
        task.error = j["error"].get<std::string>();
    }
    
    task.retry_count = j.value("retry_count", 0);
    
    return task;
}

json SubTask::to_json() const {
    json j;
    j["id"] = id;
    j["parent_task_id"] = parent_task_id;
    j["assigned_agent"] = assigned_agent;
    j["description"] = description;
    j["input"] = input;
    j["status"] = subtask_status_to_string(status);
    j["dependencies"] = dependencies;
    j["retry_count"] = retry_count;
    
    if (output) {
        j["output"] = *output;
    }
    if (error) {
        j["error"] = *error;
    }
    
    return j;
}

// CollabTaskStatus 转换
std::string collab_task_status_to_string(CollabTaskStatus status) {
    switch (status) {
        case CollabTaskStatus::Pending: return "pending";
        case CollabTaskStatus::Decomposing: return "decomposing";
        case CollabTaskStatus::Executing: return "executing";
        case CollabTaskStatus::Aggregating: return "aggregating";
        case CollabTaskStatus::Completed: return "completed";
        case CollabTaskStatus::Failed: return "failed";
        case CollabTaskStatus::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

CollabTaskStatus string_to_collab_task_status(const std::string& str) {
    if (str == "decomposing") return CollabTaskStatus::Decomposing;
    if (str == "executing") return CollabTaskStatus::Executing;
    if (str == "aggregating") return CollabTaskStatus::Aggregating;
    if (str == "completed") return CollabTaskStatus::Completed;
    if (str == "failed") return CollabTaskStatus::Failed;
    if (str == "cancelled") return CollabTaskStatus::Cancelled;
    return CollabTaskStatus::Pending;
}

// CollaborationTask 实现
CollaborationTask CollaborationTask::from_json(const json& j) {
    CollaborationTask task;
    task.id = j.value("id", generate_uuid());
    task.team_id = j.value("team_id", "");
    task.original_input = j.value("original_input", "");
    task.decomposition_prompt = j.value("decomposition_prompt", "");
    
    if (j.contains("subtasks") && j["subtasks"].is_array()) {
        for (const auto& st : j["subtasks"]) {
            task.subtasks.push_back(SubTask::from_json(st));
        }
    }
    
    if (j.contains("final_output") && !j["final_output"].is_null()) {
        task.final_output = j["final_output"].get<std::string>();
    }
    
    task.status = string_to_collab_task_status(j.value("status", "pending"));
    
    if (j.contains("error") && !j["error"].is_null()) {
        task.error = j["error"].get<std::string>();
    }
    
    if (j.contains("context") && j["context"].is_object()) {
        for (auto& [key, value] : j["context"].items()) {
            task.context[key] = value.get<std::string>();
        }
    }
    
    return task;
}

json CollaborationTask::to_json() const {
    json j;
    j["id"] = id;
    j["team_id"] = team_id;
    j["original_input"] = original_input;
    j["decomposition_prompt"] = decomposition_prompt;
    j["status"] = collab_task_status_to_string(status);
    j["context"] = context;
    
    json subtasks_array = json::array();
    for (const auto& st : subtasks) {
        subtasks_array.push_back(st.to_json());
    }
    j["subtasks"] = subtasks_array;
    
    if (final_output) {
        j["final_output"] = *final_output;
    }
    if (error) {
        j["error"] = *error;
    }
    
    return j;
}

size_t CollaborationTask::completed_count() const {
    size_t count = 0;
    for (const auto& st : subtasks) {
        if (st.status == SubTaskStatus::Completed) count++;
    }
    return count;
}

size_t CollaborationTask::failed_count() const {
    size_t count = 0;
    for (const auto& st : subtasks) {
        if (st.status == SubTaskStatus::Failed) count++;
    }
    return count;
}

bool CollaborationTask::all_subtasks_done() const {
    for (const auto& st : subtasks) {
        if (st.status != SubTaskStatus::Completed && 
            st.status != SubTaskStatus::Failed &&
            st.status != SubTaskStatus::Skipped) {
            return false;
        }
    }
    return true;
}

} // namespace geweinet
