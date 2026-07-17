# 阶段 3：真实 RAG 主链路（小白说明）

## 1. 这一阶段完成了什么

阶段 2 只能用 Mock 算法测试“切文档、存向量、TopK 检索”。阶段 3 已经接入阿里云百炼：

```text
用户问题
  -> text-embedding-v4 把问题变成向量
  -> 本地 VectorStore 计算余弦相似度并取 TopK
  -> 过滤相似度过低的片段
  -> PromptBuilder 把问题和片段拼成 Prompt
  -> qwen-plus 根据片段生成答案
  -> HTTP JSON 或 SSE 返回答案和来源
```

Embedding 和 LLM 共用 `.env` 中的一把 `BAILIAN_API_KEY`，但使用不同模型。

## 2. 新增的主要文件

- `http_json_client`：用 libcurl 发送 HTTPS JSON 请求，设置连接和总超时。
- `bailian_embedding_provider`：调用 `/embeddings`，解析 1024 维真实向量。
- `llm_client`：调用 `/chat/completions`，让 qwen-plus 生成答案。
- `prompt_builder`：把检索片段、来源编号和用户问题组成受控 Prompt。
- `rag_service`：串起建索引、查询向量、TopK、阈值过滤和大模型调用。
- `.env.example`：只包含安全的配置示例，不包含真实密码和 Key。

## 3. 为什么真实索引要重新生成

阶段 2 的索引由 `MockEmbeddingProvider` 生成，只有 256 维。阶段 3 的
`text-embedding-v4` 默认生成 1024 维。不同模型生成的向量不能相互比较。

第一次真实提问时，服务会读取 `knowledge/documents`，调用百炼为所有片段生成真实
向量，并保存到：

```text
knowledge/index/bailian-v4-1024.ragvec
```

这个目录被 `.gitignore` 忽略。以后重启会直接加载索引，不会每次重复生成和付费。
修改知识库文档后，应删除这个索引文件，让下一次提问自动重建。

## 4. 输入和费用保护

- HTTP 请求体最大 16 KiB。
- `question` 最大 4096 字节。
- `top_k` 只能为 1 到 20。
- Prompt 默认最大 12000 字节，放不下的低排名片段不会加入。
- LLM 默认最多输出 800 Token。
- Embedding 和 LLM 都有连接超时和总请求超时。
- 默认相关度阈值为 0.40。所有片段都低于阈值时，不调用 LLM，直接说明知识库没有相关内容。

## 5. SSE 是怎样工作的

业务线程先完成 Embedding、检索和 LLM 调用，然后生成以下 SSE 事件：

1. `sources`：检索来源、片段编号、相似度和原文。
2. `delta`：答案的第一部分。
3. `delta`：答案的第二部分。
4. `done`：回答结束。

这些事件仍由原有 Sub Reactor 根据 socket 的 `EPOLLOUT` 状态分段写出。因此 AI
业务没有替换 epoll、线程池和连接管理，而是复用了它们。

当前版本的 SSE 是“模型完整生成后，再由服务器分段返回”。下一阶段可以把百炼的
上游流式响应直接透传，实现更低的首 Token 延迟。

## 6. 如何启动

真实 Key 只放在 `.env`，不要提交 GitHub：

```bash
cd /root/RAGReactor
make server
./start_server.sh
```

看到 `PORT = 9006` 后，不要关闭这个终端。另开一个终端验收。

## 7. 验收命令

健康检查：

```bash
curl http://127.0.0.1:9006/api/health
```

普通 JSON 问答：

```bash
curl -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -d '{"question":"pipefd 在 WebServer 中有什么作用？","top_k":3,"stream":false}'
```

SSE 问答：

```bash
curl -N -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -H 'Accept: text/event-stream' \
  -d '{"question":"WebServer 如何保存用户密码？","top_k":3,"stream":true}'
```

无相关内容测试：

```bash
curl -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -d '{"question":"唐朝李白最著名的诗有哪些？","top_k":3}'
```

预期最后一个请求返回 `grounded: false`，而不是让大模型脱离知识库编造答案。

## 8. 常见问题

### 返回 502

查看响应里的错误消息。常见原因是 Key 错误、API Host 不匹配、账户欠费、网络超时，
或者模型没有权限。不要把完整 Key 贴到聊天、日志或 GitHub。

### 修改文档后检索不到新内容

删除旧的真实索引并重新提问：

```bash
rm -f knowledge/index/bailian-v4-1024.ragvec
```

第一次请求会重新构建索引，所以会比后续请求慢。

### 为什么来源中可能还有不够精确的片段

当前项目使用“内存暴力余弦 TopK + 固定阈值”，没有 reranker。它适合当前小知识库，
也便于面试讲清楚。后续可以增加重排模型、混合关键词检索或 HNSW。
