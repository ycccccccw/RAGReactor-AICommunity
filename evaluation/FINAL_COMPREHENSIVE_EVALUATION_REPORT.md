# RAGReactor-AICommunity 最终综合测评报告

生成时间：2026-07-28  
测试端口：9006  
测试主机：2 vCPU、3.4 GiB RAM、无 Swap  
结论性质：当前主机与当前项目语料下的实测结论，不外推为所有数据集的通用结论。

## 1. 最终结论

建议生产参数：

```text
Sub Reactor: 2
Worker: 4
SQL pool: 8
Chunk: 语义切割（上线前需重建索引）
Embedding: qwen3.7-text-embedding / 1024 维
Rerank: qwen3-rerank；延迟敏感场景用 gte-rerank-v2
top_k: 5
Rerank candidates: 20
```

选择理由：

- `2×4` 在线程矩阵中 QPS 最高且无错误，继续增加线程没有有效收益。
- 语义切割的 Recall@5 为 1.000、MRR 为 0.818，优于当前固定切割的 0.908/0.779。
- `qwen3.7-text-embedding 1024` 在相同语义切割下达到 Recall@5 1.000、MRR 0.883，是精度、维度和检索延迟的最佳平衡。
- `qwen3-rerank` 把 MRR 从 0.883 提升到 0.985，但平均增加 202.8ms；`gte-rerank-v2` 为 0.962，平均增加 166.7ms。
- 24 条端到端问答中，8 条不可回答问题全部拒答，观察到的明显幻觉率为 0%。

## 2. 测试范围与数据

本次包含：

1. 8 组 Sub Reactor/worker 线程压力测试。
2. 固定、段落结构、父子、语义四种切割方法。
3. 5 组 Embedding 模型/维度。
4. 无重排、qwen3-rerank、gte-rerank-v2 三组重排。
5. RAGAS 忠诚度、答案相关性、上下文精确率/召回率、事实正确性。
6. 未知问题拒答、突发限流、上游超时、熔断及恢复。
7. 全部已有 C++ 自动化测试。

数据分为两层：

- 线上真实层：当前 `knowledge/documents` 只有 3 个文档，共约 1.5KB；端到端集为 24 条，其中可回答 16 条、不可回答 8 条。
- 隔离扩展层：从 10 份项目历史设计/验收资料和 3 份当前知识文档构成 13 文档、65 问题检索集。历史资料只通过 Git 读取，没有恢复到网站、生产索引或 GitHub 的 `docs`。

65 条集仍属于中型项目内基准，不等价于生产规模的人工金标集。模型生成的问题已经固定到 JSON，后续应人工复核后继续扩展到至少 300 条。

## 3. 线程配置压测

工具为 `wrk`，每组 32 并发、6 秒，固定 2 个压测线程、SQL pool=8，并在 Feed 压力期间连续执行 10 次真实登录。

| Sub | Worker | QPS | Feed P99 | 非 2xx | 登录 P99 | 登录失败 |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2 | 785.33 | 42.30ms | 0 | 96.0ms | 0 |
| 1 | 4 | 782.96 | 42.00ms | 0 | 96.0ms | 0 |
| 2 | 2 | 784.80 | 42.44ms | 0 | 97.0ms | 0 |
| **2** | **4** | **786.62** | **42.06ms** | **0** | **96.0ms** | **0** |
| 2 | 8 | 783.38 | 42.08ms | 0 | 96.0ms | 0 |
| 4 | 4 | 784.66 | 42.04ms | 0 | 101.0ms | 0 |
| 4 | 8 | 785.28 | 42.02ms | 0 | 96.0ms | 0 |
| 4 | 16 | 786.01 | 42.05ms | 55 | 108.3ms | 0 |

结论：该机器只有 2 vCPU，吞吐在约 785 QPS 已饱和。更多线程增加调度和数据库竞争，`4×16` 已出现错误，因此选择 `2×4`，而不是只看接近的最高瞬时 QPS。

## 4. 切割方法

所有方法固定使用 text-embedding-v4/1024，无重排。

