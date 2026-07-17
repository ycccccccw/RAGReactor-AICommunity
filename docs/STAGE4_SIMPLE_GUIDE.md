# 阶段 4：前端页面和真正流式响应（小白说明）

## 1. 这一阶段解决了什么

阶段 3 虽然能看到两个 `delta`，但实际过程是：

```text
百炼先生成完整答案 -> C++ 把答案切成两半 -> 再通过 SSE 发送
```

这不是真正的模型流式输出。阶段 4 改成：

```text
百炼生成一个增量 Token
  -> libcurl 立刻收到
  -> 放入有上限的 SSE 发送队列
  -> SubReactor 等到 EPOLLOUT
  -> 通过非阻塞 socket 发给浏览器
  -> 页面文字立即增加
```

同时新增 `root/rag.html`，用户不需要再手写 curl。

## 2. 浏览器怎样访问

先启动服务器：

```bash
cd /root/RAGReactor
./start_server.sh
```

然后在自己电脑浏览器打开：

```text
http://服务器公网IP:9006/rag.html
```

如果云服务器安全组没有放行 9006，需要先添加 TCP 9006 入方向规则。生产环境不建议
直接暴露开发端口，应使用 Nginx、HTTPS 和鉴权；当前页面用于项目开发与面试演示。

## 3. 页面包含什么

- 问题输入框，最大 4096 字节仍由后端校验。
- TopK 选择。
- 发送和停止按钮。
- 服务、检索、生成、完成和错误状态。
- 实时增加的答案文本。
- 来源文件、片段编号、相似度和引用原文。
- 首 Token 时间和总生成时间。
- 当前页面内的简单历史消息。

页面只使用 HTML、CSS 和原生 JavaScript，不需要 npm、Node.js、Vue 或 React。

### 登录态保护

登录请求 `POST /2CGISQL.cgi` 验证成功后，服务器返回 `welcome.html`，并设置：

- `sid`：HttpOnly Session Cookie，JavaScript 无法读取。
- `csrf_token`：用于保护带 Cookie 的 POST 请求。

登录成功页面中增加了“进入 RAG 知识库问答”按钮。点击后跳转 `/rag.html`。

保护不只作用于 HTML 页面：

- 未登录直接访问 `/rag.html`，服务器返回登录页面。
- 未登录调用 `/api/ask`，返回 HTTP 401。
- Session 有效但缺少或伪造 CSRF Token，返回 HTTP 403。
- RAG 页面从 Cookie 读取 `csrf_token`，通过 `X-CSRF-Token` 请求头发送。

Session 保存在服务器内存中，默认 30 分钟过期。服务器重启后原 Session 会失效，需要
重新登录。登录和访问 RAG 页面必须使用相同主机，例如都使用 `8.156.69.74:9006`，
因为浏览器 Cookie 不会在不同域名或 IP 之间共享。

## 4. 为什么不用浏览器 EventSource

浏览器原生 `EventSource` 只方便发送 GET 请求，而 `/api/ask` 是携带 JSON 的 POST。
因此页面使用：

```text
fetch(POST) -> response.body.getReader() -> TextDecoder -> 解析 SSE
```

点击停止按钮时，`AbortController.abort()` 会终止 fetch。浏览器关闭 TCP 连接后，服务端
取消对应的上游百炼请求。

## 5. 后端如何避免悬空指针

模型流线程不保存 `http_conn*`，只保存 `shared_ptr<SseStream>`：

```text
模型线程 ---- shared_ptr ----> SseStream <---- shared_ptr ---- http_conn
```

连接关闭时：

1. `http_conn` 调用 `SseStream::cancel()`。
2. 取消标志变为 true。
3. libcurl 回调停止接收。
4. 后台线程退出并释放自己的 `shared_ptr`。
5. 队列在最后一个持有者释放后自动销毁。

因此后台线程不会访问已经关闭或被连接池复用的 `http_conn`。

## 6. 慢客户端和内存保护

每条流的待发送队列上限为 256 KiB。浏览器接收太慢时，生产者等待消费者腾出空间，
不会让内存无限增加。这叫背压。

服务器最多同时运行 16 条模型流，超过后返回 HTTP 503。每 15 秒没有模型内容时发送
SSE heartbeat，避免中间网络设备把空闲连接提前关闭。

## 7. 上游失败策略

- 连接超时和总请求超时由 libcurl 控制。
- 在尚未收到第一个 Token 时若发生临时错误，自动重试一次。
- 已经收到部分回答后不重试，避免页面出现重复内容。
- 最终失败时返回 `error` 事件，再返回 `done`。

## 8. 如何验收真正流式

命令行：

```bash
curl -N -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -H 'Accept: text/event-stream' \
  -d '{"question":"pipefd 和 EPOLLIN 是什么关系？","top_k":3,"stream":true}'
```

预期看到很多自然长度的 `delta`，而不是固定两个 `delta`。最后的 `done` 包含：

```json
{"finish_reason":"stop","ttft_ms":2718,"total_ms":3679}
```

`ttft_ms` 是模型流请求开始到第一个 Token 的时间，`total_ms` 是完整生成时间。

## 9. 已完成的自动和真实测试

- 四组单元测试全部通过。
- SSE 队列通过 ASan/UBSan 检查。
- `rag.html` 能返回 HTTP 200。
- 真实百炼流返回多个增量 `delta` 和带耗时的 `done`。
- 三个并发真实流全部返回 HTTP 200。
- 客户端生成中途断开后，健康检查仍然正常。
- 未登录页面访问、无 Cookie API 调用和伪造 Cookie 均被拒绝。

## 10. 当前限制

- 当前是页面内单轮历史展示，历史问题不会作为多轮上下文发给模型。
- 后台流任务使用受并发上限保护的独立线程；后续可以改为专用模型任务池。
- 页面已接入内存 Session；生产环境仍应增加 HTTPS、持久化 Session 和更严格的访问控制。
- 社区中的 `/uploads/*` 为公开静态媒体；需要私密文件时应使用签名 URL 或鉴权下载接口。
- 当前没有 Markdown 渲染，模型回答按安全的纯文本显示，避免 XSS。
