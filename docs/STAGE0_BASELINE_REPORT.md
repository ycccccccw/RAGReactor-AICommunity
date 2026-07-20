# 阶段 0 基线确认报告

执行日期：2026-07-20  
工作目录：`/root/RAGReactor-AICommunity`

## 1. 结论

- 服务端目标可编译，现有全部自动化测试通过。
- MySQL 正在运行，登录注册表与 `user_posts` 发帖表可访问。
- 临时账号注册、登录、会话 Cookie 和 CSRF Cookie 的真实链路通过。
- `/api/health`、欢迎页、发布页和社区页运行时访问通过。
- RAG 索引已就绪，但两次真实非流式问答均因上游模型连接超时返回 HTTP 502。
- 未执行真实发帖写入，以避免 `rebuild_community_page()` 改写已有未提交修改的
  `root/community.html`；已确认发帖表结构和现有上传处理路径存在。

因此，阶段 0 的代码基线是稳定的，但“真实模型生成回答”当前存在外部上游阻塞。在开始阶段 1
前可以继续开发不依赖生成模型的 Feed；进入 RAG/社区融合联调前必须解决百炼模型超时。

## 2. 环境基线

| 项目 | 结果 |
|---|---|
| 编译器 | GCC 13.3.0 |
| 构建工具 | GNU Make 4.3 |
| MySQL Client | 8.0.46 |
| MySQL 服务 | 运行中，监听 3306 |
| 运行配置 | `.env` 存在；未在报告中记录密钥 |
| 验证端口 | 9016（避免占用默认 9006） |

## 3. 构建与自动化测试

执行：

```bash
make server
make test
```

结果：

| 测试 | 状态 |
|---|---|
| `api_router_test` | 通过 |
| `rag_stage2_test` | 通过 |
| `rag_stage3_test` | 通过 |
| `sse_stream_test` | 通过 |
| `resilience_test` | 通过 |
| `retrieval_upgrade_test` | 通过 |

`make test` 最终退出码为 0。

## 4. 运行时冒烟测试

| 检查项 | 结果 | 说明 |
|---|---|---|
| `GET /api/health` | HTTP 200 | `rag_configured=true`、`index_ready=true` |
| `GET /welcome.html` | HTTP 200 | 静态入口正常 |
| `GET /upload.html` | HTTP 200 | 发布入口正常 |
| `GET /community.html` | HTTP 200 | 社区入口正常 |
| 注册临时账号 | HTTP 200 | 数据库内确认创建成功 |
| 登录临时账号 | HTTP 200 | 同时获得 sid 与 CSRF Cookie |
| `POST /api/ask` 第一次 | HTTP 502 | 上游模型请求超时 |
| `POST /api/ask` 第二次 | HTTP 502 | 上游模型请求超时 |
| `user_posts` 表 | 正常 | 6 个预期基础字段全部存在 |

临时测试账号已在验证后从数据库删除，没有留下测试用户或帖子。

## 5. 发帖链路确认范围

已确认：

- `root/upload.html` 提供 multipart 表单并携带 CSRF Token。
- `http_conn::handle_upload()` 校验会话、CSRF、文件类型和大小。
- `http_conn::save_post_to_db()` 使用预处理语句写入 `user_posts`。
- 数据库存在 `user_posts(id, username, content_text, file_path, file_type, created_at)`。

未执行真实写入的原因：当前工作树中的 `root/community.html` 在阶段 0 开始前已有未提交修改，
而现有上传成功后会重新生成该文件。阶段 1 将移除这项静态重建依赖，届时应增加完整发帖集成测试。

## 6. 已冻结的设计

- API 与统一内容模型：见 `docs/community-api-contract.md`。
- 数据库基础迁移：见 `migrations/001_ai_community_foundation.sql`。
- 可信度边界：知识内容默认为策展资料；未验证社区内容可推荐但默认不得进入 RAG 上下文。
- RAG 的 `sources` 缺省值保持 `knowledge`，保证旧客户端行为不变。
- 推荐失败依次回退到规则热门和时间倒序；推荐故障不能影响帖子基本浏览。

## 7. 已知风险与下一步入口

1. 百炼 Embedding 与索引健康，但 LLM 请求连续超时。需检查模型端点、配额、网络或超时配置。
2. 当前社区仍是全量静态重建；阶段 1 首先实现动态分页 Feed。
3. 当前迁移只新增表，不修改旧表；执行前后都应备份生产数据库并核对目标库名。
4. 社区事件量增长后需要批量聚合，首版不在同步请求中重算完整用户画像。

