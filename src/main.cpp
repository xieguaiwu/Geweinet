#include "geweinet/types.hpp"
#include "geweinet/logger.hpp"
#include "geweinet/config.hpp"
#include "geweinet/agent_manager.hpp"
#include "geweinet/router.hpp"
#include "geweinet/iflow_client.hpp"
#include "geweinet/task_scheduler.hpp"
#include "geweinet/ipc.hpp"

#include <iostream>
#include <fstream>
#include <csignal>
#include <cstdlib>
#include <atomic>
#include <unistd.h>
#include <pwd.h>
#include <sstream>

namespace geweinet {

std::atomic<bool> g_running(true);

// 获取用户 home 目录
std::string get_home_dir() {
    const char* home = std::getenv("HOME");
    if (home) return home;
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;
    
    return "";
}

// 查找配置文件
std::string find_config_file(const std::string& specified_path) {
    std::vector<std::string> search_paths;
    
    // 优先使用用户指定的路径
    if (!specified_path.empty()) {
        search_paths.push_back(specified_path);
    }
    
    // 添加默认搜索路径
    std::string home = get_home_dir();
    if (!home.empty()) {
        search_paths.push_back(home + "/.config/geweinet.json");
    }
    search_paths.push_back("./config/geweinet.json");
    search_paths.push_back("/etc/geweinet.json");
    
    for (const auto& path : search_paths) {
        std::ifstream f(path);
        if (f.good()) {
            return path;
        }
    }
    
    // 返回默认路径（即使不存在）
    return specified_path.empty() ? "./config/geweinet.json" : specified_path;
}

void signal_handler(int signal) {
    LOG_INFO("Received signal " + std::to_string(signal) + ", shutting down...");
    g_running = false;
}

class GeweinetPlatform {
public:
    bool initialize(const std::string& config_path) {
        // 加载配置
        auto& config_manager = ConfigManager::instance();
        
        // 查找配置文件
        std::string found_path = find_config_file(config_path);
        
        if (!config_manager.load_from_file(found_path)) {
            LOG_FATAL("Failed to load config file: " + found_path);
            LOG_FATAL("Searched paths: ~/.config/geweinet.json, ./config/geweinet.json, /etc/geweinet.json");
            return false;
        }
        
        // 从环境变量覆盖
        config_manager.load_from_env();
        
        const auto& config = config_manager.config();
        
        // 初始化日志
        auto& logger = Logger::instance();
        if (config.log_level == "debug") {
            logger.set_level(LogLevel::Debug);
        } else if (config.log_level == "warning") {
            logger.set_level(LogLevel::Warning);
        } else if (config.log_level == "error") {
            logger.set_level(LogLevel::Error);
        }
        
        if (!config.log_file.empty()) {
            logger.set_log_file(config.log_file);
        }
        
        LOG_COMPONENT_INFO("Initializing " + config.name + " v" + config.version, "Geweinet");
        
        // 检查 iFlow CLI
        auto& iflow = IFlowClient::instance();
        iflow.set_cli_path(config.iflow_cli_path);
        
        if (!iflow.is_available()) {
            LOG_COMPONENT_WARNING("iFlow CLI not available at: " + config.iflow_cli_path, "Geweinet");
            LOG_COMPONENT_WARNING("Make sure iFlow CLI is installed and in PATH, or set GEWEINET_IFLOW_PATH", "Geweinet");
        } else {
            LOG_COMPONENT_INFO("iFlow CLI version: " + iflow.get_version(), "Geweinet");
        }
        
        // 注册 Agent
        auto& agent_manager = AgentManager::instance();
        for (const auto& [id, agent_config] : config.agents) {
            if (!agent_manager.register_agent(agent_config)) {
                LOG_COMPONENT_WARNING("Failed to register agent: " + id, "Geweinet");
            }
        }
        
        LOG_COMPONENT_INFO("Registered " + std::to_string(agent_manager.agent_count()) + " agents", "Geweinet");
        
        // 启动任务调度器
        auto& scheduler = TaskScheduler::instance();
        scheduler.set_max_concurrent(config.max_concurrent_tasks);
        scheduler.start();
        
        // 启动路由器
        auto& router = Router::instance();
        router.start();
        
        // 启动 IPC 服务器
        auto& ipc = IPCServer::instance();
        ipc.set_handler([this](const std::string& request) {
            return handle_ipc_request(request);
        });
        
        if (!ipc.start(config.ipc_socket_path)) {
            LOG_COMPONENT_ERROR("Failed to start IPC server", "Geweinet");
            return false;
        }
        
        // 设置任务完成回调
        scheduler.set_complete_callback([this](const Task& task) {
            on_task_complete(task);
        });
        
        LOG_COMPONENT_INFO("Geweinet platform initialized successfully", "Geweinet");
        return true;
    }
    
