# 阶段二说明：知识库是怎样变成可检索向量的

## 1. 阶段二完成了什么

阶段一修好了 AI 数据进出 WebServer 的道路：客户端能够发送 JSON，服务器能够通过 SSE 分段返回内容。

阶段二开始建设知识库，完成下面这条离线链路：

```text
Markdown/TXT 文档
    -> 加载文档
    -> 切成带重叠的小片段
    -> Mock Embedding 生成向量
    -> 保存向量索引文件
    -> 问题生成向量
    -> 余弦相似度 TopK 检索
```

当前仍然没有调用真实大模型。阶段二解决的是“怎样找到与问题最相关的资料”，不是“怎样生成最终回答”。

## 2. 为什么要切分文档

如果把一本很长的文档整体生成一个向量，文档中的很多主题会混在一起，检索结果不够准确，而且以后放进大模型 Prompt 时可能超过长度限制。

因此需要把长文档切成多个 chunk（文本块）。例如：

```text
原文：甲乙丙丁戊己庚辛
chunk_size = 4
overlap = 2

块 0：甲乙丙丁
块 1：丙丁戊己
块 2：戊己庚辛
```

相邻块重复两个字符，这就是 overlap（重叠窗口）。它可以避免一句重要内容刚好处于切分边界时被完全拆散。

代码按 UTF-8 字符边界切分，不会从一个汉字的中间字节切开。

## 3. 什么是 Embedding

Embedding 是把文本转换成一组浮点数：

```text
"pipefd 和 epoll 的关系"
        ↓
[0.12, -0.08, 0.31, ...]
```

这组数叫向量。含义相似的文本，向量方向通常也更接近。

当前实现的是 Mock Embedding，并不具备真实模型的语义理解能力。它会：

- 提取英文单词。
- 提取中文单字和相邻双字特征。
- 使用稳定哈希把特征映射到 256 维空间。
- 对向量执行 L2 归一化。

它的优点是离线、快速、确定、没有 API 费用，适合验证完整工程链路。相同文本每次都会得到完全相同的向量。

后续真实 Embedding Provider 会遵守相同接口，因此 VectorStore 和索引流程不需要重写。

## 4. 什么是余弦相似度

余弦相似度比较两个向量的方向：

```text
cosine(A, B) = A·B / (|A| × |B|)
```

可以简单理解为：

- 越接近 1：越相似。
- 接近 0：关系较小。
- 小于 0：方向相反。

系统把问题向量和所有文本块向量逐个比较，然后返回分数最高的 K 个块。

## 5. TopK 是怎样实现的

假设知识库有 N 个文本块，需要返回最相关的 K 个：

1. 遍历全部 N 个文本块。
2. 计算问题与当前块的余弦相似度。
3. 使用大小不超过 K 的小根堆保存当前最佳结果。
4. 最后按分数从高到低排序。

复杂度：

```text
向量计算：O(N × D)
维护 TopK：O(N log K)
```

D 是向量维度。当前是精确暴力检索，适合小型个人知识库，也能作为以后 HNSW 优化的正确性基线。

## 6. 向量索引怎样保存

VectorStore 可以把索引保存为二进制文件，内容包括：

- 文件魔数 `RAGVEC01`。
- 格式版本。
- 向量维度。
- 文本块数量。
- 文档 ID。
- 来源文件名。
- chunk 编号。
- chunk 原文。
- float 向量。

加载时会检查：

- 魔数和版本是否正确。
- 向量维度是否合理。
- 记录数量是否异常。
- 字符串长度是否异常。
- 文件是否被截断。
- 文件尾是否存在意外数据。

保存时先写临时文件，完整写入后再重命名为正式索引，减少写到一半留下损坏索引的风险。

## 7. 新增模块

```text
ai_rag/rag_types.h
```

定义 Document、DocumentChunk 和 SearchResult 等公共数据结构。

```text
ai_rag/document_loader.*
```

递归加载 Markdown 和 TXT，忽略其他格式，并按路径排序以保证结果稳定。

```text
ai_rag/text_splitter.*
```

按照 UTF-8 字符和重叠窗口切分文本。

```text
ai_rag/embedding_provider.*
```

定义 EmbeddingProvider 抽象接口，并实现 MockEmbeddingProvider。

```text
ai_rag/vector_store.*
```

负责向量添加、余弦相似度、TopK、序列化和加载。

```text
ai_rag/knowledge_indexer.*
```

把加载、切分、Embedding 和 VectorStore 串成完整索引构建流程。

## 8. 示例知识库

示例文档位于：

```text
knowledge/documents/reactor.md
knowledge/documents/thread_pool.txt
knowledge/documents/security.md
```

分别介绍 Reactor/pipefd、线程池和用户安全。

## 9. 构建索引

先编译工具：

```bash
make index-documents
```

构建索引：

```bash
mkdir -p knowledge/index
./tools/index_documents \
  knowledge/documents \
  knowledge/index/sample.ragvec \
  120 20
```

最后两个参数分别是 chunk size 和 overlap。

预期：

```text
Index created: documents=3, chunks=8, dimension=256, provider=mock-hash-v1
```

`knowledge/index/` 是运行生成目录，已被 Git 忽略。

## 10. 手动检索验收

编译检索工具：

```bash
make search-index
```

执行问题：

```bash
./tools/search_index \
  knowledge/index/sample.ragvec \
  'pipefd 如何通过 epoll 处理 SIGTERM 信号' \
  3
```

预期 Top1 来自：

```text
source=reactor.md
```

并且文本包含 `pipefd`、`EPOLLIN` 等相关内容。

再测试安全问题：

```bash
./tools/search_index \
  knowledge/index/sample.ragvec \
  '用户密码如何安全保存' \
  2
```

预期 Top1 来自：

```text
source=security.md
```

## 11. 自动测试

```bash
make test
```

它会运行：

- 阶段一 API 测试。
- Markdown/TXT 加载测试。
- UTF-8 中文切分测试。
- 重叠窗口测试。
- Mock Embedding 确定性和归一化测试。
- 人工向量 TopK 排序测试。
- 示例知识库端到端检索测试。
- 向量保存、重新加载和再次检索测试。

通过时显示：

```text
api_router_test: all checks passed
rag_stage2_test: all checks passed
```

## 12. 当前限制

- Mock Embedding 主要依靠字符和词的重合，不等同于真实语义模型。
- 当前为内存暴力 TopK，大型知识库速度会下降。
- 二进制索引主要面向当前 Linux 部署环境，暂未设计跨大小端格式。
- `/api/ask` 还没有接入 VectorStore，在线回答仍是 Mock。

这些限制符合阶段二范围。下一阶段会把在线问题、TopK 结果、Prompt 和真实 LLM 串起来。

## 13. 一句话总结

阶段二已经能够把 Markdown/TXT 知识库稳定地转换为可持久化向量索引，并对用户问题执行精确余弦 TopK 检索；示例问题能够找到预期文档片段，为下一阶段的真实 RAG 在线问答提供了检索基础。
