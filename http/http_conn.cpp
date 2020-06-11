#include "http_conn.h"
#include <map>
#include <fstream>
/*定义http响应的一些状态信息*/
const char* ok_200_title="OK";
const char* error_400_title="Bad Request";
const char* error_400_form="Your Request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title="FORBIDDEN";
const char* error_403_form="You do not have permission to get the file from this server.\n";
const char* error_404_title="Not Found";
const char* error_404_form="The requested file was not found on this server.\n";
const char* error_500_title="Internal Error";
const char* error_500_form="There was an unusual problem serving the requested file.\n";
const char* doc_root="/home/knopfler/TinyWevServer/root";
/*设置为非阻塞*/
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
    event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;
    if(oneshot){
        event.events|=EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}
/*从内核事件表中删除描述符*/
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}
void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd=fd;
    event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}
int http_conn::m_user_count=0;
int http_conn::m_epollfd=-1;
void http_conn::close_conn(bool real_close){
    if(real_close&&(m_sockfd!=-1)){
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;
    }
}
void http_conn::init(int sockfd,const sockaddr_in& addr){
    m_sockfd=sockfd;
    m_address=addr;

    addfd(m_epollfd,sockfd,true);
    m_user_count++;
    init();
}
void http_conn::init(){
    m_check_state=CHECK_STATE_REQUESTLINE;
    m_linger=false;

    m_method=GET;
    m_url=0;
    m_version=0;
    m_content_length=0;
    m_host=0;
    m_start_line=0;
    m_check_index=0;
    m_read_index=0;
    m_write_index=0;
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);
}

