#include<iostream>
#include<mutex>
#include<thread>
#include<condition_variable>

/*
模拟一个简单的计数器，一个线程负责增加计数，另一个线程等待并打印计数的值
int count = 0;
std::mutex mtx;
std::condition_variable cv1;

void Count_Increment(){
    for(int i = 0; i < 5; i ++){
    std::this_thread::sleep_for(std::chrono::miliseconds(100));
    std::unique_lock<std::mutex> lock(mtx); //先给mtx加锁，构造一个unique_lock对象lock
    count++;
    std::cout << "目前计数为" << count << std::endl;
    cv1.notify_one(); //唤醒一个正在等待该变量的线程
    }
}

void print(){
    for(int i = 0; i < 5; i++){
    std::unique_lock<std::mutex> lock(mtx); //抢锁
    cv1.wait(lock); //lock进入等待，释放锁mtx，使得另一个线程能够拿到锁修改count，并阻塞
    std::cout << "当前计数是: " << count << std::endl; //wait到位后
    }
}

*/


//多线程交替打印1-100的数字

std::mutex mtx;
int num = 1;
std::condition_variable cv2;

void printodd(){
    while(num <= 100){
        std::unique_lock<std::mutex> lock(mtx);
        cv2.wait(lock,[](){
            return num > 100 || num%2 == 1;
        });
        if(num > 100) break;
        std::cout << "线程1输出:" << num << std::endl;
        num++;
        cv2.notify_one();
    }
}

void printeven(){
    while(num <= 100){
        std::unique_lock<std::mutex> lock(mtx);
        cv2.wait(lock,[](){
            return num > 100 || num%2 == 0;
        });
        if(num > 100) break;
        std::cout << "线程2输出:" << num << std::endl;
        num++;
        cv2.notify_one();
    }
}

int main(){
    std::thread t1(printodd);
    std::thread t2(printeven);

    t1.join();
    t2.join();
}