# Redis源码解析（二）：事件驱动模型

## 概述

Redis的事件驱动模型是其高性能网络通信的核心。本文将深入分析Redis的事件驱动架构，包括事件循环、文件事件、时间事件以及多路复用机制的实现。

## 1. 事件驱动架构概述

### 1.1 Reactor模式

Redis采用经典的Reactor模式，通过事件循环处理并发请求：

```c
typedef struct aeEventLoop {
    int maxfd;                   // 最大文件描述符
    int setsize;                 // 事件集合大小
    long long timeEventNextId;   // 下一个时间事件ID
    int nevents;                 // 注册事件数量
    aeFileEvent *events;         // 注册的文件事件
    aeFiredEvent *fired;         // 触发的事件
    aeTimeEvent *timeEventHead;  // 时间事件链表
    int stop;                    // 停止标志
    void *apidata;              // 多路复用API数据
    aeBeforeSleepProc *beforesleep;  // 睡眠前回调
    aeBeforeSleepProc *aftersleep;   // 睡眠后回调
    int flags;                  // 事件循环标志
    void *privdata[2];          // 私有数据
} aeEventLoop;
```

### 1.2 事件类型

Redis支持两种类型的事件：

1. **文件事件（File Events）**：处理网络I/O
2. **时间事件（Time Events）**：处理定时任务

## 2. 文件事件（File Events）

### 2.1 文件事件结构

```c
typedef struct aeFileEvent {
    int mask;                     // 事件掩码 (AE_READABLE|AE_WRITABLE|AE_BARRIER)
    aeFileProc *rfileProc;        // 读事件处理器
    aeFileProc *wfileProc;        // 写事件处理器
    void *clientData;             // 客户端数据
} aeFileEvent;
```

### 2.2 事件掩码

- `AE_READABLE`：可读事件
- `AE_WRITABLE`：可写事件
- `AE_BARRIER`：屏障事件，确保读写事件不会在同一循环中触发

### 2.3 文件事件创建

```c
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
                     aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }

    aeFileEvent *fe = &eventLoop->events[fd];

    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;

    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;
    fe->clientData = clientData;

    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    return AE_OK;
}
```

## 3. 时间事件（Time Events）

### 3.1 时间事件结构

```c
typedef struct aeTimeEvent {
    long long id;                    // 时间事件ID
    monotime when;                   // 触发时间
    aeTimeProc *timeProc;            // 时间处理器
    aeEventFinalizerProc *finalizerProc;  // 清理函数
    void *clientData;                // 客户端数据
    struct aeTimeEvent *prev;        // 前驱节点
    struct aeTimeEvent *next;        // 后继节点
    int refcount;                    // 引用计数
} aeTimeEvent;
```

### 3.2 时间事件管理

时间事件以双向链表的形式组织，按触发时间排序：

```c
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
                           aeTimeProc *proc, void *clientData,
                           aeEventFinalizerProc *finalizerProc)
{
    monotime now = monotime_now();
    monotime when = monotime_add(now, milliseconds);

    aeTimeEvent *te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;

    te->id = eventLoop->timeEventNextId++;
    te->when = when;
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    te->prev = NULL;
    te->next = NULL;
    te->refcount = 1;

    // 插入到链表的正确位置
    aeTimeEvent *prev = NULL, *curr = eventLoop->timeEventHead;
    while (curr != NULL && monotime_lt(curr->when, when)) {
        prev = curr;
        curr = curr->next;
    }

    if (prev == NULL) {
        // 插入到链表头部
        te->next = eventLoop->timeEventHead;
        eventLoop->timeEventHead = te;
        if (te->next) te->next->prev = te;
    } else {
        // 插入到中间或尾部
        te->next = prev->next;
        prev->next = te;
        if (te->next) te->next->prev = te;
        te->prev = prev;
    }

    return te->id;
}
```

## 4. 事件循环核心

### 4.1 事件处理主循环

