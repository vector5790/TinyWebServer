#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log(){
    m_count=0;
    m_is_async=false;
    pthread_mutex_init(m_mutex,NULL);
}
Log::~Log(){
    if(m_fp!=NULL){
        fclose(m_fp);
    }
    pthread_mutex_destroy(m_mutex);
    if(m_mutex!=NULL){
        delete m_mutex;
    }
}
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size){
    //写入方式通过初始化时是否设置队列大小（表示在队列中可以放几条数据）来判断，若队列大小为0，则为同步，否则为异步。
    if(max_queue_size>0){
        m_is_async=true;
        m_log_queue=new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid,NULL,flush_log_thread,NULL);
    }
    m_log_buf_size=log_buf_size;
    m_buf=new char[m_log_buf_size];
    memset(m_buf,'\0',sizeof(m_buf));
    m_split_lines=split_lines;

    time_t t=time(NULL);
    struct tm *sys_tm=localtime(&t);
    struct tm my_tm=*sys_tm;

    const char *p=strrchr(file_name,'/');
    char log_full_name[300]={0};

    if(p==NULL){
        snprintf(log_full_name,298,"%d_%02d_%02d_%s",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,file_name);
    }
    else{
        strcpy(log_name,p+1);
        strncpy(dir_name,file_name,p-file_name+1);

        snprintf(log_full_name,298,"%s%d_%02d_%02d_%s",dir_name,my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,log_name);
    }
    m_today=my_tm.tm_mday;
    m_fp=fopen(log_full_name,"a");
    if(m_fp==NULL){
        return false;
    }
    return true;
}

void Log::write_log(int level, const char *format, ...){
    struct timeval now={0,0};
    gettimeofday(&now,NULL);
    time_t t=now.tv_sec;
    struct tm *sys_tm=localtime(&t);
    struct tm my_tm=*sys_tm;
    char s[16]={0};
    switch(level){
        case 0:
            strcpy(s,"[debug]:");
            break;
        case 1:
            strcpy(s,"[info]:");
            break;
        case 2:
            strcpy(s,"[warn]:");
            break;
        case 3:
            strcpy(s,"[errno]:");
            break;
        default:
            strcpy(s,"[info]:");
            break;
    }
    pthread_mutex_lock(m_mutex);
    m_count++;
    //my_tm.tm_mday 是当前时间，m_today是创建日志时的时间
    if(m_today!=my_tm.tm_mday||m_count%m_split_lines==0){
        char new_log[260]={0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16]={0};
        snprintf(tail,16,"%d_%02d_%02d_",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday);
        //如果日志创建时间不是今天，则创建今天的日志
        if(m_today!=my_tm.tm_mday){
            snprintf(new_log,259,"%s%s%s",dir_name,tail,log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        //日志行数超过了最大行，名字加上后缀编号（m_count/m_split_lines）
        else{
            snprintf(new_log,259,"%s%s%s.%lld",dir_name,tail,log_name,m_count/m_split_lines);
        }
        m_fp=fopen(new_log,"a");
    }
    pthread_mutex_unlock(m_mutex);
    va_list valst;
    va_start(valst,format);

    string log_str;
    pthread_mutex_lock(m_mutex);
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    int m=vsnprintf(m_buf+n,m_log_buf_size-1,format,valst);
    m_buf[n+m]='\n';
    m_buf[n+m+1]='\0';
    log_str=m_buf;
    pthread_mutex_unlock(m_mutex);

    //异步日志，将日志信息加入阻塞队列，
    if(m_is_async&&!m_log_queue->full()){
        m_log_queue->push(log_str);
    }
    //同步日志，加锁写入文件
    else{
        pthread_mutex_lock(m_mutex);
        fputs(log_str.c_str(),m_fp);
        pthread_mutex_unlock(m_mutex);
    }
    va_end(valst);
}   

void Log::flush(void){
    pthread_mutex_lock(m_mutex);
    fflush(m_fp);
    pthread_mutex_unlock(m_mutex);
}