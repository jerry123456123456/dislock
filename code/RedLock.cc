#include "RedLock.h"
#include <bits/types/struct_timeval.h>
#include <chrono>
#include <random>
#include <thread>
#include <cstring>
#include<iostream>

//功能：获取当前系统时间的毫秒级时间戳，用于计算操作耗时和锁的有效时间。
static int64_t get_current_time_ms(){
    using namespace std::chrono;
    // 将系统时间转换为从纪元（1970-01-01）开始的毫秒数
    /*
    system_clock::now()   获取当前系统的时间点
    .time_since_epoch()   获取从纪元到当前时间点的时间间隔
    duration_cast<milliseconds>()   将高精度的时间间隔转为指定精度（ms）
    .count()  获取时间间隔的 数值部分（忽略单位）
    */
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

//功能：生成一个 40 位的唯一字符串，作为锁的持有者标识，避免不同客户端误释放对方的锁
//原理：通过硬件随机数生成器rd获取种子，生成 5 个 32 位随机数，格式化为十六进制字符串，确保唯一性。
static std::random_device rd;

static std::string generate_unique_id(){
    std::uniform_int_distribution<int>dist;  //生成[0, INT_MAX]的均匀分布随机数
    uint32_t buffer[5]; // 存储5个32位随机数（共160位，降低碰撞概率）
    for(auto &num : buffer) num = dist(rd);  // 填充随机数

    char buf[41] = {0};  // 存储40位十六进制字符串（+1位终止符）
    // 格式化为8位十六进制数 ×5，共40位（例如："a1b2c3d4e5f6..."）
    ::snprintf(buf,sizeof(buf),"%08x%08x%08x%08x%08x",
              buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
    return std::string(buf, 40); // 截取前40位（忽略终止符）
} 


/*
功能：向 RedLock 实例中添加一个 Redis 服务器节点，用于分布式锁的协调。
参数：
host：Redis 服务器的主机名（如"127.0.0.1"）。
port：Redis 服务器的端口（如6379）。
err：输出参数，存储错误信息（如连接失败原因）。
*/
bool RedLock::add_server(const std::string &host,int port,std::string &err){
    // 检查服务器是否已存在（通过连接状态、主机名、端口号）
    for (const auto &ctx : servers_) {
        // 确保 ctx 有效且连接成功
        if (ctx && ctx->err == 0) {
            // 直接使用 ctx->tcp.host 和 ctx->tcp.port（hiredis 已正确设置）
            if (host == ctx->tcp.host && port == ctx->tcp.port) { 
                err = "Redis server already exists";
                return false;
            }
        }
    }

    struct timeval timeout = {1, 500000};  // 1.5 秒超时
    redisContext *context = redisConnectWithTimeout(host.c_str(), port, timeout);

    //处理连接失败
    if (context == nullptr || context->err) {
        if (context) { // 连接对象存在但连接失败（如网络问题）
            err = std::string(context->errstr); // 获取Redis的错误信息
            redisFree(context); // 释放连接资源
        } else { // 连接对象分配失败（内存不足等）
            err = "Redis connection error: can't allocate redis context";
        }
        return false;
    }

    servers_.push_back(context);  //将有效连接加入服务器列表
    // 计算多数派节点数（总节点数的一半向上取整，如3节点需要2个成功）
    quorum_ = (servers_.size() / 2) + 1;  

    return true;
}

/*
功能：基于 RedLock 算法，在多个 Redis 节点上获取分布式锁，要求多数派节点成功且锁有效时间充足。
参数：
resource：被加锁的资源名称（如"stock_lock"）。
ttl_ms：锁的最大有效时间（毫秒，如5000表示 5 秒后自动失效）。
lock：输出参数，存储获取到的锁信息（资源名、持有者 ID、剩余有效时间）。
*/
bool RedLock::lock(const std::string &resource,int ttl_ms,Lock &lock){
    if(servers_.empty()){
        return false;
    }
    std::string value = generate_unique_id();  //生成唯一ID标识当前客户端的锁

    int attempt = retry_count_ + 1;  // 总尝试次数（包括首次尝试，如默认重试3次则总4次）
    while(attempt-- > 0){ // 循环尝试获取锁，直到次数耗尽
        int64_t start_time = get_current_time_ms();  //记录本次尝试的开始时间
        int success_count = 0;  //记录成功获取锁的节点数

        // 步骤1：在所有Redis节点上尝试获取锁
        for(auto &ctx : servers_){
            if(lock_instance(ctx,resource,value,ttl_ms)){
                //单个节点加锁
                success_count++;
            }
        }

        // 步骤2：计算时间漂移和有效时间（防止时钟不一致导致锁提前失效）
        // drift = TTL的1% + 2ms（经验值，补偿不同服务器的时钟差异）
        int64_t drift = static_cast<int64_t>(ttl_ms * DEFAULT_LOCK_DRIFT_FACTOR) + 2;
        int64_t elapsed_time = get_current_time_ms() - start_time; // 本次尝试耗时（毫秒）
        int64_t valid_time = ttl_ms - elapsed_time - drift; // 锁的剩余有效时间（需>0才安全）

        // 步骤3：验证是否满足多数派且有效时间充足
        if(success_count >= quorum_ && valid_time > 0){
            // 构造Lock对象，包含资源名、持有者ID、剩余有效时间
            lock = Lock(resource, value, valid_time); 
            return true; // 锁获取成功
        }

        // 步骤4：获取失败时，释放所有已获取的锁（避免残留无效锁）
        for (auto &ctx : servers_) {
            unlock_instance(ctx, resource, value); // 单个节点解锁（即使该节点加锁失败也不影响）
        }

        // 步骤5：重试前等待随机延迟（减少多客户端同时重试的竞争）
        if(attempt > 0){
            std::uniform_int_distribution<int> dist(0, retry_delay_ms_); // 0到retry_delay_ms_的随机数
            // 随机睡眠，避免所有客户端同时重试（如默认200ms内随机延迟）
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(rd))); 
        }
    }
    return false;
}

