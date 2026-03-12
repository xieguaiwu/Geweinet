#include "geweinet/iflow_client.hpp"
#include "geweinet/logger.hpp"
#include <cstdio>
#include <array>
#include <thread>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fstream>

namespace geweinet {

// 清理 iFlow 输出，移除 Execution Info
static std::string clean_iflow_output(const std::string& output) {
    std::string result = output;
    
    size_t start = result.find("<Execution Info>");
    while (start != std::string::npos) {
        size_t end = result.find("</Execution Info>", start);
        if (end != std::string::npos) {
            size_t block_end = end + 16;
            while (start > 0 && (result[start-1] == '\n' || result[start-1] == '\r')) start--;
            while (block_end < result.size() && (result[block_end] == '\n' || result[block_end] == '\r')) block_end++;
            result.erase(start, block_end - start);
        } else break;
        start = result.find("<Execution Info>");
    }
    
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    if (!result.empty()) result += '\n';
    
    return result;
}

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
    int timeout_ms,
    const std::string& model,
    StreamCallback stream_callback
) {
    IFlowResult result;
    
    LOG_COMPONENT_DEBUG("Executing iFlow with prompt: " + prompt.substr(0, 100) + "...", "IFlowClient");
    
    std::string command = build_command(prompt, working_dir, env_vars, model);
    LOG_COMPONENT_DEBUG("Command: " + command, "IFlowClient");
    
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        result.error = "Failed to create pipe";
        LOG_COMPONENT_ERROR(result.error, "IFlowClient");
        return result;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        result.error = "Failed to fork process";
        LOG_COMPONENT_ERROR(result.error, "IFlowClient");
        return result;
    }
    
    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        if (!working_dir.empty() && working_dir != ".") {
            chdir(working_dir.c_str());
        }
        
        for (const auto& [key, value] : env_vars) {
            setenv(key.c_str(), value.c_str(), 1);
        }
        
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }
    
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    std::string stdout_output, stderr_output;
    
    // 设置非阻塞
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
    
    auto start_time = std::chrono::steady_clock::now();
    int status;
    bool timed_out = false;
    
    while (true) {
        char buffer[4096];
        ssize_t n;
        
        // 读取 stdout
        while ((n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            stdout_output.append(buffer, n);
            if (stream_callback) {
                stream_callback(std::string(buffer, n), false);
            }
        }
        
        // 读取 stderr
        while ((n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            stderr_output.append(buffer, n);
            if (stream_callback) {
                stream_callback(std::string(buffer, n), true);
            }
        }
        
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            // 进程结束，读取剩余输出
            while ((n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[n] = '\0';
                stdout_output.append(buffer, n);
            }
            while ((n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[n] = '\0';
                stderr_output.append(buffer, n);
            }
            break;
        }
        if (ret < 0 && errno != EINTR) break;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        if (elapsed >= timeout_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = true;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    // 清理输出
    result.stdout_output = clean_iflow_output(stdout_output);
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
    int timeout_ms,
    const std::string& model,
    StreamCallback stream_callback
) {
    std::thread([this, prompt, working_dir, env_vars, callback, timeout_ms, model, stream_callback]() {
        auto result = execute(prompt, working_dir, env_vars, timeout_ms, model, stream_callback);
        if (callback) callback(result);
    }).detach();
}

IFlowResult IFlowClient::execute_with_agent(
    const std::string& prompt,
    const AgentConfig& agent_config,
    StreamCallback stream_callback
) {
    return execute(
        prompt,
        agent_config.working_directory,
        agent_config.env_vars,
        agent_config.timeout_seconds * 1000,
        agent_config.model,
        stream_callback
    );
}

IFlowResult IFlowClient::send_message(
    const std::string& agent_id,
    const Message& message,
    const AgentConfig& agent_config,
    StreamCallback stream_callback
) {
    std::string prompt = message.content;
    
    if (!agent_config.prompt_file.empty()) {
        std::ifstream file(agent_config.prompt_file);
        if (file.is_open()) {
            std::string system_prompt((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
            prompt = system_prompt + "\n\n[User Message]\n" + prompt;
        }
    }
    
    return execute_with_agent(prompt, agent_config, stream_callback);
}

bool IFlowClient::is_available() const {
    return !get_version().empty();
}

std::string IFlowClient::get_version() const {
    std::string command = cli_path_ + " --version 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return "";
    
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    
    return result;
}

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
    const std::map<std::string, std::string>& env_vars,
    const std::string& model
) const {
    std::string cmd;
    
    for (const auto& [key, value] : env_vars) {
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
    
    cmd += cli_path_;
    
    // 使用 -y (YOLO) 模式自动接受所有操作，确保在非交互模式下能执行文件操作
    cmd += " -y";
    
    if (!model.empty()) {
        cmd += " -m '" + shell_escape(model) + "'";
    }
    
    cmd += " -p $'" + shell_escape(prompt) + "'";
    
    return cmd;
}

} // namespace geweinet
