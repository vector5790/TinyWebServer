#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/lock.h"

using namespace std;

class connection_pool{
public:
    static connection_pool *GetInstance(string url,string User,string PassWord,string DatabaseName,int Port,unsigned int MaxConn);
    MYSQL *GetConnection();//获取数据库连接
    bool ReleaseConnection(MYSQL *conn);//释放连接
    void DestroyPool();//销毁所有连接

    int GetFreeConn();
private:
    connection_pool();
    ~connection_pool();

private:
    unsigned int MaxConn;//最大连接数
    unsigned int CurConn;//当前已使用的连接数
    unsigned int FreeConn;//当前空闲的连接数

private:
    pthread_mutex_t lock;//互斥锁
    list<MYSQL *>connList;//连接池
    connection_pool *conn;
    MYSQL *Coo;
    connection_pool(string url,string User,string PassWord,string DatabaseName,int Port,unsigned int MaxConn);
    static connection_pool *connPool;

private:
    string url;//主机地址
    string Port;//数据库端口号
    string User;//登录数据库用户名
    string PassWord;//登录数据库密码
    string DatabaseName;//数据库名
};
#endif