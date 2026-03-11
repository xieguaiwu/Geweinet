#include "geweinet/team_orchestrator.hpp"
#include "geweinet/logger.hpp"
#include <sstream>

namespace geweinet {

TeamOrchestrator& TeamOrchestrator::instance() {
    static TeamOrchestrator instance;
    return instance;
}

TeamOrchestrator::~TeamOrchestrator() {
    stop();
}

bool TeamOrchestrator::register_team(const TeamConfig& config) {
    if (config.id.empty()) {
        LOG_COMPONENT_ERROR("Cannot register team with empty ID", "TeamOrchestrator");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    teams_[config.id] = config;
    
    LOG_COMPONENT_INFO("Registered team: " + config.name + " (" + config.id + 
                       ") with " + std::to_string(config.members.size()) + " members", "TeamOrchestrator");
    return true;
}

bool TeamOrchestrator::unregister_team(const std::string& team_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = teams_.find(team_id);
    if (it == teams_.end()) {
        return false;
    }
    
    teams_.erase(it);
    LOG_COMPONENT_INFO("Unregistered team: " + team_id, "TeamOrchestrator");
    return true;
}

std::optional<TeamConfig> TeamOrchestrator::get_team(const std::string& team_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = teams_.find(team_id);
    if (it == teams_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<TeamConfig> TeamOrchestrator::get_all_teams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TeamConfig> result;
    for (const auto& [id, team] : teams_) {
        result.push_back(team);
    }
    return result;
}

size_t TeamOrchestrator::team_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return teams_.size();
}

std::string TeamOrchestrator::submit_collab_task(const std::string& team_id, const std::string& input) {
    // 检查团队是否存在
    auto team = get_team(team_id);
    if (!team) {
        LOG_COMPONENT_ERROR("Team not found: " + team_id, "TeamOrchestrator");
        return "";
    }
    
    CollaborationTask task;
    task.id = generate_uuid();
    task.team_id = team_id;
    task.original_input = input;
    task.status = CollabTaskStatus::Pending;
    task.created_at = std::chrono::system_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        collab_tasks_[task.id] = task;
        task_queue_.push(task.id);
    }
    
    cv_.notify_one();
    
    LOG_COMPONENT_INFO("Submitted collaboration task: " + task.id + " to team: " + team_id, "TeamOrchestrator");
    return task.id;
}

std::optional<CollaborationTask> TeamOrchestrator::get_collab_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = collab_tasks_.find(task_id);
    if (it == collab_tasks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<CollaborationTask> TeamOrchestrator::get_all_collab_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CollaborationTask> result;
    for (const auto& [id, task] : collab_tasks_) {
        result.push_back(task);
    }
    return result;
}

bool TeamOrchestrator::cancel_collab_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = collab_tasks_.find(task_id);
    if (it == collab_tasks_.end()) {
        return false;
    }
    
    it->second.status = CollabTaskStatus::Cancelled;
    LOG_COMPONENT_INFO("Cancelled collaboration task: " + task_id, "TeamOrchestrator");
    return true;
}

void TeamOrchestrator::set_complete_callback(CollabTaskCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    complete_callback_ = std::move(callback);
}

void TeamOrchestrator::start() {
    if (running_) return;
    
    running_ = true;
    worker_ = std::thread(&TeamOrchestrator::worker_thread, this);
    
    LOG_COMPONENT_INFO("TeamOrchestrator started", "TeamOrchestrator");
}

void TeamOrchestrator::stop() {
    if (!running_) return;
    
    running_ = false;
    cv_.notify_all();
    
    if (worker_.joinable()) {
        worker_.join();
    }
    
    LOG_COMPONENT_INFO("TeamOrchestrator stopped", "TeamOrchestrator");
}

