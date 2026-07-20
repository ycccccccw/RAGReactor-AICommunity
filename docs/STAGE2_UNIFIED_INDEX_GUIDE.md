# 阶段 2：统一内容元数据与社区索引

## 实现结果

- `Document`、`DocumentChunk` 增加来源类型、来源 ID、作者、时间、状态、可信级别和内容版本。
- 新增通用 `ContentIndexer`，知识文档和社区帖子共用切分、Embedding、upsert 和删除逻辑。
- 向量文件格式升级为 v2；仍能读取旧 v1 知识索引，旧记录自动视为
  `knowledge/ready/curated_knowledge`。
- `VectorStore`、HNSW、BM25 和 HybridRetriever 全部支持 `ContentFilter`。
- RAG 问答显式限制为 `source_type=knowledge`，社区内容不会意外进入原问答上下文。
- 社区使用独立 `.ragvec` 与 `.hnsw` 文件，与知识索引物理隔离。
- 发帖和索引注册处于同一数据库事务，成功发帖必然产生 `pending` 记录。
- `002_community_index_lifecycle.sql` 回填已有帖子，并跟踪帖子更新与删除。
- `index_community` 增量复用已有向量，处理 pending/failed 内容，并从索引移除
  blocked/deleted/禁用内容。

## 索引文件

```text
knowledge/index/bailian-v4-1024.ragvec            知识向量
knowledge/index/bailian-v4-1024.hnsw              知识 HNSW
knowledge/index/community-bailian-v4-1024.ragvec  社区向量
knowledge/index/community-bailian-v4-1024.hnsw    社区 HNSW
```

索引文件虽然物理隔离，但都使用相同 `VectorStore`、`HnswIndex`、`Bm25Index` 和
`HybridRetriever` 实现。

## 同步命令

```bash
cd /root/RAGReactor-AICommunity
set -a
source .env
set +a
make index-community
./tools/index_community
```

输出示例：

```text
community index complete: indexed=15 removed=0 failed=25 unchanged=0
```

失败是逐条记录的，不会破坏已经成功构建的索引。原因可以查询：

```sql
SELECT source_id, index_status, last_error
FROM ai_content_registry
WHERE source_type='community' AND index_status='failed';
```

下次运行会重试 `failed` 记录。

## 更新、屏蔽和删除

更新 `user_posts` 后，数据库触发器会增加 `content_version` 并设置为 `pending`。下一次同步
用新向量替换同一个 `source_id` 的旧分块。

屏蔽帖子：

```sql
UPDATE ai_content_registry
SET index_status='blocked', search_enabled=0, rag_enabled=0
WHERE source_type='community' AND source_id='帖子ID';
```

运行 `./tools/index_community` 后，该帖子从向量文件和 HNSW 中移除。即使混合索引中仍存在
旧记录，默认 `ContentFilter.require_ready=true` 也会拒绝 blocked/deleted 内容。

物理删除 `user_posts` 时，触发器自动将注册记录设置为 `deleted` 和 `search_enabled=0`；随后
同步会移除向量。保留注册记录是为了防止缓存或旧索引重新引入已删除内容。

## 检索过滤

```cpp
rag::ContentFilter knowledge_only;
knowledge_only.source_types = {"knowledge"};

rag::ContentFilter community_only;
community_only.source_types = {"community"};

rag::ContentFilter combined;
combined.source_types = {"knowledge", "community"};
```

以上过滤器可传给精确向量、HNSW、BM25 或 HybridRetriever，且默认排除非 `ready` 内容。

## 当前正式索引状态

2026-07-20 首次正式同步结果：

- `ready`：15 条。
- `failed`：25 条，其中 20 条为远程 Embedding 超时，5 条没有可索引文字。
- 知识和社区索引均正常保留；失败帖子不会出现在语义检索结果中。

关闭 VPN 或恢复百炼网络后重新运行同步工具，可以继续处理超时记录。