/*
功能：在单个 Redis 节点上执行SET命令尝试加锁，使用NX和PX选项保证原子性。
参数：
context：Redis 连接上下文（已建立的连接）。
resource、value、ttl_ms：同lock函数参数。
*/
bool RedLock::lock_instance(redisContext* context, const std::string& resource, const std::string& value, int ttl_ms) {
    if (!context || context->err != 0) {
        std::cerr << "[Error] Connection error: " << (context ? context->errstr : "null") << std::endl;
        return false;
    }

    const char* argv[] = {"SET", resource.c_str(), value.c_str(), "NX", "PX", std::to_string(ttl_ms).c_str()};
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto* reply = (redisReply*)redisCommandArgv(context, argc, argv, nullptr);
    if (!reply) {
        std::cerr << "[Error] redisCommandArgv failed: " << context->errstr << std::endl;
        return false;
    }

    // 打印reply->type的整数值（关键！）
    std::cerr << "[Debug] reply->type = " << reply->type << std::endl;

    bool ok = false;
    switch (reply->type) {
        case REDIS_REPLY_STATUS:
            ok = (reply->str && strcmp(reply->str, "ok") == 0);
            std::cerr << "[Debug] Status: " << (reply->str ? reply->str : "null") << std::endl;
            break;
        case REDIS_REPLY_ERROR:
            std::cerr << "[Debug] Error: " << (reply->str ? reply->str : "null") << std::endl;
            break;
        case REDIS_REPLY_NIL:
            std::cerr << "[Debug] Lock already exists (NIL)" << std::endl;
            break;
        default:
            std::cerr << "[Debug] Unexpected reply type: " << reply->type << std::endl;
            break;
    }

    freeReplyObject(reply);
    return ok;
}

/*
功能：在所有 Redis 节点上释放指定的锁，通过unlock_instance保证单个节点的原子性释放。
参数：lock为之前获取的锁对象，包含资源名和持有者 ID。
*/
bool RedLock::unlock(const Lock &lock) {
    if (servers_.empty()) { // 无服务器时直接返回
        return false;
    }
    // 遍历所有服务器节点，释放锁
    for (auto &ctx : servers_) {
        unlock_instance(ctx, lock.resource_, lock.value_); // 单个节点解锁
    }
    return true; // 无论是否全部成功，均返回true（不保证原子性，仅尽力释放）
}