| 方法 | Chunk 数 | Recall@5 | MRR | 检索 P95 |
|---|---:|---:|---:|---:|
| 固定 500 字符/80 overlap | 68 | 0.908 | 0.779 | 6.6ms |
| 标题与段落结构 | 54 | 0.908 | 0.752 | **5.0ms** |
| 父子切割 | 140 | 0.969 | 0.810 | 13.2ms |
| **语义切割** | 119 | **1.000** | **0.818** | 11.5ms |

语义切割根据相邻句向量相似度和最大块长度确定断点，减少一个事实跨块断裂；父子切割用小块召回、父块供生成，召回也明显优于固定切割，但索引条目最多。段落切割延迟最低，适合结构规范的 Markdown，但跨段问题表现较弱。

最终选择语义切割。若知识文档增长到十万级 Chunk，应重新比较 HNSW 延迟和内存后再决定是否采用父子混合。

## 5. Embedding 模型与维度

固定使用语义切割、无重排。

| 模型 | 维度 | Recall@5 | MRR | 检索 P95 |
|---|---:|---:|---:|---:|
| text-embedding-v3 | 512 | 0.954 | 0.862 | **5.7ms** |
| text-embedding-v4 | 512 | 0.985 | 0.810 | 6.1ms |
| text-embedding-v4 | 1024 | 1.000 | 0.818 | 11.5ms |
| text-embedding-v4 | 2048 | 1.000 | 0.831 | 22.3ms |
| **qwen3.7-text-embedding** | **1024** | **1.000** | **0.883** | **11.8ms** |

2048 维没有提高 Recall@5，MRR 增益也不足以抵消约两倍检索计算与索引存储。最终选择 qwen3.7/1024。资源非常紧张时可选 v3/512，但会损失约 4.6 个百分点 Recall@5。

注意：当前生产索引仍是 text-embedding-v4/1024。切换模型必须使用新路径安全重建向量与 HNSW 索引，不能让不同模型向量混用。

## 6. Rerank 模型

固定使用语义切割和 qwen3.7-text-embedding/1024，初召回 20 条。

| Rerank | Recall@5 | MRR | 平均额外延迟 | 检索 P95 |
|---|---:|---:|---:|---:|
| 无 | 1.000 | 0.883 | 0ms | 11.3ms |
| **qwen3-rerank** | **1.000** | **0.985** | 202.8ms | 253.5ms |
| gte-rerank-v2 | 1.000 | 0.962 | **166.7ms** | **219.9ms** |

qwen3-rerank 的首条相关结果排序最好，适合问答准确性优先；gte-rerank-v2 少约 36ms，适合延迟优先。

测试同时发现并修复了生产重排接口错误：原实现把工作区兼容地址替换为不存在的 `/compatible-api/v1/reranks`，真实请求返回 404，导致系统静默回退。现已改为 DashScope 原生 rerank URL、`input` 和 `parameters` 请求结构。修复后端到端响应确认 `rerank_applied=true`。

## 7. RAGAS、忠诚度与幻觉

端到端 24 条均成功返回，平均延迟 1731.75ms，P95 2740.41ms。

### 可回答问题

- 忠诚度 Faithfulness：0.9778（15 个可评分样本）
- Context Precision：1.0000
- Context Recall：1.0000
- Answer Relevancy：0.8235
- 16 条中有 1 条因相关度阈值过严未召回，即“怎样降低 SQL 注入风险”

### 不可回答问题

- 8/8 返回“知识库没有找到足够相关内容”
- 拒答准确率：100%
- 明显幻觉率：0/8 = 0%
- RAGAS Factual Correctness：0.9500

RAGAS 汇总中的 Answer Relevancy 0.5490 不能直接作为全体结论：该指标把正确拒答与原问题语义不相似记为 0。分层后，可回答集为 0.8235。Factual Correctness 在若干中文短答案上给出 0，即使回答和参考答案字面一致，因此报告保留原始分数，但不把总均值 0.5721作为唯一质量判断。

## 8. 限流、超时与熔断

### 限流

同一 IP 突发 20 个 `/api/ask`：

- HTTP 200：4
- HTTP 429：16
- 限流率：80%，与令牌桶容量 4 一致

