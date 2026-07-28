# 全量评测工具

该目录隔离运行分块、Embedding、Rerank、线程压力和韧性测试，不改写生产索引。
历史文档仅通过 Git 读取作为评测语料，不会恢复到网站或 GitHub 文档目录。

```bash
set -a; source .env; set +a
.venv-ragas/bin/python evaluation/benchmark/generate_dataset.py
.venv-ragas/bin/python evaluation/benchmark/benchmark.py
.venv-ragas/bin/python evaluation/benchmark/best_pipeline.py
```

所有远程响应与向量均缓存到 `cache/`（已忽略），原始结果写入 `results/`。
