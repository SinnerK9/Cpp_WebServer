#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>      
#include <arpa/inet.h>       
#include <cstring>
#include <string>
#include <sstream>
#include <iostream>

// 前置声明
class MySQLPool;

enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
};

enum HTTP_CODE {
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    INTERNAL_ERROR
};

class HttpConn {
public:
    static int m_epollfd;
    static int m_user_count;

    HttpConn();
    ~HttpConn();

    //外界接口
    void init(int sockfd, const sockaddr_in& addr);  //连接初始化：两个参数
    void close_conn(bool real_close = true);
    void process();
    bool read();
    bool write();
    void reset(); //和init()切分：专门负责长连接的复用

    //查询
    int get_fd()      const { return m_sockfd; }
    int get_port()    const;
    const char* get_ip()      const;
    bool is_keep_alive() const { return m_keep_alive; }

    //非阻塞设置工具函数
    static int set_nonblocking(int fd);

private:
    //解析
    HTTP_CODE parse_request();
    bool  parse_post_data(std::string& user, std::string& pwd);
    bool  get_line(std::string& line);

    //业务
    bool check_login(const std::string& user, const std::string& pwd);
    void make_response();

    //响应
    void serve_static_file();
    void send_error(int code, const char* msg);
    void send_login_response(bool success);

    //资源清理
    void unmap();

private:
    int          m_sockfd;
    sockaddr_in  m_addr; //客户端地址

    std::string  m_read_buf;
    std::string  m_write_buf;

    CHECK_STATE  m_check_state;
    size_t       m_checked_idx;
    size_t       m_content_length;

    std::string  m_method;
    std::string  m_url;
    std::string  m_version;
    std::string  m_post_data;
    bool         m_keep_alive;

    char*        m_mmap_addr;
    struct stat  m_file_stat;
    struct iovec m_iv[2];
    int          m_iv_count;
    size_t       m_bytes_to_send;
    size_t       m_bytes_have_send;
};

#endif