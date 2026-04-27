#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <iostream>
#include <sstream>

enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,  //请求行
    CHECK_STATE_HEADER,  //头部字段
    CHECK_STATE_CONTENT  //请求体
};
enum HTTP_CODE {
    NO_REQUEST,   //数据不完整，继续读
    GET_REQUEST,  //请求解析完成（含 GET 和 POST）
    BAD_REQUEST,  //语法错误
    INTERNAL_ERROR //服务器内部错误
};

class HttpConn {
public:
    static int m_epollfd;    //折中设计：使得httpconn可以给自己的sockfd设置监听事件，m_epollfd需要传入
    static int m_user_count; //统计连接数

    HttpConn();
    ~HttpConn();

    void init(int sockfd); //初始化新连接
    void process();   //线程池任务入口（解析+响应）
    void close_conn(bool real_close = true);

    // ET 模式下的读写（主线程调用!）
    bool read();   //循环读 socket → m_read_buf
    bool write();  //循环写 socket（ET 分批写需要）

private:
    //解析
    HTTP_CODE parse_request();     //主状态机
    bool parse_post_data(std::string& user, std::string& pwd);

    //业务
    bool check_login(const std::string& user, const std::string& pwd);
    void make_response(); //根据URL和method生成响应

    //静态文件
    void serve_static_file();
    void send_error(int code, const char* msg);
    void send_login_response(bool success);

    //从状态机，分行工具
    bool get_line(std::string& line);

private:
    int m_sockfd;

    // 读缓冲
    std::string m_read_buf;

    // 状态机
    CHECK_STATE m_check_state;
    size_t m_checked_idx;
    size_t m_content_length;

    // 解析结果
    std::string m_method;
    std::string m_url;
    std::string m_version;
    std::string m_post_data;
    bool m_keep_alive;

    // 写缓冲 & mmap
    std::string m_write_buf;       //响应头
    char* m_mmap_addr;             //文件映射
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    size_t m_bytes_to_send;
    size_t m_bytes_have_send;
};

#endif