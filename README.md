关于eventloop timerwheel poller之间的联系

poller就是个监听的类他负责监听每一个fd

timerwheel是一个有关于task的类,包括task的执行,增删之类的(其实task就是个要你执行的函数了),任意线程都可以调用这个来添加一个任务

eventloop是项目的核心类,它里面包含了poller,_eventfd._eventfd是为了唤醒线程,避免一直阻塞;每一个thread都会有一个这个对象,监听每一个fd,通过调用Start(),死循环 阻塞wait新链接,如果有新链接会直接返回,然后执行回调函数;如果被其他线程给任务了,那么也会执行readcallback
把所有的tasks装到时间轮里面执行

epoll_wait 返回
    │
    ├─ listen_fd 可读 → 有新连接
    │
    ├─ socket 可读    → 收到数据 / 对方关闭
    │
    ├─ event_fd 可读  → 其他线程投递了任务
    │
    └─ timerfd 可读   → 1 秒到了，时间轮转一格

工作流程:
┌─────────────────────────────────────────────────────────┐
│  1. 服务端启动                                           │
│     Socket lst_sock.CreateServer(8888)                   │
│     EventLoop loop.Start() 进入事件循环                   │
│     epoll_wait 等待监听套接字可读                          │
├─────────────────────────────────────────────────────────┤
│  2. 客户端连接                                           │
│     Socket cli_sock.CreateClient(8888)                   │
│     → 三次握手完成                                        │
├─────────────────────────────────────────────────────────┤
│  3. 服务端 accept                                        │
│     lst_channel 读事件触发                                │
│     → lst_sock.Accept() → newfd                          │
│     → 创建 Connection 对象                                │
│     → conn->Established()                                │
│       ├─ 状态 CONNECTING → CONNECTED                     │
│       ├─ _channel.EnableRead() 注册到 epoll              │
│       └─ 回调 OnConnected() 通知上层                      │
│     → push_back 到 g_conns 避免被析构                     │
├─────────────────────────────────────────────────────────┤
│  4. 客户端发送 "hello server, count=1"                    │
│     cli_sock.Send() → send() 系统调用                    │
├─────────────────────────────────────────────────────────┤
│  5. 服务端接收                                           │
│     Connection::HandleRead() 被触发                      │
│     → _socket.NonBlockRecv() 读取数据                    │
│     → _in_buffer.WriteAndPush() 存入缓冲区               │
│     → 回调 OnMessage()                                   │
│       ├─ buf->ReadAsStringAndPop() 取出消息              │
│       └─ conn->Send() 把消息写回 _out_buffer             │
│         → _channel.EnableWrite() 注册写事件               │
├─────────────────────────────────────────────────────────┤
│  6. 服务端发送（回声）                                     │
│     Connection::HandleWrite() 被触发                     │
│     → _socket.NonBlockSend() 发送数据                    │
│     → 发送完 DisableWrite()                              │
├─────────────────────────────────────────────────────────┤
│  7. 客户端接收回声                                        │
│     cli_sock.Recv() → 打印 "hello server, count=1"      │
│     sleep(1) → 继续第 4 步                               │
└─────────────────────────────────────────────────────────┘

================================================================================================================================================================================================问题:
1.类定义与函数实现的顺序问题
解决:
① 调顺序 — 把 TaskFunc、ReleaseFunc 的定义移到 TimerTask 之前

② 加前向声明 — 在 TimerWheel 前面告诉编译器"后面会有 EventLoop 和 Channel 这两个类"，这样 EventLoop* 指针和 unique_ptr<Channel> 成员就能编译通过

③ 拆声明和实现 — TimerWheel 的构造函数和方法里调用了 EventLoop::RunInLoop() 和 new Channel(...)，这些需要完整的类定义。所以：

类里面只留声明（成员变量 + 方法签名）
方法实现移到 EventLoop 和 Channel 全部定义完之后再写
这其实就是 C++ 里处理循环依赖的标准套路：前向声明 + 拆分 .h/.cpp（或者像这里，在同一个头文件里把实现挪到后面）。

2.类成员初始化定义问题
C++ 按声明顺序初始化，但 _loop 在 _channel 之后声明，导致 _channel 拿到野指针

3.在构造函数体内初始化
C++ 里构造函数体内不能"构造"成员对象。

4.找到 bug 了！_pool.Create() 调用时 _thread_count 还是 0，因为 SetThreadCount(2) 是在 TcpServer 构造之后才调用的。所以 _loops 为空，NextLoop() 访问空 vector 导致段错误。

5.WebBench 是一个 HTTP 压测工具，它发送的是标准 HTTP 请求（如 GET / HTTP/1.0\r\nHost: ...\r\n\r\n），但你的 EchoServer 是一个原始 TCP Echo 服务器，它只是把收到的数据原样返回，并不理解 HTTP 协议。

6.问题找到了！ReuseAddress() 在 Bind() 之后调用，但 SO_REUSEADDR 必须在 bind() 之前设置才有效：
