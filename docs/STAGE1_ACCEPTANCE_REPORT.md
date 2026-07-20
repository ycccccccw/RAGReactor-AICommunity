# 阶段 1 验收报告

执行日期：2026-07-20  
工作目录：`/root/RAGReactor-AICommunity`  
运行端口：`9006`

## 结论

阶段 1 已实现并通过自动化与端到端验收：动态 Feed 可以稳定游标分页，社区行为写入具有
身份校验、CSRF 校验和事件幂等能力，用户点赞/收藏最终状态与聚合统计一致，传统时间流可通过
配置开关恢复。

## 自动化测试

```bash
make server
make test
```

结果：现有 6 组测试全部通过，`make test` 退出码为 0。`api_router_test` 新增覆盖：

- Feed 未登录返回 401。
- Feed 参数、limit 和游标校验。
- Feed JSON、下一页游标和 fallback 标记。
- 行为接口未登录返回 401、CSRF 无效返回 403。
- 行为类型、事件 ID、帖子 ID 和停留时间校验。

## 9006 端到端验收

使用临时账号和 4 条临时帖子验证后，数据已精确清理。

| 项目 | 结果 |
|---|---|
| 注册、登录、CSRF Cookie | 通过 |
| 未登录 Feed | HTTP 401 |
| 第一页 `limit=2` | HTTP 200，帖子 87、86 |
| 第二页 | HTTP 200，帖子 85、84 |
| 两页重复/漏项 | 无 |
| 登录但缺少 CSRF 的行为写入 | HTTP 403 |
| 首次点赞 | HTTP 200 |
| 相同 `event_id` 重试 | HTTP 200，`duplicate=true` |
| 新事件重复点赞 | HTTP 200，不重复增加统计 |
| 最终事件行数 | 2 |
| 最终聚合点赞数 | 1 |
| 用户最终 liked 状态 | 1 |
| 动态社区页面 | HTTP 200 |
| 传统时间流直接入口 | HTTP 200 |
| `COMMUNITY_DYNAMIC_FEED_ENABLED=false` | `/community.html` 返回传统时间流 |

## 数据库状态

`001_ai_community_foundation.sql` 已应用到 `.env` 指向的正式项目数据库。新增 5 个社区/AI
业务表和 1 个迁移记录表，未删除或改写原有用户与帖子。

测试完成后确认：

- 临时用户数量为 0。
- 临时帖子数量为 0。
- 临时行为数量为 0。
- 9006 验收服务已停止，方便用户按正常方式启动。

## 阶段边界

阶段 1 的 `for_you` 暂时使用 `latest` 时间流回退，并在响应中返回
`fallback=true`、`fallback_reason=personalization_not_enabled`。这属于预期行为；用户兴趣向量、
语义召回和个性化排序将在阶段 2～3 接入。

