#pragma once

#include "geweinet/types.hpp"
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>

namespace geweinet {

/**
 * iFlow-CLI 执行结果
 */
struct IFlowResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    bool success = false;
    std::string error;
    
    static IFlowResult from_json(const json& j);
    json to_json() const;
};

/**
 * iFlow-CLI 客户端
 * 负责与 iFlow-CLI 进行交互，调用大模型
 */
class IFlowClient {
public:
    using ResultCallback = std::function<void(const IFlowResult&)>;
    
    static IFlowClient& instance();
    
    /**
     * 设置 iFlow-CLI 路径
     */
    void set_cli_path(const std::string& path);
    
    /**
     * 获取 iFlow-CLI 路径
     */
    std::string cli_path() const { return cli_path_; }
    
    /**
     * 执行 iFlow 命令（同步）
     * @param prompt 输入提示
     * @param working_dir 工作目录
     * @param env_vars 环境变量
     * @param timeout_ms 超时时间（毫秒）
     * @param model 指定模型（可选）
     */
    IFlowResult execute(
        const std::string& prompt,
        const std::string& working_dir = ".",
        const std::map<std::string, std::string>& env_vars = {},
        int timeout_ms = 300000,
        const std::string& model = ""
    );
    
    /**
     * 执行 iFlow 命令（异步）
     */
    void execute_async(
        const std::string& prompt,
        const std::string& working_dir,
        const std::map<std::string, std::string>& env_vars,
        ResultCallback callback,
        int timeout_ms = 300000,
        const std::string& model = ""
    );
    
    /**
     * 使用 Agent 配置执行
     */
    IFlowResult execute_with_agent(
        const std::string& prompt,
        const AgentConfig& agent_config
    );
    
    /**
     * 发送消息给指定的 Agent
     */
    IFlowResult send_message(
        const std::string& agent_id,
        const Message& message,
        const AgentConfig& agent_config
    );
    
    /**
     * 检查 iFlow-CLI 是否可用
     */
    bool is_available() const;
    
    /**
     * 获取 iFlow-CLI 版本
     */
    std::string get_version() const;

private:
    IFlowClient() = default;
    ~IFlowClient() = default;
    
    IFlowClient(const IFlowClient&) = delete;
    IFlowClient& operator=(const IFlowClient&) = delete;
    
    std::string build_command(
        const std::string& prompt,
        const std::string& working_dir,
        const std::map<std::string, std::string>& env_vars,
        const std::string& model = ""
    ) const;
    
    std::string cli_path_ = "iflow";
    mutable std::mutex mutex_;
    std::atomic<bool> available_{false};
};

} // namespace geweinet
