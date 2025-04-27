#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include "redlock.h"

/*
功能：将 C 风格的字符串数组（char**）转换为 Redis 专用的 sds 字符串数组。
参数：
count：输入字符串的数量。
args：C 风格字符串数组（以 char* 表示每个字符串）。
*/
static char **convertToSds(int count,char **args){
    int j;
    // 分配一个 char** 数组，用于存储转换后的 sds 字符串指针
    char **sds = (char **)malloc(sizeof(char*)*count);
    for(j = 0;j < count;j++){
        // 逐个将 C 风格字符串转换为 sds 字符串（sdsnew 会分配内存并复制内容）
        sds[j] = sdsnew(args[j]);
    }
    return sds;
}

/*
功能：初始化 CLock 对象的成员变量。
初始化列表：
m_validityTime：锁的有效时间，初始化为 0（单位：毫秒）。
m_resource：要锁定的资源名称，初始化为 NULL（sds 类型）。
m_val：锁的唯一标识（如进程随机名），初始化为 NULL（sds 类型）。
*/
CLock::CLock() : m_validityTime(0), m_resource(NULL), m_val(NULL) {

}

/*
功能：释放 CLock 对象中 sds 类型成员的内存，避免内存泄漏。
*/
CLock::~CLock() {
    sdsfree(m_resource); // 释放资源名称的 sds 内存
    sdsfree(m_val);      // 释放锁标识的 sds 内存
}

/*
功能：初始化 CRedLock 类的静态成员变量，设置默认的重试策略和时钟误差参数。
作用：
m_defaultRetryCount：加锁失败时的默认重试次数。
m_defaultRetryDelay：每次重试的间隔时间（毫秒）。
m_clockDriftFactor：用于估算分布式系统中各节点的时钟偏差（Redlock 算法的关键参数）。
*/
int CRedLock::m_defaultRetryCount = 3;  
int CRedLock::m_defaultRetryDelay = 200;
float CRedLock::m_clockDriftFactor = 0.01;   // 时钟漂移因子：0.01（用于计算时钟误差）

//功能：构造 CRedLock 对象时，自动调用 Initialize 函数进行初始化。
CRedLock::CRedLock() {
    Initialize(); // 调用初始化函数
}

/*
功能：释放 CRedLock 对象占用的所有资源，包括脚本、文件描述符和 Redis 连接。
关键点：
sdsfree：释放 Lua 脚本的内存（脚本在初始化时用 sdsnew 创建）。
close(m_fd)：关闭用于生成随机数的 /dev/urandom 文件。
redisFree：释放 hiredis 库创建的 Redis 连接上下文，避免资源泄漏。
*/
CRedLock::~CRedLock() {
    sdsfree(m_continueLockScript); // 释放续锁脚本的 sds 内存
    sdsfree(m_unlockScript);       // 释放解锁脚本的 sds 内存
    close(m_fd);                   // 关闭随机文件描述符（/dev/urandom）
    /* Disconnects and frees the context */
    for (int i = 0; i < (int)m_redisServer.size(); i++) {
        redisFree(m_redisServer[i]); // 释放 Redis 连接上下文
    }
}

