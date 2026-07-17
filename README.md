# RAGReactor

RAGReactor 是一个基于 C++17 的高并发 Web 与 RAG 问答服务。项目在 Reactor/Sub-Reactor
网络模型、线程池、定时器和 MySQL 连接池的基础上，集成了文档切分、向量检索、HNSW
近邻搜索、重排、答案生成和 SSE 流式输出。

## 功能特性

- 基于 epoll 的 Reactor/Sub-Reactor 并发模型
- HTTP 静态资源、用户注册登录、会话与文件上传
- 基于阿里云百炼兼容接口的 Embedding、Rerank 和大语言模型调用
- 向量检索、HNSW 索引与混合召回
- `/api/ask` 流式 RAG 问答及 `/api/health` 健康检查
- 缓存、超时、重试、熔断和基础指标等稳定性机制
- API、检索、SSE 和容错相关的自动化测试

## 项目结构

```text
ai_rag/      RAG、向量索引、召回、重排与模型客户端
api/         API 路由、SSE 输出与指标
http/        HTTP 连接解析与业务处理
threadpool/  业务线程池
timer/       定时器
CGImysql/    MySQL 连接池
root/        Web 静态资源
knowledge/   示例知识文档与本地索引目录
tests/       自动化测试
tools/       文档索引和检索命令行工具
docs/        分阶段实现及问题分析文档
```

## 环境要求

- Linux（使用 epoll）
- 支持 C++17 的 GCC/G++
- GNU Make
- MySQL Server 和 MySQL Client 开发库
- OpenSSL、Boost.JSON、libcurl 和 pthread 开发库
- 阿里云百炼 API Key 及兼容模式服务地址

以 Ubuntu/Debian 为例，可安装主要编译依赖：

```bash
sudo apt update
sudo apt install build-essential libmysqlclient-dev libssl-dev libboost-json-dev libcurl4-openssl-dev
```

## 快速开始

### 1. 准备数据库

创建数据库，并至少创建项目登录注册功能依赖的 `user` 表：

```sql
CREATE DATABASE rag_reactor CHARACTER SET utf8mb4;
USE rag_reactor;

CREATE TABLE user (
    username VARCHAR(255) PRIMARY KEY,
    passwd VARCHAR(255) NOT NULL
);
```

社区发帖功能还需要 `user_posts` 表：

```sql
CREATE TABLE user_posts (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(255) NOT NULL,
    content_text TEXT,
    file_path VARCHAR(1024),
    file_type VARCHAR(255),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 2. 配置环境变量

复制示例配置并填写真实信息。`.env` 已加入 `.gitignore`，请勿提交密钥：

```bash
cp .env.example .env
```

必须配置的变量包括：

```dotenv
MYSQL_USER="root"
MYSQL_PASSWORD="your_password"
MYSQL_DATABASE="rag_reactor"
BAILIAN_API_KEY="your_api_key"
BAILIAN_BASE_URL="https://your-workspace.cn-beijing.maas.aliyuncs.com/compatible-mode/v1"
```

Embedding、LLM、Rerank、索引和容错参数可直接使用 `.env.example` 中的默认值，再按需调整。

### 3. 编译并启动

```bash
make server
chmod +x start_server.sh
./start_server.sh
```

服务默认监听 `9006` 端口，浏览器访问：

```text
http://127.0.0.1:9006/
```

可在启动脚本后追加命令行参数，例如修改端口和线程数：

```bash
./start_server.sh -p 8080 -t 12 -r 4
```

常用参数：`-p` 端口、`-t` 业务线程数、`-r` Sub-Reactor 数量、`-s` 数据库连接数、
`-n` 最大连接数、`-m` 触发模式、`-c` 是否关闭日志。

## 构建知识索引

将文本或 Markdown 文档放入 `knowledge/documents/`。首次问答时，服务会使用配置的
Embedding 模型自动生成向量索引和 HNSW 索引；之后会优先加载已有索引。

项目还提供使用本地 Mock Embedding 的离线索引工具，适合验证索引流程：

```bash
make index-documents
./tools/index_documents knowledge/documents knowledge/index/demo.ragvec
```

服务索引的路径、向量维度和模型名称由 `.env` 中的 `RAG_INDEX_PATH`、
`RAG_HNSW_INDEX_PATH`、`RAG_EMBEDDING_DIMENSION` 和 `RAG_EMBEDDING_MODEL` 控制。

## API 示例

健康检查无需登录：

```bash
curl http://127.0.0.1:9006/api/health
```

`POST /api/ask` 和 `GET /api/metrics` 需要有效登录会话；问答接口还要求携带
`X-CSRF-Token`，Web 页面 `root/rag.html` 已实现完整调用流程。

## 测试

运行全部测试：

```bash
make test
```

也可以运行单项测试，例如：

```bash
make test-sse-stream
make test-resilience
make test-retrieval-upgrade
```

清理编译产物：

```bash
make clean
```

更多设计与演进说明见 [`docs/`](docs/)。
