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
#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define BUFFER_SIZE 1024

struct fds{
    int epollfd;
    int sockfd;
};

extern int addfd(int epollfd,int fd,int oneshot);
extern int removefd(int epollfd,int fd);
void show_error(int connfd,const char* info){
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}
int main(int argc, char *argv[]){
    if(argc<2){
        printf("usage: %s ip_address port_number",basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);

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
    while(true){
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number<0)&&(errno!=EINTR)){
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<number;i++){
            int sockfd=events[i].data.fd;
            //处理新到的客户连接
            if(sockfd==listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addrlength=sizeof(client_address);
                int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
                if(connfd<0){
                    printf("errno is: %d\n",errno);
                    continue;
                }
                if(http_conn::m_user_count>=MAX_FD){
                    show_error(connfd,"Internal server busy");
                    continue;
                }
                /*初始化客户连接*/
                users[connfd].init(connfd,client_address);
            }
            else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
                /*有异常，直接关闭客户连接*/
                users[sockfd].close_conn();
            }
            //处理客户连接上接收到的数据
            else if(events[i].events&EPOLLIN){
                if(users[sockfd].read()){
                    pool->append(users+sockfd);
                }
                else{
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events&EPOLLOUT){
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }
            }
            else{
                printf("something else happened\n");
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}