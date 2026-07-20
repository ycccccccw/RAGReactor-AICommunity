# 阶段 1：动态社区 Feed 与行为采集

## 实现内容

- `GET /api/community/feed`：登录后按帖子 ID 倒序游标分页，单页 1～20 条。
- `POST /api/community/action`：采集曝光、打开、停留、点赞、收藏、快速划过和不感兴趣。
- `event_id` 唯一约束保证事件重试不会重复计数。
- 点赞、收藏和不感兴趣保存为用户最终状态，重复操作不会让聚合数字失真。
- 社区页使用 JavaScript 动态加载，并通过 IntersectionObserver 采集曝光与停留。
- `latest` 为明确的时间流；阶段 1 的 `for_you` 自动回退到 `latest`，响应中会标记
  `fallback=true`。个性化排序在阶段 3 启用。
- `COMMUNITY_DYNAMIC_FEED_ENABLED=false` 可将社区入口切回服务端生成的传统时间流。

## 数据库迁移

在项目目录加载 `.env` 后执行：

```bash
set -a
source .env
set +a
mysql -u "$MYSQL_USER" -p "$MYSQL_DATABASE" < migrations/001_ai_community_foundation.sql
```

新增表：

- `community_actions`：不可变行为事件。
- `community_post_stats`：帖子聚合计数。
- `community_user_post_state`：点赞、收藏和不感兴趣最终状态。
- `ai_content_registry`、`user_interest_profiles`：为后续统一索引和画像预留。
- `schema_migrations`：记录迁移版本。

迁移是增量且可重复执行，不修改或删除 `user`、`user_posts` 中的现有数据。

## 验收步骤

1. 执行迁移并使用默认端口启动：

   ```bash
   ./start_server.sh
   ```

2. 登录后打开 `http://服务器地址:9006/community.html`。
3. 连续点击“加载更多”，确认帖子没有重复，直到按钮消失。
4. 点赞或收藏后刷新，确认状态与数字仍然存在。
5. 点击“不感兴趣”，确认帖子立即隐藏且数据库记录 `dislike`。
6. 未登录请求 Feed 应返回 401：

   ```bash
   curl -i 'http://127.0.0.1:9006/api/community/feed?limit=10'
   ```

7. 未带 CSRF 的行为写入应返回 403（需要有效登录 Cookie 才会进入 CSRF 校验）。
8. 设置 `COMMUNITY_DYNAMIC_FEED_ENABLED=false` 并重启，确认社区入口显示传统时间流。

## 回退行为

```text
动态页面/API 正常：community.html -> /api/community/feed
动态功能关闭：     community.html -> 服务端生成 community-legacy.html
个性化尚未启用：   for_you -> latest，并在 JSON 中明确标记 fallback
```

传统时间流只作为运行回退；新功能不再依赖每次发布后重建主 `community.html`。

