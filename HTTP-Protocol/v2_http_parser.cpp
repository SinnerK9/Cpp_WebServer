#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <sys/epoll.h>
#include <fcntl.h> 
#include <errno.h>
#include <signal.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <atomic>
#include <functional>
#include <future>
#include <sstream>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>

using namespace std; 


//列举各种解析位置
enum CHECK_STATE { 
    CHECK_STATE_REQUESTLINE = 0, //请求行
    CHECK_STATE_HEADER, //头部
    CHECK_STATE_CONTENT //请求体
};

//列举各种解析结果
enum HTTP_CODE {
    NO_REQUEST,// 请求不完整，继续读数据
    GET_REQUEST,// 成功拿到完整 HTTP 请求
    BAD_REQUEST, // 请求语法错误（400）
    INTERNAL_ERROR // 服务器内部错误（500）
};

class threadpool {
private:
    std::mutex mtx;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;  
    std::atomic<size_t> pending_tasks{0};

public:
    threadpool(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; i++) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task; 
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        condition.wait(lock, [this] {
                            return stop || !tasks.empty();
                        });
                        if (stop && tasks.empty()) {
                            return;
                        }
                        task = std::move(this->tasks.front());
                        tasks.pop();
                    } 
                    task();
                    pending_tasks--;
                }
            });
        }
    }

    template<typename F, typename ...Args> 
    auto submit(F &&f , Args && ...args) 
    -> std::future<decltype(f(args...))>{   
        using return_type = decltype(f(args...)); 
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f),std::forward<Args>(args)...));
        std::future<return_type> result = task -> get_future();

        {
            std::lock_guard<std::mutex> lock(mtx);
            if(stop){
                std::cout << "线程池已停止，不再提交" << std:: endl;
                throw std::runtime_error("线程池已停止");
            }
        
            tasks.push([task](){
                (*task)();
            });
            pending_tasks++;
        }
        condition.notify_one();
        return result;
    }

    ~threadpool(){
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop = true;
        }
        condition.notify_all();
        while(pending_tasks > 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for(auto& worker : workers){
            if(worker.joinable()){
                worker.join();
            }
        }

        std:: cout << "线程池工作结束" << std::endl;
    }
};


struct ClientState {
    int fd;
    std::string buffer;
    CHECK_STATE check_state; //存储解析状态：这是在请求行/头/体？
    int checked_idx; //前面读到哪了？

    std::string method;
    std::string url;
    std::string version;
    bool keep_alive; //增加对长连接的支持
    int content_length;//请求体长度

    //定义新的初始化函数：对于新连接和长连接的下一次请求，将所有属性清空。
    void init(int client_fd){
        fd = client_fd;
        buffer.clear();
        check_state = CHECK_STATE_REQUESTLINE;
        checked_idx = 0;
        keep_alive = false;
        content_length = 0;
        method.clear();
        url.clear();
        version.clear();
    }
};
ClientState users[65536];


// 从状态机逻辑实现：字符串切分（改为返回 bool）
bool get_line(ClientState& client, std::string& line) {
    size_t pos = client.buffer.find("\r\n", client.checked_idx);
    // 没找着换行符：返回 false，表示请求还没收全
    if (pos == std::string::npos) {
        return false;
    }
    // 找到了！把这一行切出来
    line = client.buffer.substr(client.checked_idx, pos - client.checked_idx); 
    client.checked_idx = pos + 2; //跳过 \r\n
    return true;
}

