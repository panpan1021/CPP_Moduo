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
        if (ret <= 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            ERR_LOG("socket recv失败");
            return -1;
        }
        return ret;
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
class Poller;
class Channel
{
private:
    int _fd;
    uint32_t _events;
    uint32_t _revents;
    Poller *_poller;
    using EventCallBack = std::function<void()>;
    EventCallBack _read_callback;
    EventCallBack _write_callback;
    EventCallBack _error_callback;
    EventCallBack _close_callback;
    EventCallBack _event_callback;

public:
    Channel(int fd, Poller *poller) : _fd(fd),
                                      _events(0),
                                      _revents(0),
                                      _poller(poller) {}

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
                _read_callback;
            if (_event_callback)
                _event_callback;
        }
        if (_revents & EPOLLOUT)
        {
            if (_write_callback)
                _write_callback;
            if (_event_callback)
                _event_callback;
        }
        if (_revents & EPOLLERR)
        {
            if (_event_callback)
                _event_callback;
            if (_error_callback)
                _error_callback;
        }
        if (_revents & EPOLLHUP)
        {
            if (_event_callback)
                _event_callback;
            if (_close_callback)
                _close_callback;
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
        _epfd = epoll_create1(MAX_EPOLLEVENTS);
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
            return Update(channel, EPOLL_CTL_ADD);
        }
        return Update(channel, EPOLL_CTL_MOD);
    }

    void RemoveEvent(Channel *channel)
    {
        Update(channel, EPOLL_CTL_DEL);
        auto it = _channels.find(channel->Fd());
        if (it != _channels.end())
            _channels.erase(it);
        Update(channel, EPOLL_CTL_DEL);
    }

    void Poll(std::vector<Channel *> active)
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

void Channel::Remove() { _poller->RemoveEvent(this); }

void Channel::Update() { _poller->UpdateEvent(this); }
