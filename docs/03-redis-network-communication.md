# Redis源码解析（三）：网络通信机制

## 概述

Redis的网络通信机制是其高性能的关键支撑。本文将深入分析Redis的网络通信架构，包括连接管理、协议解析、客户端处理、以及各种优化技巧。

## 1. 网络通信架构概述

### 1.1 整体架构

Redis采用单线程事件驱动模型，通过网络I/O多路复用实现高并发：

```
┌─────────────────┐
│   Event Loop    │
├─────────────────┤
│ File Events     │
│ - Read Events   │
│ - Write Events  │
├─────────────────┤
│ Time Events     │
└─────────────────┘
         │
    ┌────┴────┐
    │ Epoll   │
    │ Kqueue  │
    │ Select  │
    └────┬────┘
         │
┌────────┼────────┐
│ TCP    │ Unix    │
│ Socket │ Socket │
└────────┴────────┘
```

### 1.2 关键组件

- **Connection抽象**：统一连接管理
- **Client结构**：客户端状态管理
- **协议解析**：RESP协议处理
- **I/O缓冲区**：输入输出缓冲管理

## 2. 连接抽象层

### 2.1 Connection结构

```c
// connection.h中的connection结构
typedef struct connection {
    int type;               // 连接类型
    int fd;                 // 文件描述符
    short last_errno;       // 最后错误号
    void *private_data;     // 私有数据

    // 连接状态
    int state;

    // 事件处理器
    ConnectionCallbackFunc read_handler;
    ConnectionCallbackFunc write_handler;

    // 连接操作函数
    ConnectionCallbackFunc conn_handler;
    ConnectionCallbackFunc close_handler;

    // TLS相关
    void *tls_ctx;
    void *ssl;
} connection;
```

### 2.2 连接类型

```c
#define CONN_TYPE_NONE 0
#define CONN_TYPE_SOCKET 1
#define CONN_TYPE_TLS 2
```

### 2.3 连接状态

```c
#define CONN_STATE_NONE 0
#define CONN_STATE_CONNECTING 1
#define CONN_STATE_ACCEPTING 2
#define CONN_STATE_CONNECTED 3
#define CONN_STATE_CLOSED 4
#define CONN_STATE_ERROR 5
```

## 3. 客户端结构

### 3.1 Client核心结构

```c
typedef struct client {
    uint64_t id;            // 客户端唯一ID
    uint64_t flags;         // 客户端标志
    connection *conn;        // 连接对象
    uint8_t tid;            // 绑定线程ID
    uint8_t running_tid;    // 运行线程ID
    uint8_t io_flags;       // I/O标志
    uint8_t read_error;     // 读取错误
    int resp;               // RESP协议版本
    redisDb *db;            // 当前数据库
    robj *name;             // 客户端名称
    robj *lib_name;         // 客户端库名称
    robj *lib_ver;          // 客户端库版本
    sds querybuf;           // 输入缓冲区
    size_t qb_pos;          // 缓冲区读取位置
    size_t querybuf_peak;   // 缓冲区峰值
    int argc;               // 参数数量
    robj **argv;            // 参数数组
    int argv_len;           // 数组长度
    struct redisCommand *cmd, *lastcmd;  // 当前和最后执行的命令
    user *user;             // 用户认证信息
    mstime_t ctime;         // 创建时间
    mstime_t lastinteraction; // 最后交互时间
    mstime_t obuf_soft_limit_reached_time; // 输出缓冲区软限制时间
    size_t buf_peak;        // 输出缓冲区峰值
    int buf_pos;            // 输出缓冲区位置
    list *reply;            // 输出列表
    list *pending_queries;   // 待处理查询列表
    size_t pending_replies; // 待回复数量
    // ... 更多字段
} client;
```

### 3.2 客户端标志

