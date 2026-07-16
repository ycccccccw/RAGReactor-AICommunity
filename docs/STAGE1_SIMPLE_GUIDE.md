# 阶段一说明：我到底把 WebServer 改成了什么

这份文档专门写给刚开始学习 WebServer、HTTP 和 AI 服务的读者。

看不懂某些代码是正常的。先理解“每个模块负责什么”和“一次请求怎么走完”，再逐步看代码，会容易很多。

---

## 1. 阶段一最终完成了什么

原来的项目主要是一个传统 WebServer，擅长处理：

- 浏览器访问 HTML 页面。
- 图片、视频等静态文件。
- 用户注册和登录。
- Session 登录状态。
- 文件上传。
- MySQL 数据库访问。
- epoll、Reactor、线程池和连接池。

阶段一没有删除这些功能。

阶段一在原功能旁边增加了一套 API 能力，让服务器不仅能返回网页，还能接收程序发送的 JSON 请求，并返回 JSON 或 SSE 流式数据。

现在服务器可以同时处理两类请求：

```text
传统网页请求
    例如：GET /index.html
    返回：HTML、图片、视频等文件

API 请求
    例如：POST /api/ask
    返回：JSON 或 SSE 流式数据
```

当前完成了两个 API：

```text
GET  /api/health    检查服务器是否正常运行
POST /api/ask       接收用户问题并返回 Mock 回答
```

这里的 Mock 意思是“模拟”。

当前还没有真正调用大模型，也没有真正检索知识库。服务器先返回固定的模拟答案，用来验证 HTTP API 和流式发送能力。

---

## 2. 什么是 API

可以把服务器想象成一家餐厅。

传统网页请求类似：

```text
顾客：请给我一份完整菜单页面。
服务器：返回一个 HTML 文件。
```

API 请求类似：

```text
程序：我按照约定格式提交一个问题。
服务器：按照约定格式返回处理结果。
```

API 是客户端和服务器之间的一套约定。约定通常包括：

- 请求使用哪个地址。
- 使用 GET 还是 POST。
- 请求中需要包含哪些数据。
- 服务器用什么格式返回结果。
- 请求出错时返回什么状态码。

例如我们的问答接口约定为：

```http
POST /api/ask
Content-Type: application/json
```

请求正文：

```json
{
  "question": "解释 epoll",
  "top_k": 3,
  "stream": true
}
```

字段含义：

- `question`：用户的问题。
- `top_k`：以后从知识库选出最相关的几个文档片段。
- `stream`：是否使用流式返回。

---

## 3. 什么是 JSON

JSON 是一种结构化文本格式，方便客户端和服务器交换数据。

例如：

```json
{
  "question": "解释 epoll",
  "top_k": 3
}
```

可以简单理解为：

```text
question 对应“解释 epoll”
top_k 对应数字 3
```

阶段一使用服务器已经安装的 Boost.JSON 完成：

- 解析客户端发来的 JSON。
- 检查字段类型。
- 构造服务器返回的 JSON。
- 正确转义中文、引号等特殊字符。

使用成熟 JSON 库比自己用字符串查找安全得多。

---

## 4. 什么是 Mock 回答

现在请求：

```json
{
  "question": "解释 epoll"
}
```

服务器返回：

```json
{
  "request_id": "req-1",
  "question": "解释 epoll",
  "top_k": 5,
  "answer": "当前为 Mock 回答，RAG 服务将在后续阶段接入。",
  "sources": []
}
```

这段答案不是大模型生成的，而是代码中预先写好的测试内容。

为什么第一阶段要使用 Mock：

1. 先确认 WebServer 能正确接收 API 请求。
2. 先确认 JSON 解析和错误处理正确。
3. 先确认 SSE 能逐段发送。
4. 出现问题时，可以判断是 WebServer 的问题还是模型 API 的问题。
5. 没有大模型 API Key 时也能开发和测试。

后续阶段会逐步把 Mock 替换为：

```text
问题向量化
  -> 检索知识库
  -> 构造 Prompt
  -> 调用大模型
  -> 返回真实答案
```

---

## 5. 什么是 SSE 流式响应

SSE 全称是 Server-Sent Events，可以理解为：服务器在一个 HTTP 连接中，连续向客户端推送多条事件。

普通响应是：

```text
服务器把完整答案准备好
    ↓
一次性返回全部答案
```

流式响应是：

```text
服务器得到第一段内容 -> 马上返回第一段
服务器得到第二段内容 -> 马上返回第二段
服务器生成完毕       -> 返回结束事件
```

这和常见 AI 聊天页面中“文字逐渐出现”的效果类似。

你刚才收到的内容是：

