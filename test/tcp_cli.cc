#include "../source/server.hpp"
#include <unistd.h>
int main()
{
    Socket cli_sock;
    cli_sock.CreateClient(8500, "127.0.0.1");
    while (1)
    {
        std::string str = "ziyouminzhu";
        cli_sock.Send(&str[0], str.size());
        char buf[1024] = {0};
        cli_sock.Recv(buf, sizeof(buf) - 1);
        DBG_LOG("%s", buf);
        sleep(1);
    }
    return 0;
}