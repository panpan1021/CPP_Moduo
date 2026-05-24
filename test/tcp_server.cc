#include "../source/server.hpp"
#include <signal.h>
#include <algorithm>
#include <vector>

std::vector<PtrConnection> g_conns;

void OnConnected(const PtrConnection &conn)
{
    DBG_LOG("新连接建立, fd=%d", conn->Fd());
}

void OnMessage(const PtrConnection &conn, Buffer *buf)
{
    std::string msg = buf->ReadAsStringAndPop(buf->ReadAbleSize());
    DBG_LOG("收到消息[%lu字节]: %s", msg.size(), msg.c_str());
    // 回声: 原样返回
    conn->Send(&msg[0], msg.size());
}

void OnClosed(const PtrConnection &conn)
{
    DBG_LOG("连接关闭, fd=%d", conn->Fd());
}

void OnServerClosed(const PtrConnection &conn)
{
    auto it = std::find(g_conns.begin(), g_conns.end(), conn);
    if (it != g_conns.end())
    {
        g_conns.erase(it);
        DBG_LOG("从连接池移除, 当前连接数: %zu", g_conns.size());
    }
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    Socket lst_sock;
    lst_sock.CreateServer(8888, "0.0.0.0", true);

    EventLoop loop;
    Channel channel(lst_sock.Fd(), &loop);
    channel.SetReadCallBack([&lst_sock, &loop]()
                            {
        int newfd = lst_sock.Accept();
        if (newfd < 0)
            return;

        // 设置新连接非阻塞
        fcntl(newfd, F_SETFL, fcntl(newfd, F_GETFL, 0) | O_NONBLOCK);

        // 创建 Connection 对象
        static uint64_t id = 0;
        PtrConnection conn = std::make_shared<Connection>(&loop, ++id, newfd);

        // 设置回调
        conn->SetConnectedCallback(OnConnected);
        conn->SetMessageCallback(OnMessage);
        conn->SetCloseCallback(OnClosed);
        conn->SetSrvCloseCallback(OnServerClosed);

        // 启动连接
        conn->Established();

        // 保存到连接池
        g_conns.push_back(conn);
        DBG_LOG("接受新连接, fd=%d, 当前连接数: %zu", newfd, g_conns.size()); });
    channel.EnableRead();

    DBG_LOG("Echo 服务器启动, 监听端口: 8888");
    loop.Start();

    lst_sock.Close();
    return 0;
}