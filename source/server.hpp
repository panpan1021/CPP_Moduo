#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <functional>
#include <sys/epoll.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <sys/eventfd.h>
#include <memory>
#include <sys/timerfd.h>

#define INF 0
#define DBG 1
#define ERR 2
#define LOG_LEVEL INF

#define LOG(level, format, ...)                                                             \
    do                                                                                      \
    {                                                                                       \
        if (level < LOG_LEVEL)                                                              \
            break;                                                                          \
        time_t t = time(NULL);                                                              \
        struct tm *ltm = localtime(&t);                                                     \
        char tmp[32] = {0};                                                                 \
        strftime(tmp, 31, "%H:%M:%S", ltm);                                                 \
        fprintf(stdout, "[%s %s:%d] " format "\n", tmp, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)

#define BUFFER_DEFAULT_SIZE 1024
class Buffer
{
private:
    std::vector<char> _buffer;
    uint64_t _read_idx;
    uint64_t _write_idx;

public:
    Buffer() : _buffer(BUFFER_DEFAULT_SIZE),
               _read_idx(0),
               _write_idx(0) {}
    char *Begin() const
    {
        return (char *)&*_buffer.begin();
    }
    char *WritePostition() const
    {
        return _write_idx + Begin();
    }
    char *ReadPosition() const
    {
        return Begin() + _read_idx;
    }
    uint64_t HeadIdleSize() const
    {
        return _read_idx;
    }
    uint64_t TailIdleSize() const
    {
        return _buffer.size() - _write_idx;
    }
    uint64_t ReadAbleSize() const
    {
        return _write_idx - _read_idx;
    }
    void MoveReadoffset(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        _read_idx += len;
    }
    void MoveWriteoffset(uint64_t len)
    {
        assert(len <= TailIdleSize() + ReadAbleSize());
        _write_idx += len;
    }
    void EnsureWriteSpace(uint64_t len)
    {
        if (TailIdleSize() >= len)
            return;
        if (len <= TailIdleSize() + HeadIdleSize())
        {
            uint64_t rsz = ReadAbleSize();
            std::copy(ReadPosition(), ReadPosition() + rsz, Begin());
            _read_idx = 0;
            _write_idx = rsz;
        }
        else
            _buffer.resize(_write_idx + len);
    }
    void Write(const void *data, uint64_t len)
    {
        EnsureWriteSpace(len);
        const char *ptr = (const char *)data;
        std::copy(ptr, ptr + len, (char *)WritePostition());
    }
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteoffset(len);
    }
    void Read(void *buf, uint64_t len)
    {
        assert(len <= ReadAbleSize());
        char *ptr = (char *)buf;
        std::copy((char *)ReadPosition(), (char *)ReadPosition() + len, ptr);
    }
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadoffset(len);
    }
    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str;
        str.resize(len);
        Read(&str[0], len);
        return str;
    }
    std::string ReadAsStringAndPop(uint64_t len)
    {
        std::string res = ReadAsString(len);
        MoveReadoffset(len);
        return res;
    }
    void WriteString(const std::string &data)
    {
        Write((void *)data.c_str(), data.size());
    }
    void WriteStringAndPush(const std::string &data)
    {
        WriteString(data);
        MoveWriteoffset(data.size());
    }
    void WriteBuffer(const Buffer &data)
    {
        Write((data.ReadPosition()), data.ReadAbleSize());
    }
    void WriteBufferAndPush(const Buffer &data)
    {
        WriteBuffer(data);
        MoveWriteoffset(data.ReadAbleSize());
    }

    char *FindCRLF()
    {
        void *res = memchr(ReadPosition(), '\n', ReadAbleSize());
        return (char *)res;
    }
    std::string GetLine()
    {
        char *pos = FindCRLF();
        if (pos == NULL)
        {
            return "";
        }
        return ReadAsString(pos - ReadPosition() + 1);
    }
    std::string GetLinAndPop()
    {
        std::string str = GetLine();
        MoveReadoffset(str.size());
        return str;
    }
    void Clear()
    {
        _read_idx = 0,
        _write_idx = 0;
    }
};

#define MAX_LISTEN 1024
class Socket
{
private:
    int _sockfd;

public:
    Socket() : _sockfd(-1) {}
    Socket(int fd) : _sockfd(fd) {}
    ~Socket() { Close(); }

