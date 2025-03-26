#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>


//  Any类型：可以接受任意数据的类型
class Any
{
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    // 这个构造函数可以让Any类型接受任意其他的数据
    template<typename T>
    Any(T data) : base_(new Derive<T>(data))
    {}

    // 这个方法能把Any对象里面存储的数据提取出来
    template<typename T>
    T case_()
    {
        // 我们怎么从base_找到它所指向的Derive对象，从它里面取出data变量
        Derive<T> *pd  = dynamic_cast<Derive<T>>(base_.get());
        if (pd == nullptr)
        {
            throw "type is incompatiable";
        }
        return pd->data_;
    }
private:
    // 基类类型
    class  Base
    {
    public:
        virtual ~Base()  = default;
    };

    // 派生类类型
    template<typename T>
    class Derive : public Base
    {
    public:
        Derive(T data) : data_(data)
        {}
        T data_;
    };
private:
    // 定义一个基类的指针
    std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore
{
public:
    Semaphore(int limit = 0)
        : resLimit_(limit)
	, isExit(false)
    {}

    ~Semaphore(){
	    isExit = true;
	}

    // 获取一个信号量资源
    void wait()
    {
	if(!isExit)
		return;
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock,[&]()->bool{return resLimit_ > 0;});
        resLimit_--;
    }

    // 增加一个信号量资源
    void post()
    {
	if(!isExit)
		return;
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }

private:
    std::atomic_bool isExit;
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

class Task;
// 实现接受提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    void setVal(Any any);
    Any get();
private:
    Any any_;     // 存储任务的返回值
    Semaphore sem_;  // 线程通信信号量
    std::shared_ptr<Task> task_;  // 指向对应获取返回值对象
    std::atomic_bool isVaild_;  // 返回值是否有效
};

// 任务抽象基类
class Task
{
public:
    Task();
    ~Task() = default;
    void exec();
    void setResult(Result *res);
    // 用户可以自定义任务类型，从Task继承，重写run方法，实现自定义任务处理
    virtual Any run() = 0;
private:
    Result* result_;
};

// 线程池模式
enum class PoolMode{
    MODE_FIXED,
    MODE_CACHED,
};
  
// 线程类型
class Thread{
public:
    using ThreadFunc = std::function<void(int)>;
   
    // 线程构造
    Thread(ThreadFunc func);
    // 线程析构
    ~Thread();

    // 启动线程
    void start();

    // 获取线程id
    int getID() const;
private:
    int threadID_;
    static int generateID_;
    ThreadFunc threadFunc_;
};

// 线程池类型
class ThreadPool
{
public:
    // 线程池构造
    ThreadPool();
    
    // 线程池析构
    ~ThreadPool();

    // 设置线程池的工作模式
    void setMode(PoolMode mode);

    // 设置线程池cache模式下线程阈值
    void setThreadSizeThreshHold(int threshold);

    // 设置task任务队列的上线阈值
    void setTaskQueMaxThreshold(int threshold);

    // 给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);
    
    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());
    
    // 禁止拷贝
    ThreadPool(const ThreadPool& rhs) = delete;
    ThreadPool& operator=(const ThreadPool& rhs) = delete;
private:
    // 定义线程函数
    void threadFunc(int threadID);

    // 检查pool的运行状态
    bool checkRunningState() const;
private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;// 线程列表
    std::size_t initThreadSize_;   // 初始线程数量
    std::atomic_int curThreadSize_; // 记录当前线程池里面线程的总数量
    int threadSizeThreshold_;    // 线程数量上线的阈值
    std::atomic_int idleThreadSize_;   // 记录空闲线程的数量

    std::queue<std::shared_ptr<Task>> taskQueue_;  // 任务队列
    std::atomic_uint taskSize_;    // 任务数量
    int taskSizeThreadHold_;       // 任务数量上限的阈值

    std::mutex taskQueMtx;         // 保证任务队列的线程安全
    std::condition_variable notFull_;   // 表示任务队列不满
    std::condition_variable notEmpty_;  // 表示任务队列不空
    std::condition_variable exitCond_;

    PoolMode poolmode_;

    std::atomic_bool isPoolRunning_;    // 表示当前线程池的启动状态
    
};