风险：4 个获准请求占满 4 个 worker 后，429 请求仍在工作队列中等待，约 1.5 秒后才返回。限流结果正确，但拒绝不够快。后续如允许继续优化，应把 IP 令牌桶检查提前到业务线程排队之前，或为 RAG 使用隔离线程池。

### 超时

故障注入令 Embedding 服务延迟 350ms，客户端超时 100ms：

- 2/2 返回 HTTP 502
- 实测约 143ms
- 超时率 100%，无无限等待

### 熔断恢复

故障序列：

| 请求 | 注入状态 | HTTP | 延迟 | 结果 |
|---:|---|---:|---:|---|
| 1 | 上游 500 | 502 | 43.95ms | 记录失败 |
| 2 | 上游 500 | 502 | 41.95ms | 达阈值并打开熔断 |
| 3 | 熔断打开 | 503 | 41.91ms | 未调用上游 |
| 4 | 冷却 2.2 秒、上游恢复 | 200 | 3.21ms | 半开探测成功并复位 |

熔断恢复满足预期。

## 9. 自动化回归

`make -j2 test` 全部通过：

- rag_stage2_test
- rag_stage3_test
- sse_stream_test
- resilience_test
- retrieval_upgrade_test
- recommendation_ranker_test
- api_router_test

## 10. 本次新增与修改

- 增加 65 条隔离检索集和 24 条端到端 RAGAS 集。
- 实现固定、段落、父子、语义四种切割评测实现。
- 增加 Embedding/维度、Rerank 控制变量矩阵与向量缓存。
- 增加线程矩阵脚本，可安全启动和只停止自己创建的 9006 进程。
- 增加限流、超时、熔断恢复故障注入。
- 修复百炼 Rerank 生产接口和请求结构。
- 修复 RAGAS 异步 OpenAI 客户端及 429 重试。

主要入口：

```bash
set -a; source .env; set +a
.venv-ragas/bin/python evaluation/benchmark/benchmark.py
.venv-ragas/bin/python evaluation/benchmark/thread_matrix.py
.venv-ragas/bin/python evaluation/benchmark/resilience_test.py
.venv-ragas/bin/python evaluation/ragas/collect_responses.py
.venv-ragas/bin/python evaluation/ragas/evaluate.py
make -j2 test
```

## 11. 尚未执行的生产变更

本轮是测试与方法比较，仅将已确认错误的 Rerank API 修复到了生产代码。以下推荐项尚未直接切换：

- 生产切割器仍是固定 500/80。
- 生产 Embedding 和索引仍是 text-embedding-v4/1024。
- 默认启动参数仍需部署命令显式传入 `-r 2 -t 4`。

切换语义切割与 qwen3.7 Embedding 会改变索引格式和内容，应使用新索引路径构建、验收后原子切换，并保留旧索引用于回滚。

## 12. 模型接口依据

