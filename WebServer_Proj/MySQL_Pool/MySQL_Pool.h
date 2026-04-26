#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <memory>

class MySQLPool {
public:
    //单例入口
    static MySQLPool* get_instance();

    //获取/归还连接
    MYSQL* get_conn();
    void   return_conn(MYSQL* conn);

    //优化：查询功能
    size_t idle_count() const; //空闲连接数

    //禁止拷贝
    MySQLPool(const MySQLPool&) = delete;
    MySQLPool& operator=(const MySQLPool&) = delete;

private:
    MySQLPool();
    ~MySQLPool();
    void init_pool_();//创建初始连接
    MYSQL* create_conn_();// 创建一条新连接
    void destroy_conn_(MYSQL* conn);//销毁一条连接

    static const char* HOST;
    static const char* USER;
    static const char* PASSWORD;//把PASSWORD变为常量存起来，拒绝硬编码
    static const char* DB_NAME;
    static const int   PORT;
    static const size_t POOL_SIZE;//连接池大小
    static const size_t MAX_POOL_SIZE; //最大连接数

    //成员变量
    std::queue<MYSQL*> conn_queue_; //空闲连接队列
    mutable std::mutex mtx_;
    std::condition_variable cond_;
    size_t current_size_; //当前连接总数（空闲+使用中）

    //单例指针
    static MySQLPool* instance_;
};

//RAII连接守卫：连接在离开作用域时自动归还
class ConnGuard {
public:
    ConnGuard() : conn_(MySQLPool::get_instance()->get_conn()) {}
    ~ConnGuard() {
        if (conn_) {
            MySQLPool::get_instance()->return_conn(conn_);
        }
    }

    MYSQL* conn() { return conn_; }
    operator bool() const { return conn_ != nullptr; }

    // 禁止拷贝
    ConnGuard(const ConnGuard&) = delete;
    ConnGuard& operator=(const ConnGuard&) = delete;

private:
    MYSQL* conn_;
};

#endif