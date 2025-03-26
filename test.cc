#include <threadpool.h>

int main()
{
    ThreadPool pool;
    // 用户自己定义线程池的工作模式
    pool.setMode(PoolMode::MODE_CACHED);
    // 开始启动线程池 
    pool.start(4);
}

