#ifndef TIMER_H
#define TIMER_H

#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <list>
#include <vector>
#include <ctime>
#include <cstring>
#include <cassert>
#include <iostream>
#include <sys/epoll.h>

#include "TimerNode.h"

class Timer {
public:
    static const int TIMESLOT = 5;  //定时检测间隔

    Timer();
    ~Timer();

    //初始化：注册信号、创建管道、启动 alarm
    bool init(int epoll_fd);

    //获取管道读端fd（WebServer 注册到epoll用）
    int get_pipe_read_fd() const { return pipefd_[0]; }

    //定时器管理
    void add_timer(int fd, const sockaddr_in& addr);  //新连接加入
    void adjust_timer(int fd); //有活动，刷新
    void del_timer(int fd); //连接关闭，移除

    //心跳：处理超时，返回过期 fd 列表
    std::vector<int> tick();

private:
    //信号桥接
    static void sig_handler_(int sig);
    void handle_signal_(int sig);

    //内部工具
    void add_sig_(int sig, void (*handler)(int), bool restart);
    void set_alarm_();

    std::list<TimerNode> list_;
    int pipefd_[2]; //0=读端, 1=写端
    int epoll_fd_;

    static Timer* s_instance_;
};

#endif