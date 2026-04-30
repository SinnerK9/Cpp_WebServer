#include <sys/socket.h>
#include <sys/epoll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>
#include <list>
#include <vector>
#include <cstring>
#include <cassert>
#include <iostream>

struct TimerNode{
    int fd;
    time_t expire; //用于存储该fd的过期时间
    TimerNode(int f,time_t ex) : fd(f),expire(ex) {}
};

class TimerTest{
public:
    const static int TIMESLOT = 3;
    TimerTest() : pipefd_{-1,-1} {}
    bool init(int epoll_fd){
        epoll_fd_ = epoll_fd;
        s_instance = this; //初始化：将静态指针指向这个Timer实例
        //创建管道
        if (socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd_) < 0) {
            perror("socketpair");
            return false;
        }
        //写端设为非阻塞
        int flags = fcntl(pipefd_[1],F_GETFL,0);
        fcntl(pipefd_[1], F_SETFL, flags | O_NONBLOCK);
        //读端注册到epoll监听读事件
        struct epoll_event ev;
        ev.data.fd = pipefd_[0];
        ev.events = EPOLLIN | EPOLLET;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, pipefd_[0], &ev);
        //关键：注册信号
        struct sigaction sa; //定义sigaction结构体变量，用于描述信号来了该怎么处理
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sig_handler; //设定：信号来了调用sig_handler
        sigfillset(&sa.sa_mask); //sa_mask表示处理信号期间屏蔽哪些信号，调用sigfillset表示屏蔽全部
        sigaction(SIGALRM, &sa, nullptr);//sigaction方法：关注闹钟信号SIGALRM，执行处理方案sa(让sig_handler处理)，nullptr表示不关心旧的处理方式
        sigaction(SIGTERM, &sa, nullptr);//SIGTERM为终止信号(kill发送的信号)，执行处理方案sa，nullptr
        //启动定时 alarm
        alarm(TIMESLOT);//alarm方法：设置内核闹钟，TIMESLOT时间后给我发SIGALRM，只响一次，需要重新设置！
        std::cout << "[TimerTest] Init OK, TIMESLOT=" << TIMESLOT << "s" << std::endl;
        return true;
    }
    int get_pipe_fd() const { return pipefd_[0]; }
    //添加一个模拟连接
    void add(int fd, int lifetime_sec) {
        time_t expire = time(nullptr) + lifetime_sec; //expire的计算：当前系统时间+过期时间
        list_.emplace_back(fd, expire);//加入链表
        std::cout << "  [Timer] fd=" << fd << " added, expire in "
                  << lifetime_sec << "s" << std::endl;
    }
    //tick：返回超时fd列表
    std::vector<int> tick() {
        std::vector<int> expired;
        time_t now = time(nullptr);
        auto it = list_.begin();
        //遍历链表，判断时间戳是否小于现在时间（小于则代表超时）
        while (it != list_.end()) {
            if (it->expire <= now) {
                std::cout << "  [Timer] fd=" << it->fd << " TIMEOUT!" << std::endl;
                expired.push_back(it->fd);//加入超时数组
                it = list_.erase(it);
            } else {
                ++it;
            }
        }
        alarm(TIMESLOT);  //重新设置alarm
        return expired;
    }
private:
    //TimerTest的静态指针，通过桥接法操作真正的处理函数
    static TimerTest* s_instance;
    static void sig_handler(int sig){
        if(s_instance){
            s_instance -> handle_signal(sig);
        }
    }
    void handle_signal(int sig) {
        int save_errno = errno;
        char msg = (char)sig;
        send(pipefd_[1], &msg, 1, 0);  //实例的pipefd_
        errno = save_errno;
    }
    int pipefd_[2];
    int epoll_fd_;
    std::list<TimerNode> list_;
};
TimerTest* TimerTest::s_instance  = nullptr;


int main(){
    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0){
        perror("epoll_create1");
        return 1;
    }

    TimerTest timer;
    timer.init(epoll_fd);
    timer.add(1,2);
    timer.add(2,5);
    timer.add(3,8);
    struct epoll_event events[10];
    bool running = true;
    int alarm_count = 0;
    while (running) {
        int n = epoll_wait(epoll_fd, events, 10, 8000);  //最多等8秒，8000单位是毫秒
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            //定时器管道可读，闹钟响了
            if (fd == timer.get_pipe_fd() && (events[i].events & EPOLLIN)) {
                alarm_count++;
                std::cout << "\n[SIGNAL #" << alarm_count << "] "
                          << time(nullptr) << std::endl;
                //读管道中数据
                char buf[64];
                while (recv(fd, buf, sizeof(buf), 0) > 0) {
                    for (int j = 0; j < (int)sizeof(buf); j++) {
                        //是闹钟信号
                        if (buf[j] == SIGALRM) {
                            std::cout << "  → SIGALRM received" << std::endl;
                            //是终止信号
                        } else if (buf[j] == SIGTERM) {
                            std::cout << "  → SIGTERM received, stopping..." << std::endl;
                            running = false;
                        }
                    }
                }
                //执行tick
                std::vector<int> expired = timer.tick();
                if (expired.empty()) {
                    std::cout << "  → No expired connections." << std::endl;
                }
            }
        }
        //所有连接都超时了，自动退出
        if (alarm_count >= 4 && running) {
            std::cout << "\nAll timers should have expired. Exiting." << std::endl;
            break;
        }
    }
    close(epoll_fd);
    std::cout << "Test done." << std::endl;
    return 0;

}