    void run() {
        LOG_COMPONENT_INFO("Geweinet platform running...", "Geweinet");
        LOG_COMPONENT_INFO("IPC socket: " + IPCServer::instance().socket_path(), "Geweinet");
        LOG_COMPONENT_INFO("Press Ctrl+C to stop", "Geweinet");
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    void shutdown() {
        LOG_COMPONENT_INFO("Shutting down Geweinet...", "Geweinet");
        
        // 停止 IPC
        IPCServer::instance().stop();
        
        // 停止路由器
        Router::instance().stop();
        
        // 停止调度器
        TaskScheduler::instance().stop();
        
        // 清理 Agent
        AgentManager::instance().clear();
        
        LOG_COMPONENT_INFO("Geweinet shutdown complete", "Geweinet");
    }
    
private:
    std::string handle_ipc_request(const std::string& request) {
        try {
            json req = json::parse(request);
            std::string action = req.value("action", "");
            
            if (action == "submit_task") {
                return handle_submit_task(req);
            } else if (action == "get_task") {
                return handle_get_task(req);
            } else if (action == "list_tasks") {
                return handle_list_tasks(req);
            } else if (action == "list_agents") {
                return handle_list_agents(req);
            } else if (action == "cancel_task") {
                return handle_cancel_task(req);
            } else if (action == "send_message") {
                return handle_send_message(req);
            } else {
                return R"({"success": false, "error": "Unknown action"})";
            }
        } catch (const std::exception& e) {
            json res;
            res["error"] = std::string("Invalid request: ") + e.what();
            return res.dump();
        }
    }
    
    std::string handle_submit_task(const json& req) {
        std::string agent_id = req.value("agent_id", "");
        std::string input = req.value("input", "");
        
        if (agent_id.empty() || input.empty()) {
            return R"({"success": false, "error": "Missing agent_id or input"})";
        }
        
        if (!AgentManager::instance().has_agent(agent_id)) {
            return R"({"success": false, "error": "Agent not found"})";
        }
        
        std::string task_id = TaskScheduler::instance().submit_task(agent_id, input);
        
        json res;
        res["success"] = true;
        res["task_id"] = task_id;
        return res.dump();
    }
    
    std::string handle_get_task(const json& req) {
        std::string task_id = req.value("task_id", "");
        
        auto task = TaskScheduler::instance().get_task(task_id);
        if (!task) {
            return R"({"success": false, "error": "Task not found"})";
        }
        
        json res;
        res["success"] = true;
        res["task"] = task->to_json();
        return res.dump();
    }
    
    std::string handle_list_tasks(const json& req) {
        std::string agent_id = req.value("agent_id", "");
        
        std::vector<Task> tasks;
        if (agent_id.empty()) {
            tasks = TaskScheduler::instance().get_all_tasks();
        } else {
            tasks = TaskScheduler::instance().get_agent_tasks(agent_id);
        }
        
        json res;
        res["success"] = true;
        json tasks_array = json::array();
        for (const auto& task : tasks) {
            tasks_array.push_back(task.to_json());
        }
        res["tasks"] = tasks_array;
        return res.dump();
    }
    
    std::string handle_list_agents(const json& req) {
        auto agents = AgentManager::instance().get_all_agents();
        
        json res;
        res["success"] = true;
        json agents_array = json::array();
        for (const auto& agent : agents) {
            json agent_json;
            agent_json["id"] = agent->id();
            agent_json["name"] = agent->name();
            agent_json["status"] = agent_status_to_string(agent->status());
            agent_json["available"] = agent->is_available();
            agents_array.push_back(agent_json);
        }
        res["agents"] = agents_array;
        return res.dump();
    }
    
    std::string handle_cancel_task(const json& req) {
        std::string task_id = req.value("task_id", "");
        
        if (TaskScheduler::instance().cancel_task(task_id)) {
            return R"({"success": true})";
        }
        return R"({"success": false, "error": "Failed to cancel task"})";
    }
    
    std::string handle_send_message(const json& req) {
        // 直接发送消息给 agent（通过 iFlow）
        std::string agent_id = req.value("agent_id", "");
        std::string content = req.value("content", "");
        
        if (agent_id.empty() || content.empty()) {
            return R"({"success": false, "error": "Missing agent_id or content"})";
        }
        
        auto agent = AgentManager::instance().get_agent(agent_id);
        if (!agent) {
            return R"({"success": false, "error": "Agent not found"})";
        }
        
        Message msg;
        msg.content = content;
        msg.to_agent = agent_id;
        
        auto result = IFlowClient::instance().send_message(agent_id, msg, agent->config());
        
        json res;
        res["success"] = result.success;
        if (result.success) {
            res["output"] = result.stdout_output;
        } else {
            res["error"] = result.error.empty() ? result.stderr_output : result.error;
        }
        return res.dump();
    }
    
    void on_task_complete(const Task& task) {
        LOG_COMPONENT_INFO("Task " + task.id + " completed with status: " + 
            task_status_to_string(task.status), "Geweinet");
    }
};

} // namespace geweinet

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  -c, --config <path>  Path to config file\n"
              << "  -h, --help           Show this help message\n"
              << "  -v, --version        Show version\n\n"
              << "Config file search order:\n"
              << "  1. Path specified by -c option\n"
              << "  2. ~/.config/geweinet.json\n"
              << "  3. ./config/geweinet.json\n"
              << "  4. /etc/geweinet.json\n\n"
              << "Environment Variables:\n"
              << "  GEWEINET_IFLOW_PATH      Path to iFlow CLI\n"
              << "  GEWEINET_LOG_LEVEL       Log level (debug/info/warning/error)\n"
              << "  GEWEINET_LOG_FILE        Log file path\n"
              << "  GEWEINET_MAX_CONCURRENT  Max concurrent tasks\n"
              << "  GEWEINET_IPC_SOCKET      IPC socket path\n";
}

