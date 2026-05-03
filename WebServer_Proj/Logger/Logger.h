#ifndef LOGGER_H
#define LOGGER_H

#include <sys/time.h>   //Linux系统时间API (微秒级)
#include <ctime>        //C/C++标准时间API (转换年月日)
#include <cstdarg>      //处理可变参数 (va_list)
#include <cstdio>       //字符串格式化 (snprintf)
#include <string>
#include <mutex>
#include <fstream>      //C++文件流
#include <iostream>

// 定义日志级别
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
};

class Logger {
public:
    //get用于获取唯一的实例
    static Logger& get(){
        static Logger instance; //局部静态变量初始化，在c++11后是线程安全的，无需自行加锁
        return instance;
    }
    bool init(const char* filepath){
        file_.open(filepath,std::ios::app); //std::ios::app表示追加，不会覆盖已经有的日志
        if(!file_.is_open()){
            return false;
        }
        return true;
    }
    void write(int level,const char* format, ...){
        std::lock_guard <std::mutex> lock(mtx_);

        //获取高精度时间(微秒)：一切操作都是为了输出人能看懂的精确时间字符串
        struct timeval now; //timeval是linux系统的时间结构体，包含：tv_sec表示1970年到现在的秒数，tv_usec则是微秒
        gettimeofday(&now,nullptr);//系统调用：获取当前精确到微秒的时间
        //tm结构体中存储年月日时分秒，要转化为现实时间，年需要加1970，月是0-11，需要加1处理
        //localtime_r是线程安全的时间转换函数，将1970至今秒数转化为年月日时分秒
        struct tm* local = localtime(&now.tv_sec);
        //定义缓冲区，将时间格式化输出
        char time_buf[64];
        snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d:%02d.%06ld",
                 local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
                 local->tm_hour, local->tm_min, local->tm_sec, now.tv_usec);


        //解析日志所在的级别
        const char* level_str = "";
        switch (level) {
            case LOG_DEBUG: level_str = "[DEBUG]"; break;
            case LOG_INFO:  level_str = "[INFO] "; break;
            case LOG_WARN:  level_str = "[WARN] "; break;
            case LOG_ERROR: level_str = "[ERROR]"; break;
            default:level_str = "[INFO] "; break;
        }

        //处理可变参数
        char content_buf[4096]; //日志缓冲区
        va_list args; //声明一个可变参数列表指针
        va_start(args,format); //从format开始，后面的参数由args管理
        //vsnprintf是安全版本的字符串格式化，把format + 参数填进content_buf
        //最多只会写4096字节，不会出现溢出崩溃
        vsnprintf(content_buf,sizeof(content_buf),format,args);
        va_end(args); //固定收尾必须写

        //将前面得到的所有内容拼成一行日志
        std::string final_line = std::string(time_buf) + " " + level_str + " " + content_buf + "\n";
        if (file_.is_open()){
            file_ << final_line;
            file_.flush(); //强制刷新缓冲区，立刻写进硬盘
        }

        std::cout << final_line;
        }


private:
    Logger() = default;
    ~Logger(){
        if(file_.is_open()){
            file_.close();
        }
    }
    Logger(const Logger&) = delete;
    Logger& operator = (const Logger&) = delete;

    std::mutex mtx_;
    std::ofstream file_;   //文件输出流对象

};

// 给外界使用的简易宏接口
//通过宏可以简易地通过传入参数调用write
//VA_ARGS_前面的##表示预处理拼接符，当...没有传入任何参数时，逗号会被自动去除
#define LOG_DEBUG(format, ...) Logger::get().write(LOG_DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  Logger::get().write(LOG_INFO,  format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  Logger::get().write(LOG_WARN,  format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Logger::get().write(LOG_ERROR, format, ##__VA_ARGS__)
#endif