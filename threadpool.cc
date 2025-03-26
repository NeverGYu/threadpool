#include "threadpool.h"
#include <functional>
#include <thread>
#include <memory>
#include <chrono>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD =  10;

ThreadPool::ThreadPool()
    : initThreadSize_(0)
    , taskSizeThreadHold_(TASK_MAX_THRESHHOLD)
    , poolmode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
    , idleThreadSize_(0)
    , threadSizeThreshold_(THREAD_MAX_THRESHHOLD)
    , curThreadSize_(0)
{}

ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;
    notEmpty_.notify_all();

    // 等待线程池里面的线程都返回     状态: 阻塞状态 | 正在执行状态
    std::unique_lock<std::mutex> lock(taskQueMtx);
    exitCond_.wait(lock,[&]()->bool{ return threads_.size() == 0;});
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
    {
       return;
    }
    
    poolmode_ = mode;
}

// 设置task任务队列的上线阈值
void ThreadPool::setTaskQueMaxThreshold(int threshold)
{
    if (checkRunningState())
    {
       return;
    }
    taskSizeThreadHold_ = threshold;
}

void ThreadPool::setThreadSizeThreshHold(int threshold)
{
    if (checkRunningState())
    {
       return;
    }
    if (poolmode_ == PoolMode::MODE_CACHED)
    {
        threadSizeThreshold_ = threshold;
    }
}

bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}

// 给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx); 

    // 线程通信 等待
    // 用户提交任务，最多不能阻塞超过一秒，否则判定提交失败
    if (!notFull_.wait_for(lock,std::chrono::seconds(1),
    [&]()->bool{ return taskQueue_.size() == taskSizeThreadHold_ ;}))
    {
        std::cerr << "task queue is full, submit task fail." << std::endl;
        return Result(sp,false);
    }
    
    // 如果有空余，把任务放入任务队列
    taskQueue_.emplace(sp);
    taskSize_++;

    // 因为新放了任务，任务队列就非空，notEmppty通知
    notEmpty_.notify_all();

    // cache: 需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
    if (poolmode_ == PoolMode::MODE_CACHED
        && taskSize_ > idleThreadSize_
        && curThreadSize_ > threadSizeThreshold_)
    {
        auto ptr = std::unique_ptr<Thread>(new Thread(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1)));
        int threadID = ptr->getID();
        threads_.emplace(threadID,std::move(ptr));
        threads_[threadID]->start();
        curThreadSize_++;
        idleThreadSize_++;
    }
    

    // 返回任务的Result对象
    return Result(sp);

}
    
// 开启线程池
void ThreadPool::start(int initThreadSize)
{
    // 设置线程池的启动状态
    isPoolRunning_= true;

    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程对象
    for (int i = 0; i < initThreadSize; i++)
    {
        auto ptr = std::unique_ptr<Thread>(new Thread(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1)));
        int threadID = ptr->getID();
        threads_.emplace(threadID,std::move(ptr));
    }
    
    // 启动所有线程
    for (int i = 0; i < initThreadSize; i++)
    {
        threads_[i]->start();    // 执行一个线程函数
        idleThreadSize_++;      // 记录初始线程的数量
    }
}

void ThreadPool::threadFunc(int threadID)
{
    auto lastTime = std::chrono::high_resolution_clock().now();
    // 所有任务必须完成，线程池才可以回收所有资源
    for(;;)
    {
        std::shared_ptr<Task> task;
        {
            // 先获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx);
            // 每一秒中返回一次     怎么区分：超时返回？还是有任务待执行返回
            while (taskQueue_.size() == 0)
            {
                if (!isPoolRunning_)
                {
                    threads_.erase(threadID);
                    std::cout << "threadid:"<< std::this_thread::get_id() << "exit !" << std::endl;
                    exitCond_.notify_all();
                    return;
                }

                if(poolmode_ == PoolMode::MODE_CACHED)
                {
                    // 条件变量, 超时返回了
                    if(std::cv_status::timeout == notEmpty_.wait_for(lock,std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= 60
                            && curThreadSize_ > initThreadSize_)
                        {
                            // 开始回收当前线程
                            // 记录线程数量的相关变量的只修改
                            // 把线程对象从线程列表容器中删除
                            // threadid => thread对象 => 删除
                            threads_.erase(threadID);
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout << "threadid:"<< std::this_thread::get_id() << "exit !" << std::endl;
                            return;
                        }
                            
                    }
                }
                else
                {
                    // 等待notEmpty条件
                    notEmpty_.wait(lock);
                }
              
            }
            

            idleThreadSize_--;
            // 从任务队列中取出一个任务
            task = taskQueue_.front();
            taskQueue_.pop();
            taskSize_--;

            // 如果依然有剩余任务，继续通知其他线程执行任务
            if (taskQueue_.size() > 0)
            {
                notEmpty_.notify_all();
            }

            // 取出一个任务，进行通知，通知可以继续提交任务
            notFull_.notify_all();
        }

        // 当前线程执行这个任务
        if ( task != nullptr)
        {
            // task->run();    // 执行任务; 把任务的返回值setVal方法给到Result
            task->exec();
        }

        idleThreadSize_++;
        lastTime = std::chrono::high_resolution_clock().now();
    }
}

// ------- 线程方法实现 --------
int generateID_ = 0;

int Thread::getID() const
{
    return threadID_;
}

Thread::Thread(ThreadFunc func)
    : threadFunc_(func)
    , threadID_(generateID_++)
{}

void Thread::start()
{
    // 创建一个线程来执行一个线程函数
    std::thread t(threadFunc_,threadID_);
    // 设置分离线程
    t.detach();
    
}

// --------- Task方法实现 ------
Task::Task()
    : result_(nullptr)
{}

void Task::exec()
{
    result_->setVal(run());
}

void Task::setResult(Result *res)
{
    result_ = res;
}

// --------- Result方法实现 ------
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : isVaild_(isValid)
    , task_(task)
{
    task->setResult(this);
}

Any Result::get()
{
    if (!isVaild_)
    {
        return "";
    }
    
    sem_.wait();
    return std::move(any_);
}

void Result::setVal(Any any)
{
    // 存储task的返回值
    this->any_ = std::move(any);
    sem_.post();
}