void TeamOrchestrator::worker_thread() {
    while (running_) {
        std::string task_id;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !task_queue_.empty() || !running_; });
            
            if (!running_) break;
            
            if (task_queue_.empty()) continue;
            
            task_id = task_queue_.front();
            task_queue_.pop();
        }
        
        // 获取任务和团队配置
        auto task_opt = get_collab_task(task_id);
        if (!task_opt) continue;
        
        auto team_opt = get_team(task_opt->team_id);
        if (!team_opt) {
            std::lock_guard<std::mutex> lock(mutex_);
            collab_tasks_[task_id].status = CollabTaskStatus::Failed;
            collab_tasks_[task_id].error = "Team not found: " + task_opt->team_id;
            continue;
        }
        
        CollaborationTask task = *task_opt;
        TeamConfig team = *team_opt;
        
        // 执行协作任务
        execute_collab_task(task);
    }
}

void TeamOrchestrator::execute_collab_task(CollaborationTask& task) {
    auto team_opt = get_team(task.team_id);
    if (!team_opt) {
        task.status = CollabTaskStatus::Failed;
        task.error = "Team not found";
        return;
    }
    
    TeamConfig team = *team_opt;
    
    LOG_COMPONENT_INFO("Executing collaboration task: " + task.id + 
                       " with team: " + team.name, "TeamOrchestrator");
    
    // 阶段1: 任务分解
    task.status = CollabTaskStatus::Decomposing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        collab_tasks_[task.id] = task;
    }
    
    auto subtasks = decompose_task(task, team);
    if (subtasks.empty()) {
        // 如果分解失败，创建一个默认子任务
        SubTask default_task;
        default_task.id = generate_uuid();
        default_task.parent_task_id = task.id;
        default_task.description = "Execute original task";
        default_task.input = task.original_input;
        subtasks.push_back(default_task);
    }
    
    task.subtasks = subtasks;
    
    // 阶段2: 执行子任务
    task.status = CollabTaskStatus::Executing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        collab_tasks_[task.id] = task;
    }
    
    // 根据协调策略执行
    if (team.coordination_strategy == "sequential") {
        // 顺序执行
        for (auto& subtask : task.subtasks) {
            if (!running_) break;
            
            assign_subtask(subtask, team);
            execute_subtask(subtask, task);
            
            // 更新任务状态
            {
                std::lock_guard<std::mutex> lock(mutex_);
                collab_tasks_[task.id] = task;
            }
        }
    } else if (team.coordination_strategy == "parallel") {
        // 并行执行（简化版：按依赖关系执行）
        int max_iterations = 100;  // 防止无限循环
        while (!task.all_subtasks_done() && running_ && max_iterations-- > 0) {
            auto ready_subtasks = get_ready_subtasks(task);
            
            if (ready_subtasks.empty()) {
                // 没有可执行的子任务，可能是循环依赖或全部失败
                break;
            }
            
            // 执行所有就绪的子任务（可以并行，这里简化为顺序）
            for (auto* subtask : ready_subtasks) {
                if (subtask->status != SubTaskStatus::Running) {
                    assign_subtask(*subtask, team);
                    execute_subtask(*subtask, task);
                }
            }
            
            // 更新任务状态
            {
                std::lock_guard<std::mutex> lock(mutex_);
                collab_tasks_[task.id] = task;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        // hierarchical 或其他策略：默认使用顺序
        for (auto& subtask : task.subtasks) {
            if (!running_) break;
            
            assign_subtask(subtask, team);
            execute_subtask(subtask, task);
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                collab_tasks_[task.id] = task;
            }
        }
    }
    
    // 检查是否所有子任务都完成了
    if (task.failed_count() > task.subtasks.size() / 2) {
        task.status = CollabTaskStatus::Failed;
        task.error = "Too many subtasks failed";
    } else {
        // 阶段3: 汇总结果
        task.status = CollabTaskStatus::Aggregating;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            collab_tasks_[task.id] = task;
        }
        
        task.final_output = aggregate_results(task, team);
        task.status = CollabTaskStatus::Completed;
    }
    
    task.completed_at = std::chrono::system_clock::now();
    
    // 保存最终状态
    {
        std::lock_guard<std::mutex> lock(mutex_);
        collab_tasks_[task.id] = task;
    }
    
    LOG_COMPONENT_INFO("Collaboration task " + task.id + " completed with status: " + 
                       collab_task_status_to_string(task.status), "TeamOrchestrator");
    
    // 调用回调
    if (complete_callback_) {
        complete_callback_(task);
    }
}