void print_interactive_help() {
    std::cout << "\nGeweinet Interactive Commands:\n"
              << "  help                Show this help\n"
              << "  list                List all agents\n"
              << "  tasks               List all tasks\n"
              << "  send <agent> <msg>  Send message to agent (sync)\n"
              << "  task <agent> <msg>  Submit async task to agent\n"
              << "  status <task_id>    Get task status\n"
              << "  quit / exit         Exit program\n\n";
}

void run_interactive(geweinet::GeweinetPlatform& platform) {
    std::string line;
    
    while (geweinet::g_running) {
        std::cout << "geweinet> " << std::flush;
        
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        // 去除首尾空白
        line.erase(0, line.find_first_not_of(" \t\n\r"));
        line.erase(line.find_last_not_of(" \t\n\r") + 1);
        
        if (line.empty()) continue;
        
        // 解析命令
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        
        if (cmd == "help") {
            print_interactive_help();
        }
        else if (cmd == "list") {
            auto agents = geweinet::AgentManager::instance().get_all_agents();
            std::cout << "\nAgents:\n";
            for (const auto& agent : agents) {
                std::cout << "  - " << agent->id() 
                          << " (" << agent->name() << ")"
                          << " [" << geweinet::agent_status_to_string(agent->status()) << "]"
                          << (agent->is_available() ? " available" : "") << "\n";
            }
            std::cout << "\n";
        }
        else if (cmd == "tasks") {
            auto tasks = geweinet::TaskScheduler::instance().get_all_tasks();
            std::cout << "\nTasks (" << tasks.size() << "):\n";
            for (const auto& task : tasks) {
                std::cout << "  - " << task.id.substr(0, 8) << "... "
                          << "[" << geweinet::task_status_to_string(task.status) << "] "
                          << "agent: " << task.agent_id << "\n";
            }
            std::cout << "\n";
        }
        else if (cmd == "send") {
            std::string agent_id, content;
            iss >> agent_id;
            std::getline(iss, content);
            content.erase(0, content.find_first_not_of(" \t"));
            
            if (agent_id.empty() || content.empty()) {
                std::cout << "Usage: send <agent_id> <message>\n";
                continue;
            }
            
            auto agent = geweinet::AgentManager::instance().get_agent(agent_id);
            if (!agent) {
                std::cout << "Error: Agent not found: " << agent_id << "\n";
                continue;
            }
            
            std::cout << "Sending to " << agent_id << "...\n";
            
            geweinet::Message msg;
            msg.content = content;
            msg.to_agent = agent_id;
            
            auto result = geweinet::IFlowClient::instance().send_message(agent_id, msg, agent->config());
            
            if (result.success) {
                std::cout << "\n" << result.stdout_output << "\n";
            } else {
                std::cout << "Error: " << (result.error.empty() ? result.stderr_output : result.error) << "\n";
            }
        }
        else if (cmd == "task") {
            std::string agent_id, content;
            iss >> agent_id;
            std::getline(iss, content);
            content.erase(0, content.find_first_not_of(" \t"));
            
            if (agent_id.empty() || content.empty()) {
                std::cout << "Usage: task <agent_id> <input>\n";
                continue;
            }
            
            std::string task_id = geweinet::TaskScheduler::instance().submit_task(agent_id, content);
            std::cout << "Task submitted: " << task_id << "\n";
        }
        else if (cmd == "status") {
            std::string task_id;
            iss >> task_id;
            
            auto task = geweinet::TaskScheduler::instance().get_task(task_id);
            if (!task) {
                std::cout << "Task not found: " << task_id << "\n";
                continue;
            }
            
            std::cout << "\nTask: " << task->id << "\n"
                      << "Status: " << geweinet::task_status_to_string(task->status) << "\n"
                      << "Agent: " << task->agent_id << "\n";
            
            if (task->output) {
                std::cout << "Output:\n" << *task->output << "\n";
            }
            if (task->error) {
                std::cout << "Error: " << *task->error << "\n";
            }
            std::cout << "\n";
        }
        else if (cmd == "quit" || cmd == "exit") {
            std::cout << "Goodbye!\n";
            geweinet::g_running = false;
        }
        else {
            std::cout << "Unknown command: " << cmd << ". Type 'help' for commands.\n";
        }
    }
}

int main(int argc, char* argv[]) {
    std::string config_path;  // 默认为空，触发自动搜索
    
    // 解析参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "Geweinet v1.0.0\n";
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }
    
    // 设置信号处理
    std::signal(SIGINT, geweinet::signal_handler);
    std::signal(SIGTERM, geweinet::signal_handler);
    
    // 初始化并运行平台
    geweinet::GeweinetPlatform platform;
    
    if (!platform.initialize(config_path)) {
        return 1;
    }
    
    // 运行交互式界面
    run_interactive(platform);
    
    platform.shutdown();
    
    return 0;
}
