# Redis源码解析（一）：核心数据结构

## 概述

Redis是一个高性能的内存数据库，其核心数据结构的设计是其高性能的关键。本文将深入分析Redis的核心数据结构，包括`redisObject`、字符串(SDS)、字典、压缩列表等。

## 1. redisObject - Redis对象的基石

### 1.1 结构定义

```c
struct redisObject {
    unsigned type:4;                // 对象类型
    unsigned encoding:4;            // 编码方式
    unsigned lru:LRU_BITS;          // LRU时间或LFU数据
    unsigned iskvobj : 1;           // 是否为键值对对象
    unsigned expirable : 1;         // 是否有过期时间
    unsigned refcount : OBJ_REFCOUNT_BITS;  // 引用计数
    void *ptr;                      // 指向实际数据的指针
};
```

### 1.2 字段解析

**type（4位）**：表示对象的类型，包括：
- `OBJ_STRING`：字符串
- `OBJ_LIST`：列表
- `OBJ_SET`：集合
- `OBJ_ZSET`：有序集合
- `OBJ_HASH`：哈希表
- `OBJ_MODULE`：模块类型
- `OBJ_STREAM`：流类型

**encoding（4位）**：表示对象的编码方式，Redis根据数据的特点自动选择最优编码：
- `OBJ_ENCODING_RAW`：原始字符串
- `OBJ_ENCODING_INT`：整数编码
- `OBJ_ENCODING_HT`：哈希表
- `OBJ_ENCODING_ZIPLIST`：压缩列表
- `OBJ_ENCODING_INTSET`：整数集合
- `OBJ_ENCODING_SKIPLIST`：跳表
- `OBJ_ENCODING_EMBSTR`：嵌入字符串
- `OBJ_ENCODING_QUICKLIST`：快速列表

**lru（24位）**：用于LRU（最近最少使用）淘汰策略或LFU（最不经常使用）淘汰策略。

**iskvobj（1位）**：标识是否为键值对对象，这是Redis 7.0引入的新特性。

**expirable（1位）**：标识对象是否有过期时间。

**refcount**：引用计数，用于内存管理。

**ptr**：指向实际数据的指针。

### 1.3 设计亮点

1. **位域优化**：使用位域（bit field）技术，将多个标志位压缩在一个int中，节省内存。
2. **多态设计**：通过type和encoding字段，同一个结构可以表示不同类型的数据。
3. **内存效率**：根据数据特点自动选择最优编码方式。

## 2. SDS - 动态字符串

### 2.1 传统C字符串的问题

传统C字符串有以下问题：
- 获取长度需要遍历，时间复杂度O(n)
- 缓冲区溢出风险
- 内存重新分配时可能需要大量拷贝

### 2.2 SDS结构

```c
/* 注意：实际SDS有5种头部结构，根据字符串长度选择 */
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags;  /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};

struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len;           /* used */
    uint8_t alloc;         /* excluding the header and null terminator */
    unsigned char flags;    /* 3 lsb of type, 5 unused bits */
    char buf[];
};

struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len;          /* used */
    uint16_t alloc;        /* excluding the header and null terminator */
    unsigned char flags;    /* 3 lsb of type, 5 unused bits */
    char buf[];
};

struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len;          /* used */
    uint32_t alloc;        /* excluding the header and null terminator */
    unsigned char flags;    /* 3 lsb of type, 5 unused bits */
    char buf[];
};

struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len;          /* used */
    uint64_t alloc;        /* excluding the header and null terminator */
    unsigned char flags;    /* 3 lsb of type, 5 unused bits */
    char buf[];
};
```

### 2.3 SDS的优势

1. **O(1)时间复杂度获取长度**：直接读取len字段
2. **避免缓冲区溢出**：检查alloc字段
3. **减少内存重新分配**：预分配策略
4. **二进制安全**：可以存储任意二进制数据
5. **兼容C字符串**：以'\0'结尾

### 2.4 内存优化

根据字符串长度选择不同的头部结构：
- `sdshdr5`：长度 < 1 << 5 (32)
- `sdshdr8`：长度 < 1 << 8 (256)
- `sdshdr16`：长度 < 1 << 16 (65536)
- `sdshdr32`：长度 < 1 << 32 (4294967296)
- `sdshdr64`：长度 >= 1 << 32

## 3. 字典（Dict）- 核心哈希表实现

### 3.1 字典结构

```c
typedef struct dict {
    dictType *type;            // 字典类型，包含回调函数
    dictEntry **ht_table[2];    // 哈希表数组，用于rehash
    unsigned long ht_used[2];  // 已使用节点数
    long rehashidx;            // rehash进度，-1表示未rehash
    int16_t pauserehash;       // 暂停rehash的计数器
} dict;

typedef struct dictEntry {
    void *key;                 // 键
    union {
        void *val;            // 值
        uint64_t u64;         // uint64_t值
        int64_t s64;          // int64_t值
        double d;             // double值
    } v;
    struct dictEntry *next;    // 下一个节点，用于解决哈希冲突
} dictEntry;
```

### 3.2 渐进式rehash

Redis采用渐进式rehash，避免一次性rehash导致的性能问题：

1. **rehash过程**：
   - 分配新的哈希表
   - 将ht_table[0]的数据迁移到ht_table[1]
   - 完成后交换两个哈希表

2. **渐进式迁移**：
   - 每次执行字典操作时迁移一部分数据
   - 在定时任务中完成剩余迁移

### 3.3 哈希冲突解决

使用链地址法解决哈希冲突，每个哈希桶都是一个链表。

## 4. 压缩列表（ziplist）- 紧凑存储

### 4.1 ziplist结构

