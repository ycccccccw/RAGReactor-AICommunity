# 阶段 4：RAG 与社区双向融合

## 已实现

- `POST /api/ask` 新增布尔参数 `include_community`，默认 `false`。
- 默认模式只检索知识库并继续使用原语义缓存；融合模式同时检索物理隔离的知识索引与社区索引，且不与默认缓存混用。
- `sources` 始终携带 `source_type`、`source_id`、`trust_level`；响应和 SSE 来源事件额外返回 `related_community_posts`。
- Prompt 明确把知识库资料作为主要依据，把社区帖子标为“未经核验的社区经验”。
- `GET /api/community/related?post_id=...` 返回 `related_knowledge` 和 `related_posts`，并排除当前帖子。
- 社区卡片新增“相关内容”和“基于此提问”；后者通过浏览器临时存储传递帖子上下文，并使用不发送到服务器的 `#community` 标记开启社区检索，避免静态文件查询参数和超长 URL。
- 每次成功生成问题向量后，将其作为当前进程内的短期兴趣信号。默认占推荐画像 20%，每 60 分钟衰减一半，最多保留最近 20 次提问；服务重启后自动清空，不污染长期画像。

## 接口示例

```json
{
  "question": "Reactor 模式如何处理高并发？",
  "top_k": 5,
  "stream": true,
  "include_community": true
}
```

关闭社区检索时省略 `include_community`，或显式传 `false`。

## 配置

```text
COMMUNITY_INDEX_PATH=knowledge/index/community-bailian-v4-1024.ragvec
COMMUNITY_QUESTION_INTEREST_WEIGHT=0.20
COMMUNITY_QUESTION_INTEREST_HALF_LIFE_MINUTES=60
```

## 9006 验收

1. 重启新编译的服务并访问 `http://服务器地址:9006/rag.html`。
2. 不勾选“包含社区经验”提问，来源应全部显示“知识库”，行为与原问答一致。
3. 勾选后询问测试帖主题；达到相关度的帖子应显示“社区经验 · 作者”和“未经核验”。
4. 访问社区，点击“相关内容”，应出现相关知识和相关帖子；点击“基于此提问”，应进入问答页、自动勾选社区并预填问题。
5. 连续问某一主题后返回“为你推荐”，该主题应短期升权；等待后影响逐步衰减。

注意：社区索引需要保持最新。发布、修改或删除帖子后执行 `make index-community && ./tools/index_community`，或接入后续后台任务。
