#include "MySQL_Pool/MySQL_Pool.h"
#include <iostream>
#include <cstring>

//配置常量定义
const char*  MySQLPool::HOST      = "localhost";
const char*  MySQLPool::USER      = "root";
const char*  MySQLPool::PASSWORD  = "123456";
const char*  MySQLPool::DB_NAME   = "webserver"; 
const int    MySQLPool::PORT      = 3306;
const size_t MySQLPool::POOL_SIZE = 8;
const size_t MySQLPool::MAX_POOL_SIZE = 16;

//单例指针初始化为nullptr
MySQLPool* MySQLPool::instance_ = nullptr;

MySQLPool* MySQLPool::get_instance() {
    if (instance_ == nullptr) {
        instance_ = new MySQLPool();
    }
    return instance_;
}


//构造函数：预创建连接
MySQLPool::MySQLPool()
    : current_size_(0)
{
    init_pool_();
}

//新增析构函数：销毁所有连接
MySQLPool::~MySQLPool() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!conn_queue_.empty()) {
        MYSQL* conn = conn_queue_.front();
        conn_queue_.pop();
        destroy_conn_(conn);
    }
    current_size_ = 0;
}

//创建初始连接
void MySQLPool::init_pool_() {
    for (size_t i = 0; i < POOL_SIZE; i++) {
        MYSQL* conn = create_conn_();
        if (conn != nullptr) {
            conn_queue_.push(conn);
            current_size_++;
        }
    }
    std::cout << "[MySQLPool] Initialized with " << current_size_
              << " connections." << std::endl;
}

//创建单条连接
MYSQL* MySQLPool::create_conn_() {
    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr) {
        std::cerr << "[MySQLPool] mysql_init() failed!" << std::endl;
        return nullptr;
    }

    //新优化：设置自动重连（注意：此处的options必须在real_connect之前）
    bool reconnect = 1;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
    //设置UTF-8编码
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    //mysql_real_connect判断是否连接成功
    if (!mysql_real_connect(conn, HOST, USER, PASSWORD,
                            DB_NAME, PORT, nullptr, 0)) {
        std::cerr << "[MySQLPool] mysql_real_connect() failed: "
                  << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

//destroy_conn_()销毁单条连接
void MySQLPool::destroy_conn_(MYSQL* conn) {
    if (conn != nullptr) {
        mysql_close(conn);
    }
}


//新优化：超时等待
MYSQL* MySQLPool::get_conn() {
    std::unique_lock<std::mutex> lock(mtx_);

    //等待空闲连接，最多等 500ms
    if (conn_queue_.empty()) {
        cond_.wait_for(lock, std::chrono::milliseconds(500),
                       [this] { return !conn_queue_.empty(); });
    }

    if (conn_queue_.empty()) {
        //超时！实在没有返回来的空闲连接，返回个NULL
        std::cerr << "[MySQLPool] No idle connection available!" << std::endl;
        return nullptr;
    }

    MYSQL* conn = conn_queue_.front();
    conn_queue_.pop();
    return conn;
}

//归还链接
void MySQLPool::return_conn(MYSQL* conn) {
    if (conn == nullptr) return;

    std::lock_guard<std::mutex> lock(mtx_);
    conn_queue_.push(conn);
    cond_.notify_one();  //唤醒一个等待的线程
}

//查询空闲连接数
size_t MySQLPool::idle_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return conn_queue_.size(); 
}