#include "Timer.h"

Timer* Timer::s_instance_ = nullptr;

// 构造 / 析构
Timer::Timer()
    : pipefd_{-1, -1}
    , epoll_fd_(-1)
{
    list_.clear();
}

Timer::~Timer() {
    if (pipefd_[0] >= 0) close(pipefd_[0]);
    if (pipefd_[1] >= 0) close(pipefd_[1]);
    list_.clear();
}

//注册信号 + 创建管道 + 启动 alarm
bool Timer::init(int epoll_fd) {
    epoll_fd_ = epoll_fd;
    s_instance_ = this;

    //创建管道
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd_) < 0) {
        perror("[Timer] socketpair failed");
        return false;
    }

    //两端都设非阻塞
    int flags = fcntl(pipefd_[1], F_GETFL, 0);
    fcntl(pipefd_[1], F_SETFL, flags | O_NONBLOCK);

    flags = fcntl(pipefd_[0], F_GETFL, 0);
    fcntl(pipefd_[0], F_SETFL, flags | O_NONBLOCK);

    //管道读端注册到epoll
    struct epoll_event ev;
    ev.data.fd = pipefd_[0];
    ev.events  = EPOLLIN;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, pipefd_[0], &ev);

    //注册信号
    add_sig_(SIGALRM, sig_handler_, true);
    add_sig_(SIGTERM, sig_handler_, true);

    //首次alarm
    set_alarm_();

    std::cout << "[Timer] Initialized. TIMESLOT=" << TIMESLOT << "s"
              << ", pipefd=[" << pipefd_[0] << "," << pipefd_[1] << "]"
              << std::endl;
    return true;
}

void Timer::sig_handler_(int sig) {
    if (s_instance_) {
        s_instance_->handle_signal_(sig);
    }
}

void Timer::handle_signal_(int sig) {
    //信号处理函数中只做一件事：把信号值写入管道
    int save_errno = errno;
    char msg = (char)sig;
    send(pipefd_[1], &msg, 1, 0);
    errno = save_errno;
}

//注册信号处理
void Timer::add_sig_(int sig, void (*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    //SA_RESTART:epoll_wait被信号中断后自动重启，不会返回 EINTR，不影响管道事件
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

//设置下一次SIGALRM
void Timer::set_alarm_() {
    alarm(TIMESLOT);
}

//升序插入：遍历链表，找到第一个expire比新节点大的位置。
//初始过期时间=当前+3×TIMESLOT（3 个检测周期）。
void Timer::add_timer(int fd, const sockaddr_in& addr) {
    time_t expire = time(nullptr) + 3 * TIMESLOT;
    TimerNode node(fd, expire, addr);

    if (list_.empty()) {
        list_.push_back(node);
        return;
    }

    for (auto it = list_.begin(); it != list_.end(); ++it) {
        if (node.expire < it->expire) {
            list_.insert(it, node);
            return;
        }
    }
    list_.push_back(node);
}

//fd产生活动，重置计时器！删除旧节点并插入新节点
void Timer::adjust_timer(int fd) {
    for (auto it = list_.begin(); it != list_.end(); ++it) {
        if (it->fd == fd) {
            sockaddr_in addr = it->addr;
            list_.erase(it);
            add_timer(fd, addr);
            return;
        }
    }
}

//del_timer()—连接关闭，移除定时器
void Timer::del_timer(int fd) {
    for (auto it = list_.begin(); it != list_.end(); ++it) {
        if (it->fd == fd) {
            list_.erase(it);
            return;
        }
    }
}

//升序链表：遍历到第一个不过期的就停
std::vector<int> Timer::tick() {
    std::vector<int> expired;
    time_t now = time(nullptr);

    auto it = list_.begin();
    while (it != list_.end()) {
        if (it->expire <= now) {
            std::cout << "  [Timer] fd=" << it->fd << " timeout" << std::endl;
            expired.push_back(it->fd);
            it = list_.erase(it);
        } else {
            break;  // 升序：后面的expire都更大
        }
    }

    set_alarm_();  //重新设置alarm，开始下一轮
    return expired;
}