http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_check_index<m_read_index;++m_check_index){
        temp=m_read_buf[m_check_index];
        if(temp=='\r'){
            if((m_check_index+1)==m_read_index){
                return LINE_OPEN;
            }
            else if(m_read_buf[m_check_index+1]=='\n'){
                m_read_buf[m_check_index++]='\0';
                m_read_buf[m_check_index++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp=='\n'){
            if((m_check_index>1)&&(m_read_buf[m_check_index-1]=='\r')){
                m_read_buf[m_check_index-1]='\0';
                m_read_buf[m_check_index++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}
bool http_conn::read(){
    if(m_read_index>=READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read=0;
    while(true){
        bytes_read=recv(m_sockfd,m_read_buf+m_read_index,READ_BUFFER_SIZE-m_read_index,0);
        if(bytes_read==-1){
            /*以 O_NONBLOCK的标志打开文件/socket/FIFO，如果你连续做read操作而没有数据可读。
            此时程序不会阻塞起来等待数据准备就绪返 回，read函数会返回一个错误EAGAIN，
            提示你的应用程序现在没有数据可读请稍后再试。*/
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                break;
            }
            return false;
        }
        else if(bytes_read==0){
            return false;
        }
        m_read_index+=bytes_read;
    }
    return true;
}
/*解析http请求行，获得请求方法，目标URL，http版本号*/
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    /*strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针*/
    m_url=strpbrk(text," \t");
    if(!m_url){
        return BAD_REQUEST;
    }
    *m_url++='\0';
    char* method=text;
    /*strcasecmp忽略大小写比较字符串*/
    if(strcasecmp(method,"GET")==0){
        printf("GET\n");
        m_method=GET;
    }
    else if(strcasecmp(method,"POST")==0){
        printf("POST\n");
        m_method=POST;
    }
    else return BAD_REQUEST;
    /*size_t strspn(const char *str1, const char *str2) 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    跳过method和URL之间可能存在的空格*/
    m_url+=strspn(m_url," \t");
    m_version=strpbrk(m_url," \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++='\0';
    /* 跳过URL和version之间可能存在的空格*/
    m_version+=strspn(m_version," \t");
    printf("%s\n",m_version);
    if(strcasecmp(m_version,"HTTP/1.1")!=0){
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url,"http://",7)==0){
        m_url+=7;
        /*strchr函数功能为在一个串中查找给定字符的第一个匹配之处*/
        m_url=strchr(m_url,'/');
    }
    printf("url:%s\n",m_url);
    if(!m_url||m_url[0]!='/'){
        return BAD_REQUEST;
    }
    m_check_state=CHECK_STATE_HEADER;
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_header(char* text){
    if(text[0]=='\0'){
        if(m_content_length!=0){
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    /*处理Connection头部字段*/
    else if(strncasecmp(text,"Connection:",11)==0){
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0){
            m_linger=true;
        }
    }
    /*处理Content-Length头部字段*/
    else if(strncasecmp(text,"Content-Length:",15)==0){
        text+=15;
        text+=strspn(text," \t");
        /*atol 字符串转化成长整型*/
        m_content_length=atol(text);
    }
    /*处理Host头部字段*/
    else if(strncasecmp(text,"Host:",5)==0){
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }
    else{
        printf("oop! unknow header %s\n",text);
    }
    return NO_REQUEST;
}
/*解析消息体，只判断是否被完整的读入了*/
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_index>=(m_content_length+m_check_index)){
        text[m_content_length]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
/*主状态机*/
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;
    char* text=0;
    while(((m_check_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status=parse_line())==LINE_OK)){
        text=get_line();
        m_start_line=m_check_index;
        printf("got 1 http line: %s\n",text);
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret=parse_header(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret==GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret=parse_content(text);
                if(ret==GET_REQUEST){
                    return do_request();
                }
                line_status=LINE_OPEN;
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file,doc_root);
    int len=strlen(doc_root);
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    if(stat(m_real_file,&m_file_stat)<0){
        return NO_RESOURCE;
    }
    //判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if(!(m_file_stat.st_mode&S_IROTH)){
        return FORBIDDEN_REQUEST;
    }
    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }
    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd=open(m_real_file,O_RDONLY);
    m_file_address=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}
//取消内存映射
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}
/*写HTTP响应*/
bool http_conn::write(){
    int temp=0;
    int bytes_to_send=m_write_index;
    int bytes_have_send=0;
    if(bytes_to_send==0){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }
    while(1){
        temp=writev(m_sockfd,m_iv,m_iv_count);
        if(temp<=-1){
            //缓冲区没有空间，等待下一轮EPOLLOUT 事件
            if(errno==EAGAIN){
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }    
            unmap();
            return false;
        }
        bytes_to_send-=temp;
        bytes_have_send+=temp;
        if(bytes_to_send<=0){
            unmap();
            if(m_linger){
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else{
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return false;
            }
        }
    }   
}

bool http_conn::add_response(const char* format,...){
    if(m_write_index>=WRITE_BUFFER_SIZE){
        return false;
    }
    //定义可变参数列表
    va_list arg_list;
    //将变量arg_list初始化为传入参数
    va_start(arg_list,format);
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len=vsnprintf(m_write_buf+m_write_index,WRITE_BUFFER_SIZE-1-m_write_index,format,arg_list);
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_index)){
        return false;
    }
    m_write_index+=len;
    //清空可变参列表
    va_end(arg_list);
    return true;
}
bool http_conn::add_content(const char* content){
    return add_response("%s",content);
}
bool http_conn::add_status_line(int status,const char* title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}
bool http_conn::add_headers(int content_length){
    add_content_length(content_length);
    add_linger();
    add_black_line();
}
bool http_conn::add_content_length(int content_length){
    return add_response("Content-Length: %d\r\n",content_length);
}
bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n",(m_linger==true)?"keep-live" : "close");
}
bool http_conn::add_black_line(){
    return add_response("%s","\r\n");
}
bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:{
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:{
            add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        }
        case NO_RESOURCE:{
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:{
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:{
            add_status_line(200,ok_200_title);
            if(m_file_stat.st_size!=0){
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base=m_write_buf;
                m_iv[0].iov_len=m_write_index;
                m_iv[0].iov_base=m_file_address;
                m_iv[0].iov_len=m_file_stat.st_size;
                m_iv_count=2;
                return true;
            }
            else{
                const char* ok_string="<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)){
                   return false;
                }
            }
            break;
        }
        default:{
            return false;
        }
    }
    m_iv[0].iov_base=m_write_buf;
    m_iv[0].iov_len=m_write_index;
    m_iv_count=1;
    return true;
}
void http_conn::process(){
    HTTP_CODE read_ret=process_read();
    /*请求不完整，需要继续接收数据*/
    if(read_ret==NO_REQUEST){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return ;
    }
    bool write_ret=process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}