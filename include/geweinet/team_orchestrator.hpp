#pragma once

#include "geweinet/types.hpp"
#include "geweinet/agent_manager.hpp"
#include "geweinet/iflow_client.hpp"
#include "geweinet/router.hpp"
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <queue>

namespace geweinet {

/**
 * 团队协调器
 * 负责多 agent 协作任务的管理和执行
 */
class TeamOrchestrator {
public:
    static TeamOrchestrator& instance();
    
    /**
     * 注册团队
     */
    bool register_team(const TeamConfig& config);
    
    /**
     * 注销团队
     */
    bool unregister_team(const std::string& team_id);
    
    /**
     * 获取团队配置
     */
    std::optional<TeamConfig> get_team(const std::string& team_id) const;
    
    /**
     * 获取所有团队
     */
    std::vector<TeamConfig> get_all_teams() const;
    
    /**
     * 团队数量
     */
    size_t team_count() const;
    
    /**
     * 提交协作任务
     * @param team_id 团队 ID
     * @param input 原始输入
     * @return 任务 ID
     */
    std::string submit_collab_task(const std::string& team_id, const std::string& input);
    
    /**
     * 获取协作任务状态
     */
    std::optional<CollaborationTask> get_collab_task(const std::string& task_id) const;
    
    /**
     * 获取所有协作任务
     */
    std::vector<CollaborationTask> get_all_collab_tasks() const;
    
    /**
     * 取消协作任务
     */
    bool cancel_collab_task(const std::string& task_id);
    
    /**
     * 设置完成回调
     */
    void set_complete_callback(CollabTaskCallback callback);
    
    /**
     * 启动协调器
     */
    void start();
    
    /**
     * 停止协调器
     */
    void stop();
    
    /**
     * 是否正在运行
     */
    bool is_running() const { return running_; }

private:
    TeamOrchestrator() = default;
    ~TeamOrchestrator();
    
    TeamOrchestrator(const TeamOrchestrator&) = delete;
    TeamOrchestrator& operator=(const TeamOrchestrator&) = delete;
    
    // 工作线程
    void worker_thread();
    
    // 执行协作任务
    void execute_collab_task(CollaborationTask& task);
    
    // 任务分解（由 leader agent 执行）
    std::vector<SubTask> decompose_task(CollaborationTask& task, const TeamConfig& team);
    
    // 分配子任务给 agent
    void assign_subtask(SubTask& subtask, const TeamConfig& team);
    
    // 执行单个子任务
    bool execute_subtask(SubTask& subtask, CollaborationTask& parent_task);
    
    // 汇总结果（由 leader agent 执行）
    std::string aggregate_results(CollaborationTask& task, const TeamConfig& team);
    
    // 检查子任务依赖是否满足
    bool check_dependencies(const SubTask& subtask, const CollaborationTask& task) const;
    
    // 获取可执行的子任务
    std::vector<SubTask*> get_ready_subtasks(CollaborationTask& task);
    
    // 构建 agent 间通信的消息
    std::string build_agent_message(const std::string& from_agent, 
                                     const std::string& to_agent,
                                     const std::string& content,
                                     const CollaborationTask& task);
    
    mutable std::mutex mutex_;
    std::map<std::string, TeamConfig> teams_;
    std::map<std::string, CollaborationTask> collab_tasks_;
    std::queue<std::string> task_queue_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    CollabTaskCallback complete_callback_;
    
    // 最大并发子任务数
    int max_concurrent_subtasks_ = 5;
};

} // namespace geweinet