    int Fd() { return _sockfd; }
    bool Create()
    {
        _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_sockfd < 0)
        {
            ERR_LOG("创建socket失败");
            return false;
        }
        return true;
    }

    bool Bind(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(sockaddr_in);
        int ret = bind(_sockfd, (sockaddr *)&addr, len);
        if (ret < 0)
        {
            ERR_LOG("socket bind失败");
            return false;
        }
        return true;
    }

    bool Listen(int backlog = MAX_LISTEN)
    {
        int ret = listen(_sockfd, backlog);
        if (ret < 0)
        {
            ERR_LOG("socket listen失败");
            return false;
        }
        return true;
    }

    bool Connect(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(sockaddr_in);
        int ret = connect(_sockfd, (sockaddr *)&addr, len);
        if (ret < 0)
        {
            ERR_LOG("socket connect失败");
            return false;
        }
        return true;
    }

    int Accept()
    {
        int newfd = accept(_sockfd, NULL, NULL);
        if (newfd < 0)
        {
            ERR_LOG("socket accept失败");
            return -1;
        }
        return newfd;
    }

    ssize_t Recv(void *buf, size_t len, int flag = 0)
    {
        ssize_t ret = recv(_sockfd, buf, len, flag);
        if (ret > 0)
            return ret;
        if (ret == 0)
        {
            DBG_LOG("对端关闭连接");
            return 0;
        }
        // ret < 0
        if (errno == EAGAIN || errno == EINTR)
        {
            return 0;
        }
        ERR_LOG("socket recv失败, errno=%d", errno);
        return -1;
    }

    ssize_t NonBlockRecv(void *buf, size_t len)
    {
        return Recv(buf, len, MSG_DONTWAIT);
    }

    ssize_t Send(void *buf, size_t len, int flag = 0)
    {
        ssize_t ret = send(_sockfd, buf, len, flag);
        if (ret < 0)
        {
            ERR_LOG("socket send失败");
            return -1;
        }
        return ret;
    }
    ssize_t NonBlockSend(void *buf, size_t len)
    {
        return Send(buf, len, MSG_DONTWAIT);
    }
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }

    bool CreateServer(uint16_t port, const std::string &ip = "0.0.0.0", bool flag = 0)
    {
        if (Create() == false)
            return false;
        if (flag)
            NonBlock();
        if (Bind(ip, port) == false)
            return false;
        if (Listen() == false)
            return false;
        ReuseAddress();
        return true;
    }

    bool CreateClient(uint16_t port, const std::string &ip)
    {
        if (Create() == false)
            return false;
        if (Connect(ip, port) == false)
            return false;
        return true;
    }

    void ReuseAddress()
    {
        int val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(int));
        val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(int));
    }

    void NonBlock()
    {
        int flag = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }
};
class EventLoop;
class Poller;
class Channel
{
private:
    int _fd;
    uint32_t _events;
    uint32_t _revents;
    EventLoop *_loop;
    using EventCallBack = std::function<void()>;
    EventCallBack _read_callback;
    EventCallBack _write_callback;
    EventCallBack _error_callback;
    EventCallBack _close_callback;
    EventCallBack _event_callback;

public:
    Channel(int fd, EventLoop *loop) : _fd(fd),
                                       _events(0),
                                       _revents(0),
                                       _loop(loop) {}

    int Fd() { return _fd; }

    void SetRevents(uint32_t events) { _revents = events; }

    uint32_t Events() { return _events; }

    void SetReadCallBack(const EventCallBack &cb) { _read_callback = cb; }

    void SetWriteCallBack(const EventCallBack &cb) { _write_callback = cb; }

    void SetErrorCallBack(const EventCallBack &cb) { _error_callback = cb; }

    void SetCloseCallBack(const EventCallBack &cb) { _close_callback = cb; }

    void SetEventCallBack(const EventCallBack &cb) { _event_callback = cb; }

    bool ReadAble() { return (_events & EPOLLIN); }

    bool WriteAble() { return (_events & EPOLLOUT); }

    void EnableRead()
    {
        (_events |= EPOLLIN);
        Update();
    }

    void EnableWrite()
    {
        (_events |= EPOLLOUT);
        Update();
    }

    void DisableRead()
    {
        (_events &= ~EPOLLIN);
        Update();
    }