```c
void aeMain(aeEventLoop *eventLoop) {
    eventLoop->stop = 0;
    while (!eventLoop->stop) {
        aeProcessEvents(eventLoop, AE_ALL_EVENTS|
                                   AE_CALL_BEFORE_SLEEP|
                                   AE_CALL_AFTER_SLEEP);
    }
}
```

### 4.2 事件处理函数

`aeProcessEvents`是事件驱动的核心：

```c
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    int processed = 0, numevents;

    // 没有事件需要处理，直接返回
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    // 调用睡眠前回调
    if (eventLoop->beforesleep != NULL && (flags & AE_CALL_BEFORE_SLEEP))
        eventLoop->beforesleep(eventLoop);

    // 计算等待时间
    struct timeval tv, *tvp = NULL;
    if ((flags & AE_DONT_WAIT) || (eventLoop->flags & AE_DONT_WAIT)) {
        // 非阻塞模式
        tv.tv_sec = tv.tv_usec = 0;
        tvp = &tv;
    } else if (flags & AE_TIME_EVENTS) {
        // 根据最近的时间事件计算等待时间
        int64_t usUntilTimer = usUntilEarliestTimer(eventLoop);
        if (usUntilTimer >= 0) {
            tv.tv_sec = usUntilTimer / 1000000;
            tv.tv_usec = usUntilTimer % 1000000;
            tvp = &tv;
        }
    }

    // 等待事件
    numevents = aeApiPoll(eventLoop, tvp);

    // 调用睡眠后回调
    if (eventLoop->aftersleep != NULL && flags & AE_CALL_AFTER_SLEEP)
        eventLoop->aftersleep(eventLoop);

    // 处理文件事件
    for (int j = 0; j < numevents; j++) {
        aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
        int mask = eventLoop->fired[j].mask;
        int fd = eventLoop->fired[j].fd;
        int fired = 0;

        // 先处理读事件
        if (fe->mask & mask & AE_READABLE) {
            fe->rfileProc(eventLoop, fd, fe->clientData, mask);
            fired++;
        }

        // 处理写事件
        if (fe->mask & mask & AE_WRITABLE) {
            if (!fired || fe->mask & AE_BARRIER) {
                fe->wfileProc(eventLoop, fd, fe->clientData, mask);
                fired++;
            }
        }

        processed++;
    }

    // 处理时间事件
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed;
}
```

## 5. 多路复用实现

### 5.1 多路复用API抽象

Redis抽象了不同操作系统的多路复用机制：

```c
// 根据系统选择最优的多路复用实现
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
    #ifdef HAVE_EPOLL
    #include "ae_epoll.c"
    #else
        #ifdef HAVE_KQUEUE
        #include "ae_kqueue.c"
        #else
        #include "ae_select.c"
        #endif
    #endif
#endif
```

### 5.2 Epoll实现示例

```c
// ae_epoll.c
typedef struct aeApiState {
    int epfd;                    // epoll文件描述符
    struct epoll_event *events;  // 事件数组
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));
    if (!state) return -1;

    state->events = zmalloc(sizeof(struct epoll_event)*eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }

    state->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (state->epfd == -1) {
        zfree(state->events);
        zfree(state);
        return -1;
    }

    eventLoop->apidata = state;
    return 0;
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd, state->events, eventLoop->setsize,
                        tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    if (retval > 0) {
        numevents = retval;
        for (int j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events+j;

            if (e->events & EPOLLIN) mask |= AE_READABLE;
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            if (e->events & EPOLLERR) mask |= AE_WRITABLE;
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE;

            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    }

    return numevents;
}
```

## 6. 网络事件处理

### 6.1 连接接受处理

```c
// TCP连接接受处理器
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd;
    char cip[NET_IP_STR_LEN];
    client *c;

    // 循环接受所有连接
    while (1) {
        cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                serverLog(LL_WARNING,
                    "Accepting client connection: %s", server.neterr);
            return;
        }

        serverLog(LL_VERBOSE,"Accepted %s:%d", cip, cport);

        // 创建客户端
        c = createClient(cfd);
        if (c) {
            // 设置客户端
            acceptCommonHandler(c, 0, cip);
        } else {
            serverLog(LL_WARNING,
                "Error creating client: %s", server.neterr);
            close(cfd);
        }
    }
}
```

