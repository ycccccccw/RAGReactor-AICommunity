# Reactor 与信号管道

RAGReactor 使用 Main-Sub Reactor 架构。Main Reactor 负责 accept 新连接，Sub Reactor 使用 epoll 管理客户端连接的 EPOLLIN 和 EPOLLOUT 事件。

进程收到 SIGTERM 时，信号处理函数把信号编号写入 pipefd 的写端。pipefd 的读端已经注册到 epoll，因此会产生 EPOLLIN，主事件循环读取信号并安全退出。

EPOLLIN 表示文件描述符当前有数据可以读取；pipefd 是承载信号数据的文件描述符。pipefd 是对象，EPOLLIN 是对象的可读状态通知。
