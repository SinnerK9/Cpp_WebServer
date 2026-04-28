#include "WebServer/WebServer.h"
#include "MySQL_Pool/MySQL_Pool.h"

int main() {
    //初始化 MySQL 连接池
    MySQLPool::get_instance();

    //启动服务器：端口8080，8个工作线程
    WebServer server(8080, 8);
    server.start();

    return 0;
}