/*
功能：通过 Lua 脚本原子化执行 “检查锁持有者 + 删除锁” 操作，确保仅删除当前客户端的锁。
Lua 脚本逻辑：
若GET resource等于value，则DEL resource并返回 1；
否则返回 0（不删除）。
*/
bool RedLock::unlock_instance(redisContext* context, const std::string& resource, const std::string& value) {
    // Lua脚本参数：
    // KEYS[1] = resource，ARGV[1] = value
    const char* argv[] = {
        "EVAL", UNLOCK_SCRIPT.c_str(), "1", resource.c_str(), value.c_str()
    };
    // 执行Lua脚本，原子化检查并删除锁（避免误删其他客户端的锁）
    redisReply* reply = (redisReply*)redisCommandArgv(context, 
        sizeof(argv) / sizeof(argv[0]), argv, nullptr); // 参数个数自动计算
    
    if (!reply) { // 命令执行失败
        return false;
    }
    
    // 检查返回值是否为1（表示成功删除锁）
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}

/*
功能：延长分布式锁的有效时间，逻辑与lock类似，但调用续锁的 Lua 脚本。
参数：
resource、ttl_ms：同lock，ttl_ms为新的有效时间。
lock：输入时提供锁的持有者 ID，输出时更新剩余有效时间。
*/
bool RedLock::continue_lock(const std::string& resource, int ttl_ms, Lock& lock) {
    if (servers_.empty()) { // 无服务器时失败
        return false;
    }
    int attempts = retry_count_ + 1; // 总尝试次数（同lock函数逻辑）
    while (attempts-- > 0) {
        
        int64_t start_time = get_current_time_ms(); // 记录开始时间
        int success_count = 0; // 成功续锁的节点数
        
        // 步骤1：在所有节点上尝试续锁
        for (auto &ctx : servers_) {
            if (continue_lock_instance(ctx, resource, lock.value_, ttl_ms)) { // 单个节点续锁
                success_count++;
            }
        }
        
        // 步骤2：计算时间漂移和新有效时间（逻辑同lock函数）
        int64_t drift = static_cast<int64_t>(ttl_ms * DEFAULT_LOCK_DRIFT_FACTOR) + 2;
        int64_t elapsed_time = get_current_time_ms() - start_time;
        int64_t valid_time = ttl_ms - elapsed_time - drift;
        
        // 步骤3：验证多数派和有效时间
        if (success_count >= quorum_ && valid_time > 0) {
            lock.valid_time_ = valid_time; // 更新锁的剩余有效时间
            return true; // 续锁成功
        }
        
        // 步骤4：重试前随机延迟（不释放锁，仅等待后重试）
        if (attempts > 0) {
            std::uniform_int_distribution<int> dist(0, retry_delay_ms_);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(rd)));
        }
    }
    return false; // 所有尝试失败
}

/*
功能：通过 Lua 脚本原子化执行 “检查锁持有者 + 延长过期时间” 操作。
*/
bool RedLock::continue_lock_instance(redisContext* context, const std::string& resource, const std::string& value, int ttl_ms) {
    std::string ttl_ms_str = std::to_string(ttl_ms); // 将ttl转为字符串（Lua脚本需要）
    // Lua脚本参数：
    // KEYS[1] = resource，ARGV[1] = value，ARGV[2] = ttl_ms
    const char* argv[] = {
        "EVAL", CONTINUE_LOCK_SCRIPT.c_str(), "1", resource.c_str(), value.c_str(), ttl_ms_str.c_str()
    };
    // 执行Lua脚本，原子化检查并续期锁
    redisReply* reply = (redisReply*)redisCommandArgv(context, 
        sizeof(argv) / sizeof(argv[0]), argv, nullptr);
    
    if (!reply) { // 命令执行失败
        return false;
    }
    
    // 检查返回值是否为1（表示成功设置过期时间）
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}

/*
功能：设置获取锁或续锁时的重试次数（失败后重试的最大次数）。
参数：count为新的重试次数（≥0）。
*/
bool RedLock::set_retry_count(int count) {
    if (count < 0) { // 重试次数不能为负数
        return false;
    }
    retry_count_ = count; // 更新成员变量
    return true;
}