    void DisableWrite()
    {
        (_events &= ~EPOLLOUT);
        Update();
    }

    void DisableAll()
    {
        _events = 0;
        Update();
    }

    void Remove();

    void Update();

    void HandlerEvent()
    {
        if ((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            if (_read_callback)
                _read_callback();
            if (_event_callback)
                _event_callback();
        }
        if (_revents & EPOLLOUT)
        {
            if (_write_callback)
                _write_callback();
            if (_event_callback)
                _event_callback();
        }
        if (_revents & EPOLLERR)
        {
            if (_event_callback)
                _event_callback();
            if (_error_callback)
                _error_callback();
        }
        if (_revents & EPOLLHUP)
        {
            if (_event_callback)
                _event_callback();
            if (_close_callback)
                _close_callback();
        }
    }
};

#define MAX_EPOLLEVENTS 1024
class Poller
{
private:
    int _epfd;
    struct epoll_event _evs[MAX_EPOLLEVENTS];
    std::unordered_map<int, Channel *> _channels;

private:
    void Update(Channel *channel, int op)
    {
        int fd = channel->Fd();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
        {
            ERR_LOG("epoll失败");
        }
        return;
    }

    bool HasChannel(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it == _channels.end())
        {
            return false;
        }
        return true;
    }

public:
    Poller()
    {
        _epfd = epoll_create(MAX_EPOLLEVENTS);
        if (_epfd < 0)
        {
            ERR_LOG("epoll创建失败");
            abort();
        }
    }

    void UpdateEvent(Channel *channel)
    {
        bool ret = HasChannel(channel);
        if (ret == false)
        {
            Update(channel, EPOLL_CTL_ADD);
            _channels[channel->Fd()] = channel;
            return;
        }
        Update(channel, EPOLL_CTL_MOD);
    }

    void RemoveEvent(Channel *channel)
    {
        Update(channel, EPOLL_CTL_DEL);
        auto it = _channels.find(channel->Fd());
        if (it != _channels.end())
            _channels.erase(it);
    }
    // 阻塞获取现在所有事件
    void Poll(std::vector<Channel *> &active)
    {
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            ERR_LOG("epoll wait error:%s\n", strerror(errno));
            abort();
        }
        for (int i = 0; i < nfds; i++)
        {
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());
            it->second->SetRevents(_evs[i].events);
            active.push_back(it->second);
        }
    }
};

class EventLoop
{
private:
    std::thread::id _thread_id;
    int _event_fd;
    Poller _poller;
    using Functor = std::function<void()>;
    std::vector<Functor> _tasks;
    std::mutex _mutex;
    std::unique_ptr<Channel> _event_channel;
    TimerWheel _timer_wheel;

public:
    void RunAllTask()
    {
        std::vector<Functor> functor;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.swap(functor);
        }
        for (auto &f : functor)
        {
            f();
        }
        return;
    }
    static int CreateEventFd()
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0)
        {
            ERR_LOG("failed");
            abort();
        }
        return efd;
    }
    // 被唤醒之后调用的函数,把值读出来避免被一直唤醒
    void ReadEventFd(int efd)
    {
        uint64_t res = 0;
        // 读出来的值没用,只是为了读出来,避免一直被唤醒
        int ret = read(_event_fd, &res, sizeof(res));
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            ERR_LOG("Read EVENT 失败");
            abort();
        }
        return;
    }
    // 唤醒用的函数,把1写进_eventfd
    void WeakUpEventFd()
    {
        uint64_t val = 1;
        int ret = write(_event_fd, &val, sizeof(val));
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return;
            }
            ERR_LOG("Read Event 失败");
            abort();
        }
        return;
    }

