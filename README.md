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

================================================================================================================================================================================================问题:
1.类定义与函数实现的顺序问题
解决:
① 调顺序 — 把 TaskFunc、ReleaseFunc 的定义移到 TimerTask 之前

② 加前向声明 — 在 TimerWheel 前面告诉编译器"后面会有 EventLoop 和 Channel 这两个类"，这样 EventLoop* 指针和 unique_ptr<Channel> 成员就能编译通过

③ 拆声明和实现 — TimerWheel 的构造函数和方法里调用了 EventLoop::RunInLoop() 和 new Channel(...)，这些需要完整的类定义。所以：

类里面只留声明（成员变量 + 方法签名）
方法实现移到 EventLoop 和 Channel 全部定义完之后再写
这其实就是 C++ 里处理循环依赖的标准套路：前向声明 + 拆分 .h/.cpp（或者像这里，在同一个头文件里把实现挪到后面）。

