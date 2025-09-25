# Redis源码解析（五）：多线程架构

## 概述

Redis在保持单线程事件驱动模型的基础上，逐步引入了多线程架构来提升性能。本文将深入分析Redis的多线程架构，包括I/O多线程、后台任务线程、线程间通信等实现机制。

## 1. 多线程架构概述

### 1.1 线程类型

Redis包含以下几种线程类型：

1. **主线程**：处理命令执行、事件循环
2. **I/O线程**：处理网络读写操作
3. **后台线程**：处理AOF同步、文件关闭、延迟释放等
4. **模块线程**：处理特定模块的异步任务

### 1.2 线程协作模型

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Main Thread   │    │   I/O Threads   │    │  Background     │
│                 │    │                 │    │   Threads       │
│  - Event Loop   │◄──►│  - Read/Write   │◄──►│  - AOF Fsync    │
│  - Command Exec │    │  - Protocol     │    │  - Close File   │
│  - Client Mgmt  │    │    Parsing      │    │  - Lazy Free    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## 2. I/O多线程实现

### 2.1 I/O线程结构

```c
// I/O线程结构
typedef struct IOThread {
    int id;                             // 线程ID
    pthread_t thread;                   // 线程句柄
    pthread_mutex_t lock;               // 线程锁
    pthread_cond_t cond;                // 条件变量
    int pending;                        // 待处理任务数
    int running;                        // 运行状态
    list *clients;                      // 客户端列表
    list *pending_clients_to_main_thread; // 待返回主线程的客户端
    eventLoop *el;                      // 事件循环
    int clients_num;                     // 客户端数量
    int max_processing_time;             // 最大处理时间
} IOThread;

// 全局I/O线程数组
static IOThread IOThreads[IO_THREADS_MAX_NUM];
```

### 2.2 I/O线程初始化

```c
// 初始化I/O线程
void initIOThreads(int num) {
    // 验证线程数量
    if (num < 1 || num > IO_THREADS_MAX_NUM) {
        serverLog(LL_WARNING, "Invalid number of I/O threads: %d", num);
        return;
    }

    server.io_threads_num = num;

    // 创建主线程相关数据结构
    for (int i = 0; i < num; i++) {
        // 初始化待处理客户端列表
        mainThreadPendingClientsToIOThreads[i] = listCreate();
        mainThreadProcessingClients[i] = listCreate();
        mainThreadPendingClients[i] = listCreate();

        // 初始化互斥锁和通知器
        pthread_mutex_init(&mainThreadPendingClientsMutexes[i], NULL);
        mainThreadPendingClientsNotifiers[i] = createEventNotifier();
    }

    // 创建I/O线程
    for (int i = 0; i < num; i++) {
        IOThread *t = &IOThreads[i];
        t->id = i;
        t->pending = 0;
        t->running = 0;
        t->clients = listCreate();
        t->pending_clients_to_main_thread = listCreate();
        t->clients_num = 0;
        t->max_processing_time = 1000; // 1ms

        // 创建事件循环
        t->el = aeCreateEventLoop(1024);
        if (!t->el) {
            serverPanic("Failed to create event loop for I/O thread %d", i);
        }

        // 创建线程
        pthread_mutex_init(&t->lock, NULL);
        pthread_cond_init(&t->cond, NULL);
        if (pthread_create(&t->thread, NULL, IOThreadMain, t) != 0) {
            serverPanic("Failed to create I/O thread %d", i);
        }

        // 设置线程名称
        char thread_name[16];
        snprintf(thread_name, sizeof(thread_name), "redis_io_%d", i);
        pthread_setname_np(t->thread, thread_name);
    }

    serverLog(LL_NOTICE, "Created %d I/O threads", num);
}
```

### 2.3 I/O线程主函数

