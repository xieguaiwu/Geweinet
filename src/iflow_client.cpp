#include "geweinet/iflow_client.hpp"
#include "geweinet/logger.hpp"
#include <cstdio>
#include <array>
#include <thread>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

namespace geweinet {

// IFlowResult 实现
IFlowResult IFlowResult::from_json(const json& j) {
    IFlowResult result;
    result.exit_code = j.value("exit_code", -1);
    result.stdout_output = j.value("stdout", "");
    result.stderr_output = j.value("stderr", "");
    result.success = j.value("success", false);
    result.error = j.value("error", "");
    return result;
}

json IFlowResult::to_json() const {
    json j;
    j["exit_code"] = exit_code;
    j["stdout"] = stdout_output;
    j["stderr"] = stderr_output;
    j["success"] = success;
    j["error"] = error;
    return j;
}

// IFlowClient 实现
IFlowClient& IFlowClient::instance() {
    static IFlowClient instance;
    return instance;
}

void IFlowClient::set_cli_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    cli_path_ = path;
    LOG_COMPONENT_INFO("iFlow CLI path set to: " + path, "IFlowClient");
}

IFlowResult IFlowClient::execute(
    const std::string& prompt,
    const std::string& working_dir,
    const std::map<std::string, std::string>& env_vars,
    int timeout_ms
) {
    IFlowResult result;
    
    LOG_COMPONENT_DEBUG("Executing iFlow with prompt: " + prompt.substr(0, 100) + "...", "IFlowClient");
    
    // 构建命令
    std::string command = build_command(prompt, working_dir, env_vars);
    
    // 创建管道
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        result.error = "Failed to create pipe";
        LOG_COMPONENT_ERROR(result.error, "IFlowClient");
        return result;
    }
    
    // Fork 进程
    pid_t pid = fork();
    if (pid < 0) {
        result.error = "Failed to fork process";
        LOG_COMPONENT_ERROR(result.error, "IFlowClient");
        return result;
    }
    
    if (pid == 0) {
        // 子进程
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // 切换工作目录
        if (!working_dir.empty() && working_dir != ".") {
            chdir(working_dir.c_str());
        }
        
        // 设置环境变量
        for (const auto& [key, value] : env_vars) {
            setenv(key.c_str(), value.c_str(), 1);
        }
        
        // 执行命令
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }
    
    // 父进程
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    // 非阻塞读取输出
    std::string stdout_output, stderr_output;
    std::thread stdout_reader([&]() {
        char buffer[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
            stdout_output.append(buffer, n);
        }
    });
    
    std::thread stderr_reader([&]() {
        char buffer[4096];
        ssize_t n;
        while ((n = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
            stderr_output.append(buffer, n);
        }
    });
    
    // 等待进程结束或超时
    auto start_time = std::chrono::steady_clock::now();
    int status;
    bool timed_out = false;
    
    while (true) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            break;
        }
        if (ret < 0 && errno != EINTR) {
            break;
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        if (elapsed >= timeout_ms) {
            // 超时，杀死进程
            kill(pid, SIGKILL);
            // 必须回收僵尸进程
            waitpid(pid, &status, 0);
            timed_out = true;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    stdout_reader.join();
    stderr_reader.join();
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    result.stdout_output = stdout_output;
    result.stderr_output = stderr_output;
    
    if (timed_out) {
        result.error = "Command timed out";
        result.exit_code = -1;
        LOG_COMPONENT_WARNING("iFlow command timed out", "IFlowClient");
    } else {
        result.exit_code = WEXITSTATUS(status);
        result.success = (result.exit_code == 0);
        
        if (result.success) {
            LOG_COMPONENT_DEBUG("iFlow command completed successfully", "IFlowClient");
        } else {
            LOG_COMPONENT_WARNING("iFlow command failed with exit code: " + std::to_string(result.exit_code), "IFlowClient");
        }
    }
    
    return result;
}

void IFlowClient::execute_async(
    const std::string& prompt,
    const std::string& working_dir,
    const std::map<std::string, std::string>& env_vars,
    ResultCallback callback,
    int timeout_ms
) {
    std::thread([this, prompt, working_dir, env_vars, callback, timeout_ms]() {
        auto result = execute(prompt, working_dir, env_vars, timeout_ms);
        if (callback) {
            callback(result);
        }
    }).detach();
}

IFlowResult IFlowClient::execute_with_agent(
    const std::string& prompt,
    const AgentConfig& agent_config
) {
    return execute(
        prompt,
        agent_config.working_directory,
        agent_config.env_vars,
        agent_config.timeout_seconds * 1000
    );
}

IFlowResult IFlowClient::send_message(
    const std::string& agent_id,
    const Message& message,
    const AgentConfig& agent_config
) {
    // 构建包含消息上下文的 prompt
    std::string prompt = message.content;
    
    // 添加元数据
    if (!message.metadata.empty()) {
        prompt += "\n\n[Metadata]\n";
        for (const auto& [key, value] : message.metadata) {
            prompt += key + ": " + value + "\n";
        }
    }
    
    // 如果有 prompt 文件，先读取
    if (!agent_config.prompt_file.empty()) {
        std::ifstream file(agent_config.prompt_file);
        if (file.is_open()) {
            std::string system_prompt((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
            prompt = system_prompt + "\n\n[User Message]\n" + prompt;
        }
    }
    
    return execute_with_agent(prompt, agent_config);
}

bool IFlowClient::is_available() const {
    std::string version = get_version();
    return !version.empty();
}

std::string IFlowClient::get_version() const {
    std::string command = cli_path_ + " --version 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    
    // 去除换行符
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    
    return result;
}

// Shell 转义函数，防止命令注入
static std::string shell_escape(const std::string& str) {
    std::string result;
    result.reserve(str.size() * 2);
    
    for (char c : str) {
        switch (c) {
            case '\'': result += "'\\''"; break;
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '$':  result += "\\$"; break;
            case '`':  result += "\\`"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

std::string IFlowClient::build_command(
    const std::string& prompt,
    const std::string& working_dir,
    const std::map<std::string, std::string>& env_vars
) const {
    std::string cmd;
    
    // 添加环境变量（安全转义）
    for (const auto& [key, value] : env_vars) {
        // 验证环境变量名只包含合法字符
        bool valid_key = !key.empty();
        for (char c : key) {
            if (!std::isalnum(c) && c != '_') {
                valid_key = false;
                break;
            }
        }
        if (valid_key) {
            cmd += key + "='" + shell_escape(value) + "' ";
        }
    }
    
    // 构建 iFlow 命令 (使用 -p 参数)
    cmd += cli_path_ + " -p $'";
    
    // 转义 prompt 中的特殊字符
    cmd += shell_escape(prompt) + "'";
    
    return cmd;
}

} // namespace geweinet