```c
#define CLIENT_SLAVE (1<<0)    // 从客户端
#define CLIENT_MASTER (1<<1)   // 主客户端
#define CLIENT_MONITOR (1<<2)  // 监控客户端
#define CLIENT_MULTI (1<<3)   // 事务模式
#define CLIENT_BLOCKED (1<<4) // 阻塞状态
#define CLIENT_DIRTY_CAS (1<<5) // 事务CAS检查失败
#define CLIENT_CLOSE_AFTER_REPLY (1<<6) // 回复后关闭
#define CLIENT_UNBLOCKED (1<<7) // 已解除阻塞
#define CLIENT_LUA (1<<8)     // Lua脚本客户端
#define CLIENT_ASKING (1<<9)  // ASKING模式
#define CLIENT_CLOSE_ASAP (1<<10) // 尽快关闭
#define CLIENT_UNIX_SOCKET (1<<11) // Unix套接字
#define CLIENT_DIRTY_EXEC (1<<12) // 事务执行失败
#define CLIENT_MASTER_FORCE_REPLY (1<<13) // 强制回复
#define CLIENT_FORCE_AOF (1<<14) // 强制AOF
#define CLIENT_FORCE_REPL (1<<15) // 强制复制
#define CLIENT_PREVENT_AOF_PROP (1<<16) // 防止AOF传播
#define CLIENT_PREVENT_REPL_PROP (1<<17) // 防止复制传播
#define CLIENT_PENDING_COMMAND (1<<18) // 待处理命令
#define CLIENT_REPL_RDBONLY (1<<19) // 仅RDB复制
#define CLIENT_REPLIED (1<<20) // 已回复
#define CLIENT_NO_TOUCH (1<<21) // 不更新LRU
#define CLIENT_NO_EVICT (1<<22) // 不驱逐
#define CLIENT_SCRIPT (1<<23) // 脚本客户端
#define CLIENT_REJECT_RDB_EXP (1<<24) // 拒绝RDB过期
#define CLIENT_PUSHING (1<<25) // 正在推送
#define CLIENT_PENDING_READ (1<<26) // 待读取
#define CLIENT_REPLY_OFF (1<<27) // 回复关闭
#define CLIENT_REPLY_SKIP_NEXT (1<<28) // 跳过下一个回复
#define CLIENT_REPLY_SKIP (1<<29) // 跳过回复
#define CLIENT_LUA_DEBUG (1<<30) // Lua调试
```

## 4. 网络初始化

### 4.1 服务器启动时的网络初始化

```c
// 创建TCP监听套接字
void createSocketClients(void) {
    int j;

    for (j = 0; j < server.ipfd.count; j++) {
        if (aeCreateFileEvent(server.el, server.ipfd.fd[j], AE_READABLE,
                            acceptTcpHandler,NULL) == AE_ERR)
        {
            serverPanic("Unrecoverable error creating server.ipfd file event.");
        }
    }

    // Unix套接字
    if (server.sofd > 0) {
        aeCreateFileEvent(server.el, server.sofd, AE_READABLE,
                         acceptUnixHandler,NULL);
    }

    // TLS监听
    if (server.tls_context) {
        for (j = 0; j < server.tlsfd.count; j++) {
            if (aeCreateFileEvent(server.el, server.tlsfd.fd[j], AE_READABLE,
                                acceptTLSHandler,NULL) == AE_ERR)
            {
                serverPanic("Unrecoverable error creating server.tlsfd file event.");
            }
        }
    }
}
```

