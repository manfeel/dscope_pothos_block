//
// http://blog.csdn.net/big_yellow_duck/article/details/52601543
//

#ifndef _BLOCKINGQUEUE_H_
#define _BLOCKINGQUEUE_H_

#include <mutex>
#include <condition_variable>
#include <deque>
#include <assert.h>

template <typename T>
class BlockingQueue {
public:
    using MutexLockGuard = std::lock_guard<std::mutex>;

    BlockingQueue()
            : _mutex(),
              _notEmpty(),
              _queue()
    {
    }

    BlockingQueue(const BlockingQueue &) = delete;
    BlockingQueue& operator=(const BlockingQueue &) = delete;

    void put(const T &x)
    {
        {
            MutexLockGuard lock(_mutex);
            _queue.push_back(x);
        }
        _notEmpty.notify_one();
    }

    void put(T &&x)
    {
        {
            MutexLockGuard lock(_mutex);
            _queue.push_back(std::move(x));
        }
        _notEmpty.notify_one();
    }

    T take()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _notEmpty.wait(lock, [this]{  return !this->_queue.empty(); });
        assert(!_queue.empty());

        T front(std::move(_queue.front()));
        _queue.pop_front();

        return  front;
    }

    size_t size() const
    {
        MutexLockGuard lock(_mutex);
        return _queue.size();
    }

private:
    mutable std::mutex _mutex;
    std::condition_variable _notEmpty;
    std::deque<T> _queue;
};

#endif  // _BLOCKINGQUEUE_H_