### 6.2 命令读取处理

```c
// 命令读取处理器
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    client *c = (client*) privdata;
    int nread, readlen;

    // 读取客户端数据
    readlen = PROTO_IOBUF_LEN;
    nread = read(fd, c->querybuf+c->qblen, readlen);

    if (nread == -1) {
        if (errno == EAGAIN) {
            // 非阻塞模式，没有数据可读
            return;
        } else {
            serverLog(LL_VERBOSE, "Reading from client: %s",strerror(errno));
            freeClient(c);
            return;
        }
    } else if (nread == 0) {
        // 客户端关闭连接
        serverLog(LL_VERBOSE, "Client closed connection");
        freeClient(c);
        return;
    }

    // 更新读取统计
    c->qblen += nread;
    c->lastinteraction = server.unixtime;

    // 处理输入缓冲区
    if (c->querybuf[c->qblen-1] != '\n') {
        // 数据不完整，等待更多数据
        return;
    }

    // 处理客户端命令
    processInputBuffer(c);
}
```

## 7. 时间事件处理

### 7.1 定时任务

```c
// 处理时间事件
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te, *prev;
    monotime now = monotime_now();

    // 遍历时间事件链表
    te = eventLoop->timeEventHead;
    while (te) {
        prev = te;
        te = te->next;

        // 检查是否已经触发
        if (monotime_gte(now, te->when)) {
            // 调用处理器
            int retval = te->timeProc(eventLoop, te->id, te->clientData);

            processed++;

            // 根据返回值决定是否重复
            if (retval != AE_NOMORE) {
                // 重复事件，更新触发时间
                te->when = monotime_add(now, retval);
            } else {
                // 一次性事件，删除
                if (prev->next == te) {
                    prev->next = te->next;
                    if (te->next) te->next->prev = prev;
                } else if (te == eventLoop->timeEventHead) {
                    eventLoop->timeEventHead = te->next;
                    if (te->next) te->next->prev = NULL;
                } else {
                    // 中间节点
                    te->prev->next = te->next;
                    if (te->next) te->next->prev = te->prev;
                }

                if (te->finalizerProc)
                    te->finalizerProc(eventLoop, te->clientData);

                zfree(te);
            }
        }
    }

    return processed;
}
```

### 7.2 服务器定时任务

```c
// 服务器定时任务
int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    int j;
    UNUSED(eventLoop);
    UNUSED(id);
    UNUSED(clientData);

    // 更新服务器时间
    server.unixtime = time(NULL);
    server.mstime = mstime();

    // 更新LRU时钟
    updateLRUClock();

    // 处理过期键
    if (server.active_expire_enabled && server.masterhost == NULL) {
        activeExpireCycle(ACTIVE_EXPIRE_CYCLE_SLOW);
    }

    // 处理客户端输出缓冲区
    clientsAreUnblocked();

    // 更新统计信息
    record_sampling_info();

    // AOF重写检查
    if (server.aof_rewrite_scheduled) {
        rewriteAppendOnlyFileBackground();
    }

    // 检查是否需要执行RDB持久化
    if (server.saveparamslen > 0) {
        for (j = 0; j < server.saveparamslen; j++) {
            struct saveparam *sp = server.saveparams + j;
            if (server.dirty >= sp->changes &&
                server.unixtime-server.lastsave > sp->seconds) {
                serverLog(LL_NOTICE,"%d changes in %d seconds. Saving...",
                    sp->changes, (int)sp->seconds);
                rdbSaveBackground(server.rdb_filename);
                break;
            }
        }
    }

    // 返回下一次执行的间隔（毫秒）
    return 1000/server.hz;
}
```

## 8. 设计模式分析

