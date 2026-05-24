#include "../source/server.hpp"
#include <unistd.h>
#include <string.h>

int main()
{
    Socket cli_sock;
    if (!cli_sock.CreateClient(8888, "127.0.0.1"))
    {
        ERR_LOG("连接服务器失败");
        return -1;
    }
    DBG_LOG("已连接到服务器 127.0.0.1:8888");

    int count = 0;
    while (1)
    {
        // 发送消息
        std::string msg = "hello server, count=" + std::to_string(++count);
        ssize_t ret = cli_sock.Send(&msg[0], msg.size());
        if (ret < 0)
        {
            ERR_LOG("发送失败");
            break;
        }
        DBG_LOG("发送[%zu字节]: %s", msg.size(), msg.c_str());

        // 接收回声
        char buf[1024] = {0};
        ssize_t n = cli_sock.Recv(buf, sizeof(buf) - 1);
        if (n <= 0)
        {
            ERR_LOG("服务器断开连接");
            break;
        }
        DBG_LOG("收到回声[%zd字节]: %s", n, buf);

        sleep(1);
    }

    cli_sock.Close();
    DBG_LOG("客户端退出");
    return 0;
}