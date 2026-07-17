# 阶段 7：HNSW、混合检索与 Rerank

## 最终链路

```text
问题
  ├── query embedding -> HNSW 向量召回
  └── 本地分词 -> BM25 关键词召回
                         |
                         v
                    RRF 融合去重
                         |
                         v
                qwen3-rerank 精排
                         |
                         v
                     TopK -> LLM
```

## 哪些是自己实现的？

- HNSW 图算法使用系统库 `libhnswlib-dev`，项目自己完成索引构建、保存、加载、标签映射
  和暴力 TopK 回退。
- BM25 倒排索引、词频、文档频率、评分和 TopK 由项目实现。
- 中文单字/二元组以及英文、数字、下划线技术词分词由项目实现。
- 两路结果的 RRF 排名融合和去重由项目实现。
- Rerank 模型使用百炼 `qwen3-rerank`，HTTP Provider、超时和回退由项目实现。

## API Key

Embedding、LLM、Rerank 共用同一个 `BAILIAN_API_KEY`，不需要创建第二个 Key。Rerank
使用百炼 `compatible-api/v1/reranks` 接口，程序会根据现有 `BAILIAN_BASE_URL` 自动推导。

## 本机依赖

```bash
sudo apt-get install libhnswlib-dev
```

## 主要配置

```bash
RAG_RERANK_ENABLED="true"
RAG_RERANK_MODEL="qwen3-rerank"
RAG_RERANK_CANDIDATES="20"
RAG_RERANK_TOP_N="5"
RAG_RERANK_TIMEOUT_MS="10000"

RAG_HNSW_M="16"
RAG_HNSW_EF_CONSTRUCTION="200"
RAG_HNSW_EF_SEARCH="50"
RAG_HNSW_INDEX_PATH="knowledge/index/bailian-v4-1024.hnsw"
```

## 故障回退

- HNSW 加载或构建失败：回退到原来的暴力余弦 TopK。
- Rerank 请求失败或超时：回退到本地 BM25 + 向量检索的 RRF 顺序。
- 回退不会中断问答，`rerank_fallback` 和监控指标会记录故障。

## 验收

```bash
make test-retrieval-upgrade
curl http://127.0.0.1:9006/api/health
```

健康检查应包含：

```json
{
  "retrieval_mode": "hnsw+bm25+rrf+qwen3-rerank",
  "rerank_enabled": true
}
```

问答的 `sources` SSE 事件会返回 `rerank_applied` 和 `rerank_fallback`。正常情况下前者为
`true`、后者为 `false`。
