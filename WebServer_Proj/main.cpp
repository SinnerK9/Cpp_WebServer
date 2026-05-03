#include "WebServer/WebServer.h"
#include "MySQL_Pool/MySQL_Pool.h"
#include "Logger/Logger.h"
#include <iostream>

int main() {
    //初始化日志系统 (必须放在最前面)
    //尝试在当前目录下创建并打开 server.log
    if (!Logger::get().init("server.log")) {
        std::cerr << "Fatal Error: Failed to initialize logger!" << std::endl;
        return -1;
    }
    LOG_INFO("========== TinyWebServer Starting ==========");

    LOG_INFO("Initializing MySQL connection pool...");
    MySQLPool::get_instance();
    LOG_INFO("MySQL Pool initialized successfully.");

    int port = 8080;
    int thread_num = 8;
    LOG_INFO("Starting WebServer on port: %d, worker threads: %d", port, thread_num);
    
    WebServer server(port, thread_num);
    server.start();  // 服务器进入主循环，开始阻塞监听

    LOG_INFO("========== TinyWebServer Stopped ==========");
    
    return 0;
}
