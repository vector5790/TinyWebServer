# TinyWebServer
## 采用epoll ET模式的多线程 socket server

* 基础的socket通信  
* 采用I/O复用epoll ET模式  
* 多线程
* 用telnet测试

详细函数原型参见游双《Linux高性能服务器编程》
### socket通信的一般步骤  
> 创建 socket , 返回socket 文件描述符,需指明域(本地文件socket还是网络socket),类型(TCP 还是 UDP)  
> 绑定 bind, 绑定socket地址(本地socket文件地址 或 网络地址 IP + port)  
> 监听 listen, 为socket创建监听队列, 新的客户连接将会进入这个队列, 需要指明队列最大长度  
> 接收 accept, 接收客户端链接，返回接收到的 客户socket文件描述符  
> 读写 send/recv, 对客户socket文件描述符 进行 读写操作来进行通信  
> close, 通信结束, 关闭客户socket文件描述符, 整个server结束，也要关闭 server socket文件描述符  
***
### epoll系列系统调用

***
### 多线程

pthread_creat()
***
### telnet测试
test4