#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <pthread.h>
#include <string.h>
#include "./http/http_conn.h"
#include "./threadpool/threadpool.h"
#include "./lock/lock.h"
#include "./timer/lst_timer.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define BUFFER_SIZE 1024
#define TIMESLOT 5             //最小超时单位

#define SYNSQL //同步数据库校验

#define SYNLOG //同步写日志

struct fds{
    int epollfd;
    int sockfd;
};

extern int addfd(int epollfd,int fd,int oneshot);
extern int removefd(int epollfd,int fd);
extern int setnonblocking(int fd);
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd=0;

//信号处理函数
void sig_handler(int sig){
    int save_errno=errno;
    int msg=sig;
    send(pipefd[1],(char*)&msg,1,0);
    errno=save_errno;
}
//设置信号函数
void addsig(int sig,void(handler)(int),bool restart=true){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;
    if(restart){
        sa.sa_flags|=SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);
}
//定时处理任务
void timer_handler(){
    
    timer_lst.tick();
    alarm(TIMESLOT);
}

void cb_func(client_data *user_data){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;

    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}
void show_error(int connfd,const char* info){
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}
int main(int argc, char *argv[]){
#ifdef SYNLOG
    Log::get_instance()->init("./mylog.log", 8192, 2000000, 0); //同步日志模型
#endif
    if(argc<2){
        printf("usage: %s ip_address port_number",basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);

    //单例模式创建数据库连接池
    connection_pool *connPool = connection_pool::GetInstance("localhost", "gzy", "password", "gzydb", 3306, 8);
    //创建线程池
    threadpool<http_conn>* pool=NULL;
    try{
        pool=new threadpool<http_conn>;
    }
    catch(...){
        return 1;
    }
    //预先为每个可能客户连接分配一个http_conn对象
    http_conn* users=new http_conn[MAX_FD];
    assert(users);
    int user_count=0;

    #ifdef SYNSQL
    //初始化数据库读取表
    users->initmysql_result(connPool);
    #endif
    //创建套接字
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd>0);
    int ret=0;
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_ANY);
    address.sin_port=htons(port);

    //设置端口复用
    struct linger tmp={1,0};
    setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    //绑定端口
    ret=bind(listenfd,(struct sockaddr *)&address,sizeof(address));
    assert(ret>=0);
    //监听端口
    ret=listen(listenfd,5);
    assert(ret>=0);
    //创建epoll内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);
    assert(epollfd!=-1);

    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd=epollfd;
    //创建管道（定时器）
    ret=socketpair(PF_UNIX,SOCK_STREAM,0,pipefd);
    assert(ret!=-1);
    //管道写端非阻塞
    setnonblocking(pipefd[1]);
    //管道读端ET非阻塞
    addfd(epollfd,pipefd[0],false);
    //传递给主循环的信号量，只考虑这两种
    addsig(SIGALRM,sig_handler,false);
    addsig(SIGTERM,sig_handler,false);
    
    client_data *users_timer=new client_data[MAX_FD];

    bool timeout=false;
    bool stop_server=false;
    //每隔TIMESLOT时间触发SIGALRM信号
    alarm(TIMESLOT);
    while(!stop_server){
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number<0)&&(errno!=EINTR)){
            LOG_ERROR("%s","epoll failure");
            break;
        }
        for(int i=0;i<number;i++){
            int sockfd=events[i].data.fd;
            //处理新到的客户连接
            if(sockfd==listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addrlength=sizeof(client_address);

                int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
                printf("new client:%d\n",connfd);
                if(connfd<0){
                    LOG_ERROR("%s:errno is: %d","accept error",errno);
                    continue;
                }
                if(http_conn::m_user_count>=MAX_FD){
                    show_error(connfd,"Internal server busy");
                    LOG_ERROR("%s","Internal server busy");
                    continue;
                }
                /*初始化客户连接*/
                users[connfd].init(connfd,client_address);

                users_timer[connfd].address=client_address;
                users_timer[connfd].sockfd=connfd;
                util_timer *timer=new util_timer;
                timer->user_data=&users_timer[connfd];
                timer->cb_func=cb_func;
                time_t cur=time(NULL);
                timer->expire=cur+3*TIMESLOT;
                users_timer[connfd].timer=timer;
                timer_lst.add_timer(timer);
                printf("done\n");
            }
            else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
                /*有异常，直接关闭客户连接*/
                //users[sockfd].close_conn();
                util_timer *timer=users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                if(timer){
                    timer_lst.del_timer(timer);
                }
            }
            else if((sockfd==pipefd[0])&&(events[i].events&EPOLLIN)){
                int sig;
                char signals[1024];
                ret=recv(pipefd[0],signals,sizeof(signals),0);
                if(ret==-1||ret==0){
                    continue;
                }
                else{
                    for(int i=0;i<ret;++i){
                        switch(signals[i]){
                            case SIGALRM:{
                                timeout=true;
                                break;
                            }
                            case SIGTERM:{
                                stop_server=true;
                            }
                        }
                    }
                }
            }
            //处理客户连接上接收到的数据
            else if(events[i].events&EPOLLIN){
                util_timer *timer=users_timer[sockfd].timer;

                if(users[sockfd].read()){
                    LOG_INFO("deal with the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    pool->append(users+sockfd);
                    if(timer){
                        time_t cur=time(NULL);
                        timer->expire=cur+3*TIMESLOT;
                        LOG_INFO("%s","adjust time once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);

                    }
                }
                else{
                    //users[sockfd].close_conn();
                    timer->cb_func(&users_timer[sockfd]);
                    if(timer){
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if(events[i].events&EPOLLOUT){
                util_timer *timer=users_timer[sockfd].timer;

                if(!users[sockfd].write()){
                   // users[sockfd].close_conn();
                    
                    timer->cb_func(&users_timer[sockfd]);
                    if(timer){

                        timer_lst.del_timer(timer);
                    }
                }
                else{
                    LOG_INFO("send data to the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    if(timer){
                        time_t cur=time(NULL);
                        timer->expire=cur+3*TIMESLOT;
                        LOG_INFO("%s","adjust time once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
            }

            else{
                printf("something else happened\n");
            }
        }
        if(timeout){
            timer_handler();
            timeout=false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users;
    delete[] users_timer;
    delete pool;
    connPool->DestroyPool();
    return 0;
}