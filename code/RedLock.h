#pragma once
#include <hiredis/hiredis.h>
#include <random>
#include <string>
#include <vector>

//表示一个分布式锁的状态的结构体
struct Lock{
    // 默认构造函数：初始化有效时间为0
    Lock() : valid_time_(0) {};
    //构造函数：通过资源名、持有者标识、有效时间初始化锁对象
    Lock(const std::string &resource,const std::string &value,int valid_time)
        : resource_(resource),value_(value),valid_time_(valid_time){}

    std::string resource_;  // 被加锁的资源名称（如"user_123_lock"）    
    std::string value_;  // 锁的唯一持有者标识（防止误释放其他客户端的锁）
    int valid_time_;  // 锁的剩余有效时间（单位：毫秒）
};

// 基于Redis的分布式锁实现类（遵循RedLock算法）
class RedLock{
public:
    // RedLock.h
    ~RedLock() {
        for (auto ctx : servers_) {
            if (ctx) {
                redisFree(ctx); // 释放所有Redis连接
            }
        }
    }
    // 设置获取锁/续锁时的重试次数（用于失败后重试）
    bool set_retry_count(int count);

    // 向分布式锁实例中添加一个Redis服务器节点
    bool add_server(const std::string &host,int port,std::string &err);

    //尝试获取分布式锁（核心方法）
    bool lock(const std::string& resource, int ttl_ms, Lock& lock);

    // 释放分布式锁（在所有Redis节点上删除锁）
    bool unlock(const Lock &lock);

    // 延长锁的有效时间（续锁）
    bool continue_lock(const std::string &resource,int ttl_ms,Lock &lock);

private:
    // 私有辅助函数：在单个Redis节点上尝试获取锁
    bool lock_instance(redisContext* context, const std::string& resource, const std::string& value, int ttl_ms);
    // 私有辅助函数：在单个Redis节点上释放锁（通过Lua脚本保证原子性）
    bool unlock_instance(redisContext* context, const std::string& resource, const std::string& value);
    // 私有辅助函数：在单个Redis节点上续锁（延长锁的有效时间）
    bool continue_lock_instance(redisContext* context, const std::string& resource, const std::string& value, int ttl_ms);

    // 静态常量成员：默认配置参数
    static constexpr float DEFAULT_LOCK_DRIFT_FACTOR = 0.01f;  // 时钟漂移因子（用于补偿不同服务器的时间差）
    static constexpr int DEFAULT_LOCK_RETRY_COUNT = 3;         // 默认重试次数（获取锁失败时的重试次数）
    static constexpr int DEFAULT_LOCK_RETRY_DELAY = 200;        // 默认重试延迟（毫秒，失败后等待的时间）

    //成员变量
    std::vector<redisContext*> servers_; // 存储所有Redis服务器的连接上下文（hiredis的连接对象）
    int quorum_;   // 多数派节点数（锁操作需要成功的最小节点数，防止脑裂）
    int retry_count_ = DEFAULT_LOCK_RETRY_COUNT;  // 当前设置的重试次数（可通过set_retry_count修改）
    int retry_delay_ms_ = DEFAULT_LOCK_RETRY_DELAY;  // 重试间隔时间（毫秒
    std::mt19937 rng_;  // Mersenne Twister随机数生成器（用于生成随机延迟和唯一锁标识）

     // Lua脚本（用于原子化操作Redis）
    // 解锁脚本：仅当锁的持有者标识匹配时才删除锁（防止误删其他客户端的锁）
    const std::string UNLOCK_SCRIPT = 
        "if redis.call('get', KEYS[1]) == ARGV[1] then "  // 检查当前锁的值是否等于客户端的唯一标识
        "return redis.call('del', KEYS[1]) "              // 匹配则删除锁
        "else "                                            // 不匹配
        "return 0 "                                        // 返回0（表示未删除）
        "end"; 

    // 续锁脚本：仅当锁的持有者标识匹配时，延长锁的有效时间
    const std::string CONTINUE_LOCK_SCRIPT = 
        "if redis.call('get', KEYS[1]) == ARGV[1] then "  // 检查锁的值是否匹配
        "return redis.call('pexpire', KEYS[1], ARGV[2]) "  // 匹配则设置新的有效时间（毫秒）
        "end";  // 不匹配则无操作（返回nil）
};