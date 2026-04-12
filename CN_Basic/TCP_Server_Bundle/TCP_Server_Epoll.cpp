#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <sys/epoll.h>

using namespace std; 

int main() {
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("Socket Error!");
        return -1;
    }

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

    //关键api:创建一个epoll内核事件表
    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0){
        perror("Epoll Error!");
        close(listenfd);
        return -1;
    }

    //添加listenfd到epoll
    //所有的fd在内核中都是以epoll_event的形式存储的，它包含所存fd，以及其期望窃听的事件
    struct epoll_event ev;
    ev.events = EPOLLIN; //基础LT触发，监听读事件
    ev.data.fd = listenfd; //将listenfd赋值给它

    //关键api：对事件表epoll_fd执行操作EPOLL_CTL_ADD添加listenfd的事件ev到epoll_fd指向的事件表
    //增加错误判断逻辑
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listenfd,&ev) < 0){
        perror("epoll_ctl:listenfd error");
        close(listenfd);
        return -1;
    }

    //用于接收返回的就绪事件，最多可以一次处理1024个就绪事件
    struct epoll_event events[1024];

    while(true){ 
        //等待就绪事件
        int n = epoll_wait(epoll_fd,events,1024,-1);
        if(n < 0){
            perror("epoll_wait");
            continue;
        }

        //开始遍历就绪fd集合events
        for(int i = 0; i < n; i++){
            int curr_fd = events[i].data.fd; //得到目前fd

            if(curr_fd == listenfd){
                sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int client_fd = accept(listenfd,(sockaddr*) &client_addr,&client_addr_len);

                if(client_fd > 0){
                    std::cout << "新连接：" << client_fd << std::endl;
                    std::cout << "新连接IP:" << inet_ntoa(client_addr.sin_addr) << std::endl;
                    //同理：接收到新连接，将其加入事件表
                    struct epoll_event client_ev;
                    client_ev.events = EPOLLIN;
                    client_ev.data.fd = client_fd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0){
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                    }
                }else{
                    perror("accept");
                    continue;
                }

            }else{
                char buf[1024] = {0};
                int byte_read = recv(curr_fd,buf,sizeof(buf)-1,0);
                if(byte_read > 0){
                    std::cout << "fd: " << curr_fd << "received: " << buf << std::endl;
                    const char* response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h1>Hello from epoll server!</h1>";
                    send(curr_fd, response, strlen(response), 0);
                }else if(byte_read == 0){
                    //客户端断开连接
                    cout << "fd: " << curr_fd << "断开连接" << endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL);
                    close(curr_fd);
                }else{
                    //接收数据异常
                    perror("recv");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL);
                    close(curr_fd);
                }
            }
        }
    }
    close(listenfd);
    close(epoll_fd);
    return 0;
}