//核心：主状态机逻辑实现
HTTP_CODE parse_http_request(ClientState& client){
    std::string line;
    //循环逻辑：能够读到行 / 读请求体（请求体不一定有换行符）
    while( get_line(client,line) || client.check_state == CHECK_STATE_CONTENT){

        switch(client.check_state){
            case CHECK_STATE_REQUESTLINE:{
                //正在解析请求行，由于能保证读到了行，可以直接用stringstream
                std::stringstream ss(line);
                ss >> client.method >> client.url >> client.version;
                //格式有错误，请求有问题！
                if(client.method.empty() || client.url.empty()){
                    return BAD_REQUEST;
                }
                client.check_state = CHECK_STATE_HEADER; //请求行只有一行，读完就更新到请求头了
                break;
            }
            case CHECK_STATE_HEADER:{
                //注意：请求头和请求体中间必定有一个空行，这可以作为切换状态的判断标准
                //读不到又分两种情况：这请求报文的请求体就是空的 / 有请求体，但是在空行同样读不到
                if(line.empty()){
                    if(client.content_length == 0){
                        return GET_REQUEST; //没请求体了，已经读完了
                    }
                    client.check_state = CHECK_STATE_CONTENT;
                    break;
                }
                //开始解析具体的词条!
                //是否connection词条？是否长连接？
                if(line.find("Connection:") != std::string::npos){
                    if(line.find("keep-alive") != std::string::npos){
                        client.keep_alive = true;
                    }
                }
                //解析请求体长度
                if(line.find("Content-Length:")!= std::string::npos){
                    client.content_length = std::stoi(line.substr(15)); //请求体长度：Content-Length:后面的子字符串转为数字
                }
                break;
            }
            case CHECK_STATE_CONTENT:{ 
                //请求体阶段不再按行读取，而是按字节判断
                //缓冲区里还没处理的字节数大于等于请求体长度，说明确实解析完毕了
                if(client.buffer.size() - client.checked_idx >= client.content_length){
                    return GET_REQUEST;
                }
                return NO_REQUEST; //还有请求体内容没完全发过来，退出解析
            }
            //出了未知问题，报内部错误
            default:
                return INTERNAL_ERROR; 
        }
    }
    return NO_REQUEST; //连一个完整行都没读到，请求不完整
}

