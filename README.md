# Geweinet

A multi-agent coordination platform written in C++ that leverages iFlow-CLI to orchestrate multiple AI agents.

## Overview

Geweinet is a lightweight, high-performance platform for managing and coordinating multiple AI agents. Unlike NanoClaw, it does not integrate a skill system but focuses purely on agent orchestration through the iFlow-CLI interface.

### Key Features

- **Multi-Agent Management**: Register, configure, and manage multiple agents
- **Task Scheduling**: Asynchronous task queue with concurrent execution
- **Message Routing**: Point-to-point and broadcast messaging between agents
- **iFlow-CLI Integration**: Seamless integration with iFlow-CLI for AI model interaction
- **IPC Interface**: Unix socket-based IPC for external program integration
- **Interactive CLI**: Built-in interactive command-line interface

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 10+)
- CMake 3.16+
- iFlow-CLI installed and available in PATH

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Configuration

Geweinet searches for configuration files in the following order:

1. Path specified by `-c` option
2. `~/.config/geweinet.json`
3. `./config/geweinet.json`
4. `/etc/geweinet.json`

### Configuration File Format

```json
{
  "name": "Geweinet",
  "version": "1.0.0",
  "iflow_cli_path": "iflow",
  "log_level": "info",
  "log_file": "geweinet.log",
  "max_concurrent_tasks": 10,
  "task_timeout_seconds": 600,
  "ipc_socket_path": "/tmp/geweinet.sock",
  "agents": {
    "assistant": {
      "name": "Assistant",
      "description": "General purpose assistant agent",
      "working_directory": ".",
      "prompt_file": "",
      "model": "",
      "timeout_seconds": 300,
      "max_retries": 3,
      "enabled": true,
      "env_vars": {}
    },
    "code_reviewer": {
      "name": "Code Reviewer",
      "description": "Specialized agent for code review",
      "working_directory": ".",
      "prompt_file": "./prompts/code_reviewer.txt",
      "model": "glm-4",
      "timeout_seconds": 600,
      "max_retries": 2,
      "enabled": true,
      "env_vars": {}
    }
  }
}
```

### Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `name` | string | "Geweinet" | Platform name |
| `version` | string | "1.0.0" | Platform version |
| `iflow_cli_path` | string | "iflow" | Path to iFlow-CLI executable |
| `log_level` | string | "info" | Log level: debug, info, warning, error |
| `log_file` | string | "geweinet.log" | Log file path |
| `max_concurrent_tasks` | int | 10 | Maximum concurrent tasks |
| `task_timeout_seconds` | int | 600 | Task timeout in seconds |
| `ipc_socket_path` | string | "/tmp/geweinet.sock" | Unix socket path for IPC |

### Agent Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `name` | string | required | Agent display name |
| `description` | string | "" | Agent description |
| `working_directory` | string | "." | Working directory for agent |
| `prompt_file` | string | "" | System prompt file path |
| `model` | string | "" | AI model to use (e.g., "glm-4", "gpt-4"). Empty means default model |
| `timeout_seconds` | int | 300 | Agent operation timeout |
| `max_retries` | int | 3 | Maximum retry attempts |
| `enabled` | bool | true | Whether agent is enabled |
| `env_vars` | object | {} | Environment variables |

## Usage

### Command Line Options

```bash
./geweinet [options]

Options:
  -c, --config <path>  Path to config file
  -h, --help           Show help message
  -v, --version        Show version
```

### Environment Variables

| Variable | Description |
|----------|-------------|
| `GEWEINET_IFLOW_PATH` | Path to iFlow CLI |
| `GEWEINET_LOG_LEVEL` | Log level (debug/info/warning/error) |
| `GEWEINET_LOG_FILE` | Log file path |
| `GEWEINET_MAX_CONCURRENT` | Max concurrent tasks |
| `GEWEINET_IPC_SOCKET` | IPC socket path |

### Interactive Commands

After starting Geweinet, you can use the following commands:

```
geweinet> help
geweinet> list              # List all agents
geweinet> tasks             # List all tasks
geweinet> send <agent> <msg>   # Send message to agent (synchronous)
geweinet> task <agent> <msg>   # Submit async task to agent
geweinet> status <task_id>     # Get task status
geweinet> quit              # Exit program
```

### IPC API

External programs can communicate with Geweinet via Unix socket:

```bash
# List agents
echo '{"action":"list_agents"}' | nc -U /tmp/geweinet.sock

# Send message (synchronous)
echo '{"action":"send_message","agent_id":"assistant","content":"Hello"}' | nc -U /tmp/geweinet.sock

# Submit task (asynchronous)
echo '{"action":"submit_task","agent_id":"assistant","input":"Your task"}' | nc -U /tmp/geweinet.sock

# Get task status
echo '{"action":"get_task","task_id":"<task_id>"}' | nc -U /tmp/geweinet.sock

# List all tasks
echo '{"action":"list_tasks"}' | nc -U /tmp/geweinet.sock

# Cancel task
echo '{"action":"cancel_task","task_id":"<task_id>"}' | nc -U /tmp/geweinet.sock
```

### IPC Response Format

All responses are JSON:

```json
// Success
{"success": true, "task_id": "abc123..."}

// Error
{"success": false, "error": "Error message"}
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Geweinet Platform                  │
├─────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │
│  │   Logger    │  │   Config    │  │    IPC      │  │
│  │   System    │  │   Manager   │  │   Server    │  │
│  └─────────────┘  └─────────────┘  └─────────────┘  │
├─────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────┐    │
│  │              Agent Manager                   │    │
│  │   ┌───────┐  ┌───────┐  ┌───────┐          │    │
│  │   │Agent 1│  │Agent 2│  │Agent N│          │    │
│  │   └───────┘  └───────┘  └───────┘          │    │
│  └─────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────────────────────┐  │
│  │   Router    │  │      Task Scheduler         │  │
│  │  (Messages) │  │   (Queue & Execution)       │  │
│  └─────────────┘  └─────────────────────────────┘  │
├─────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────┐    │
│  │              iFlow Client                    │    │
│  │         (iFlow-CLI Integration)             │    │
│  └─────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────┘
                          │
                          ▼
                   ┌─────────────┐
                   │  iFlow CLI  │
                   │  (AI Model) │
                   └─────────────┘
```

## Project Structure

```
Geweinet/
├── CMakeLists.txt          # Build configuration
├── config/
│   └── geweinet.json       # Example configuration
├── include/geweinet/
│   ├── types.hpp           # Core type definitions
│   ├── logger.hpp          # Logging system
│   ├── config.hpp          # Configuration management
│   ├── agent_manager.hpp   # Agent lifecycle management
│   ├── router.hpp          # Message routing
│   ├── iflow_client.hpp    # iFlow-CLI integration
│   ├── task_scheduler.hpp  # Task queue and scheduling
│   └── ipc.hpp             # IPC communication
├── src/
│   ├── main.cpp            # Entry point
│   ├── types.cpp
│   ├── logger.cpp
│   ├── config.cpp
│   ├── agent_manager.cpp
│   ├── router.cpp
│   ├── iflow_client.cpp
│   ├── task_scheduler.cpp
│   └── ipc.cpp
└── build/
    └── geweinet            # Compiled binary
```

## License

MIT License

## Acknowledgments

Inspired by [NanoClaw](https://github.com/nanoclaw/nanoclaw), a TypeScript-based agent orchestration platform with skill system integration.