### 4.2 TCP连接处理

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
            acceptCommonHandler(c, 0, cip);
        } else {
            serverLog(LL_WARNING,
                "Error creating client: %s", server.neterr);
            close(cfd);
        }
    }
}
```

## 5. 客户端创建和管理

### 5.1 客户端创建

```c
client *createClient(connection *conn) {
    client *c = zmalloc(sizeof(client));

    // 清零客户端结构
    memset(c, 0, sizeof(client));

    // 设置连接
    if (conn) {
        connSetPrivateData(conn, c);
        connSetReadHandler(conn, readQueryFromClient);
        connSetWriteHandler(conn, sendReplyToClient);
        c->conn = conn;
        c->conn_type = conn->type;
    } else {
        // 无连接客户端（用于Lua等）
        c->conn = NULL;
    }

    // 初始化客户端状态
    c->id = server.client_next_id++;
    c->resp = 2;  // 默认RESP 2
    c->fd = conn ? conn->fd : -1;
    c->name = NULL;
    c->bufpos = 0;
    c->buf_peak = 0;
    c->querybuf = sdsempty();
    c->querybuf_peak = 0;
    c->qb_pos = 0;
    c->argc = 0;
    c->argv = NULL;
    c->argv_len = 0;
    c->cmd = c->lastcmd = NULL;
    c->user = NULL;
    c->flags = 0;
    c->ctime = c->lastinteraction = server.unixtime;
    c->obuf_soft_limit_reached_time = 0;
    c->authenticated = 0;
    c->replstate = REPL_STATE_NONE;
    c->repl_start_cmd_stream_on_ack = 0;
    c->reploff = 0;
    c->read_reploff = 0;
    c->repl_applied = 0;
    c->repl_put_online_on_ack = 0;
    c->repl_last_partial_write = 0;
    c->repl_last_io = server.unixtime;
    c->slave_listening_port = 0;
    c->slave_addr = NULL;
    c->slave_capa = SLAVE_CAPA_NONE;
    c->slave_req = SLAVE_REQ_NONE;
    c->reply = listCreate();
    c->reply_bytes = 0;
    c->obuf_soft_limit_reached_time = 0;
    c->buf_peak = 0;
    c->buf_pos = 0;
    c->pending_queries = listCreate();
    c->pending_replies = 0;
    c->tid = IOTHREAD_MAIN_THREAD_ID;
    c->running_tid = IOTHREAD_MAIN_THREAD_ID;
    c->io_flags = 0;
    c->read_error = 0;
    c->bstate.btype = BLOCKED_NONE;
    c->bstate.timeout = 0;
    c->bstate.blocked_by = NULL;
    c->woff = 0;
    c->watched_keys = listCreate();
    c->pubsub_channels = dictCreate(&objectKeyPointerValueDictType);
    c->pubsub_patterns = listCreate();
    c->peerid = NULL;
    c->sockname = NULL;
    c->client_list_node = NULL;
    c->postponed_list_node = NULL;
    c->pending_read_list_node = NULL;
    c->clients_pending_write_node = NULL;
    c->mem_usage_bucket = NULL;
    c->mem_usage = 0;
    c->last_memory_usage = 0;
    c->last_memory_usage_sample_time = 0;
    c->ref_repl_buf_node = NULL;
    c->ref_block_pos = 0;
    c->ref_block_len = 0;
    c->ref_repl_buf_node = NULL;
    c->ref_block_pos = 0;
    c->ref_block_len = 0;
    c->deferred_objects = NULL;
    c->deferred_objects_num = 0;

    // 添加到客户端列表
    if (conn) {
        linkClient(c);
    }

    return c;
}
```

### 5.2 客户端销毁

```c
void freeClient(client *c) {
    // 如果是从客户端，更新主从复制状态
    if (c->flags & CLIENT_SLAVE) {
        if (c->replstate == SLAVE_STATE_WAIT_BGSAVE_START ||
            c->replstate == SLAVE_STATE_WAIT_BGSAVE_END) {
            serverLog(LL_WARNING,"Connection with replica %s lost.",
                replicationGetSlaveName(c));
        }
        listNode *ln;
        listIter li;
        listRewind(server.slaves,&li);
        while ((ln = listNext(&li)) != NULL) {
            if (ln->value == c) {
                listDelNode(server.slaves,ln);
                break;
            }
        }
    }

    // 如果是主客户端，更新复制状态
    if (c->flags & CLIENT_MASTER) {
        serverLog(LL_WARNING,"Connection with master lost.");
        if (server.repl_state == REPL_STATE_CONNECTED) {
            freeReplicationBacklog();
            server.repl_state = REPL_STATE_CONNECT;
        }
    }

    // 移除事件处理器
    if (c->conn) {
        connSetReadHandler(c->conn, NULL);
        connSetWriteHandler(c->conn, NULL);
    }

    // 清理订阅信息
    pubsubUnsubscribeAllChannels(c,0);
    pubsubUnsubscribeAllPatterns(c,0);
    if (c->flags & CLIENT_PUBSUB) {
        dictRelease(c->pubsub_channels);
        listRelease(c->pubsub_patterns);
    }

    // 清理监视的键
    unwatchAllKeys(c);
    listRelease(c->watched_keys);

    // 清理阻塞状态
    unblockClient(c);

    // 清理输出缓冲区
    listRelease(c->reply);
    sdsfree(c->buf);
    sdsfree(c->querybuf);
    sdsfree(c->pending_querybuf);

    // 清理参数
    if (c->argv) {
        for (int j = 0; j < c->argc; j++)
            decrRefCount(c->argv[j]);
        zfree(c->argv);
    }

    // 清理延迟对象
    if (c->deferred_objects) {
        for (int j = 0; j < c->deferred_objects_num; j++)
            decrRefCount(c->deferred_objects[j]);
        zfree(c->deferred_objects);
    }

    // 清理其他资源
    decrRefCount(c->name);
    decrRefCount(c->lib_name);
    decrRefCount(c->lib_ver);
    sdsfree(c->peerid);
    sdsfree(c->sockname);
    sdsfree(c->slave_addr);

    // 移除从客户端列表
    if (c->client_list_node) {
        listDelNode(server.clients,c->client_list_node);
    }

    // 释放客户端结构
    zfree(c);
}
```

## 6. 协议解析

### 6.1 RESP协议概述

Redis使用RESP（Redis Serialization Protocol）协议：

- **简单字符串**："+OK\r\n"
- **错误**："-Error message\r\n"
- **整数**：":1000\r\n"
- **批量字符串**："$6\r\nfoobar\r\n"
- **数组**："*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"

### 6.2 输入缓冲区处理

```c
void readQueryFromClient(connection *conn) {
    client *c = connGetPrivateData(conn);
    int nread, readlen;
    size_t qblen;

    // 读取客户端数据
    readlen = PROTO_IOBUF_LEN;
    qblen = sdslen(c->querybuf);
    if (c->querybuf_peak < qblen) c->querybuf_peak = qblen;
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    nread = connRead(c->conn, c->querybuf+qblen, readlen);

    if (nread == -1) {
        if (connGetState(conn) == CONN_STATE_CONNECTED) {
            return;
        } else {
            serverLog(LL_VERBOSE, "Reading from client: %s",connGetLastError(conn));
            freeClient(c);
            return;
        }
    } else if (nread == 0) {
        // 客户端关闭连接
        serverLog(LL_VERBOSE, "Client closed connection");
        freeClient(c);
        return;
    }

    // 更新缓冲区长度
    sdsIncrLen(c->querybuf,nread);
    c->lastinteraction = server.unixtime;
    if (c->flags & CLIENT_MASTER) c->read_reploff += nread;

    // 处理输入缓冲区
    if (c->flags & CLIENT_PENDING_READ) {
        c->flags |= CLIENT_PENDING_COMMAND;
    } else {
        processInputBuffer(c);
    }
}
```

### 6.3 协议解析核心

```c
int processInputBuffer(client *c) {
    /* Keep processing while there is something in the input buffer */
    while(c->qb_pos < sdslen(c->querybuf)) {
        /* Immediately abort if the client is in the middle of something. */
        if (c->flags & CLIENT_BLOCKED) break;

        /* Don't process more buffers from clients that have already pending
         * commands to execute in c->argv. */
        if (c->flags & CLIENT_PENDING_COMMAND) break;

        /* Don't process input from the master while there is a busy script
         * condition on the slave. We want just to accumulate the replication
         * stream (instead of replying -BUSY like we do with other clients) and
         * later resume the processing. */
        if (c->flags & CLIENT_MASTER && isInsideYieldingLongCommand()) break;

        /* CLIENT_CLOSE_AFTER_REPLY closes the connection once the reply is
         * written to the client. Make sure to not let the reply grow after
         * this flag has been set (i.e. don't process more commands).
         *
         * The same applies for clients we want to terminate ASAP. */
        if (c->flags & (CLIENT_CLOSE_AFTER_REPLY|CLIENT_CLOSE_ASAP)) break;

        /* Determine request type when unknown. */
        if (!c->reqtype) {
            if (c->querybuf[c->qb_pos] == '*') {
                c->reqtype = PROTO_REQ_MULTIBULK;
            } else {
                c->reqtype = PROTO_REQ_INLINE;
            }
        }

        if (c->reqtype == PROTO_REQ_INLINE) {
            if (processInlineBuffer(c) != C_OK) break;
        } else if (c->reqtype == PROTO_REQ_MULTIBULK) {
            if (processMultibulkBuffer(c) != C_OK) break;
        } else {
            serverPanic("Unknown request type");
        }

        /* Multibulk processing could have a side effect of calling the C function,
         * so we need to make sure we don't call it again with the same
         * arguments and state. */
        if (c->flags & CLIENT_PENDING_COMMAND) break;
    }

    /* Trim to pos */
    if (c->qb_pos) {
        sdsrange(c->querybuf,c->qb_pos,-1);
        c->qb_pos = 0;
    }

    return C_OK;
}
```

### 6.4 多批量请求处理

```c
int processMultibulkBuffer(client *c) {
    char *newline = NULL;
    int ok;
    long long ll;
    size_t querybuf_len = sdslen(c->querybuf);

    if (c->multibulklen == 0) {
        /* The client should have been reset */
        serverAssertWithInfo(c,NULL,c->argc == 0);

        /* Multi bulk length cannot be read without a \r\n */
        newline = strchr(c->querybuf+c->qb_pos,'\r');
        if (newline == NULL) {
            if (querybuf_len-c->qb_pos > PROTO_INLINE_MAX_SIZE) {
                c->read_error = CLIENT_READ_TOO_BIG_MBULK_COUNT_STRING;
            }
            return C_ERR;
        }

        /* Buffer should also contain \n */
        if (newline-(c->querybuf+c->qb_pos) > (ssize_t)(querybuf_len-c->qb_pos-2))
            return C_ERR;

        /* Parse the bulk count. */
        if (string2ll(c->querybuf+c->qb_pos,newline-(c->querybuf+c->qb_pos),&ll) == C_ERR) {
            c->read_error = CLIENT_READ_INVALID_MBULK_COUNT;
            return C_ERR;
        }

        c->qb_pos = newline-c->querybuf+2; /* Skip \r\n */

        if (ll <= 0) {
            /* Fast path: the client sent an empty array. */
            resetClient(c);
            return C_OK;
        }

        c->multibulklen = ll;

        /* Setup argv array on the client. */
        if (c->argv_len < c->multibulklen) {
            c->argv = zrealloc(c->argv,sizeof(robj*)*c->multibulklen);
            c->argv_len = c->multibulklen;
        }
    }

    while(c->multibulklen) {
        /* Read bulk length if we don't have it */
        if (c->bulklen == -1) {
            newline = strchr(c->querybuf+c->qb_pos,'\r');
            if (newline == NULL) {
                if (querybuf_len-c->qb_pos > PROTO_INLINE_MAX_SIZE) {
                    c->read_error = CLIENT_READ_TOO_BIG_BULK_COUNT_STRING;
                }
                break;
            }

            /* Buffer should also contain \n */
            if (newline-(c->querybuf+c->qb_pos) > (ssize_t)(querybuf_len-c->qb_pos-2))
                break;

            if (string2ll(c->querybuf+c->qb_pos,newline-(c->querybuf+c->qb_pos),&ll) == C_ERR) {
                c->read_error = CLIENT_READ_INVALID_BULK_COUNT;
                return C_ERR;
            }

            c->qb_pos = newline-c->querybuf+2; /* Skip \r\n */
            if (ll < 0) {
                c->read_error = CLIENT_READ_INVALID_BULK_COUNT;
                return C_ERR;
            }
            c->bulklen = ll;
        }

        /* Read bulk argument */
        if (querybuf_len-c->qb_pos < (size_t)(c->bulklen+2)) {
            /* Not enough data (+2 == \r\n) */
            break;
        }

        /* Create the string object. */
        if (c->argv[c->argc] == NULL) {
            c->argv[c->argc] = createStringObject(c->querybuf+c->qb_pos,c->bulklen);
        } else {
            /* Reuse existing string object */
            robj *o = c->argv[c->argc];
            o->ptr = sdscpylen(o->ptr,c->querybuf+c->qb_pos,c->bulklen);
        }

        c->qb_pos += c->bulklen+2; /* Skip \r\n */
        c->argc++;
        c->multibulklen--;
        c->bulklen = -1;
    }

    /* We're done when c->multibulklen is zero */
    if (c->multibulklen == 0) {
        if (c->argc == 0) {
            /* Empty command, reset the client. */
            resetClient(c);
        } else {
            /* We have a complete command, execute it. */
            if (processCommand(c) == C_OK) {
                resetClient(c);
            } else {
                /* Command execution failed, client is freed. */
                return C_ERR;
            }
        }
    }

    return C_OK;
}
```

## 7. 命令执行

### 7.1 命令查找和执行

```c
int processCommand(client *c) {
    // 查找命令
    c->cmd = c->lastcmd = lookupCommand(c->argv[0]->ptr,sdslen(c->argv[0]->ptr));

    // 命令不存在
    if (!c->cmd) {
        flagTransaction(c);
        addReplyErrorFormat(c,"unknown command '%s'",
            (char*)c->argv[0]->ptr);
        return C_OK;
    }

    // 检查权限
    if (c->cmd->flags & CMD_MODULE && !moduleAllCommandsLoaded()) {
        addReplyError(c,"Module command not yet loaded");
        return C_OK;
    }

    // 检查认证
    if (server.requirepass && !c->authenticated && c->cmd->proc != authCommand) {
        flagTransaction(c);
        addReply(c,shared.noautherr);
        return C_OK;
    }

    // 检查集群模式
    if (server.cluster_enabled && !(c->flags & CLIENT_MASTER) &&
        !(c->cmd->proc == infoCommand || c->cmd->proc == slaveofCommand ||
          c->cmd->proc == clusterCommand || c->cmd->proc == shutdownCommand ||
          c->cmd->proc == commandCommand || c->cmd->proc == authCommand))
    {
        int hashslot;
        int ask;

        // 检查命令是否可以在这个节点执行
        if (getNodeByQuery(c,c->cmd,c->argc,c->argv,&hashslot,&ask) != NULL) {
            if (ask) {
                // 重定向到ASK节点
                addReplySds(c,sdscatprintf(sdsempty(),
                    "-ASK %d %s\r\n",hashslot,
                    server.cluster->myself->slaveof->name));
            } else {
                // 重定向到MOVED节点
                addReplySds(c,sdscatprintf(sdsempty(),
                    "-MOVED %d %s\r\n",hashslot,
                    server.cluster->myself->slaveof->name));
            }
            return C_OK;
        }
    }

    // 检查内存
    if (server.maxmemory && !c->cmd->flags & CMD_DENYOOM &&
        (server.maxmemory_policy == MAXMEMORY_VOLATILE_LRU ||
         server.maxmemory_policy == MAXMEMORY_ALLKEYS_LRU ||
         server.maxmemory_policy == MAXMEMORY_VOLATILE_RANDOM ||
         server.maxmemory_policy == MAXMEMORY_ALLKEYS_RANDOM))
    {
        // 尝试释放内存
        freeMemoryIfNeeded();
    }

    // 检查是否在事务中
    if (c->flags & CLIENT_MULTI) {
        // 事务中的命令
        queueMultiCommand(c);
        addReply(c,shared.queued);
        return C_OK;
    }

    // 执行命令
    call(c,CMD_CALL_FULL);

    return C_OK;
}
```

### 7.2 命令调用

```c
void call(client *c, int flags) {
    long long dirty;
    int client_old_flags = c->flags;
    struct redisCommand *realcmd = c->cmd;

    // 设置执行标志
    server.fixed_time_expire++;
    c->flags &= ~(CLIENT_FORCE_AOF|CLIENT_FORCE_REPL|CLIENT_PREVENT_REPL_PROP);
    c->flags |= flags & (CLIENT_FORCE_AOF|CLIENT_FORCE_REPL|CLIENT_PREVENT_REPL_PROP);

    // 记录脏数据数量
    dirty = server.dirty;

    // 执行命令
    c->cmd->proc(c);

    // 更新统计信息
    server.fixed_time_expire--;
    server.stat_numcommands++;
    c->cmd->calls++;
    c->cmd->microseconds += duration;

    // 检查是否需要传播
    if (dirty < server.dirty) {
        int propagation_flags = flags & (CMD_PROPAGATE_AOF|CMD_PROPAGATE_REPL);
        if (propagation_flags) {
            propagate(realcmd,c->db->id,c->argv,c->argc,propagation_flags);
        }
    }

    // 清理
    c->flags = client_old_flags;
}
```

## 8. 输出处理

### 8.1 回复生成

```c
void addReply(client *c, robj *obj) {
    if (prepareClientToWrite(c) != C_OK) return;

    // 尝试使用静态缓冲区
    if (c->bufpos + sdslen(obj->ptr) < PROTO_REPLY_CHUNK_BYTES) {
        memcpy(c->buf+c->bufpos,obj->ptr,sdslen(obj->ptr));
        c->bufpos += sdslen(obj->ptr);
    } else {
        // 使用动态缓冲区
        listAddNodeTail(c->reply,obj);
        c->reply_bytes += sdslen(obj->ptr);
        incrRefCount(obj);
    }

    // 设置写事件处理器
    if (!(c->flags & CLIENT_PENDING_WRITE)) {
        c->flags |= CLIENT_PENDING_WRITE;
        aeCreateFileEvent(server.el, c->fd, AE_WRITABLE, sendReplyToClient, c);
    }
}
```

### 8.2 发送回复

```c
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    client *c = (client*) privdata;
    ssize_t nwritten = 0;
    size_t totwritten = 0;

    // 先发送静态缓冲区
    if (c->bufpos > 0) {
        nwritten = write(fd,c->buf,c->bufpos);
        if (nwritten <= 0) {
            // 处理错误
            if (nwritten == -1 && errno != EAGAIN) {
                freeClient(c);
            }
            return;
        }
        c->bufpos -= nwritten;
        c->buf = c->buf+nwritten;
        totwritten += nwritten;
    }

    // 发送动态缓冲区
    while (listLength(c->reply)) {
        listNode *ln = listFirst(c->reply);
        robj *o = ln->value;
        size_t len = sdslen(o->ptr);
        size_t towrite = len;

        if (c->bufpos > 0) {
            // 还有静态缓冲区数据
            if (towrite > PROTO_REPLY_CHUNK_BYTES-c->bufpos) {
                towrite = PROTO_REPLY_CHUNK_BYTES-c->bufpos;
            }
            memcpy(c->buf+c->bufpos,o->ptr,towrite);
            nwritten = write(fd,c->buf,c->bufpos+towrite);
            if (nwritten <= 0) break;
            c->bufpos = 0;
            if (nwritten > towrite) {
                // 发送了部分数据
                o->ptr = (char*)o->ptr+(nwritten-towrite);
                o->ptr = sdscatlen(o->ptr,c->buf+towrite,nwritten-towrite);
                len = sdslen(o->ptr);
            } else {
                // 全部发送完成
                listDelNode(c->reply,ln);
                decrRefCount(o);
                totwritten += nwritten;
                continue;
            }
        } else {
            // 直接发送
            nwritten = write(fd,o->ptr,len);
            if (nwritten <= 0) break;
            totwritten += nwritten;
            if (nwritten < len) {
                // 部分发送
                o->ptr = (char*)o->ptr+nwritten;
                break;
            } else {
                // 全部发送完成
                listDelNode(c->reply,ln);
                decrRefCount(o);
            }
        }
    }

    // 检查是否发送完成
    if (totwritten > 0) {
        c->lastinteraction = server.unixtime;
        if (c->flags & CLIENT_MASTER) c->reploff += totwritten;
    }

    // 更新状态
    if (listLength(c->reply) == 0 && c->bufpos == 0) {
        c->flags &= ~CLIENT_PENDING_WRITE;
        aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
    }

    // 检查是否需要关闭
    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) {
        freeClient(c);
    }
}
```

## 9. 网络优化技巧

### 9.1 TCP优化

```c
// TCP_NODELAY禁用Nagle算法
anetEnableTcpNoDelay(NULL, fd);

