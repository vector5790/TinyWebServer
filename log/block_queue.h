#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
using namespace std;

template<class T>
class block_queue{
public:
    block_queue(int max_size=1000){
        if(max_size<=0){
            exit(-1);
        }

        m_max_size=max_size;
        m_array=new T[max_size];
        m_size=0;
        m_front=-1;
        m_back=-1;
        //创建互斥锁和条件变量
        m_mutex=new pthread_mutex_t;
        m_cond=new pthread_cond_t;
        pthread_mutex_init(m_mutex,NULL);
        pthread_cond_init(m_cond,NULL);
    }
    void clear(){
        pthread_mutex_lock(m_mutex);
        m_size=0;
        m_front=-1;
        m_back=-1;
        pthread_mutex_unlock(m_mutex);
    }
    ~block_queue(){
        pthread_mutex_lock(m_mutex);
        if(m_array!=NULL){
            delete m_array;
        }
        pthread_mutex_unlock(m_mutex);
        
        pthread_mutex_destroy(m_mutex);
        pthread_cond_destroy(m_cond);

        delete m_mutex;
        delete m_cond;

    }
    //判断队列是否满了
    bool full()const{
        pthread_mutex_lock(m_mutex);
        if(m_size>=m_max_size){
            pthread_mutex_unlock(m_mutex);
            return true;
        }
        pthread_mutex_unlock(m_mutex);
        return false;
    }
    //判断队伍是否为空
    bool empty() const{
        pthread_mutex_lock(m_mutex);
        if(m_size==0){
            pthread_mutex_unlock(m_mutex);
            return true;
        }
        pthread_mutex_unlock(m_mutex);
        return false;
    }
    //返回队首元素
    bool front(T &value)const{
        pthread_mutex_lock(m_mutex);
        if(m_size==0){
            pthread_mutex_unlock(m_mutex);
            return false;
        }
        value=m_array[m_front];
        pthread_mutex_unlock(m_mutex);
        return true;
    }
    //返回队尾元素
    bool back(T &value)const{
        pthread_mutex_lock(m_mutex);
        if(m_size==0){
            pthread_mutex_unlock(m_mutex);
            return false;
        }
        value=m_array[m_back];
        pthread_mutex_unlock(m_mutex);
        return true;
    }
    int size() const{
        int tmp=0;
        pthread_mutex_lock(m_mutex);
        tmp=m_size;
        pthread_mutex_unlock(m_mutex);
        return tmp;
    }
    int max_size() const{
        int tmp=0;
        pthread_mutex_lock(m_mutex);
        tmp=m_max_size;
        pthread_mutex_unlock(m_mutex);
        return tmp;
    }
    bool push(const T &item){
        pthread_mutex_lock(m_mutex);
        if(m_size>=m_max_size){
            pthread_cond_broadcast(m_cond);
            pthread_mutex_unlock(m_mutex);
            return false;
        }

        m_back=(m_back+1)%m_max_size;
        m_array[m_back]=item;
        m_size++;
        //唤醒所有阻塞线程
        pthread_cond_broadcast(m_cond);
        pthread_mutex_unlock(m_mutex);
        return true;
    }
    bool pop(T &item){
        pthread_mutex_lock(m_mutex);
        while(m_size<=0){
            //当重新抢到互斥锁，pthread_cond_wait返回为0
            if(pthread_cond_wait(m_cond,m_mutex)!=0){
                pthread_mutex_unlock(m_mutex);
                return false;
            }
        }

        m_front=(m_front+1)%m_max_size;
        item=m_array[m_front];
        m_size--;
        pthread_mutex_unlock(m_mutex);
        return true;
    }
    //增加超时处理
    bool pop(T &item,int ms_timeout){
        struct timespec t={0,0};
        struct timeval now={0,0};
        gettimeofday(&now,NULL);
        pthread_mutex_lock(m_mutex);
        if(m_size<=0){
            t.tv_sec=now.tv_sec+ms_timeout/1000;
            t.tv_nsec=(ms_timeout%1000)*1000;
            if(pthread_cond_timedwait(m_cond,m_mutex,&t)!=0){
                pthread_mutex_unlock(m_mutex);
                return false;
            }
        }

        if(m_size<=0){
            pthread_mutex_unlock(m_mutex);
            return false;
        }

        m_front=(m_front+1)%m_max_size;
        item=m_array[m_front];
        m_size--;
        pthread_mutex_unlock(m_mutex);
        return true;
    }
private:
    pthread_mutex_t *m_mutex;
    pthread_cond_t *m_cond;
    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};
#endif