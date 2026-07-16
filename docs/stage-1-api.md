# 阶段 1：WebServer API 化

## 已实现接口

### 健康检查

```bash
curl -i http://127.0.0.1:9006/api/health
```

### Mock JSON 问答

```bash
curl -i -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -d '{"question":"解释 epoll","top_k":3}'
```

### Mock SSE 流式问答

```bash
curl -N -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -H 'Accept: text/event-stream' \
  -d '{"question":"解释 epoll","top_k":3,"stream":true}'
```

服务按顺序增量发送：

```text
sources -> delta -> delta -> done
```

Mock 阶段使用约 250ms 的发送间隔，以验证 Reactor 定时推进、SSE 增量发送、
`EPOLLOUT` 续写和客户端断开处理。真实 LLM 接入后，发送节奏由上游 Token 到达时间决定。

## 请求限制

- `Content-Type` 必须包含 `application/json`。
- API 请求体不超过 16KiB。
- `question` 必须是非空字符串，最大 4096 字节。
- `top_k` 必须是 1 到 20 的整数，默认值为 5。
- `stream` 必须是布尔值。

## 错误结构

```json
{
  "error": {
    "code": "INVALID_ARGUMENT",
    "message": "question must not be empty"
  }
}
```

已覆盖 `400`、`404`、`405`、`413`、`415` 和 `500` 的统一 JSON 表达。

## 构建和测试

```bash
make -j2
make test-api
```

完整集成测试需要 MySQL 正常运行，并设置 `MYSQL_USER`、`MYSQL_PASSWORD` 和
`MYSQL_DATABASE` 后启动服务器。