public:
    EventLoop() : _thread_id(std::this_thread::get_id()),
                  _event_fd(CreateEventFd()),
                  _event_channel(new Channel(_event_fd, this)),
                  _timer_wheel(this)
    { // 设置setReadcallback的意义是为了在写入1唤醒epoll后,避免一直唤醒,要把1读出来
        _event_channel->SetReadCallBack(std::bind(&EventLoop::ReadEventFd, this, _event_fd));
        // 开始监控可读事件
        _event_channel->EnableRead();
    }

    void RunInLoop(const Functor &cb)
    {
        if (IsInLoop())
        {
            cb();
        }
        else
            QueueInLoop(cb);
    }

    void QueueInLoop(const Functor &cb)
    {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        WeakUpEventFd();
    }

    bool IsInLoop()
    {
        return _thread_id == std::this_thread::get_id();
    }

    void UpdateEvent(Channel *channel)
    {
        return _poller.UpdateEvent(channel);
    }

    void RemoveEvent(Channel *channel) { _poller.RemoveEvent(channel); }

    void TimerAdd(uint64_t id, const uint32_t delay, const TaskFunc &cb) { _timer_wheel.TimerAdd(id, delay, cb); }

    void TimerRefresh(uint64_t id) { _timer_wheel.TimerRefresh(id); }

    void TimerCancel(uint64_t id) { return _timer_wheel.TimerCancel(id); }

    bool HasTimer(uint64_t id) { return _timer_wheel.HasTimer(id); }

    void Start()
    {
        while (true)
        { // ← 改成内部循环
            std::vector<Channel *> actives;
            _poller.Poll(actives);
            for (auto &channel : actives)
                channel->HandlerEvent();
            RunAllTask();
        }
    }
};
void Channel::Remove() { _loop->RemoveEvent(this); }

void Channel::Update() { _loop->UpdateEvent(this); }

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
// 任务类,当被析构的时候会执行析构函数,析构函数中有任务
class TimerTask
{
private:
    uint64_t _id;
    uint32_t _timeout;
    TaskFunc _task_cb;
    ReleaseFunc _release;
    bool _cancel;

public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb) : _id(id),
                                                                 _timeout(delay),
                                                                 _task_cb(cb),
                                                                 _cancel(false) {}

    ~TimerTask()
    {

        if (!_cancel)
        {
            _task_cb();
        }
        _release();
    }
    void Cancel() { _cancel = true; }
    void SetRelease(const ReleaseFunc &cb) { _release = cb; }

    uint32_t DelayTime() { return _timeout; }
};

class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;

    int _capacity;
    int _tick;
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<uint64_t, WeakTask> _timers;
    EventLoop *_loop;
    int _timerfd;
    std::unique_ptr<Channel> _timer_channel;

private:
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            _timers.erase(it);
        }
    }
    static int CreateTimerfd()
    {
        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd < 0)
        {
            ERR_LOG("Timerfd 创建失败");
            abort();
        }
        struct itimerspec itime;
        itime.it_value.tv_sec = 1;
        itime.it_value.tv_nsec = 0;
        itime.it_interval.tv_sec = 1;
        itime.it_interval.tv_nsec = 0;
        timerfd_settime(timerfd, 0, &itime, NULL);
        return timerfd;
    }
    int ReadTimerfd()
    {
        uint64_t timers;
        int ret = read(_timerfd, &timers, 8);
        if (ret < 0)
        {
            ERR_LOG("read time失败");
            abort();
        }
        // 注意返回值,不是ret
        return timers;
    }

public:
    TimerWheel(EventLoop *loop) : _capacity(60),
                                  _tick(0),
                                  _wheel(_capacity),
                                  _loop(loop),
                                  _timerfd(CreateTimerfd()),
                                  _timer_channel(new Channel(_timerfd, _loop))
    {
        _timer_channel->SetReadCallBack(std::bind(&TimerWheel::OnTime, this));
        _timer_channel->EnableRead();
    }

    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        _loop->RunInLoop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
    }

    void TimerRefresh(uint64_t id)
    {
        _loop->RunInLoop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
    }

    void TimerCancel(uint64_t id)
    {
        _loop->RunInLoop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
    }

    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        _timers[id] = WeakTask(pt);
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }

    void TimerRefreshInLoop(uint64_t id)
    {
        auto it = _timers.find(id);

        if (it == _timers.end())
        {
            return;
        }
        PtrTask pt = it->second.lock();
        if (!pt)
        {
            return;
        }
        int delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }

    void TimerCancelInLoop(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            PtrTask pt = (it->second.lock());
            if (pt)
                pt->Cancel();
        }
    }

    // 执行下一个index的任务
    void RunTimerTask()
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();
    }

    void OnTime()
    {

        int times = ReadTimerfd();
        for (int i = 0; i < times; i++)
        {
            RunTimerTask();
        }
    }
    // 线程安全问题

    bool HasTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return false;
        }
        return true;
    }
};
