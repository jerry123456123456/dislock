// g++ -o redlock RedLock.cc main.cc -lhiredis -std=c++11

#include "RedLock.h"
#include <iostream>
#include <thread>

int main() {
    RedLock redlock;
    std::string err;
    if (redlock.add_server("127.0.0.1", 6379, err)) {
        std::cout << "Server added successfully.\n";
    } else {
        std::cerr << "Failed to add server. err : " << err << "\n";
        return 1;
    }

    //redlock.add_server("127.0.0.1", 6380,err);
    //redlock.add_server("127.0.0.1", 6381,err);

    while (true) {
        std::cout << "Attempting to acquire lock...\n";
        Lock mtx;
        if (redlock.lock("my_resource", 10000, mtx)) {
            std::cout << "Lock acquired. Validity: " << mtx.valid_time_ << "ms\n";
            // 执行业务逻辑...
            std::this_thread::sleep_for(std::chrono::seconds(10));
            redlock.unlock(mtx);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            std::cerr << "Failed to acquire lock. Retrying...\n";
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    return 0;
}