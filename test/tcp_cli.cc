#include "../source/server.hpp"

int main()
{
    Socket cli_sock;
    cli_sock.CreateClient(8500, "127.0.0.1");
    std::string str = "ziyouminzhu";
    cli_sock.Send(&str[0], str.size());
    char buf[1024];
    cli_sock.Recv(buf, 1023);
    DBG_LOG("%s", buf);
    return 0;
}