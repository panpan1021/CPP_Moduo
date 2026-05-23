#include "../source/server.hpp"

void HandleClose(Channel *channel)
{
    std::cout << "close: " << channel->Fd() << std::endl;
    channel->Remove();
    delete channel;
}
void HandleRead(Channel *channel)
{
    int fd = channel->Fd();
    char buf[1024] = {0};
    int ret = recv(fd, buf, 1023, 0);
    if (ret <= 0)
    {

        HandleClose(channel);
        return;
    }
    // 为了给人回复
    channel->EnableWrite();
    std::cout << buf << std::endl;
}

void HandleWrite(Channel *channel)
{
    int fd = channel->Fd();
    char *data = "ziyouminzhu";
    int ret = send(fd, data, strlen(data), 0);
    if (ret < 0)
    {
        return HandleClose(channel);
    }
    channel->DisableWrite();
}

void HandleError(Channel *channel)
{
    return HandleClose(channel);
}

void HandleEvent(EventLoop *loop, Channel *channel, uint64_t id)
{
    loop->TimerRefresh(id);
    std::cout << "有一个事件\n";
}

void Acceptor(EventLoop *loop, Channel *lst_channel)
{
    uint64_t timerid = rand() % 10000;
    int fd = lst_channel->Fd();
    int newfd = accept(fd, NULL, NULL);
    if (newfd < 0)
        return;
    Channel *channel = new Channel(newfd, loop);
    channel->SetReadCallBack(std::bind(HandleRead, channel));
    channel->SetWriteCallBack(std::bind(HandleWrite, channel));
    channel->SetCloseCallBack(std::bind(HandleClose, channel));
    channel->SetErrorCallBack(std::bind(HandleError, channel));
    channel->SetEventCallBack(std::bind(HandleEvent, loop, channel, timerid));
    channel->EnableRead();

    loop->TimerAdd(timerid, 10, std::bind(HandleClose, channel));
    channel->EnableRead();
}
int main()
{
    Socket lst_sock;
    lst_sock.CreateServer(8500);

    EventLoop loop;
    Channel channel(lst_sock.Fd(), &loop);
    channel.SetReadCallBack(std::bind(Acceptor, &loop, &channel));
    channel.EnableRead();

    loop.Start();

    lst_sock.Close();
    return 0;
}