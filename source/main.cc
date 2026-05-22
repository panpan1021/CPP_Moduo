#include "server.hpp"

int main()
{
    Buffer buf;
    for (int i = 0; i < 33; i++)
    {
        std::string str = "hello" + std::to_string(i) + '\n';
        buf.WriteStringAndPush(str);
    }
    std::string tmp;
    tmp = buf.ReadAsStringAndPop(buf.ReadAbleSize());
    std::cout << tmp << std::endl;
    return 0;
}