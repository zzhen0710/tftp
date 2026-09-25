# TFTP-based File Transfer

基于 TFTP（Trivial File Transfer Protocol，简单文件传输协议）的文件传输系统，
客户端与服务器之间通过 UDP 完成文件的上传 / 下载。支持停等传输、超时重传、
块号校验，并遵循 RFC 1123 / RFC 1350 的相关约定。

## 编译

```bash
cmake -B build -S .
cmake --build build
```

生成两个可执行文件：`tftp_server`、`tftp_client`。

## 运行

```bash
# 服务器：目录 [端口]
./tftp_server server_files 8888

# 客户端：IP 昵称 [端口]  （IP 必传）
./tftp_client 127.0.0.1 8888
```

可开多个客户端，互相发消息。

**客户端菜单**：

| 选项 | 作用 |
|------|------|
| 1 | 下载文件（从服务器取） |
| 2 | 上传文件（发给服务器） |
| 3 | 退出 |

---

## 文件结构

```
tftp/
├── CMakeLists.txt
├── README.md
├── tftp_client         # 可执行文件（构建生成）
├── tftp_server         # 可执行文件（构建生成）
├── include/
│   ├── common.h        # 协议 + 工具
│   ├── server.h        # TftpServer
│   └── client.h        # TftpClient
├── src/
│   ├── common.cpp
│   ├── client/
│   │   ├── main.cpp
│   │   ├── client.cpp
│   │   ├── upload.cpp
│   │   └── download.cpp
│   └── server/
│       ├── main.cpp
│       ├── server.cpp
│       ├── do_upload.cpp
│       └── do_download.cpp
├── server_files/       # 服务器端文件区
├── client_files/       # 客户端文件区
└── build/              # CMake 中间文件（不提交 Git）
```

> `build/` 为 CMake 构建目录，存放中间文件（`.o`、缓存、Makefile 等），
> 不属于源码，已在 `.gitignore` 中排除。

---

## 文件业务

### common：双方共用的"协议 + 工具"

- **常量**：`TFTP_PORT`（端口）、`BUF_SIZE`（报文缓冲）、`DATA_SIZE`（数据段）、`NAME_SIZE`/`TEXT_SIZE`
- **操作码**：`OP_RRQ` / `OP_WRQ` / `OP_DATA` / `OP_ACK` / `OP_ERROR`
- **收发函数**：内部 `send_all` / `recv_all`（循环收发）、`recv_with_retry`（收包 + 超时重传 + 次数控制）
- **日志宏**：`LOG`（信息）、`ERR_LOG`（错误，`perror` + 位置）

### server：TFTP 服务器

| 文件 | 业务 |
|------|------|
| `main.cpp` | 程序入口：解析目录/端口，创建并运行 `TftpServer` |
| `server.cpp` | 构造（socket/bind）、析构、`run`（收包 → 分发 RRQ/WRQ）、`send_error` |
| `do_download.cpp` | 处理 RRQ：读本地文件、发 DATA、等 ACK（含超时重传） |
| `do_upload.cpp` | 处理 WRQ：回 ACK(0)、收 DATA、写文件、回 ACK（含超时重传） |

### client：TFTP 客户端

| 文件 | 业务 |
|------|------|
| `main.cpp` | 程序入口：解析 IP/端口，创建并运行 `TftpClient` |
| `client.cpp` | 连接、菜单、`run`（按选项分发） |
| `upload.cpp` | 上传：发 WRQ、循环发 DATA、等 ACK（含超时重传） |
| `download.cpp` | 下载：发 RRQ、循环收 DATA、回 ACK（含超时重传） |

---

## 通信逻辑

**统一包格式**：`opcode(2)` + 具体字段，序列化后经 UDP 收发。

### 下载（RRQ）

```
客户端 → RRQ → 服务器
客户端 ← DATA(1) ← 服务器
客户端 → ACK(1) → 服务器
...（循环，直到 DATA 长度 < 516）
```

### 上传（WRQ）

```
客户端 → WRQ → 服务器
客户端 ← ACK(0) ← 服务器
客户端 → DATA(1) → 服务器
客户端 ← ACK(1) ← 服务器
...（循环，直到数据 < 512，发空包后等最后 ACK）
```

### 关键规则

- **块号**：RRQ 直接收 DATA(1)，`num` 初值 1；WRQ 先收 ACK(0)，`num` 初值 0
- **超时重传**：`recv_with_retry` 统一处理（超时重发上一个包 + 次数控制）
- **RFC 1123**：收到重复/不匹配的包时，绝不因"收到不对的包"重传，只靠超时触发
- **结束标志**：数据长度不足一块（DATA 总长 < 516）

---

## 消息类型（操作码）

| 值 | 类型 | 方向 | 说明 |
|----|------|------|------|
| 1 | `OP_RRQ` | 客户端→服务器 | 读请求（下载） |
| 2 | `OP_WRQ` | 客户端→服务器 | 写请求（上传） |
| 3 | `OP_DATA` | 双向 | 数据包（块号 + 数据） |
| 4 | `OP_ACK` | 双向 | 确认包（块号） |
| 5 | `OP_ERROR` | 双向 | 错误包（错误码 + 信息） |

---

## 技术栈

- **语言 / 标准**：C++11
- **网络**：UDP socket（`socket` / `sendto` / `recvfrom`）
- **协议**：TFTP（RRQ / WRQ / DATA / ACK / ERROR），字节序 `htons` / `ntohs`
- **健壮性**：停等传输、超时重传（`SO_RCVTIMEO`）、次数控制、RFC 1123 / 1350 约定
- **语法 / 特性**：`std::string`、`std::move`、`memcpy`（防非对齐）、RAII、`= delete`
- **构建**：CMake

---

## 版本历史

| 版本 | 说明 |
|------|------|
| Release 1.0 | TFTP 客户端 / 服务器：上传、下载、停等传输 |
| Release 1.1 | 加入超时重传（`recv_with_retry`）、块号校验、RFC 1123 修复 |