/*
功能：初始化 Redlock 的核心资源，包括 Lua 脚本、重试策略和随机数文件。
关键参数 / 逻辑：
续锁脚本（m_continueLockScript）：
逻辑：检查锁是否属于当前客户端（get KEYS[1] == ARGV[1]），若是则删除旧锁，再用新参数加锁（set ... nx 确保原子性）。
作用：续锁时保证原子性，避免其他客户端抢占。
解锁脚本（m_unlockScript）：
逻辑：仅当锁属于当前客户端时删除锁（避免误删其他客户端的锁），返回删除结果。
作用：保证解锁的安全性，通过 Lua 脚本原子性验证锁的归属。
随机数文件（/dev/urandom）：
用于生成唯一的锁 ID（如 GetUniqueLockId 函数），确保分布式环境下锁的唯一性。
*/
bool CRedLock::Initialize(){
    //初始化续锁脚本（lua脚本，保证原子性操作）
    m_continueLockScript = sdsnew("if redis.call('get', KEYS[1]) == ARGV[1] then redis.call('del', KEYS[1]) end return redis.call('set', KEYS[1], ARGV[2], 'px', ARGV[3], 'nx')");
    // 初始化解锁脚本（Lua 脚本，保证原子性验证和删除）
    m_unlockScript       = sdsnew("if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end");

    //设置默认重试策略
    m_retryCount = m_defaultRetryCount;
    //重试延迟
    m_retryDelay = m_defaultRetryDelay;
    // 多数派数量初始化为 0（后续根据服务器数量计算）
    m_quoRum = 0;

    //打开随机数文件，用于生成唯一锁ID
    m_fd = open("/dev/urandom",O_RDONLY);
    if (m_fd == -1) {
        // 错误处理：打开文件失败时输出日志并退出（实际项目中应更优雅处理）
        printf("Can't open file /dev/urandom\n");
        return false; // 代码逻辑上无法执行到这里
    }
    return true; // 初始化成功
}

/*
功能：向 Redlock 实例中添加 Redis 服务器节点，建立连接并更新多数派数量。
参数：
ip：Redis 服务器的 IP 地址（字符串）。
port：Redis 服务器的端口号（整数）。
*/
bool CRedLock::AddServerUrl(const char *ip,const int port){
    redisContext *c = NULL;
    // 设置连接超时时间：1.5 秒（1 秒 + 500000 微秒）
    struct timeval timeout = {1,500000};
    //连接到redis服务器，带超时设置
    c = redisConnectWithTimeout(ip,port,timeout);
    if(c){
        //连接成功
        //将连接上下文添加到服务器列表
        m_redisServer.push_back(c);
    }else{
        return false;
    }
    // 计算多数派数量（Redlock 算法要求至少半数以上服务器加锁成功）
    m_quoRum = (int)m_redisServer.size() / 2 + 1;
    return true;
}

/*
功能：自定义加锁失败时的重试次数和间隔时间。
参数：
count：重试次数（如 3 次）。
delay：每次重试的间隔时间（单位：毫秒，如 200ms）。
*/
void CRedLock::SetRetry(const int count, const int delay) {
    m_retryCount = count; // 设置重试次数
    m_retryDelay = delay; // 设置重试延迟（毫秒）
}

/*
功能：尝试对指定资源加锁，使用 Redlock 算法，在多个 Redis 实例上进行加锁操作，只有当多数派 Redis 实例加锁成功且锁的有效时间大于 0 时，才认为加锁成功。
参数：
resource：要加锁的资源名称。
ttl：锁的过期时间（毫秒）。
lock：用于存储锁信息的对象。
*/
bool CRedLock::Lock(const char *resource,const int ttl,CLock &lock){
    //生成唯一的锁id
    sds val = GetUniqueLockId();
    // 如果生成失败，返回 false
    if (!val) {
        return false;
    }

    // 复制资源名称到锁对象中
    lock.m_resource = sdsnew(resource);
    // 将唯一锁 ID 赋值给锁对象
    lock.m_val = val;
    // 打印生成的唯一锁 ID
    printf("Get the unique id is %s\n", val);
    // 获取重试次数
    int retryCount = m_retryCount;
    do{
        //记录成功加锁的redis实例的数量
        int n = 0;
        // 记录开始加锁的时间（毫秒）
        int startTime = (int)time(NULL) * 1000;
        //获取redis服务器列表的长度
        int slen = (int)m_redisServer.size();
        //遍历所有redis服务器示例
        for(int i = 0;i < slen;i++){
            //尝试在当前redis示例上加锁
            if(LockInstance(m_redisServer[i],resource,val,ttl)){
                //加锁成功
                n++;
            }
        }
        // 计算时钟漂移，考虑 Redis 过期精度和小 TTL 时的最小漂移
        int drift = (ttl * m_clockDriftFactor) + 2;
        // 计算锁的有效时间
        int validityTime = ttl - ((int)time(NULL) * 1000 - startTime) - drift;
        // 打印锁的有效时间、成功加锁的实例数量和多数派数量
        printf("The resource validty time is %d, n is %d, quo is %d\n",
               validityTime, n, m_quoRum);
        // 如果成功加锁的实例数量达到多数派且锁的有效时间大于 0
        if (n >= m_quoRum && validityTime > 0) {
            // 设置锁对象的有效时间
            lock.m_validityTime = validityTime;
            // 加锁成功，返回 true
            return true;
        } else {
            // 加锁失败，解锁所有已加锁的实例
            Unlock(lock);
        }
        // 计算重试前的随机延迟时间
        int delay = rand() % m_retryDelay + floor(m_retryDelay / 2);
        // 线程休眠指定的延迟时间
        usleep(delay * 1000);
        // 重试次数减 1
        retryCount--;
    }while(retryCount > 0);
    // 重试次数用完仍未成功加锁，返回 false
    return false;
}

