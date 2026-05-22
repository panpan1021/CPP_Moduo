#include <iostream>
#include <string>
#include <regex>

int main()
{
    std::string str = "get /bitejiuyeke/login?user=xiaoming&pass=123123 HTTP/1.1";
    std::smatch matches;

    // std::regex e("(get|head|post|put|delete) ([^?]*).*");
    std::regex e("(get|head|post|put|delete) ([^?]*)\\?(.*) (HTTP/1\\.[01])");

    bool ret = std::regex_match(str, matches, e);

    if (ret == false)
    {
        return -1;
    }
    for (auto &s : matches)
    {
        std::cout << s << std::endl;
    }
    return 0;
}