// TCP keepalive
anetKeepAlive(NULL, fd, server.tcpkeepalive);

// 设置发送和接收超时
anetSendTimeout(NULL, fd, server.tcp_timeout);
anetRecvTimeout(NULL, fd, server.tcp_timeout);
```

### 9.2 内存优化

```c
// 静态缓冲区
char buf[PROTO_REPLY_CHUNK_BYTES];

// 动态缓冲区列表
list *reply;

// 缓冲区复用
void resetReusableQueryBuf(client *c) {
    if (c->querybuf && sdslen(c->querybuf) > PROTO_QUERYBUF_MAX_SAFESIZE) {
        sdsfree(c->querybuf);
        c->querybuf = sdsempty();
    }
    c->qb_pos = 0;
}
```

### 9.3 I/O多路复用优化

```c
// 批量处理事件
int aeProcessEvents(aeEventLoop *eventLoop, int flags) {
    // ...
    for (int j = 0; j < numevents; j++) {
        // 批量处理多个事件
    }
    // ...
}
```

## 10. 错误处理

### 10.1 协议错误处理

```c
void handleClientReadError(client *c) {
    switch (c->read_error) {
        case CLIENT_READ_TOO_BIG_INLINE_REQUEST:
            addReplyError(c,"Protocol error: too big inline request");
            setProtocolError("too big inline request",c);
            break;
        case CLIENT_READ_UNBALANCED_QUOTES:
            addReplyError(c,"Protocol error: unbalanced quotes in request");
            setProtocolError("unbalanced quotes in request",c);
            break;
        case CLIENT_READ_TOO_BIG_MBULK_COUNT_STRING:
            addReplyError(c,"Protocol error: too big mbulk count string");
            setProtocolError("too big mbulk count string",c);
            break;
        // ... 其他错误处理
    }
}
```

### 10.2 网络错误处理

```c
void handleNetworkError(client *c) {
    if (c->flags & CLIENT_MASTER) {
        // 主客户端错误处理
        serverLog(LL_WARNING,"Connection with master lost.");
        if (server.repl_state == REPL_STATE_CONNECTED) {
            freeReplicationBacklog();
            server.repl_state = REPL_STATE_CONNECT;
        }
    }

    // 清理资源
    freeClient(c);
}
```

## 11. 单元测试

```c
// 测试客户端创建和销毁
void test_client_lifecycle() {
    client *c = createClient(NULL);
    assert(c != NULL);
    assert(c->id > 0);
    assert(c->querybuf != NULL);
    freeClient(c);
}