- [阿里云百炼文本 Embedding 同步接口与维度](https://help.aliyun.com/zh/model-studio/text-embedding-synchronous-api/)
- [阿里云百炼 Embedding 与 Rerank 模型说明](https://help.aliyun.com/zh/model-studio/embedding-rerank-model/)
- [阿里云百炼文本 Rerank API](https://help.aliyun.com/zh/model-studio/text-rerank-api)

## 13. 最佳模型组合专项端到端测试

本节是追加的独立实测，目的是让质量指标和分阶段时延来自同一套模型，而不是拼接前面不同实验的结果。

### 13.1 配置与口径

```text
测试集：24 条端到端金标问题
可回答：16 条
不可回答：8 条
切割：语义切割
Embedding：qwen3.7-text-embedding / 1024 维
向量召回：余弦相似度
Rerank：qwen3-rerank，候选 3，返回 3
生成模型：qwen-plus，temperature=0.2，max_tokens=800
```

当前真实知识库只有 3 个短文档，因此每份文档在本轮都可作为一个完整语义单元，向量检索的规模很小。这里的阶段时延能反映远程模型开销，但 0.57ms 的向量检索耗时不能外推到十万级索引。

### 13.2 检索与回答指标

| 指标 | 全部 24 条 | 可回答 16 条 | 不可回答 8 条 |
|---|---:|---:|---:|
| Recall@1 | — | **1.0000** | 不适用 |
| Recall@3 | — | **1.0000** | 不适用 |
| MRR | — | **1.0000** | 不适用 |
| Faithfulness | 0.9243 | 0.8637 | 1.0000 |
| Answer Relevancy | 0.6306 | **0.9458** | 0.0000 |
| Context Precision | 0.6667 | **1.0000** | 0.0000 |
| Context Recall | 0.9583 | **1.0000** | 0.8750 |
| Factual Correctness | 0.6690 | 0.4908 | 0.9587 |
| 正确拒答率 | — | 不适用 | **100%（8/8）** |
| 明显幻觉率 | — | — | **0%（0/8）** |

RAGAS 对正确拒答存在已知的统计口径影响：回答“无法根据资料回答”与原问题语义不相似，因此 8 条拒答的 Answer Relevancy 被记为 0；这些问题没有相关上下文，Context Precision 也被记为 0。判断可回答能力时应看可回答子集，判断幻觉时应看拒答准确率，不能直接使用全部 24 条的混合平均。

Factual Correctness 在中文短答案上仍存在偏低现象，部分语义正确回答被评为 0。本报告保留 RAGAS 原始结果，同时使用 Faithfulness、人工可验证参考答案和拒答率交叉判断，不将单一指标包装成最终准确率。

### 13.3 每个阶段的时延

所有数值均为 24 条逐请求实测。总时延从开始生成查询向量到完整收到 LLM 回答，不包含离线索引构建。

| 阶段 | 平均 | P50 | P95 | P99 | 平均占比 |
|---|---:|---:|---:|---:|---:|
| 查询向量生成 | 224.78ms | 227.00ms | 245.20ms | 254.15ms | 7.67% |
| 向量召回与排序 | **0.57ms** | 0.56ms | 0.65ms | 0.69ms | 0.02% |
| qwen3-rerank | 180.79ms | 174.96ms | 283.52ms | 300.90ms | 6.17% |
| Prompt 构建 | **0.009ms** | 0.009ms | 0.011ms | 0.011ms | <0.01% |
| qwen-plus 完整返回 | **2523.27ms** | 2081.00ms | 4822.91ms | 8207.50ms | **86.14%** |
| **端到端总计** | **2929.41ms** | **2473.28ms** | **5185.77ms** | **8654.25ms** | 100% |

3 份知识文档的离线 Embedding 构建总耗时为 503.34ms。该耗时发生在索引构建阶段，不进入在线请求总时延。

### 13.4 瓶颈分析

第一瓶颈是大模型生成。qwen-plus 占平均时延的 86.14%，并贡献几乎全部长尾：LLM P99 为 8.21 秒，而端到端 P99 为 8.65 秒。向量检索和 Prompt 拼接合计不到 1ms，即使继续优化 C++ 字符串或余弦计算，对用户体验几乎没有可见改善。

第二层开销是查询 Embedding 和 Rerank，合计平均 405.56ms，占 13.84%。Rerank 把前面的 MRR 从 0.883 提高到 0.985，因此默认保留是合理的；若接口需要极低延迟，可以在“向量首名分数明显领先”时跳过 Rerank，但必须重新测量质量下降。

建议按收益排序优化：

1. 保持 SSE 流式输出并监控 TTFT，让用户在完整回答前先看到首字；完整返回 P99 高不代表首字也同样慢。
2. 缩短 Prompt 和限制回答长度。当前真实文档很短，未来父块增大后，输入 token 会进一步推高生成长尾。
3. 对相似问题使用已有语义缓存，命中时可跳过 Rerank 和 LLM。
4. 对短事实问题评估更快的生成模型，并用同一 24 条集设置质量下限，不能只比较速度。
5. 为高置信度查询增加可配置的 Rerank bypass；低置信度、多文档问题仍使用 qwen3-rerank。
6. 不优先优化 Prompt 构建或当前小索引的向量搜索，因为二者合计占比约 0.02%。

专项测试可通过以下命令复现：

```bash
set -a; source .env; set +a
.venv-ragas/bin/python evaluation/benchmark/best_pipeline.py
.venv-ragas/bin/python evaluation/ragas/evaluate.py \
  --input evaluation/ragas/dataset/collected.best.json
```