```c
<zlbytes> <zltail> <zllen> <entry> <entry> ... <entry> <zlend>
```

- **zlbytes**：ziplist总字节数
- **zltail**：最后一个元素的偏移量
- **zllen**：元素数量
- **entry**：元素列表
- **zlend**：结束标志(0xFF)

### 4.2 entry结构

```
<prevlen> <encoding> <data>
```

- **prevlen**：前一个元素的长度
- **encoding**：编码方式和数据长度
- **data**：实际数据

### 4.3 级联更新问题

当中间元素长度变化导致prevlen字段需要扩展时，可能引发级联更新，但这种情况在实际使用中很少发生。

## 5. 快速列表（quicklist）- 列表优化

### 5.1 quicklist结构

```c
typedef struct quicklist {
    quicklistNode *head;      // 头节点
    quicklistNode *tail;      // 尾节点
    unsigned long len;        // 元素总数
    signed long fill;         // 填充因子
    unsigned int compress;    // 压缩深度
} quicklist;

typedef struct quicklistNode {
    struct quicklistNode *prev; // 前一个节点
    struct quicklistNode *next; // 后一个节点
    unsigned char *zl;         // 指向ziplist
    unsigned int sz;          // ziplist大小
    unsigned int count : 16;  // ziplist中的元素数量
    unsigned int encoding : 2; // 编码方式
    unsigned int container : 2; // 容器类型
    unsigned int recompress : 1; // 是否需要重新压缩
    unsigned int attempted_compress : 1; // 是否尝试压缩
    unsigned int extra : 10;  // 预留字段
} quicklistNode;
```

### 5.2 设计优势

1. **内存效率**：每个节点是一个ziplist，节省内存
2. **平衡性能**：在内存使用和访问性能之间找到平衡
3. **压缩支持**：可以对节点进行LZF压缩

## 6. 整数集合（intset）- 整数优化

### 6.1 intset结构

```c
typedef struct intset {
    uint32_t encoding;  // 编码方式
    uint32_t length;    // 元素数量
    int8_t contents[];  // 元素数组
} intset;
```

### 6.2 编码方式

- `INTSET_ENC_INT16`：16位整数
- `INTSET_ENC_INT32`：32位整数
- `INTSET_ENC_INT64`：64位整数

### 6.3 升级机制

当添加的整数超过当前编码范围时，intset会自动升级到更大的编码方式。

## 7. 跳表（skiplist）- 有序集合实现

### 7.1 跳表结构

```c
typedef struct zskiplistNode {
    sds ele;              // 元素
    double score;         // 分数
    struct zskiplistNode *backward; // 后退指针
    struct zskiplistLevel {
        struct zskiplistNode *forward; // 前进指针
        unsigned long span;            // 跨度
    } level[];            // 层级数组
} zskiplistNode;

typedef struct zskiplist {
    struct zskiplistNode *header, *tail; // 头尾节点
    unsigned long length;               // 节点数量
    int level;                          // 最大层数
} zskiplist;
```

### 7.2 跳表特点

1. **多层结构**：通过多层索引加速查找
2. **概率平衡**：通过随机概率决定节点层数
3. **时间复杂度**：平均O(logN)，最坏O(N)
4. **空间换时间**：相比平衡树实现更简单

## 8. 设计模式总结

### 8.1 工厂模式
- 通过`createObject`函数创建不同类型的redisObject
- 根据数据特点自动选择最优编码方式

### 8.2 策略模式
- 不同的数据类型使用不同的编码策略
- 运行时根据数据特征动态调整编码

### 8.3 享元模式
- 小整数对象共享，减少内存分配
- 通过引用计数管理共享对象

### 8.4 原型模式
- 通过复制创建相似对象
- 支持深度复制和浅复制

## 9. 性能优化技巧

### 9.1 内存对齐
- 使用`__attribute__ ((__packed__))`避免内存对齐浪费
- 根据数据大小选择合适的数据类型

### 9.2 缓存友好
- 数据结构设计考虑CPU缓存行
- 减少指针跳转，提高缓存命中率

### 9.3 预分配策略
- SDS空间预分配，减少频繁分配
- 字典rehash时渐进式迁移

## 10. 总结

Redis的核心数据结构设计体现了以下几个重要原则：

1. **内存效率**：通过各种编码技巧减少内存使用
2. **时间复杂度**：确保关键操作的时间复杂度
3. **可扩展性**：支持多种数据类型和编码方式
4. **健壮性**：完善的错误处理和内存管理

这些数据结构的设计思想对于高性能系统的开发具有重要参考价值。在后续文章中，我们将继续深入分析Redis的其他核心组件。

## 单元测试

```c
// 测试redisObject创建和销毁
void test_redisObject() {
    robj *obj = createObject(OBJ_STRING, sdsnew("test"));
    assert(obj->type == OBJ_STRING);
    assert(obj->encoding == OBJ_ENCODING_RAW);
    assert(obj->refcount == 1);
    decrRefCount(obj);
}

// 测试SDS操作
void test_sds() {
    sds s = sdsnew("hello");
    assert(sdslen(s) == 5);
    s = sdscat(s, " world");
    assert(sdslen(s) == 11);
    sdsfree(s);
}

// 测试字典操作
void test_dict() {
    dict *d = dictCreate(&hashDictType);
    dictEntry *entry = dictAddRaw(d, sdsnew("key"), NULL);
    dictSetKey(d, entry, sdsnew("value"));
    assert(dictSize(d) == 1);
    dictRelease(d);
}
```

## 下一步

在下一篇文章中，我们将深入分析Redis的事件驱动模型，了解其如何实现高性能的网络通信。