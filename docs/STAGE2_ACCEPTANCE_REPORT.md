# 阶段 2 验收报告

执行日期：2026-07-20  
工作目录：`/root/RAGReactor-AICommunity`  
约定端口：`9006`

## 结论

阶段 2 的统一元数据、通用索引、严格来源过滤、社区独立索引、发帖入队以及更新/屏蔽/删除
同步已经实现。自动化测试和隔离数据库端到端同步通过。

## 自动化验收

`rag_stage2_test` 新增验证：

- 通用 `ContentIndexer` 索引社区内容。
- 同一个 `source_id` 更新时替换旧向量而不是重复追加。
- 删除内容后对应分块从 VectorStore 消失。
- v2 文件保存/加载后元数据保持完整。

`retrieval_upgrade_test` 新增验证：

- VectorStore/HNSW/BM25/HybridRetriever 只检索知识。
- 同一检索内核只检索社区。
- 同一检索内核同时检索知识和社区。
- `blocked` 社区内容不会出现在任何默认结果中。

项目原有 v1 知识索引现场验证成功：文件头版本为 1，新代码可以加载并检索 `reactor.md`。

## 隔离数据库同步验收

在专用临时数据库中完成以下流程，随后删除临时数据库和索引文件：

1. 插入社区帖子并回填 `pending` 注册记录。
2. 使用 Mock Embedding 构建独立社区 `.ragvec` 与 `.hnsw`。
3. 搜索命中 `community-post:1`。
4. 设置 `blocked/search_enabled=0` 后重新同步。
5. 再次搜索返回 `No matching chunks found`。
6. 更新被屏蔽帖子后状态仍为 `blocked`，不会因为编辑自动解除审核屏蔽。
7. 删除帖子后注册记录进入 `deleted/search_enabled=0`。

## 9006 发帖生命周期验收

通过 9006 使用临时账号完成纯文字发布：

| 检查 | 结果 |
|---|---|
| 注册/登录/CSRF | 通过 |
| 发布帖子 | HTTP 200 |
| 发布后注册状态 | `pending` |
| 更新帖子后 | `pending:2` |
| 删除帖子后 | `deleted:0` |

临时帖子、注册记录和用户已经清理。

## 正式数据库与索引

- `002_community_index_lifecycle` 已应用。
- 现有社区帖子已回填 40 条内容注册记录。
- insert/update/delete 三个生命周期路径由应用事务和数据库触发器共同保证。
- 首次正式百炼同步：15 条 ready，25 条 failed。
- failed 中 20 条为远程 Embedding 超时，5 条没有文字内容。
- 社区向量与 HNSW 文件均已成功生成，约 63 KiB。

失败记录不会进入检索结果；关闭 VPN 或网络恢复后重新运行 `./tools/index_community` 会自动重试。

## 运行状态说明

验收时 9006 已有用户启动的旧进程，因此没有强制终止它。代码已重新编译，但要让正在运行的
服务加载阶段 2 新二进制，需要用户自行 `Ctrl+C` 后重新执行 `./start_server.sh`。