```c
// I/O线程主函数
void *IOThreadMain(void *arg) {
    IOThread *t = (IOThread *)arg;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 1000000; // 1ms

    // 设置线程本地存储
    io_thread_id = t->id;

    while (1) {
        // 获取线程锁
        pthread_mutex_lock(&t->lock);

        // 检查是否有待处理任务
        while (t->pending == 0) {
            // 等待任务
            pthread_cond_timedwait(&t->cond, &t->lock, &timeout);
        }

        // 设置运行状态
        t->running = 1;
        atomicIncr(server.io_threads_active, 1);

        // 处理待处理客户端
        if (listLength(mainThreadPendingClientsToIOThreads[t->id]) > 0) {
            processPendingClientsFromMainThread(t);
        }

        // 处理网络事件
        processIOThreadEvents(t);

        // 处理待返回主线程的客户端
        sendPendingClientsToMainThreadIfNeeded(t, 0);

        // 重置状态
        t->pending--;
        t->running = 0;
        atomicDecr(server.io_threads_active, 1);

        // 释放锁
        pthread_mutex_unlock(&t->lock);
    }

    return NULL;
}
```

### 2.4 客户端分配策略

```c
// 将客户端分配给I/O线程
void assignClientToIOThread(client *c) {
    // 计算线程ID（基于客户端ID取模）
    int tid = c->id % server.io_threads_num;

    // 如果已经是I/O线程管理的客户端，直接返回
    if (c->tid != IOTHREAD_MAIN_THREAD_ID) {
        return;
    }

    // 暂停I/O线程
    pauseIOThread(tid);

    // 将客户端添加到I/O线程
    IOThread *t = &IOThreads[tid];
    listAddNodeTail(t->clients, c);
    c->io_thread_client_list_node = listLast(t->clients);
    c->tid = tid;
    c->running_tid = tid;

    // 绑定到I/O线程的事件循环
    connRebindEventLoop(c->conn, t->el);
    connSetReadHandler(c->conn, ioThreadReadQueryFromClient);
    connSetWriteHandler(c->conn, ioThreadSendReplyToClient);

    // 更新统计信息
    server.io_threads_clients_num[tid]++;

    // 恢复I/O线程
    resumeIOThread(tid);
}
```

## 3. 线程间通信

### 3.1 主线程到I/O线程

```c
// 主线程向I/O线程发送客户端
void sendClientToIOThread(client *c) {
    // 验证状态
    if (c->tid != IOTHREAD_MAIN_THREAD_ID) {
        return;
    }

    // 验证I/O线程是否启用
    if (!server.io_threads_num || !server.io_threads_active) {
        return;
    }

    // 从主线程事件循环解绑
    connUnbindEventLoop(c->conn);

    // 添加到I/O线程待处理队列
    int tid = c->id % server.io_threads_num;
    listAddNodeTail(mainThreadPendingClientsToIOThreads[tid], c);
    c->io_thread_client_list_node = listLast(mainThreadPendingClientsToIOThreads[tid]);

    // 通知I/O线程
    pthread_mutex_lock(&IOThreads[tid].lock);
    IOThreads[tid].pending++;
    pthread_cond_signal(&IOThreads[tid].cond);
    pthread_mutex_unlock(&IOThreads[tid].lock);
}
```

### 3.2 I/O线程到主线程

```c
// I/O线程向主线程发送客户端
void enqueuePendingClientsToMainThread(client *c, int unbind) {
    // 如果需要，解绑事件循环
    if (unbind) {
        connUnbindEventLoop(c->conn);
    }

    // 检查是否已经在传输中
    if (c->io_thread_client_list_node) {
        IOThread *t = &IOThreads[c->tid];

        // 通知主线程（如果需要）
        sendPendingClientsToMainThreadIfNeeded(t, 1);

        // 禁用读写以避免竞争
        c->io_flags &= ~(CLIENT_IO_READ_ENABLED | CLIENT_IO_WRITE_ENABLED);

        // 从I/O线程移除，添加到主线程待处理队列
        listUnlinkNode(t->clients, c->io_thread_client_list_node);
        listLinkNodeTail(t->pending_clients_to_main_thread, c->io_thread_client_list_node);
        c->io_thread_client_list_node = NULL;
    }
}

// 发送待处理客户端到主线程
static inline void sendPendingClientsToMainThreadIfNeeded(IOThread *t, int check_size) {
    size_t len = listLength(t->pending_clients_to_main_thread);
    if (len == 0 || (check_size && len < IO_THREAD_MAX_PENDING_CLIENTS)) {
        return;
    }

    int running = 0, pending = 0;
    pthread_mutex_lock(&mainThreadPendingClientsMutexes[t->id]);
    pending = listLength(mainThreadPendingClients[t->id]);
    listJoin(mainThreadPendingClients[t->id], t->pending_clients_to_main_thread);
    pthread_mutex_unlock(&mainThreadPendingClientsMutexes[t->id]);

    if (!pending) {
        atomicGetWithSync(server.running, running);
    }

    // 只有在主线程未运行且没有待处理客户端时才通知
    if (!running && !pending) {
        triggerEventNotifier(mainThreadPendingClientsNotifiers[t->id]);
    }
}
```