```text
event: sources
data: {"request_id":"req-1","documents":[]}

event: delta
data: {"text":"这是 RAGReactor 的 "}

event: delta
data: {"text":"Mock 流式回答。真实知识库将在下一阶段接入。"}

event: done
data: {"finish_reason":"stop"}
```

四个事件分别表示：

### sources

```text
event: sources
```

告诉客户端答案参考了哪些知识库文档。

当前：

```json
"documents": []
```

表示还没有接入知识库，所以来源为空。这是正常结果。

### delta

```text
event: delta
```

表示答案新增了一小段内容。

客户端收到一个 `delta`，就把里面的 `text` 追加到聊天页面。

### done

```text
event: done
```

表示本次回答已经全部生成完毕，客户端可以结束“正在生成”的状态。

---

## 6. SSE 和 EPOLLOUT 有什么关系

`EPOLLOUT` 的意思是：

> 这个 socket 现在可以继续写数据。

SSE 是应用层的数据格式，`EPOLLOUT` 是 Linux 网络 I/O 层的可写通知。

两者关系如下：

```text
RAG/Mock 产生一个 SSE 事件
          ↓
事件放进连接的待发送数据中
          ↓
WebServer 关注这个 socket 的 EPOLLOUT
          ↓
socket 可写时，Sub Reactor 调用 write()
          ↓
使用 send() 发送 SSE 数据
```

为什么不能假设一次 `send()` 能把所有数据发完？

因为 socket 发送缓冲区可能暂时没有足够空间。一次 `send()` 可能：

- 写完全部数据。
- 只写出一部分数据。
- 返回 `EAGAIN`，表示现在暂时不能继续写。

因此代码会记录：

```text
总共需要发送多少字节
已经发送了多少字节
下一次应该从哪个位置继续发送
```

如果遇到 `EAGAIN`：

```text
暂时停止发送
    ↓
继续监听 EPOLLOUT
    ↓
socket 再次可写
    ↓
从上次位置继续发送
```

这就是“部分写入”和“续写”。

---

## 7. 为什么 Mock SSE 要分时间发送

如果把所有 SSE 内容一次性放进一个字符串，再通过一次发送交给客户端，虽然格式看起来像 SSE，但不能充分证明服务器真的支持持续流式输出。

阶段一把四个事件分开发送：

```text
sources
  ↓ 大约 300ms
delta
  ↓ 大约 300ms
delta
  ↓ 大约 300ms
done
```

Sub Reactor 会周期性检查哪些 SSE 连接到了下一次发送时间，然后重新关注该连接的 `EPOLLOUT`。

真实大模型接入后，不再需要固定等待 300ms。模型什么时候产生新 Token，服务器就什么时候把对应的 `delta` 放入发送队列。

---

## 8. 一次 `/api/ask` 请求是怎么运行的

你执行：

```bash
curl -N -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -H 'Accept: text/event-stream' \
  -d '{"question":"解释 epoll","top_k":3,"stream":true}'
```

服务器内部大致经历以下过程。

### 第一步：curl 建立 TCP 连接

```text
curl
  ↓ TCP 连接
WebServer 监听 socket
```

Main Reactor 接受新连接，并把连接交给某个 Sub Reactor。

### 第二步：epoll 通知连接可读

curl 把 HTTP 请求发到服务器后，客户端 socket 中出现可读数据。

```text
客户端 socket 出现数据
    ↓
epoll 返回 EPOLLIN
    ↓
Sub Reactor 读取请求
```

### 第三步：解析 HTTP 请求

`http_conn` 解析：

- 请求方法：`POST`
- 路径：`/api/ask`
- `Content-Type`
- `Accept`
- `Content-Length`
- JSON 请求正文

### 第四步：API 路由分发

服务器发现路径以 `/api/` 开头，将请求交给 `ApiRouter`。

```text
/api/health -> health() 处理
/api/ask    -> ask() 处理
其他 /api/* -> 返回 404
```

### 第五步：校验 JSON

服务器检查：

- JSON 是否合法。
- `question` 是否存在。
- `question` 是否为字符串。
- `question` 是否为空。
- `question` 是否过长。
- `top_k` 是否为整数。
- `top_k` 是否在 1 到 20 之间。
- `stream` 是否为布尔值。

### 第六步：构造 Mock SSE

因为 `stream` 是 `true`，服务器准备：

```text
sources
delta
delta
done
```

### 第七步：Sub Reactor 分段发送

每次到了发送时间：

```text
准备下一段 SSE
    ↓
监听 EPOLLOUT
    ↓
调用 send()
    ↓
curl 立即显示这一段
```

### 第八步：发送 done 并关闭本次连接

最后一个 `done` 事件发送完成后，服务器释放本次流式连接资源，curl 回到命令提示符。

完整流程：