std::vector<SubTask> TeamOrchestrator::decompose_task(CollaborationTask& task, const TeamConfig& team) {
    LOG_COMPONENT_DEBUG("Decomposing task " + task.id + " using leader: " + team.leader_id, "TeamOrchestrator");
    
    // 获取 leader agent
    auto leader = AgentManager::instance().get_agent(team.leader_id);
    if (!leader) {
        LOG_COMPONENT_WARNING("Leader agent not found: " + team.leader_id + 
                              ", using default decomposition", "TeamOrchestrator");
        
        // 默认分解：将任务分配给每个成员
        std::vector<SubTask> subtasks;
        for (const auto& member : team.members) {
            SubTask st;
            st.id = generate_uuid();
            st.parent_task_id = task.id;
            st.assigned_agent = member.agent_id;
            st.description = "Process part of: " + task.original_input.substr(0, 50);
            st.input = task.original_input;
            subtasks.push_back(st);
        }
        return subtasks;
    }
    
    // 构建分解提示词
    std::stringstream prompt;
    prompt << "You are the leader of a team. Your task is to decompose the following task into subtasks.\n\n";
    prompt << "Team Members:\n";
    for (const auto& member : team.members) {
        auto agent = AgentManager::instance().get_agent(member.agent_id);
        prompt << "- " << member.agent_id;
        if (agent) {
            prompt << " (" << agent->name() << ")";
        }
        prompt << " [Role: " << team_role_to_string(member.role);
        if (!member.specialty.empty()) {
            prompt << ", Specialty: " << member.specialty;
        }
        prompt << "]\n";
    }
    prompt << "\nOriginal Task:\n" << task.original_input << "\n\n";
    prompt << "Please decompose this task into subtasks. For each subtask, provide:\n";
    prompt << "1. A brief description\n";
    prompt << "2. Which team member should handle it (by agent_id)\n";
    prompt << "3. The specific input for that member\n";
    prompt << "4. Any dependencies on other subtasks (by description reference)\n\n";
    prompt << "Format your response as JSON array:\n";
    prompt << "[{\"description\": \"...\", \"agent_id\": \"...\", \"input\": \"...\", \"dependencies\": []}, ...]\n";
    
    // 调用 leader agent 进行分解
    IFlowResult result = IFlowClient::instance().execute(
        prompt.str(),
        leader->config().working_directory,
        leader->config().env_vars,
        leader->config().timeout_seconds * 1000
    );
    
    if (!result.success) {
        LOG_COMPONENT_WARNING("Leader decomposition failed: " + result.error + 
                              ", using default decomposition", "TeamOrchestrator");
        
        // 默认分解
        std::vector<SubTask> subtasks;
        for (const auto& member : team.members) {
            SubTask st;
            st.id = generate_uuid();
            st.parent_task_id = task.id;
            st.assigned_agent = member.agent_id;
            st.input = task.original_input;
            subtasks.push_back(st);
        }
        return subtasks;
    }
    
    // 解析分解结果
    std::vector<SubTask> subtasks;
    try {
        // 尝试从输出中提取 JSON
        std::string output = result.stdout_output;
        
        // 查找 JSON 数组
        size_t start = output.find('[');
        size_t end = output.rfind(']');
        
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string json_str = output.substr(start, end - start + 1);
            json subtask_array = json::parse(json_str);
            
            for (const auto& st_json : subtask_array) {
                SubTask st;
                st.id = generate_uuid();
                st.parent_task_id = task.id;
                st.description = st_json.value("description", "");
                st.assigned_agent = st_json.value("agent_id", "");
                st.input = st_json.value("input", "");
                st.status = SubTaskStatus::Pending;
                st.created_at = std::chrono::system_clock::now();
                
                // 解析依赖（这里暂时忽略，因为依赖的是描述而非 ID）
                
                if (!st.assigned_agent.empty()) {
                    subtasks.push_back(st);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_COMPONENT_WARNING("Failed to parse decomposition result: " + 
                              std::string(e.what()), "TeamOrchestrator");
    }
    
    // 如果解析失败，使用默认分解
    if (subtasks.empty()) {
        for (const auto& member : team.members) {
            SubTask st;
            st.id = generate_uuid();
            st.parent_task_id = task.id;
            st.assigned_agent = member.agent_id;
            st.input = task.original_input;
            subtasks.push_back(st);
        }
    }
    
    return subtasks;
}

void TeamOrchestrator::assign_subtask(SubTask& subtask, const TeamConfig& team) {
    // 如果已经分配了 agent，验证其存在
    if (!subtask.assigned_agent.empty()) {
        if (AgentManager::instance().has_agent(subtask.assigned_agent)) {
            subtask.status = SubTaskStatus::Assigned;
            return;
        }
    }
    
    // 否则，根据专业领域选择合适的 agent
    for (const auto& member : team.members) {
        if (AgentManager::instance().has_agent(member.agent_id)) {
            subtask.assigned_agent = member.agent_id;
            subtask.status = SubTaskStatus::Assigned;
            return;
        }
    }
    
    LOG_COMPONENT_ERROR("No available agent for subtask: " + subtask.id, "TeamOrchestrator");
    subtask.status = SubTaskStatus::Failed;
    subtask.error = "No available agent";
}

bool TeamOrchestrator::execute_subtask(SubTask& subtask, CollaborationTask& parent_task) {
    if (subtask.status == SubTaskStatus::Completed || 
        subtask.status == SubTaskStatus::Skipped) {
        return true;
    }
    
    if (subtask.status == SubTaskStatus::Failed) {
        return false;
    }
    
    auto agent = AgentManager::instance().get_agent(subtask.assigned_agent);
    if (!agent) {
        subtask.status = SubTaskStatus::Failed;
        subtask.error = "Agent not found: " + subtask.assigned_agent;
        return false;
    }
    
    LOG_COMPONENT_DEBUG("Executing subtask " + subtask.id + " with agent: " + 
                        subtask.assigned_agent, "TeamOrchestrator");
    
    subtask.status = SubTaskStatus::Running;
    subtask.started_at = std::chrono::system_clock::now();
    
    // 构建包含上下文的输入
    std::string input = subtask.input;
    
    // 如果有团队上下文，添加到输入
    if (!parent_task.context.empty()) {
        std::stringstream ss;
        ss << input << "\n\n[Team Context]\n";
        for (const auto& [key, value] : parent_task.context) {
            ss << key << ": " << value << "\n";
        }
        input = ss.str();
    }
    
    // 执行
    IFlowResult result = IFlowClient::instance().execute(
        input,
        agent->config().working_directory,
        agent->config().env_vars,
        agent->config().timeout_seconds * 1000
    );
    
    subtask.completed_at = std::chrono::system_clock::now();
    
    if (result.success) {
        subtask.status = SubTaskStatus::Completed;
        subtask.output = result.stdout_output;
        
        // 将结果添加到团队上下文
        parent_task.context["subtask_" + subtask.id.substr(0, 8)] = 
            result.stdout_output.substr(0, 500);  // 限制长度
        
        return true;
    } else {
        subtask.retry_count++;
        if (subtask.retry_count < agent->config().max_retries) {
            subtask.status = SubTaskStatus::Assigned;  // 重试
            return false;
        }
        
        subtask.status = SubTaskStatus::Failed;
        subtask.error = result.error.empty() ? result.stderr_output : result.error;
        return false;
    }
}

std::string TeamOrchestrator::aggregate_results(CollaborationTask& task, const TeamConfig& team) {
    LOG_COMPONENT_DEBUG("Aggregating results for task: " + task.id, "TeamOrchestrator");
    
    // 获取 leader agent
    auto leader = AgentManager::instance().get_agent(team.leader_id);
    if (!leader) {
        // 简单汇总：拼接所有输出
        std::stringstream ss;
        ss << "=== Task Results ===\n\n";
        ss << "Original Task: " << task.original_input << "\n\n";
        
        for (const auto& subtask : task.subtasks) {
            if (subtask.status == SubTaskStatus::Completed && subtask.output) {
                ss << "--- " << subtask.assigned_agent << " ---\n";
                ss << *subtask.output << "\n\n";
            }
        }
        
        return ss.str();
    }
    
    // 构建汇总提示词
    std::stringstream prompt;
    prompt << "You are the leader of a team. The team has completed a collaborative task.\n";
    prompt << "Please aggregate the results from all team members into a final output.\n\n";
    prompt << "Original Task:\n" << task.original_input << "\n\n";
    prompt << "=== Team Member Results ===\n\n";
    
    for (const auto& subtask : task.subtasks) {
        if (subtask.status == SubTaskStatus::Completed && subtask.output) {
            prompt << "--- " << subtask.assigned_agent << " ---\n";
            if (!subtask.description.empty()) {
                prompt << "Task: " << subtask.description << "\n";
            }
            prompt << *subtask.output << "\n\n";
        } else if (subtask.status == SubTaskStatus::Failed) {
            prompt << "--- " << subtask.assigned_agent << " (FAILED) ---\n";
            if (subtask.error) {
                prompt << "Error: " << *subtask.error << "\n\n";
            }
        }
    }
    
    prompt << "=== Instructions ===\n";
    prompt << "Please provide a coherent final output that:\n";
    prompt << "1. Synthesizes the contributions from all team members\n";
    prompt << "2. Addresses the original task completely\n";
    prompt << "3. Is well-formatted and easy to understand\n";
    
    // 调用 leader agent 进行汇总
    IFlowResult result = IFlowClient::instance().execute(
        prompt.str(),
        leader->config().working_directory,
        leader->config().env_vars,
        leader->config().timeout_seconds * 1000
    );
    
    if (result.success) {
        return result.stdout_output;
    }
    
    // 失败时返回简单汇总
    std::stringstream ss;
    ss << "=== Task Results ===\n\n";
    for (const auto& subtask : task.subtasks) {
        if (subtask.status == SubTaskStatus::Completed && subtask.output) {
            ss << "--- " << subtask.assigned_agent << " ---\n";
            ss << *subtask.output << "\n\n";
        }
    }
    return ss.str();
}

bool TeamOrchestrator::check_dependencies(const SubTask& subtask, const CollaborationTask& task) const {
    for (const auto& dep_id : subtask.dependencies) {
        for (const auto& st : task.subtasks) {
            if (st.id == dep_id || st.description.find(dep_id) != std::string::npos) {
                if (st.status != SubTaskStatus::Completed) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<SubTask*> TeamOrchestrator::get_ready_subtasks(CollaborationTask& task) {
    std::vector<SubTask*> ready;
    
    for (auto& subtask : task.subtasks) {
        if (subtask.status == SubTaskStatus::Pending || 
            subtask.status == SubTaskStatus::Assigned) {
            if (check_dependencies(subtask, task)) {
                ready.push_back(&subtask);
            }
        }
    }
    
    return ready;
}

std::string TeamOrchestrator::build_agent_message(
    const std::string& from_agent,
    const std::string& to_agent,
    const std::string& content,
    const CollaborationTask& task) {
    
    std::stringstream ss;
    ss << "[Message from " << from_agent << " to " << to_agent << "]\n";
    ss << "[Task: " << task.id.substr(0, 8) << "]\n\n";
    ss << content;
    return ss.str();
}

} // namespace geweinet
