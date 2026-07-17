# 用户安全设计

用户密码使用 PBKDF2-HMAC-SHA256、独立随机盐和多轮迭代保存，数据库不保存明文密码。注册 SQL 使用 prepared statement，避免把用户输入直接拼接为 SQL。

登录成功后服务器生成随机 Session ID，通过 HttpOnly Cookie 返回。服务端保存 Session 与用户、CSRF Token 和过期时间的映射。

单机限流使用令牌桶算法，对登录、注册、上传和普通请求设置不同速率，超出限制返回 HTTP 429。