int setnonblocking(int fd){
    int old_flag = fcntl(fd,F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
    return old_flag;
}

void handle_client(int fd,int epoll_fd){
    ClientState& client = users[fd];
    HTTP_CODE ret = parse_http_request(client);
    //请求尚不完整，回去重新recv！需要重置EPOLLONESHOT
    if(ret == NO_REQUEST){
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP |EPOLLET;
        epoll_ctl(epoll_fd,EPOLL_CTL_MOD,fd,&ev);
        return;
    //请求语法有问题，关闭连接
    }else if(ret == BAD_REQUEST){
        std::string response;
        response = "HTTP/1.1 400 Bad Request\r\nContent-Length: " + std::to_string(strlen("Bad Request")) + "\r\n\r\n";
        send(fd,response.c_str(),response.size(),0);
        close(fd);
        return;
    }else if(ret == GET_REQUEST){
        std::cout << "方法为： " << client.method << "  路径为： " << client.url << " 版本为： " << client.version << " 长连接： " << client.keep_alive << std::endl;
        if(client.url == "/"){
            client.url = "/index.html"; 
        }
        std::string filepath = "./resources" + client.url; 
        struct stat file_stat;
        if(stat(filepath.c_str(),&file_stat) < 0){
            std::string body = "<h1>404 Not Found</h1>";
            std::string header = "HTTP/1.1 404 Not Found\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
            send(fd,header.c_str(),header.size(),0); 
            send(fd,body.c_str(),body.size(),0);
        }else{
            std::string header = "HTTP/1.1 200 OK\r\n";
            //是否为长连接？将这部分信息在响应头中传出去
            if(client.keep_alive == true){
                header +=  "Connection: keep-alive\r\n";
            }else{
                header += "Connection: close\r\n";
            }
            if(client.url.find(".jpg") != std::string::npos || client.url.find(".png") != std::string::npos) {
                header += "Content-Type: image/jpeg\r\n"; // 图片类型
            } else {
                header += "Content-Type: text/html; charset=utf-8\r\n"; // 网页类型
            }
            header += "Content-Length: "+ std::to_string(file_stat.st_size) + "\r\n\r\n"; //stat返回=0，找到了
            int src_fd = open(filepath.c_str(),O_RDONLY); 
            char* file_address = (char*) mmap(NULL,file_stat.st_size,PROT_READ,MAP_PRIVATE,src_fd,0);
            close(src_fd); 
            struct iovec iv[2];
            iv[0].iov_base = (void*)header.c_str(); 
            iv[0].iov_len = header.size();
            iv[1].iov_base = file_address;
            iv[1].iov_len = file_stat.st_size;
            writev(fd,iv,2); 
            munmap(file_address, file_stat.st_size); 
        }//对于长连接：不断掉连接，重新将其初始化清空，激活EPOLLONESHOT
        if(client.keep_alive){
            client.init(fd);
            struct epoll_event ev;
            ev.data.fd = fd;
            ev.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP |EPOLLET;
            epoll_ctl(epoll_fd,EPOLL_CTL_MOD,fd,&ev);
        }else{
            //不是长连接，从事件表里删去，关闭文件描述符
            epoll_ctl(epoll_fd,EPOLL_CTL_DEL,fd,NULL);
            close(fd);
        }

    }
}

int main() {

    signal(SIGPIPE, SIG_IGN); 
    threadpool pool(8);
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("Socket Error!");
        return -1;
    }
    setnonblocking(listenfd);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;      
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  
    addr.sin_port = htons(8080);

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
    int ret = bind(listenfd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        perror("Bind Error!");
        return -1;
    }

    ret = listen(listenfd, 5);
    if (ret < 0) {
        perror("Listen Error!");
        return -1;
    }

    cout << "waiting..." << endl;

    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0){
        perror("Epoll Error!");
        close(listenfd);
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP; 
    ev.data.fd = listenfd; 

    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listenfd,&ev) < 0){
        perror("epoll_ctl:listenfd error");
        close(listenfd);
        return -1;
    }

    struct epoll_event events[1024];

    while(true){ 
        int n = epoll_wait(epoll_fd,events,1024,-1);
        if(n < 0){
            perror("epoll_wait");
            continue;
        }

        for(int i = 0; i < n; i++){
            int curr_fd = events[i].data.fd; 
            if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                cout << "fd: " << curr_fd << " 异常断开，已清理" << endl;
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL); 
                close(curr_fd);                                
                continue;                                       
            }

            if(curr_fd == listenfd){
                while(true){ 
                    sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    int client_fd = accept(listenfd,(sockaddr*) &client_addr,&client_addr_len);

                    if(client_fd < 0){
                        if(errno == EAGAIN||errno == EWOULDBLOCK) break; 
                        perror("Accept Error");
                        break;
                    }
                    setnonblocking(client_fd);
                    users[client_fd].init(client_fd);
                    std::cout << "新连接：" << client_fd << std::endl;
                    std::cout << "新连接IP:" << inet_ntoa(client_addr.sin_addr) << std::endl;
                    struct epoll_event client_ev;
                    client_ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT; 
                    client_ev.data.fd = client_fd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0){
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                    }
                }
            }else{
                while(true){ 
                    char buf[1024] = {0};
                    int byte_read = recv(curr_fd,buf,sizeof(buf)-1,0);
                    if(byte_read > 0){
                        users[curr_fd].buffer += buf; 
                    }else if(byte_read == 0){
                        cout << "fd: " << curr_fd << "断开连接" << endl;
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL);
                        close(curr_fd);
                        users[curr_fd].buffer.clear(); 
                        break;
                    }else if(byte_read < 0){
                        if(errno == EAGAIN||errno == EWOULDBLOCK){
                            if(!users[curr_fd].buffer.empty()){
                            pool.submit(handle_client,curr_fd,epoll_fd);                    
                            }else{
                                struct epoll_event ev;
                                ev.data.fd = curr_fd;
                                ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, curr_fd, &ev);
                            }
                            break;
                        }
                        perror("recv");
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL);
                        close(curr_fd); 
                        users[curr_fd].buffer.clear();
                        break;
                    }
                }
            }
        }
    }
    close(listenfd);
    close(epoll_fd);
    return 0;
}