/*
功能：尝试对指定资源进行续锁，使用 Redlock 算法，在多个 Redis 实例上进行续锁操作，只有当多数派 Redis 实例续锁成功且锁的有效时间大于 0 时，才认为续锁成功。
参数：
resource：要续锁的资源名称。
ttl：续锁后的过期时间（毫秒）。
lock：用于存储锁信息的对象。
*/
bool CRedLock::ContinueLock(const char *resource, const int ttl, CLock &lock) {
    // 生成一个唯一的锁 ID
    sds val = GetUniqueLockId();
    // 如果生成失败，返回 false
    if (!val) {
        return false;
    }
    // 复制资源名称到锁对象中
    lock.m_resource = sdsnew(resource);
    // 将唯一锁 ID 赋值给锁对象
    lock.m_val = val;
    // 如果续锁对象的资源名称为空
    if (m_continueLock.m_resource == NULL) {
        // 复制资源名称到续锁对象中
        m_continueLock.m_resource = sdsnew(resource);
        // 复制唯一锁 ID 到续锁对象中
        m_continueLock.m_val = sdsnew(val);
    }
    // 打印生成的唯一锁 ID
    printf("Get the unique id is %s\n", val);
    // 获取重试次数
    int retryCount = m_retryCount;
    do {
        // 记录成功续锁的 Redis 实例数量
        int n = 0;
        // 记录开始续锁的时间（毫秒）
        int startTime = (int)time(NULL) * 1000;
        // 获取 Redis 服务器列表的长度
        int slen = (int)m_redisServer.size();
        // 遍历所有 Redis 服务器实例
        for (int i = 0; i < slen; i++) {
            // 尝试在当前 Redis 实例上续锁
            if (ContinueLockInstance(m_redisServer[i], resource, val, ttl)) {
                // 续锁成功，计数器加 1
                n++;
            }
        }
        // 释放续锁对象的旧唯一锁 ID
        sdsfree(m_continueLock.m_val);
        // 更新续锁对象的唯一锁 ID
        m_continueLock.m_val = sdsnew(val);
        // 计算时钟漂移，考虑 Redis 过期精度和小 TTL 时的最小漂移
        int drift = (ttl * m_clockDriftFactor) + 2;
        // 计算锁的有效时间
        int validityTime = ttl - ((int)time(NULL) * 1000 - startTime) - drift;
        // 打印锁的有效时间、成功续锁的实例数量和多数派数量
        printf("The resource validty time is %d, n is %d, quo is %d\n",
               validityTime, n, m_quoRum);
        // 如果成功续锁的实例数量达到多数派且锁的有效时间大于 0
        if (n >= m_quoRum && validityTime > 0) {
            // 设置锁对象的有效时间
            lock.m_validityTime = validityTime;
            // 续锁成功，返回 true
            return true;
        } else {
            // 续锁失败，解锁所有已加锁的实例
            Unlock(lock);
        }
        // 计算重试前的随机延迟时间
        int delay = rand() % m_retryDelay + floor(m_retryDelay / 2);
        // 线程休眠指定的延迟时间
        usleep(delay * 1000);
        // 重试次数减 1
        retryCount--;
    } while (retryCount > 0);
    // 重试次数用完仍未成功续锁，返回 false
    return false;
}