```text
curl
  ↓ HTTP POST
Main Reactor 接受连接
  ↓
Sub Reactor 收到 EPOLLIN
  ↓
http_conn 解析 HTTP
  ↓
ApiRouter 解析 JSON
  ↓
构造 Mock SSE 事件
  ↓
Sub Reactor 定时推进
  ↓
EPOLLOUT + send() 分段发送
  ↓
curl 收到 sources/delta/delta/done
```

---

## 9. 新增了哪些文件

### `api/api_router.h`

定义 API 请求、API 响应和路由器的结构。

可以把它理解为 API 模块的“说明书”：

```text
请求包含什么
响应包含什么
路由器提供什么功能
```

### `api/api_router.cpp`

实现具体 API 逻辑：

- `/api/health`
- `/api/ask`
- JSON 解析
- 参数检查
- Mock 普通回答
- Mock SSE 事件
- 统一 JSON 错误

### `tests/api_router_test.cpp`

自动测试 API 路由：

- 健康检查是否返回 200。
- 正常问题是否返回 200。
- 非法 JSON 是否返回 400。
- SSE 是否包含四个事件。
- 不存在的 API 是否返回 404。
- 错误方法是否返回 405。

### `docs/stage-1-api.md`

记录 API 使用方法和 curl 命令，偏向接口手册。

### `.gitignore`

告诉 Git 不要提交：

- `.env` 密钥文件。
- 运行日志。
- 测试生成的二进制文件。
- 临时文件。

---

## 10. 修改了哪些原文件

### `http/http_conn.h` 和 `http/http_conn.cpp`

这是阶段一最重要的集成位置。

主要增加：

- 识别 API 路径。
- 读取 `Accept` 请求头。
- 支持动态 JSON 响应体。
- 支持 SSE 状态。
- 记录 SSE 当前发送到第几段。
- 记录下一段 SSE 的发送时间。
- 处理部分写入。
- 处理 `EAGAIN`。
- SSE 完成后释放连接。

原来的静态文件、登录、Session、上传和 MySQL 路径仍然保留。

### `sub_reactor.h` 和 `sub_reactor.cpp`

主要增加 SSE 推进能力。

Sub Reactor 除了处理普通 `EPOLLIN/EPOLLOUT`，现在还会检查：

```text
有没有正在等待下一段数据的 SSE 连接？
这个连接是否已经到发送时间？
```

如果到了发送时间，就让连接继续发送下一段。

### `Makefile`

增加：

- 编译 `api/api_router.cpp`。
- 链接 Boost.JSON。
- 头文件变化后重新编译。
- `make test-api` 测试命令。

### `start_server.sh`

原脚本把数据库用户名和密码直接写在脚本中。

现在改成：

```text
从 .env 读取数据库配置
检查必要配置是否存在
再启动服务器
```

这样真实密码不会继续写进 Git 跟踪的脚本。

### `.env.example`

只保存配置格式示例，不保存真实密码。

真正配置保存在 `.env`，并被 `.gitignore` 忽略。

### 日志模块

顺便修复了原代码中几个明确问题：

- 日志线程函数声明返回指针却没有返回值。
- 日志文件名缓冲区可能过小。
- `getcwd()` 的失败结果原来没有检查。

修复后完整编译没有警告。

---

## 11. 请求限制是什么

为了防止客户端发送异常大或异常格式的数据，阶段一增加了限制。

### API 请求体最大 16KiB

```text
16KiB = 16 × 1024 字节
```

超过限制返回：

```http
413 Payload Too Large
```

### question 最大 4096 字节

防止单个问题过大，导致以后 Prompt、日志和模型调用消耗过多资源。

### top_k 范围 1 到 20

防止一次检索返回过多文档片段。

### Content-Type 必须是 application/json

如果客户端发送普通文本却调用 JSON API，返回：

```http
415 Unsupported Media Type
```

---

## 12. 什么是统一错误结构

以前传统 WebServer 出错时可能返回 HTML 或普通字符串。

API 调用者更适合接收固定 JSON 格式：

```json
{
  "error": {
    "code": "INVALID_ARGUMENT",
    "message": "question must not be empty"
  }
}
```

其中：

- `code`：方便程序判断错误类型。
- `message`：方便人理解错误原因。

当前支持的重要状态码：

```text
200  请求成功
400  JSON 或参数错误
404  API 不存在
405  请求方法错误
413  请求体过大
415  Content-Type 错误
500  服务器内部错误
```

---

## 13. 你的原项目亮点还在吗

还在。

阶段一没有删除：

- Main Reactor 和 Sub Reactor。
- epoll。
- `EPOLLIN/EPOLLOUT`。
- 工作线程池。
- MySQL 连接池。
- Session。
- 密码带盐哈希。
- 登录失败限制。
- 单机令牌桶限流。
- 文件上传。
- 静态资源服务。
- 日志系统。

