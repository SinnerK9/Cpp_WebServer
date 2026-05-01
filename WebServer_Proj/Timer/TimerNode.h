#ifndef TIMER_NODE_H
#define TIMER_NODE_H

#include <ctime>
#include <netinet/in.h>
#include <cstring>

struct TimerNode {
    int fd;//被计时的 socket
    time_t expire; //绝对过期时间（秒）
    sockaddr_in  addr;//客户端地址

    TimerNode() : fd(-1), expire(0) {
        memset(&addr, 0, sizeof(addr));
    }

    TimerNode(int f, time_t exp, const sockaddr_in& a)
        : fd(f), expire(exp), addr(a) {}
};

#endif