/*
功能：对指定的锁对象进行解锁操作，遍历所有 Redis 实例，在每个实例上执行解锁操作。
参数：
lock：要解锁的锁对象。
*/
bool CRedLock::Unlock(const CLock &lock){
    int slen = (int)m_redisServer.size();
    for(int i =0;i < slen;i++){
        // 在当前 Redis 实例上解锁
        UnlockInstance(m_redisServer[i],lock.m_resource,lock.m_val);
    }
    return true;
}

/*
功能：在单个 Redis 实例上尝试对指定资源加锁，使用 SET 命令并设置过期时间和 NX 选项，只有当返回结果为 "OK" 时，才认为加锁成功。
参数：
c：Redis 上下文对象，用于与 Redis 服务器通信。
resource：要加锁的资源名称。
val：唯一的锁 ID。
ttl：锁的过期时间（毫秒）。
*/
bool CRedLock::LockInstance(redisContext *c,const char *resource,const char *val,const int ttl){
    //定义redis响应对象指针
    redisReply *reply;
    //向redis服务器发送SET命令，尝试加锁
    reply = (redisReply *)redisCommand(c,"set %s %s px %d nx",resource, val, ttl);
    //如果有响应
    if(reply){
        // 打印 SET 命令的返回结果
        printf("Set return: %s [null == fail, OK == success]\n", reply->str);
    }   
    //如果响应不为空，且返回的结果为ok
    if(reply && reply->str && strcmp(reply->str,"ok") == 0){
        //释放redis对象
        freeReplyObject(reply);
        return true;
    }
    if(reply){
        // 释放 Redis 响应对象
        freeReplyObject(reply);
    }
    return false;
}

/*
功能
在单个 Redis 实例上执行续锁操作，通过 Lua 脚本原子性地删除旧锁并设置新锁，确保续锁过程的安全性和原子性。
参数
c：Redis 连接上下文（用于与 Redis 服务器通信）。
resource：要续锁的资源名称。
val：新生成的唯一锁 ID（用于标识当前客户端的新锁）。
ttl：续锁后的过期时间（毫秒）。
*/
bool CRedLock::ContinueLockInstance(redisContext *c,const char *resource,const char *val,const int ttl){
    // 参数数量：7 个（EVAL 命令固定格式：脚本、key 数量、key、参数...）
    int argc = 7;
    sds sdsTTL = sdsempty();
    // 将 ttl 格式化为字符串并拼接到 sdsTTL 中（例如 "1000"）
    sdsTTL = sdscatprintf(sdsTTL,"%d",ttl);
    //构造lua脚本执行所需要的参数数组
    char *continueLockScriptArgv[] = {
        (char *)"EVAL",   //redis命令：执行lua脚本
        m_continueLockScript,    //续锁脚本的内容，lua代码
        (char *)"1",      //key数量：1个，资源名
        (char *)resource,   //第一个key的资源名称
        m_continueLock.m_val,  //脚本参数1：旧锁唯一ID
        (char *)val,  //新锁的唯一ID
        sdsTTL    //新锁的过期时间
    };
    
    // 调用 RedisCommandArgv 执行 Lua 脚本
    redisReply *reply = RedisCommandArgv(c,argc,continueLockScriptArgv);
    //释放sdsTTL
    sdsfree(sdsTTL);

    // 打印 Redis 响应（调试用）
    if (reply) {
        printf("Set return: %s [null == fail, OK == success]\n", reply->str);
    }
    
    // 判断响应是否为 "OK"（续锁成功）
    if (reply && reply->str && strcmp(reply->str, "OK") == 0) {
        freeReplyObject(reply);  // 释放响应对象内存
        return true;             // 续锁成功
    }
    
    // 释放响应对象内存（失败时）
    if (reply) {
        freeReplyObject(reply);
    }
    return false;  // 续锁失败
}

