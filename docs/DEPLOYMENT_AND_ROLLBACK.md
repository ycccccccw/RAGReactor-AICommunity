# 部署与回滚

## 部署前

```bash
cd /root/RAGReactor-AICommunity
cp server "backups/server-$(date +%Y%m%d-%H%M%S)"
cp knowledge/index/community-bailian-v4-1024.ragvec backups/
make test
make server
```

阶段 5 没有新增数据库表，因此不需要新的数据库迁移。现有迁移仍为 `001`、`002`，均可重复执行且不删除业务数据。

推荐配置：

```text
COMMUNITY_FEED_SNAPSHOT_SECONDS=10
COMMUNITY_QUESTION_INTEREST_WEIGHT=0.20
COMMUNITY_QUESTION_INTEREST_HALF_LIFE_MINUTES=60
RAG_RELEVANCE_THRESHOLD=0.45
RAG_CONNECT_TIMEOUT_MS=5000
```

## 9006 部署

```bash
pkill -f './server -p 9006'
set -a
source .env
set +a
./server -p 9006
```

建议实际生产使用 systemd 或 supervisor，不要依赖交互式 Shell。启动后执行：

```bash
BASE_URL=http://127.0.0.1:9006 ./tests/system_acceptance.sh
```

## 索引升级与恢复

- 当前向量格式为 v2，同时兼容读取 v1；v1 数据加载后自动补齐为知识来源元数据。
- 魔数、版本、维度、记录数、字符串长度、数据截断和尾随数据均会校验。
- 加载失败不会覆盖内存中的旧 `VectorStore`。
- 知识索引启动时不兼容会安全重建；社区索引故障会回退热门/最新 Feed。

社区索引恢复：

```bash
make index-community
./tools/index_community
```

## 应用回滚

1. 停止 9006 新进程。
2. 将备份的 `server-*` 复制回 `server`。
3. 如索引同步导致异常，恢复备份 `.ragvec` 和 `.hnsw`。
4. 重新启动 9006，执行 `tests/system_acceptance.sh`。

阶段 5 的快照缓存只存在内存，回滚或重启即可清空。若只想禁用快照而保留新二进制：

```text
COMMUNITY_FEED_SNAPSHOT_SECONDS=0
```

