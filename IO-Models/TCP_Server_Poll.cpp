#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <poll.h>

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

    struct pollfd fds[1024]; //结构上的优化：变为结构体，无需反复传入拷贝
    memset(fds, 0, sizeof(fds));
    int nfds = 0; //有效fd数量
    fds[0].fd = listenfd; //第一个监听的fd是listenfd
    fds[0].events = POLLIN; //通知内核监听事件
    nfds++;

    while(true){ 
        int ready = poll(fds,nfds,-1); 
        if(ready < 0){
            perror("poll");
            continue;
        }

        //开始遍历
        for(int i = 0; i < nfds; i++){
            //关键：内核检查：检查是否有读事件
            if(fds[i].revents & POLLIN){
                if(fds[i].fd == listenfd){
                    sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    int client_fd = accept(listenfd,(sockaddr*)&client_addr,&client_addr_len);
                    if(client_fd > 0){
                        if(nfds >= 1024){
                            std::cout << "超出连接限度,放弃该链接"<< std::endl;
                            close(client_fd);
                            continue;
                        }
                        //client_fd创建成功，将其加入poll监听
                        fds[nfds].fd = client_fd;
                        fds[nfds].events = POLLIN;
                        nfds++; //记得更新最大nfds
                        std::cout << "新连接：" << client_fd << std::endl;
                        std::cout << "新连接IP:" << inet_ntoa(client_addr.sin_addr) << std::endl;
                    }
                }else{
                    char buf[1024] = {0};
                    int byte_read = recv(fds[i].fd,buf,sizeof(buf)-1,0);
                    if(byte_read > 0){
                        cout << "fd: " << fds[i].fd << " received: " << buf << endl;
                        const char* response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h1>Hello from poll server!</h1>";
                        send(fds[i].fd, response, strlen(response), 0);
                    }else{
                        //客户端断开连接或异常
                        std::cout << "fd: " << fds[i].fd << "断开连接" << std::endl;
                        close(fds[i].fd);
                        //关键：poll的删除操作，用最后一个元素覆盖当前位置
                        fds[i] = fds[nfds-1];
                        nfds--;
                        i--; //当前位置已被替换，需要重新检查
                    }
                }
            }
        }
    }
    return 0;
}