## 4. 后台任务线程

### 4.1 BIO线程结构

```c
// 后台I/O线程结构
typedef struct bio_job {
    int type;           // 任务类型
    time_t created_time; // 创建时间
    void *arg1, *arg2, *arg3; // 任务参数
    comp_fn *comp_func; // 完成回调函数
    void *comp_user_data; // 完成回调数据
} bio_job;

// BIO线程状态
typedef struct bio_worker {
    pthread_t thread;        // 线程句柄
    list *job_queue;         // 任务队列
    pthread_mutex_t lock;    // 队列锁
    pthread_cond_t cond;     // 条件变量
    int pending;             // 待处理任务数
    unsigned long processed; // 已处理任务数
} bio_worker;

// 全局BIO工作线程
static bio_worker bio_workers[BIO_WORKER_NUM];
```

### 4.2 BIO任务类型

```c
typedef enum bio_job_type_t {
    BIO_CLOSE_FILE = 0,          // 延迟关闭文件
    BIO_AOF_FSYNC,               // 延迟AOF同步
    BIO_LAZY_FREE,               // 延迟释放对象
    BIO_CLOSE_AOF,               // 关闭AOF文件
    BIO_COMP_RQ_CLOSE_FILE,      // 关闭文件完成请求
    BIO_COMP_RQ_AOF_FSYNC,       // AOF同步完成请求
    BIO_COMP_RQ_LAZY_FREE,       // 延迟释放完成请求
    BIO_NUM_OPS
} bio_job_type_t;
```

### 4.3 BIO线程初始化

```c
// 初始化BIO线程
void bioInit(void) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // 创建各种类型的BIO线程
    for (int i = 0; i < BIO_WORKER_NUM; i++) {
        bio_worker *worker = &bio_workers[i];

        // 初始化工作线程
        worker->job_queue = listCreate();
        worker->pending = 0;
        worker->processed = 0;
        pthread_mutex_init(&worker->lock, NULL);
        pthread_cond_init(&worker->cond, NULL);

        // 创建线程
        char thread_name[16];
        switch (i) {
            case BIO_WORKER_CLOSE_FILE:
                snprintf(thread_name, sizeof(thread_name), "redis_bio_close");
                break;
            case BIO_WORKER_AOF_FSYNC:
                snprintf(thread_name, sizeof(thread_name), "redis_bio_aof");
                break;
            case BIO_WORKER_LAZY_FREE:
                snprintf(thread_name, sizeof(thread_name), "redis_bio_lazy");
                break;
        }

        if (pthread_create(&worker->thread, &attr, bioProcessBackgroundJobs, (void*)(long)i) != 0) {
            serverPanic("Failed to create BIO thread %d", i);
        }

        pthread_setname_np(worker->thread, thread_name);
    }

    pthread_attr_destroy(&attr);
}
```

### 4.4 BIO任务处理

