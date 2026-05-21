
#include <iostream>
#include <string>
#include <regex>

int main()
{
    std::string str = "/number/1234";
    std::regex e("/number/(\\d+)");

    std::smatch matchs;
    bool ret = std::regex_match(str, matchs, e);
    if (ret == false)
    {

        return -1;
    }

    for (auto &s : matchs)
    {
        std::cout << s << std::endl;
    }

    return 0;
}