### 8.1 Reactor模式

Redis完整实现了Reactor模式：

- **事件分发器**：`aeEventLoop`
- **事件处理器**：`aeFileProc`和`aeTimeProc`
- **多路复用器**：`aeApiPoll`

### 8.2 策略模式

不同的多路复用实现作为不同的策略：

```c
// 每种多路复用实现都提供相同的接口
static int aeApiCreate(aeEventLoop *eventLoop);
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask);
static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask);
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp);
static char *aeApiName(void);
```

### 8.3 观察者模式

通过回调函数实现观察者模式：

```c
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
```

## 9. 性能优化技巧

### 9.1 批量处理

```c
// 批量处理文件事件
for (int j = 0; j < numevents; j++) {
    // 处理单个文件事件
    // ...
}
```

### 9.2 非阻塞I/O

所有网络操作都使用非阻塞I/O：

```c
// 设置非阻塞模式
anetNonBlock(NULL, fd);
anetEnableTcpNoDelay(NULL, fd);
```

### 9.3 边缘触发优化

在支持边缘触发的系统上，使用边缘触发模式提高效率。

## 10. 内存管理

### 10.1 事件池管理

```c
// 预分配事件池
eventLoop->events = zmalloc(sizeof(aeFileEvent)*eventLoop->nevents);
eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*eventLoop->nevents);
```

### 10.2 动态调整

```c
// 根据需要调整事件池大小
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    // 重新分配内存
    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    eventLoop->nevents = setsize;
    return AE_OK;
}
```

## 11. 错误处理

### 11.1 优雅降级

```c
// 如果某种多路复用不可用，使用select作为备选
#ifdef HAVE_EPOLL
#include "ae_epoll.c"
#else
    #ifdef HAVE_KQUEUE
    #include "ae_kqueue.c"
    #else
    #include "ae_select.c"
    #endif
#endif
```

### 11.2 资源清理

```c
// 释放事件循环资源
void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);
    zfree(eventLoop->events);
    zfree(eventLoop->fired);

    // 释放时间事件
    aeTimeEvent *next_te, *te = eventLoop->timeEventHead;
    while (te) {
        next_te = te->next;
        if (te->finalizerProc)
            te->finalizerProc(eventLoop, te->clientData);
        zfree(te);
        te = next_te;
    }
    zfree(eventLoop);
}
```

## 12. 单元测试

```c
// 测试事件循环创建和销毁
void test_event_loop() {
    aeEventLoop *loop = aeCreateEventLoop(1024);
    assert(loop != NULL);
    aeDeleteEventLoop(loop);
}

// 测试文件事件
void test_file_event() {
    aeEventLoop *loop = aeCreateEventLoop(1024);
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    // 注册读事件
    int ret = aeCreateFileEvent(loop, fd, AE_READABLE, test_read_handler, NULL);
    assert(ret == AE_OK);

    // 删除事件
    aeDeleteFileEvent(loop, fd, AE_READABLE);
    close(fd);
    aeDeleteEventLoop(loop);
}

// 测试时间事件
void test_time_event() {
    aeEventLoop *loop = aeCreateEventLoop(1024);

    // 创建时间事件
    long long id = aeCreateTimeEvent(loop, 100, test_time_handler, NULL, NULL);
    assert(id != AE_ERR);

    // 处理事件
    aeProcessEvents(loop, AE_TIME_EVENTS);

    aeDeleteEventLoop(loop);
}
```

## 13. 总结

Redis的事件驱动模型体现了以下设计原则：

1. **高效性**：通过多路复用实现单线程高并发
2. **可扩展性**：抽象多路复用API，支持不同系统
3. **可靠性**：完善的错误处理和资源管理
4. **灵活性**：支持文件事件和时间事件

这个事件驱动模型是Redis高性能的基石，对于理解和设计高性能网络服务器具有重要参考价值。

## 下一步

在下一篇文章中，我们将深入分析Redis的网络通信机制，包括协议解析、连接管理等。