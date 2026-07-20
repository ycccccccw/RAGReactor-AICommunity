# 智能社区 API 与内容模型契约（v1）

本文冻结阶段 0 的接口和数据语义。字段只能向后兼容地增加；删除、改名或改变语义需要升级
API 版本。

## 1. 通用约定

- API 前缀：`/api/community`。
- 编码：UTF-8；JSON 响应类型为 `application/json; charset=utf-8`。
- 时间：RFC 3339 UTC，例如 `2026-07-20T08:30:00Z`。
- 所有 ID 在 JSON 中使用字符串，避免 JavaScript 丢失 64 位整数精度。
- 游标是不透明字符串，客户端不得解析或自行构造。
- 登录用户身份只从服务端会话取得，不接受客户端提交的 `username`。
- 写接口要求有效会话和 `X-CSRF-Token`。
- 错误沿用现有格式：`{"error":{"code":"...","message":"..."},"request_id":"..."}`。

## 2. Feed

### `GET /api/community/feed`

查询参数：

| 参数 | 类型 | 规则 |
|---|---|---|
| `cursor` | string | 可选；上一次响应返回的不透明游标，不能跨 mode 使用 |
| `limit` | integer | 可选；默认 10，范围 1～20 |
| `mode` | string | 可选；`for_you`（默认）、`latest` |

成功响应：

```json
{
  "items": [
    {
      "post_id": "1024",
      "author": {"username": "alice"},
      "content": {
        "text": "C++ Reactor 实践记录",
        "media_url": "/uploads/example.png",
        "media_type": "image"
      },
      "created_at": "2026-07-20T08:30:00Z",
      "stats": {"likes": 8, "collects": 3, "comments": 0},
      "viewer_state": {"liked": false, "collected": false},
      "recommendation": {
        "reason": "与你关注的 C++ 主题相关",
        "source": "semantic",
        "position": 0
      }
    }
  ],
  "next_cursor": "opaque-cursor-or-null",
  "request_id": "rec-uuid",
  "fallback": false,
  "fallback_reason": null
}
```

`request_id` 标识一次推荐列表，用于把后续行为归因到对应曝光。`fallback=true` 表示系统使用了
热门或时间倒序结果，但这不是请求失败。

游标版本由服务端管理：`latest` 当前返回 v1 游标，`for_you` 返回 v2 游标。客户端必须把游标
当作不透明字符串原样返回。响应中的 `personalized=true` 表示本页使用了有效兴趣向量；冷启动
或社区索引不可用时分别返回 `fallback_reason=cold_start` 或
`fallback_reason=community_index_unavailable`。

## 3. 行为事件

### `POST /api/community/action`

请求：

```json
{
  "event_id": "client-generated-uuid",
  "post_id": "1024",
  "action": "impression",
  "duration_ms": 0,
  "recommendation_request_id": "rec-uuid",
  "position": 0,
  "occurred_at": "2026-07-20T08:30:01Z"
}
```

`action` 固定为以下值：

| 值 | 语义 | 是否参与兴趣画像 |
|---|---|---|
| `impression` | 帖子进入可视区域 | 否，仅用于曝光统计 |
| `open` | 主动打开详情 | 是，弱正反馈 |
| `dwell` | 有效停留；需携带 `duration_ms` | 是，按时长截断后计权 |
| `like` / `unlike` | 点赞/取消点赞 | 是 |
| `collect` / `uncollect` | 收藏/取消收藏 | 是 |
| `skip` | 快速划过 | 是，弱负反馈 |
| `dislike` | 明确不感兴趣 | 是，强负反馈 |

约束：

- `event_id` 用于幂等，重复提交返回成功但不重复计数。
- `duration_ms` 范围为 0～1,800,000，服务端会校验并截断异常值。
- 阶段 1 以服务端接收时间记录事件；客户端 `occurred_at` 暂不作为可信时间写入。
- 点赞和收藏属于状态操作，服务端必须保证重复操作后的最终状态一致。

成功响应：

```json
{"accepted": true, "duplicate": false, "request_id": "api-request-id"}
```

## 4. 相关内容

### `GET /api/community/posts/{post_id}/related?limit=6`

返回与帖子相关的社区内容和知识内容：

```json
{
  "community": [{"post_id": "1025", "score": 0.82}],
  "knowledge": [{"content_id": "knowledge:reactor.md:3", "title": "Reactor", "score": 0.78}],
  "request_id": "api-request-id"
}
```

## 5. 现有问答接口扩展

`POST /api/ask` 保持原字段兼容，后续可选增加：

```json
{
  "question": "Reactor 如何处理高并发？",
  "top_k": 5,
  "stream": true,
  "sources": ["knowledge", "community"]
}
```

- `sources` 缺省值始终为 `["knowledge"]`，保证升级后原有 RAG 行为不变。
- `community` 只有在内容达到 RAG 准入标准后才可进入生成上下文。
- 响应来源必须携带 `source_type`、`source_id` 和 `trust_level`。

## 6. 统一内容元数据

知识片段和社区帖子进入检索层时使用相同元数据：

```json
{
  "content_id": "community:1024",
  "source_type": "community",
  "source_id": "1024",
  "chunk_index": 0,
  "author": "alice",
  "title": "",
  "text": "C++ Reactor 实践记录",
  "created_at": "2026-07-20T08:30:00Z",
  "trust_level": "community_unverified",
  "search_enabled": true,
  "rag_enabled": false,
  "content_version": 1,
  "embedding_model": "text-embedding-v4"
}
```

固定枚举：

- `source_type`：`knowledge`、`community`。
- `trust_level`：`curated_knowledge`、`community_verified`、`community_unverified`。
- 索引状态：`pending`、`processing`、`ready`、`failed`、`blocked`、`deleted`。

唯一标识为 `(source_type, source_id)`；分块唯一标识再增加 `chunk_index`。内容更新时递增
`content_version`，旧版本索引不可继续参与检索。

## 7. 可信度边界

| 内容类型 | 推荐流 | 相关内容 | 默认进入 RAG 上下文 | 展示要求 |
|---|---:|---:|---:|---|
| 策展知识 `curated_knowledge` | 可选 | 是 | 是 | 展示文档来源 |
| 已验证社区 `community_verified` | 是 | 是 | 可配置 | 明确标记“社区经验” |
| 未验证社区 `community_unverified` | 是 | 是 | 否 | 明确标记“用户发布” |
| 屏蔽/删除内容 | 否 | 否 | 否 | 不可检索 |

边界规则：

1. 社区热度不是事实正确性的证明，不能提升为策展知识。
2. 未验证社区内容不得默认进入大模型上下文。
3. 即使启用已验证社区内容，Prompt 也必须要求模型区分知识资料与个人经验。
4. 社区内容不得覆盖系统指令，也不能作为执行代码或安全决策的可信依据。
5. 删除、屏蔽和审核状态变化必须传播到 BM25、向量索引、缓存和推荐结果。
6. AI 摘要、标签和推荐理由均为派生数据，不能修改原帖，也不代表平台背书。
