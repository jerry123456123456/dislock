/* SDS (Simple Dynamic Strings), A C dynamic strings library.
 *
 * Copyright (c) 2006-2014, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>

// 定义 sds 类型，实际上是 char* 类型
typedef char *sds;

// 定义 SDS 字符串的头部结构体
struct sdshdr {
    int len;  // 当前字符串的长度（不包括字符串结束符 '\0'）
    int free; // 字符串剩余的空闲空间
    char buf[]; // 存储字符串的缓冲区，柔性数组
};

// 内联函数，用于获取 sds 字符串的长度
// 参数 s: 要获取长度的 sds 字符串
// 返回值: sds 字符串的长度
static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (struct sdshdr*)(s-sizeof *sh);
    return sh->len;
}

// 内联函数，用于获取 sds 字符串的可用空闲空间
// 参数 s: 要获取可用空间的 sds 字符串
// 返回值: sds 字符串的可用空闲空间
static inline size_t sdsavail(const sds s) {
    struct sdshdr *sh = (struct sdshdr*)(s-sizeof *sh);
    return sh->free;
}

// 创建一个指定长度的 sds 字符串
// 参数 init: 初始化字符串的指针，可以为 NULL
// 参数 initlen: 初始化字符串的长度
// 返回值: 新创建的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsnewlen(const void *init, size_t initlen);

// 创建一个以 null 结尾的 C 字符串对应的 sds 字符串
// 参数 init: 初始化的 C 字符串，可以为 NULL
// 返回值: 新创建的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsnew(const char *init);

// 创建一个空的 sds 字符串
// 返回值: 新创建的空 sds 字符串，如果内存分配失败则返回 NULL
sds sdsempty(void);

// 获取 sds 字符串的长度
// 参数 s: 要获取长度的 sds 字符串
// 返回值: sds 字符串的长度
size_t sdslen(const sds s);

// 复制一个 sds 字符串
// 参数 s: 要复制的 sds 字符串
// 返回值: 复制后的新 sds 字符串，如果内存分配失败则返回 NULL
sds sdsdup(const sds s);

// 释放 sds 字符串占用的内存
// 参数 s: 要释放的 sds 字符串，如果为 NULL 则不进行任何操作
void sdsfree(sds s);

// 获取 sds 字符串的可用空闲空间
// 参数 s: 要获取可用空间的 sds 字符串
// 返回值: sds 字符串的可用空闲空间
size_t sdsavail(const sds s);

// 将 sds 字符串增长到指定长度，新增部分用 0 填充
// 参数 s: 要增长的 sds 字符串
// 参数 len: 要增长到的长度
// 返回值: 增长后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsgrowzero(sds s, size_t len);

// 将指定长度的二进制数据追加到 sds 字符串末尾
// 参数 s: 要追加数据的 sds 字符串
// 参数 t: 要追加的二进制数据的指针
// 参数 len: 要追加的二进制数据的长度
// 返回值: 追加后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscatlen(sds s, const void *t, size_t len);

// 将以 null 结尾的 C 字符串追加到 sds 字符串末尾
// 参数 s: 要追加数据的 sds 字符串
// 参数 t: 要追加的 C 字符串
// 返回值: 追加后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscat(sds s, const char *t);

// 将另一个 sds 字符串追加到当前 sds 字符串末尾
// 参数 s: 要追加数据的 sds 字符串
// 参数 t: 要追加的另一个 sds 字符串
// 返回值: 追加后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscatsds(sds s, const sds t);

// 将指定长度的二进制数据复制到 sds 字符串中
// 参数 s: 要复制数据的 sds 字符串
// 参数 t: 要复制的二进制数据的指针
// 参数 len: 要复制的二进制数据的长度
// 返回值: 复制后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscpylen(sds s, const char *t, size_t len);

// 将以 null 结尾的 C 字符串复制到 sds 字符串中
// 参数 s: 要复制数据的 sds 字符串
// 参数 t: 要复制的 C 字符串
// 返回值: 复制后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscpy(sds s, const char *t);

// 以可变参数列表的形式将格式化字符串追加到 sds 字符串末尾
// 参数 s: 要追加格式化字符串的 sds 字符串
// 参数 fmt: 格式化字符串
// 参数 ap: 可变参数列表
// 返回值: 追加后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscatvprintf(sds s, const char *fmt, va_list ap);

#ifdef __GNUC__
// 以可变参数的形式将格式化字符串追加到 sds 字符串末尾
// 参数 s: 要追加格式化字符串的 sds 字符串
// 参数 fmt: 格式化字符串
// ...: 可变参数
// 返回值: 追加后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
// 以可变参数的形式将格式化字符串追加到 sds 字符串末尾
// 参数 s: 要追加格式化字符串的 sds 字符串
// 参数 fmt: 格式化字符串
// ...: 可变参数
// 返回值: 追加后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

// 从 sds 字符串的首尾移除指定字符集合中的字符
// 参数 s: 要修剪的 sds 字符串
// 参数 cset: 要移除的字符集合
void sdstrim(sds s, const char *cset);

// 截取 sds 字符串的指定范围
// 参数 s: 要截取的 sds 字符串
// 参数 start: 截取的起始位置，可以为负数，表示从末尾开始计数
// 参数 end: 截取的结束位置，可以为负数，表示从末尾开始计数
void sdsrange(sds s, int start, int end);

// 更新 sds 字符串的长度，以第一个 null 字符为结束标志
// 参数 s: 要更新长度的 sds 字符串
void sdsupdatelen(sds s);

// 清空 sds 字符串，将其长度置为 0，但不释放内存
// 参数 s: 要清空的 sds 字符串
void sdsclear(sds s);

// 比较两个 sds 字符串的大小
// 参数 s1: 第一个 sds 字符串
// 参数 s2: 第二个 sds 字符串
// 返回值: 如果 s1 > s2 返回 1，如果 s1 < s2 返回 -1，如果 s1 == s2 返回 0
int sdscmp(const sds s1, const sds s2);

// 将指定长度的字符串按指定分隔符分割成多个 sds 字符串
// 参数 s: 要分割的字符串
// 参数 len: 要分割的字符串的长度
// 参数 sep: 分隔符字符串
// 参数 seplen: 分隔符字符串的长度
// 参数 count: 用于存储分割后的字符串数量的指针
// 返回值: 分割后的 sds 字符串数组，如果内存分配失败则返回 NULL
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);

// 释放由 sdssplitlen 函数返回的 sds 字符串数组
// 参数 tokens: 要释放的 sds 字符串数组
// 参数 count: 数组中 sds 字符串的数量
void sdsfreesplitres(sds *tokens, int count);

// 将 sds 字符串中的所有字符转换为小写
// 参数 s: 要转换的 sds 字符串
void sdstolower(sds s);

// 将 sds 字符串中的所有字符转换为大写
// 参数 s: 要转换的 sds 字符串
void sdstoupper(sds s);

// 将一个 long long 类型的整数转换为 sds 字符串
// 参数 value: 要转换的 long long 类型的整数
// 返回值: 转换后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsfromlonglong(long long value);

// 将指定长度的字符串以转义形式追加到 sds 字符串末尾
// 参数 s: 要追加转义字符串的 sds 字符串
// 参数 p: 要追加的字符串的指针
// 参数 len: 要追加的字符串的长度
// 返回值: 追加后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdscatrepr(sds s, const char *p, size_t len);

// 将一个字符串按特定规则分割成多个参数
// 参数 line: 要分割的字符串
// 参数 argc: 用于存储分割后的参数数量的指针
// 返回值: 分割后的 sds 字符串数组，如果内存分配失败或输入格式错误则返回 NULL
sds *sdssplitargs(const char *line, int *argc);

// 将 sds 字符串中的指定字符集合替换为另一个字符集合
// 参数 s: 要进行字符替换的 sds 字符串
// 参数 from: 要替换的字符集合
// 参数 to: 替换后的字符集合
// 参数 setlen: 字符集合的长度
// 返回值: 替换后的 sds 字符串
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);

// 将多个 C 字符串用指定分隔符连接成一个 sds 字符串
// 参数 argv: 要连接的 C 字符串数组
// 参数 argc: 数组中 C 字符串的数量
// 参数 sep: 分隔符字符串
// 参数 seplen: 分隔符字符串的长度
// 返回值: 连接后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsjoin(char **argv, int argc, char *sep, size_t seplen);

// 将多个 sds 字符串用指定分隔符连接成一个 sds 字符串
// 参数 argv: 要连接的 sds 字符串数组
// 参数 argc: 数组中 sds 字符串的数量
// 参数 sep: 分隔符字符串
// 参数 seplen: 分隔符字符串的长度
// 返回值: 连接后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API */

// 为 sds 字符串预留指定长度的空间
// 参数 s: 要预留空间的 sds 字符串
// 参数 addlen: 要预留的空间长度
// 返回值: 预留空间后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsMakeRoomFor(sds s, size_t addlen);

// 增加 sds 字符串的长度，并减少相应的空闲空间
// 参数 s: 要增加长度的 sds 字符串
// 参数 incr: 要增加的长度，可以为负数
void sdsIncrLen(sds s, int incr);

// 移除 sds 字符串末尾的空闲空间
// 参数 s: 要移除空闲空间的 sds 字符串
// 返回值: 移除空闲空间后的 sds 字符串，如果内存分配失败则返回 NULL
sds sdsRemoveFreeSpace(sds s);

// 获取 sds 字符串分配的总内存大小
// 参数 s: 要获取总内存大小的 sds 字符串
// 返回值: sds 字符串分配的总内存大小
size_t sdsAllocSize(sds s);

#endif