#pragma once

#include "geweinet/types.hpp"
#include "geweinet/iflow_client.hpp"
#include <queue>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>

namespace geweinet {

/**
 * 任务调度器
 * 负责任务的调度、执行和监控
 */
class TaskScheduler {
public:
    using TaskCompleteCallback = std::function<void(const Task&)>;
    
    static TaskScheduler& instance();
    
    /**
     * 启动调度器
     */
    void start();
    
    /**
     * 停止调度器
     */
    void stop();
    
    /**
     * 提交任务
     */
    std::string submit_task(const std::string& agent_id, const std::string& input);
    
    /**
     * 取消任务
     */
    bool cancel_task(const std::string& task_id);
    
    /**
     * 获取任务状态
     */
    std::optional<Task> get_task(const std::string& task_id) const;
    
    /**
     * 获取所有任务
     */
    std::vector<Task> get_all_tasks() const;
    
    /**
     * 获取指定 Agent 的任务
     */
    std::vector<Task> get_agent_tasks(const std::string& agent_id) const;
    
    /**
     * 设置任务完成回调
     */
    void set_complete_callback(TaskCompleteCallback callback);
    
    /**
     * 获取正在运行的任务数
     */
    size_t running_count() const { return running_count_; }
    
    /**
     * 获取等待中的任务数
     */
    size_t pending_count() const;
    
    /**
     * 设置最大并发任务数
     */
    void set_max_concurrent(size_t max) { max_concurrent_ = max; }

private:
    TaskScheduler() = default;
    ~TaskScheduler();
    
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;
    
    void worker_thread();
    void execute_task(Task& task);
    bool can_start_task() const;
    
    mutable std::mutex mutex_;
    std::queue<std::string> task_queue_;  // 任务 ID 队列
    std::unordered_map<std::string, Task> tasks_;
    
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> running_count_{0};
    size_t max_concurrent_ = 10;
    
    TaskCompleteCallback complete_callback_;
};

} // namespace geweinet
