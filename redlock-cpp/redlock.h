#ifndef __redlock__
#define __redlock__

#include<iostream>
#include<vector>
#include<hiredis/hiredis.h>

//使用extern "C"声明，确保sds.h中的函数能够按照c语言的命名和调用约定进行链接
extern "C"{
#include "sds.h" 
}

using namespace std;

//定义一个CLock类，用于表示一个锁对象
class CLock{
public:
    CLock();
    ~CLock();
public:
    //当前锁可以存活的时间，单位为ms
    int m_validityTime;
    //要锁住的资源名称，使用sds类型（简单动态字符串)
    sds m_resource;
    //锁住资源的进程的随机名字，使用sds类型
    sds m_val;
};

//定义一个CRedLock类，用于实现RedLock算法
//一个 CRedLock 对象是一个基于 RedLock 算法实现的、用于在多个 Redis 实例上管理分布式锁的工具类，支持对指定资源进行加锁、续锁和解锁操作
class CRedLock{
public:
    CRedLock();
    //类的虚析构函数，在对象销毁的时候调用，使用虚析构函数可以确保正确是否派生类对象
    virtual ~CRedLock();
public:
    //初始化CRedLock类的相关成员
    bool Initialize();
    // 向 CRedLock 类中添加 Redis 服务器的 IP 地址和端口号，返回添加是否成功
    bool AddServerUrl(const char *ip,const int port);
    //设置重试次数和重试延迟时延
    void SetRetry(const int count,const int delay);
    //尝试对指定资源加锁,ttl 为锁的过期时间，lock 为锁对象，返回加锁是否成功
    bool Lock(const char *resource,const int ttl,CLock &lock);
    // 尝试对指定资源进行续锁，ttl 为续锁后的过期时间，lock 为锁对象，返回续锁是否成功
    bool                    ContinueLock(const char *resource, const int ttl,CLock &lock);
    // 对指定的锁对象进行解锁操作，返回解锁是否成功
    bool Unlock(const CLock &lock);
private:
    // 对单个 Redis 实例进行加锁操作，c 为 Redis 上下文，resource 为资源名称，val 为锁的值，ttl 为锁的过期时间，返回加锁是否成功
    bool LockInstance(redisContext *c, const char *resource,
        const char *val, const int ttl);
     // 对单个 Redis 实例进行续锁操作，c 为 Redis 上下文，resource 为资源名称，val 为锁的值，ttl 为续锁后的过期时间，返回续锁是否成功
    bool ContinueLockInstance(redisContext *c, const char *resource,
                                                 const char *val, const int ttl);
    // 对单个 Redis 实例进行解锁操作，c 为 Redis 上下文，resource 为资源名称，val 为锁的值
    void UnlockInstance(redisContext *c, const char *resource,
        const char *val);
    // 生成一个唯一的锁 ID，返回该 ID 的 sds 类型字符串
    sds GetUniqueLockId();
    // 向 Redis 服务器发送带有多个参数的命令，c 为 Redis 上下文，argc 为参数数量，inargv 为参数数组，返回 Redis 服务器的响应
    redisReply *RedisCommandArgv(redisContext *c, int argc, char **inargv);                                        
private:
    // 静态成员变量，默认的重试次数，初始值为 3
    static int              m_defaultRetryCount;    
    // 静态成员变量，默认的重试延迟时间，单位为毫秒，初始值为 200
    static int              m_defaultRetryDelay;    
    // 静态成员变量，电脑时钟误差因子，初始值为 0.01
    static float            m_clockDriftFactor;  
private:
    // 解锁脚本的 sds 类型字符串
    sds                     m_unlockScript;         
    // 实际的重试次数
    int                     m_retryCount;           
    // 实际的重试延迟时间
    int                     m_retryDelay;           
    // 多数派数量，用于判断加锁是否成功
    int                     m_quoRum;               
    // 随机文件的文件描述符
    int                     m_fd;                   
    // 存储多个 Redis 服务器上下文的向量
    vector<redisContext *>  m_redisServer;          
    // 续锁对象
    CLock                   m_continueLock;         
    // 续锁脚本的 sds 类型字符串
    sds                     m_continueLockScript;   
};

#endif