```c
// BIO线程主函数
void *bioProcessBackgroundJobs(void *arg) {
    bio_job *job;
    unsigned long processed = 0;
    int type = (int)(long)arg;
    bio_worker *worker = &bio_workers[type];

    while (1) {
        // 获取任务
        pthread_mutex_lock(&worker->lock);
        while (listLength(worker->job_queue) == 0) {
            // 等待任务
            pthread_cond_wait(&worker->cond, &worker->lock);
        }

        // 获取任务
        job = listNodeValue(listFirst(worker->job_queue));
        listDelNode(worker->job_queue, listFirst(worker->job_queue));
        worker->pending--;
        pthread_mutex_unlock(&worker->lock);

        // 处理任务
        switch (job->type) {
            case BIO_CLOSE_FILE:
                // 关闭文件
                close((long)job->arg1);
                if (job->comp_func) {
                    job->comp_func((uint64_t)job->arg1, job->comp_user_data);
                }
                break;

            case BIO_AOF_FSYNC:
                // AOF同步
                aof_fsync((long)job->arg1);
                if (job->comp_func) {
                    job->comp_func((uint64_t)job->arg1, job->comp_user_data);
                }
                break;

            case BIO_LAZY_FREE:
                // 延迟释放
                {
                    lazy_free_fn free_fn = (lazy_free_fn)job->arg1;
                    void *args[] = { job->arg2, job->arg3 };
                    free_fn(args);
                }
                break;

            // 其他任务类型...
        }

        // 释放任务
        zfree(job);
        processed++;
        worker->processed = processed;
    }

    return NULL;
}
```

## 5. 性能优化策略

### 5.1 线程亲和性

```c
// 设置线程亲和性
void setThreadAffinity(pthread_t thread, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
}
```

### 5.2 批量处理

```c
// 批量处理网络事件
void processIOThreadEvents(IOThread *t) {
    int processed = 0;
    int max_events = 16; // 批量处理事件数

    // 处理事件
    while (processed < max_events) {
        int numevents = aeProcessEvents(t->el, AE_FILE_EVENTS);
        if (numevents == 0) break;
        processed += numevents;
    }
}
```

### 5.3 负载均衡

```c
// 动态负载均衡
void balanceIOThreadsLoad(void) {
    if (!server.io_threads_num) return;

    // 计算平均负载
    long long total_clients = 0;
    for (int i = 0; i < server.io_threads_num; i++) {
        total_clients += server.io_threads_clients_num[i];
    }

    long long avg_clients = total_clients / server.io_threads_num;

    // 重新平衡负载
    for (int i = 0; i < server.io_threads_num; i++) {
        if (server.io_threads_clients_num[i] > avg_clients * 1.5) {
            // 从高负载线程迁移客户端到低负载线程
            migrateClientsFromThread(i);
        }
    }
}
```

## 6. 线程安全机制

### 6.1 原子操作

```c
// 原子递增
#define atomicIncr(var,val) __sync_add_and_fetch(&(var), (val))

// 原子递减
#define atomicDecr(var,val) __sync_sub_and_fetch(&(var), (val))

// 原子读取
#define atomicGet(var,val) do { (val) = __sync_fetch_and_add(&(var), 0); } while(0)

// 原子比较交换
#define atomicCas(var,old,new) __sync_bool_compare_and_swap(&(var), (old), (new))
```

### 6.2 内存屏障

```c
// 内存屏障
#define atomicGetWithSync(var,val) do { \
    __sync_synchronize(); \
    (val) = __sync_fetch_and_add(&(var), 0); \
} while(0)

// 写屏障
#define writeBarrier() __sync_synchronize()

// 读屏障
#define readBarrier() __sync_synchronize()
```

### 6.3 锁优化

```c
// 读写锁
typedef struct rwlock_t {
    pthread_mutex_t mutex;
    pthread_cond_t read_cond;
    pthread_cond_t write_cond;
    int readers;
    int writers;
    int waiting_writers;
} rwlock_t;

// 获取读锁
void rwlockReadLock(rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    while (lock->writers || lock->waiting_writers) {
        pthread_cond_wait(&lock->read_cond, &lock->mutex);
    }
    lock->readers++;
    pthread_mutex_unlock(&lock->mutex);
}

// 释放读锁
void rwlockReadUnlock(rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    lock->readers--;
    if (lock->readers == 0 && lock->waiting_writers) {
        pthread_cond_signal(&lock->write_cond);
    }
    pthread_mutex_unlock(&lock->mutex);
}
```

## 7. 错误处理和恢复

### 7.1 线程健康检查

```c
// 线程健康检查
void checkIOThreadsHealth(void) {
    for (int i = 0; i < server.io_threads_num; i++) {
        IOThread *t = &IOThreads[i];

        // 检查线程是否响应
        if (t->running && time(NULL) - t->last_activity > 5) {
            serverLog(LL_WARNING, "I/O thread %d appears to be stuck", i);

            // 尝试恢复
            if (pthread_kill(t->thread, 0) != 0) {
                // 线程已死亡，需要重新创建
                recreateIOThread(i);
            }
        }
    }
}
```

