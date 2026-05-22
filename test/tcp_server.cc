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
        channel->Remove();
        HandleClose(channel);
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
}

void HandleEvent(Channel *channel)
{
}

void Acceptor(Poller *poller, Channel *lst_channel)
{
    int fd = lst_channel->Fd();
    int newfd = accept(fd, NULL, NULL);
    if (newfd < 0)
        return;
    Channel *channel = new Channel(newfd, poller);
    channel->SetReadCallBack(std::bind(HandleRead, channel));
    channel->SetWriteCallBack(std::bind(HandleWrite, channel));
    channel->SetCloseCallBack(std::bind(HandleClose, channel));
    channel->EnableRead();
}
int main()
{
    Socket lst_sock;
    lst_sock.CreateServer(8500);

    Poller poller;
    Channel channel(lst_sock.Fd(), &poller);
    channel.SetReadCallBack();

    while (1)
    {
        int newfd = lst_sock.Accept();
        if (newfd < 0)
        {
            continue;
        }
        Socket cli_sock(newfd);
        char buf[1024] = {0};
        int ret = cli_sock.Recv(buf, 1023);
        if (ret < 0)
        {
            cli_sock.Close();
            continue;
        }
        cli_sock.Send(buf, ret);
        cli_sock.Close();
    }
    lst_sock.Close();
    return 0;
}