新的 AI API 是建立在这些能力上面的，不是用新代码把旧项目全部替换掉。

最终关系是：

```text
原 WebServer 能力 = 网络和安全底座
RAG 新模块       = AI 业务能力
```

---

## 14. 这次做了哪些测试

### 编译测试

```bash
make -B -j2
```

结果：编译成功，没有错误和警告。

### API 单元测试

```bash
make test-api
```

结果：

```text
api_router_test: all checks passed
```

### 健康检查

```bash
curl -i http://127.0.0.1:9006/api/health
```

结果：HTTP 200。

### 普通 JSON 问答

结果：正确返回 Mock JSON。

### 非法请求

测试了：

- 非法 JSON。
- 空问题。
- 错误 Content-Type。
- 错误 HTTP 方法。
- 不存在的 API。
- 超过 16KiB 的请求体。

都返回了对应状态码和统一 JSON 错误。

### SSE 流式测试

你亲自执行了：

```bash
curl -N -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -H 'Accept: text/event-stream' \
  -d '{"question":"解释 epoll","top_k":3,"stream":true}'
```

成功收到：

```text
sources
delta
delta
done
```

这表示阶段一的主要验收目标已经通过。

### 并发 SSE 测试

同时发起 12 个流式请求，12 个请求都收到了 `done`。

### 中途断开测试

客户端在 SSE 发送完成前断开，然后再次请求健康检查，服务器仍然正常运行。

### 原页面回归

访问 `/` 仍然能够返回原来的 HTML 首页。

---

## 15. 你刚才的验收结果说明了什么

你看到：

```text
event: sources
event: delta
event: delta
event: done
```

至少说明：

1. 服务器成功运行在 9006 端口。
2. MySQL 初始化成功。
3. TCP 连接建立成功。
4. WebServer 正确读取了 POST 请求。
5. `/api/ask` 路由匹配成功。
6. JSON 解析成功。
7. `question`、`top_k` 和 `stream` 校验成功。
8. 服务器进入 SSE 模式。
9. Sub Reactor 分批发送了多个事件。
10. curl 持续接收到了数据。
11. `done` 发送成功。
12. 连接正常结束，服务器没有崩溃。

因此阶段一可以正式判定为完成。

---

## 16. 当前还不能做什么

现在问任何问题，服务器都会返回固定 Mock 内容。

例如：

```text
解释 epoll
今天天气如何
什么是线程池
```

得到的主体回答都一样。

这是因为当前还没有：

- 文档加载。
- 文本切分。
- Embedding。
- 向量存储。
- TopK 检索。
- Prompt 构造。
- 大模型调用。

阶段一只是把以后 AI 数据进出服务器的“道路”修好。

---

## 17. 下一阶段要做什么

下一阶段是知识库和向量检索：

```text
Markdown/TXT 文档
    ↓
DocumentLoader 读取文档
    ↓
TextSplitter 切成小块
    ↓
Mock Embedding 生成测试向量
    ↓
VectorStore 保存向量和文本
    ↓
用户问题生成 Query 向量
    ↓
Cosine Similarity 计算相似度
    ↓
选择 TopK 文档片段
```

第二阶段完成后，`sources` 不再是空数组，而会类似：

```json
{
  "documents": [
    {
      "source": "epoll.md",
      "chunk_index": 2,
      "score": 0.91
    }
  ]
}
```

但回答仍可以先使用 Mock。第三阶段再把检索内容交给真实大模型。

---

## 18. 常用命令

进入项目：

```bash
cd /root/RAGReactor
```

编译：

```bash
make -B -j2
```

运行 API 测试：

```bash
make test-api
```

启动服务器：

```bash
./start_server.sh
```

健康检查：

```bash
curl -i http://127.0.0.1:9006/api/health
```

普通 JSON 问答：

```bash
curl -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -d '{"question":"解释 epoll","top_k":3}'
```

SSE 流式问答：

```bash
curl -N -X POST http://127.0.0.1:9006/api/ask \
  -H 'Content-Type: application/json' \
  -H 'Accept: text/event-stream' \
  -d '{"question":"解释 epoll","top_k":3,"stream":true}'
```

停止前台服务器：

```text
按 Ctrl+C
```

---

## 19. 一句话总结阶段一

阶段一把原来的 C++ 网页服务器扩展成了一个能够接收 JSON API 请求，并通过 Reactor、`EPOLLOUT` 和 SSE 分段返回 AI 风格响应的服务框架；目前答案是 Mock，后续会在这条已经打通的链路中加入知识库检索和真实大模型。
