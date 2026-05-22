#include <iostream>
#include <cassert>
#include <typeinfo>
class Any
{
private:
    class holder
    {
    public:
        virtual ~holder() {}
        virtual const std::type_info &type() = 0;
        virtual holder *clone() = 0;
    };
    template <class T>
    class placeholder : public holder
    {
    public:
        placeholder(const T &val) : _val(val)
        {
        }
        const std::type_info &type() override { return typeid(T); }
        holder *clone() override { return new placeholder(_val); }

    public:
        T _val;
    };
    holder *_content;

public:
    Any() : _content(NULL) {};
    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val))
    {
    }

    Any(const Any &other) : _content(other._content ? other._content->clone() : NULL) {}
    ~Any() { delete _content; }

    Any &swap(Any &other)
    {
        std::swap(_content, other._content);
        return *this;
    }
    template <class T>
    T *get()
    {
        assert(typeid(T) == _content->type());
        return &(((placeholder<T> *)_content)->_val);
    }
    template <class T>
    Any &operator=(const T &val)
    {
        Any(val).swap(*this);
        return *this;
    }
    Any &operator=(const Any &other)
    {
        Any(other).swap(*this);
        return *this;
    }
};
int main()
{

    Any a;
    a = 10;
    int *pa = a.get<int>();
    std::cout << *pa << std::endl;
    return 0;
}