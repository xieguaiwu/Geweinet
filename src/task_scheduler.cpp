#include "geweinet/task_scheduler.hpp"
#include "geweinet/agent_manager.hpp"
#include "geweinet/logger.hpp"

namespace geweinet {

TaskScheduler& TaskScheduler::instance() {
    static TaskScheduler instance;
    return instance;
}

TaskScheduler::~TaskScheduler() {
    stop();
}

void TaskScheduler::start() {
    if (running_) return;
    
    running_ = true;
    
    // 创建工作线程
    for (size_t i = 0; i < max_concurrent_; ++i) {
        workers_.emplace_back(&TaskScheduler::worker_thread, this);
    }
    
    LOG_COMPONENT_INFO("TaskScheduler started with " + std::to_string(max_concurrent_) + " workers", "TaskScheduler");
}

void TaskScheduler::stop() {
    if (!running_) return;
    
    running_ = false;
    cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    
    LOG_COMPONENT_INFO("TaskScheduler stopped", "TaskScheduler");
}

std::string TaskScheduler::submit_task(const std::string& agent_id, const std::string& input) {
    Task task;
    task.id = generate_uuid();
    task.agent_id = agent_id;
    task.input = input;
    task.status = TaskStatus::Pending;
    task.created_at = std::chrono::system_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_[task.id] = task;
        task_queue_.push(task.id);
    }
    
    cv_.notify_one();
    
    LOG_COMPONENT_INFO("Task submitted: " + task.id + " for agent: " + agent_id, "TaskScheduler");
    return task.id;
}

bool TaskScheduler::cancel_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }
    
    if (it->second.status == TaskStatus::Running) {
        LOG_COMPONENT_WARNING("Cannot cancel running task: " + task_id, "TaskScheduler");
        return false;
    }
    
    it->second.status = TaskStatus::Cancelled;
    it->second.completed_at = std::chrono::system_clock::now();
    
    LOG_COMPONENT_INFO("Task cancelled: " + task_id, "TaskScheduler");
    return true;
}

std::optional<Task> TaskScheduler::get_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Task> TaskScheduler::get_all_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Task> result;
    result.reserve(tasks_.size());
    for (const auto& [id, task] : tasks_) {
        result.push_back(task);
    }
    return result;
}

std::vector<Task> TaskScheduler::get_agent_tasks(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Task> result;
    for (const auto& [id, task] : tasks_) {
        if (task.agent_id == agent_id) {
            result.push_back(task);
        }
    }
    return result;
}

void TaskScheduler::set_complete_callback(TaskCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    complete_callback_ = std::move(callback);
}

size_t TaskScheduler::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return task_queue_.size();
}

bool TaskScheduler::can_start_task() const {
    return running_count_ < max_concurrent_;
}

void TaskScheduler::worker_thread() {
    while (running_) {
        std::string task_id;
        Task task;
        bool got_task = false;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // 等待有任务或停止信号
            cv_.wait(lock, [this] { return !task_queue_.empty() || !running_; });
            
            if (!running_) break;
            
            if (task_queue_.empty()) continue;
            
            // 检查并发限制（原子操作，在锁内）
            if (running_count_ >= max_concurrent_) {
                // 等待有空闲槽位后再重试
                continue;
            }
            
            task_id = task_queue_.front();
            task_queue_.pop();
            
            auto it = tasks_.find(task_id);
            if (it == tasks_.end()) continue;
            
            task = it->second;
            
            // 检查任务状态
            if (task.status == TaskStatus::Cancelled) continue;
            
            // 原子地设置状态并增加计数
            task.status = TaskStatus::Running;
            task.started_at = std::chrono::system_clock::now();
            it->second = task;
            running_count_++;
            got_task = true;
        }
        
        if (!got_task) continue;
        
        // 执行任务（锁外执行，避免阻塞其他线程）
        execute_task(task);
        
        // 减少运行计数
        running_count_--;
    }
}

void TaskScheduler::execute_task(Task& task) {
    LOG_COMPONENT_INFO("Executing task: " + task.id, "TaskScheduler");
    
    // 获取 Agent 配置
    auto agent = AgentManager::instance().get_agent(task.agent_id);
    if (!agent) {
        task.status = TaskStatus::Failed;
        task.error = "Agent not found: " + task.agent_id;
        task.completed_at = std::chrono::system_clock::now();
        
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_[task.id] = task;
        
        LOG_COMPONENT_ERROR(task.error.value(), "TaskScheduler");
        return;
    }
    
    // 更新 Agent 状态
    AgentManager::instance().update_status(task.agent_id, AgentStatus::Busy);
    
    // 通过 iFlow 执行
    auto& iflow = IFlowClient::instance();
    auto result = iflow.execute_with_agent(task.input, agent->config());
    
    // 更新任务状态
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (result.success) {
            task.status = TaskStatus::Completed;
            task.output = result.stdout_output;
            LOG_COMPONENT_INFO("Task completed: " + task.id, "TaskScheduler");
        } else {
            task.retry_count++;
            
            if (task.retry_count < agent->config().max_retries) {
                // 重试
                task.status = TaskStatus::Pending;
                task_queue_.push(task.id);
                LOG_COMPONENT_WARNING("Task failed, retrying (" + 
                    std::to_string(task.retry_count) + "/" + 
                    std::to_string(agent->config().max_retries) + "): " + task.id, "TaskScheduler");
            } else {
                task.status = TaskStatus::Failed;
                task.error = result.error.empty() ? result.stderr_output : result.error;
                LOG_COMPONENT_ERROR("Task failed: " + task.id + " - " + task.error.value(), "TaskScheduler");
            }
        }
        
        task.completed_at = std::chrono::system_clock::now();
        tasks_[task.id] = task;
    }
    
    // 恢复 Agent 状态
    AgentManager::instance().update_status(task.agent_id, AgentStatus::Idle);
    
    // 调用完成回调
    TaskCompleteCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = complete_callback_;
    }
    
    if (callback && task.status != TaskStatus::Pending) {
        callback(task);
    }
}

} // namespace geweinet
