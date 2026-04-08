/*
假设你的 TCP 服务器接收到了一个复杂的客户端请求，你需要将这个请求拆分成不同的子任务交给多线程处理。
任务要求：
请编写一个 C++ 程序，包含一个主函数 main() 和几个特定的线程操作，具体要求如下：
共享数据与引用传递 (std::ref & Lambda)：
在 main 中定义一个整型变量 int request_count = 0。
创建一个线程 t1（使用 Lambda 表达式），让它模拟核心业务处理：
将 request_count 的值循环增加 5 次，每次增加后休眠 100 毫秒（使用 sleep_for）。要求：必须能真正修改到 main 中的实参。
后台日志分离执行 (detach & 按值传递 & 函数指针)：
定义一个普通函数 void background_logger(int count)，模拟写日志操作。
该函数内部休眠 300 毫秒，然后打印当前处理的 count 值和当前线程 ID（get_id）。
在 main 中创建线程 t2 执行此函数，按值传入 request_count，并将其与主线程分离 (detach)。
线程所有权转移 (移动语义 & yield)：
创建一个无参线程对象 t3_worker（暂不分配任务）。
创建一个临时线程（使用仿函数或 Lambda），模拟一个低优先级的计算任务：
在循环中调用 yield() 主动让出时间片 3 次，然后打印完成信息。
要求：使用移动赋值，将这个临时线程的所有权转移给 t3_worker。
生命周期与有效性检查 (joinable & 销毁规则)：
在程序结束前，必须严格检查各个线程的状态（joinable()），正确地调用 join() 回收资源，
并打印 t3_worker 移动前后的 joinable 状态，确保满足你笔记中“被销毁的线程必须是无效线程”的规则。
*/

#include<thread>
#include<iostream>
#include<chrono>
#include<functional>

void background_logger(int count){
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cout << "当前处理count值为" << count << ",当前线程id为" << std::this_thread::get_id();
}

struct low_priority_task{
    void operator()(){
        std::cout << "低优先级线程 ID: " << std::this_thread::get_id() << "开始执行\n";
        for (int i = 0; i < 3; ++i) {
            std::cout << "让出时间片...\n";
            std::this_thread::yield(); // 主动让出CPU
        }
        std::cout << "执行完毕\n";
    }
};

/*

auto low_priority_task=[](){
std::cout << "低优先级线程 ID: " << std::this_thread::get_id() << "开始执行\n";
for (int i = 0; i < 3; ++i) {
    std::cout << "让出时间片...\n";
    std::this_thread::yield(); // 主动让出CPU
    }
    std::cout << "执行完毕\n";
};

*/

int main(){
    std::cout << "主线程 ID: " << std::this_thread::get_id() << "启动\n";
    int request_count = 0;
    std::thread t1([](int &count){
        for(int i = 0; i < 5; i++){
            count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "业务线程ID为" << std::this_thread::get_id() << "核心业务处理完毕\n";
        },std::ref(request_count));

    std::thread t2(background_logger,request_count);
    t2.detach();

    std::thread t3_worker;
    low_priority_task task_object;
    std::cout << "前状态:" <<  (t3_worker.joinable() ? "true" : "false") << "\n";
    t3_worker = std::thread(task_object);
    std::cout << "后状态:" <<  (t3_worker.joinable() ? "true" : "false") << "\n";

    if(t1.joinable()){
        t1.join();
    }
    if(t3_worker.joinable()){
        t3_worker.join();
    }

    std:: cout << "总的请求数量是" << request_count << std::endl;
    return 0;
}
