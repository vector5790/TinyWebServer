#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <netinet/in.h>
class util_timer;
//用户数据结构：客户端socket地址、socket文件描述符，定时器
struct client_data{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};
class util_timer{
public:
    util_timer():prev(NULL),next(NULL){}

public:
    time_t expire;/*任务的超时时间（绝对时间）*/
    void (*cb_func)(client_data*);/*回调函数*/
    client_data* user_data;/*连接资源*/
    util_timer* prev;/*指向前一个定时器*/
    util_timer* next;/*指向后一个定时器*/
};
/*定时器链表，一个升序、双向链表，且带有头节点和尾节点*/
class sort_timer_lst{
public:
    sort_timer_lst():head(NULL),tail(NULL){}
    /*链表被销毁时，删除其中所有的定时器*/
    ~sort_timer_lst(){
        util_timer* tmp=head;
        while(tmp){
            head=tmp->next;
            delete tmp;
            tmp=head;
        }
    }
    /*将目标定时器添加到链表中*/
    void add_timer(util_timer* timer){
        if(!timer){
            return ;
        }
        if(!head){
            head=tail=timer;
            return ;
        }
        if(timer->expire<head->expire){
            timer->next=head;
            head->prev=timer;
            head=timer;
            return;
        }
        add_timer(timer,head);  
    }
    /*调整定时器在链表中的位置。只考虑被调整的定时器的超时时间延长的情况，即该定时器需要往链表尾部移动*/
    void adjust_timer(util_timer* timer){
        if(!timer){
            return ;
        }
        util_timer* tmp=timer->next;
        /*被调整的目标定时器处在链表尾部，超时值仍然小于下一个定时器的超时值，不用调整*/
        if(!tmp||(timer->expire<tmp->expire)){
            return;
        }
        if(timer==head){
            head=head->next;
            head->prev=NULL;
            timer->next=NULL;
            add_timer(timer,head);
        }
        else{
            timer->prev->next=timer->next;
            timer->next->prev=timer->prev;
            add_timer(timer,timer->next);
        }
    }
    void del_timer(util_timer* timer){
        if(!timer){
            return;
        }
        /*只有一个目标定时器，即目标定时器*/
        if((timer==head)&&(timer==tail)){
            delete timer;
            head=NULL;
            tail=NULL;
            return;
        }
        /*至少有两个定时器，目标定时器为链表的头节点*/
        if(timer==head){
            head=head->next;
            head->prev=NULL;
            delete timer;
            return;
        }
        /*至少有两个定时器，目标定时器为链表的尾节点*/
        if(timer==tail){
            tail=tail->prev;
            tail->next=NULL;
            delete timer;
            return;
        }
        //被删除的定时器在链表内部
        timer->prev->next=timer->next;
        timer->next->prev=timer->prev;
        delete timer;
    }
    void tick(){
        if(!head){
            return;
        }
        time_t cur=time(NULL);/*获得系统当前时间*/
        util_timer* tmp=head;
        while(tmp){
            if(cur<tmp->expire){
                break;
            }
            //当前定时器到期，则调用回调函数，执行定时事件
            tmp->cb_func(tmp->user_data);
            head=tmp->next;
            if(head){
                head->prev=NULL;
            }
            delete tmp;
            tmp=head;
        }
    }
private:
    void add_timer(util_timer* timer,util_timer* lst_head){
        util_timer* prev=lst_head;
        util_timer* tmp=prev->next;
        while(tmp){
            if(timer->expire<tmp->expire){
                prev->next=timer;
                timer->next=tmp;
                tmp->prev=timer;
                timer->prev=prev;
                break;
            }
            prev=tmp;
            tmp=tmp->next;
        }
        if(!tmp){
            prev->next=timer;
            timer->prev=prev;
            timer->next=NULL;
            tail=timer;
        }
    }
private:
    util_timer* head;
    util_timer* tail;
};
#endif