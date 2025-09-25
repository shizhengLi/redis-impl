#ifndef MINIREDIS_SDS_H
#define MINIREDIS_SDS_H

#include <cstring>
#include <string>
#include <iostream>
#include <functional>

class Sds {
private:
    // 字符串结构: [len][free][buf...]
    char* buf_;           // 实际数据指针
    size_t len_;          // 字符串长度
    size_t free_;         // 剩余可用空间
    static const size_t SDS_MAX_PREALLOC = 1024 * 1024;  // 最大预分配空间

public:
    // 构造函数
    Sds();
    explicit Sds(const char* init);
    explicit Sds(const std::string& init);
    Sds(const Sds& other);
    Sds(Sds&& other) noexcept;
    ~Sds();

    // 赋值运算符
    Sds& operator=(const Sds& other);
    Sds& operator=(Sds&& other) noexcept;

    // 基本操作
    size_t length() const { return len_; }
    size_t avail() const { return free_; }
    bool empty() const { return len_ == 0; }
    const char* data() const { return buf_; }
    char* data() { return buf_; }

    // 字符串操作
    Sds& append(const char* str);
    Sds& append(const Sds& other);
    Sds& append(const std::string& str);
    Sds& operator+=(const char* str) { return append(str); }
    Sds& operator+=(const Sds& other) { return append(other); }
    Sds& operator+=(const std::string& str) { return append(str); }

    // 比较操作
    bool operator==(const Sds& other) const;
    bool operator!=(const Sds& other) const { return !(*this == other); }
    bool operator<(const Sds& other) const;
    bool operator>(const Sds& other) const { return other < *this; }
    bool operator<=(const Sds& other) const { return !(other < *this); }
    bool operator>=(const Sds& other) const { return !(*this < other); }

    // 访问操作
    char operator[](size_t index) const;
    char& operator[](size_t index);

    // 容量操作
    void clear();
    void resize(size_t newlen);
    void reserve(size_t newsize);

    // 转换操作
    std::string toString() const;
    operator std::string() const { return toString(); }

    // 输出操作
    friend std::ostream& operator<<(std::ostream& os, const Sds& sds);

private:
    void init();
    void growIfNeeded(size_t add_len);
    void realloc(size_t newsize);
};

// 为Sds提供哈希函数特化
namespace std {
    template<>
    struct hash<Sds> {
        size_t operator()(const Sds& sds) const {
            return hash<string>()(sds.toString());
        }
    };
}

#endif // MINIREDIS_SDS_H