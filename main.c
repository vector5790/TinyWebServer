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

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define BUFFER_SIZE 1024

struct fds{
    int epollfd;
    int sockfd;
};
/*使用epoll ET模式的文件描述符应该是非阻塞的。如果文件描述符是阻塞的，那么读或写操作会因为没有后续事件而
一直处于阻塞状态*/
int setnonblocking(int fd){
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}
/*将fd上的EPOLLIN和EPOLLET事件注册到epollfd指示的内核事件表中，参数oneshot指示是否注册fd上的EPOLLONESHOT事件*/
void addfd(int epollfd,int fd,int oneshot){
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET;
    if(oneshot){
        event.events|=EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}
//重置该socket上的注册事件，使epoll有机会再次检测到该socket上的的EPOLLIN事件，进而使其他线程有机会为该socket服务
void reset_oneshot(int epollfd,int fd){
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET|EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}
/*工作线程*/
void* worker(void* arg){
    int sockfd=((fds*)arg)->sockfd;
    int epollfd=((fds*)arg)->epollfd;
    printf("start new thread to receive data on fd: %d\n",sockfd);
    char buf[BUFFER_SIZE];
    memset(buf,'\0',BUFFER_SIZE);
    /*循环读取sockfd上的数据直到遇到EAGAIN错误*/
    while(1){
        int ret=recv(sockfd,buf,BUFFER_SIZE-1,0);
        if(ret==0){
            close(sockfd);
            printf("foreiner closed the connection\n");
            break;
        }
        else if(ret<0){
            /*对于非阻塞IO，下列条件成立表示数据已经全部读取完毕。此后要重置注册事件*/
            if(errno==EAGAIN){
                reset_oneshot(epollfd,sockfd);
                printf("read later\n");
                break;
            }
        }
        else{
            printf("get content: %s\n",buf);
            /*休眠5s，模拟数据处理*/
            sleep(5);
        }
    }
    printf("end thread to receiving data on fd: %d\n",sockfd);
}
int main(int argc, char *argv[]){
    if(argc<2){
        printf("usage: %s ip_address port_number",basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);
    //创建套接字
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd>0);
    
    int ret=0;
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_ANY);
    address.sin_port=htons(port);
/*
    //设置端口复用
    int flag=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
*/
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
    int cnt=0;
    while(1){
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number<0){
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
                addfd(epollfd,connfd,true);
            }
            //处理客户连接上接收到的数据
            else if(events[i].events&EPOLLIN){
                pthread_t thread;
                fds fds_for_new_worker;
                fds_for_new_worker.epollfd=epollfd;
                fds_for_new_worker.sockfd=sockfd;
                pthread_create(&thread,NULL,worker,(void*)&fds_for_new_worker);
            }
            else{
                printf("something else happened\n");
            }
        }
    }
    close(epollfd);
    close(listenfd);
    return 0;
}