/*
功能
在单个 Redis 实例上执行解锁操作，通过 Lua 脚本原子性地验证锁的归属并删除锁，避免误删其他客户端的锁。
参数
c：Redis 连接上下文。
resource：要解锁的资源名称。
val：锁的唯一 ID（用于验证当前客户端是否为锁的持有者）。
*/
void CRedLock::UnlockInstance(redisContext *c,const char *resource,const char *val){
    // 参数数量：5 个（EVAL 命令固定格式：脚本、key 数量、key、参数）
    int argc = 5;
    // 构造解锁脚本执行所需的参数数组
    char *unlockScriptArgv[] = {
        (char*)"EVAL",         // Redis 命令：执行 Lua 脚本
        m_unlockScript,        // 解锁脚本内容（Lua 代码）
        (char*)"1",            // key 数量：1 个（资源名）
        (char*)resource,       // 第一个 key：资源名称
        (char*)val             // 脚本参数：锁的唯一 ID（验证是否为锁的持有者）
    };
    
    // 调用 RedisCommandArgv 执行解锁脚本
    redisReply *reply = RedisCommandArgv(c, argc, unlockScriptArgv);
    // 释放响应对象内存（无论成功与否）
    if (reply) {
        freeReplyObject(reply);
    }
}

/*
封装 hiredis 的 redisCommandArgv 接口，支持二进制安全的参数传递，用于执行带多个参数的 Redis 命令（如 EVAL 脚本）。
参数
c：Redis 连接上下文。
argc：参数数量。
inargv：参数数组（char* 类型，需转换为 sds 以支持二进制安全）。
*/
redisReply *CRedLock::RedisCommandArgv(redisContext *c,int argc,char **inargv){
    // 将输入的 char** 转换为 sds 数组（Redis 要求参数为二进制安全的字符串）
    char **argv = convertToSds(argc,inargv);
    //分配参数长度数组（存储每个参数的字节长度）
    size_t *argvlen = (size_t *)malloc(argc * sizeof(size_t));
    // 填充参数长度（sdslen 获取 sds 字符串的实际长度，支持二进制数据）
    for(int j = 0;j < argc;j++){
        argvlen[j] = sdslen(argv[j]);
    }

    //调用hiredis的redisCommandArgv执行命令（带参数长度，支持二进制安全）
    redisReply *reply = (redisReply *)redisCommandArgv(c,argc,(const char **)argv,argvlen);
    // 打印响应（调试用，仅针对整数类型响应，实际需根据命令类型处理）
    if (reply) {
        printf("RedisCommandArgv return: %lld\n", reply->integer);
    }
    
    // 释放内存：先释放参数长度数组，再释放 sds 数组
    free(argvlen);
    sdsfreesplitres(argv, argc);  // sdsfreesplitres 释放 sds 数组及每个 sds 字符串
    
    return reply;  // 返回 Redis 响应
}

/*
功能
生成一个全局唯一的锁 ID，用于标识加锁的客户端，确保不同客户端的锁相互隔离。
*/
sds CRedLock::GetUniqueLockId(){
    unsigned char buffer[20];   // 20 字节的随机数据缓冲区（160 位，足够唯一）
    //从/dev/urandom读取20字节随机数据
    if(read(m_fd,buffer,sizeof(buffer)) == sizeof(buffer)){
        sds s = sdsempty();
        // 将每个字节转换为 16 进制字符串（如 0x12 转为 "12"），拼接成 40 位的唯一 ID
        for (int i = 0; i < 20; i++) {
            s = sdscatprintf(s, "%02X", buffer[i]);
        }
        return s;  // 返回生成的唯一锁 ID（如 "5A3D2B7F1E4C89012D3E4F5A6B7C8D9E"）
    }else {
        // 读取失败时打印错误信息（实际项目中需处理错误，如重试或抛出异常）
        printf("Error: GetUniqueLockId %d\n", __LINE__);
    }
    return NULL;
}