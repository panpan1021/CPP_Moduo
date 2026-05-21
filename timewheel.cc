#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <memory>
#include <unistd.h>

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;

class TimerTask
{
private:
    uint64_t _id;
    uint32_t _timeout;
    TaskFunc _task_cb;
    ReleaseFunc _release;

public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb) : _id(id),
                                                                 _timeout(delay),
                                                                 _task_cb(cb) {}

    ~TimerTask()
    {
        _task_cb();
        _release();
    }

    void SetRelease(const ReleaseFunc &cb) { _release = cb; }

    uint32_t DelayTime() { return _timeout; }
};

class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;

    int _capacity;
    int _tick;
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<uint64_t, WeakTask> _timers;

private:
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            _timers.erase(it);
        }
    }

public:
    TimerWheel() : _capacity(60),
                   _tick(0),
                   _wheel(_capacity)
    {
    }

    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        _timers[id] = WeakTask(pt);
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }
    void TimerRefresh(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return;
        }
        PtrTask pt = it->second.lock();
        if (!pt)
        {
            return;
        }
        int delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }
    void RunTimerTask()
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();
    }
};

class Test
{
public:
    Test() { std::cout << "构造" << std::endl; }
    ~Test() { std::cout << "析构" << std::endl; }
};
void Deltest(Test *t)
{
    delete t;
}
int main()
{
    TimerWheel tw;
    Test *t = new (Test);
    tw.TimerAdd(888, 5, std::bind(Deltest, t));

    for (int i = 0; i < 5; i++)
    {
        sleep(1);
        tw.TimerRefresh(888);
        tw.RunTimerTask();
        std::cout << "刷新了一下定时任务\n";
    }
    while (1)
    {
        std::cout << "---------------------------\n";
        tw.RunTimerTask();
        sleep(1);
    }
}