### 7.2 资源清理

```c
// 清理I/O线程资源
void cleanupIOThreads(void) {
    for (int i = 0; i < server.io_threads_num; i++) {
        IOThread *t = &IOThreads[i];

        // 取消线程
        pthread_cancel(t->thread);

        // 等待线程结束
        pthread_join(t->thread, NULL);

        // 清理资源
        pthread_mutex_destroy(&t->lock);
        pthread_cond_destroy(&t->cond);
        listRelease(t->clients);
        listRelease(t->pending_clients_to_main_thread);
        aeDeleteEventLoop(t->el);
    }
}
```

## 8. 监控和统计

### 8.1 线程统计

```c
// 线程统计信息
typedef struct io_thread_stats {
    unsigned long long read_bytes;      // 读取字节数
    unsigned long long write_bytes;     // 写入字节数
    unsigned long long processed_commands; // 处理命令数
    unsigned long long network_errors;  // 网络错误数
    double avg_process_time;            // 平均处理时间
} io_thread_stats;

// 更新统计信息
void updateIOThreadStats(int tid, int bytes_read, int bytes_written, int commands) {
    io_thread_stats *stats = &server.io_threads_stats[tid];

    stats->read_bytes += bytes_read;
    stats->write_bytes += bytes_written;
    stats->processed_commands += commands;
}
```

### 8.2 性能监控

```c
// 监控线程性能
void monitorThreadPerformance(void) {
    static time_t last_check = 0;
    time_t now = time(NULL);

    if (now - last_check < 5) return; // 每5秒检查一次

    for (int i = 0; i < server.io_threads_num; i++) {
        IOThread *t = &IOThreads[i];
        io_thread_stats *stats = &server.io_threads_stats[i];

        // 计算吞吐量
        double throughput = (double)stats->processed_commands / (now - last_check);

        // 记录性能指标
        serverLog(LL_DEBUG, "I/O thread %d: %.2f commands/sec, %llu bytes read, %llu bytes written",
                 i, throughput, stats->read_bytes, stats->write_bytes);
    }

    last_check = now;
}
```

## 9. 单元测试

```c
// 测试I/O线程创建和销毁
void test_io_thread_lifecycle() {
    // 初始化I/O线程
    initIOThreads(4);
    assert(server.io_threads_num == 4);

    // 检查线程状态
    for (int i = 0; i < 4; i++) {
        assert(IOThreads[i].running == 0);
        assert(IOThreads[i].pending == 0);
    }

    // 清理
    cleanupIOThreads();
}

// 测试客户端分配
void test_client_assignment() {
    initIOThreads(2);

    // 创建测试客户端
    client *c = createClient(NULL);
    c->id = 12345;

    // 分配到I/O线程
    assignClientToIOThread(c);

    // 验证分配结果
    int expected_tid = 12345 % 2;
    assert(c->tid == expected_tid);
    assert(c->running_tid == expected_tid);

    // 清理
    freeClient(c);
    cleanupIOThreads();
}

// 测试线程间通信
void test_thread_communication() {
    initIOThreads(2);

    // 创建测试客户端
    client *c = createClient(NULL);
    c->id = 67890;

    // 发送到I/O线程
    sendClientToIOThread(c);

    // 验证客户端在I/O线程中
    int tid = 67890 % 2;
    assert(listLength(mainThreadPendingClientsToIOThreads[tid]) == 1);

    // 清理
    cleanupIOThreads();
}
```

## 10. 总结

Redis的多线程架构体现了以下设计原则：

1. **性能优化**：通过多线程充分利用多核CPU
2. **线程安全**：完善的同步机制和内存屏障
3. **负载均衡**：智能的客户端分配策略
4. **可扩展性**：灵活的线程数量配置
5. **错误恢复**：线程健康检查和自动恢复

这个多线程架构在不牺牲单线程模型简单性的前提下，显著提升了Redis的I/O处理能力，使其能够更好地应对高并发场景。

## 下一步

在下一篇文章中，我们将深入分析Redis的设计模式应用，总结其软件工程实践。