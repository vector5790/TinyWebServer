#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/lock.h"
#include "../CGImysql/sql_connection_pool.h"
template<typename T>
class threadpool{
public:
    threadpool(connection_pool *connPool,int thread_number=8,int max_requests=10000);
    ~threadpool();
    //向请求队列添加任务
    bool append(T* request);
private:
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number;
    int m_max_requests;
    pthread_t* m_threads;
    std::list<T*>m_workqueue;
    locker m_queuelocker;
    sem m_queuestat;//是否有任务需要处理
    bool m_stop;//是否结束线程
    connection_pool *m_connPool;  //数据库
};
template<typename T>
threadpool<T>::threadpool(connection_pool *connPool,int thread_number,int max_requests):
    m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(NULL),m_connPool(connPool){
    if((thread_number<=0)||(max_requests<=0)){
        throw std::exception();
    }
    m_threads=new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }
    //创建thread_number个线程，并设置成脱离线程
    for(int i=0;i<thread_number;i++){
        printf("create the %dth thread\n",i);
        //类对象传递时用this指针，传递给静态函数后，将其转换为线程池类，并调用私有成员函数run
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}
template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop=true;
}
template<typename T>
bool threadpool<T>::append(T* request){
    m_queuelocker.lock();
    if(m_workqueue.size()>m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool=(threadpool*)arg;
    pool->run();
    return pool;
}
template<typename T>
void threadpool<T>::run(){
     while(!m_stop){
         m_queuestat.wait();
         m_queuelocker.lock();
         if(m_workqueue.empty()){
             m_queuelocker.unlock();
             continue;
         }
        T* request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        request->mysql = m_connPool->GetConnection();
        request->process();
        m_connPool->ReleaseConnection(request->mysql);
     }
}
#endif