// 测试协议解析
void test_protocol_parsing() {
    client *c = createClient(NULL);

    // 测试简单命令
    c->querybuf = sdsnew("PING\r\n");
    assert(processInputBuffer(c) == C_OK);
    assert(c->argc == 1);
    assert(strcmp(c->argv[0]->ptr, "PING") == 0);

    freeClient(c);
}

// 测试多批量命令
void test_multibulk_parsing() {
    client *c = createClient(NULL);

    // 测试多批量命令
    c->querybuf = sdsnew("*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n");
    assert(processInputBuffer(c) == C_OK);
    assert(c->argc == 3);
    assert(strcmp(c->argv[0]->ptr, "SET") == 0);
    assert(strcmp(c->argv[1]->ptr, "key") == 0);
    assert(strcmp(c->argv[2]->ptr, "value") == 0);

    freeClient(c);
}
```

## 12. 总结

Redis的网络通信机制体现了以下设计原则：

1. **高性能**：通过事件驱动和非阻塞I/O实现高并发
2. **可扩展性**：支持多种连接类型（TCP、Unix、TLS）
3. **健壮性**：完善的错误处理和资源管理
4. **协议兼容**：完整的RESP协议实现
5. **内存效率**：智能的缓冲区管理

这个网络通信架构是Redis能够处理百万级并发连接的基础，对于设计和实现高性能网络服务器具有重要的参考价值。

## 下一步

在下一篇文章中，我们将深入分析Redis的持久化机制，包括RDB和AOF的实现原理。