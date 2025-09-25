#include "sds.h"
#include <algorithm>

// 私有方法：初始化
void Sds::init() {
    buf_ = nullptr;
    len_ = 0;
    free_ = 0;
}

// 构造函数
Sds::Sds() {
    init();
    buf_ = new char[1];
    buf_[0] = '\0';
    len_ = 0;
    free_ = 0;
}

Sds::Sds(const char* init_str) {
    init();
    if (init_str) {
        len_ = strlen(init_str);
        size_t total_len = len_ + 1;
        buf_ = new char[total_len];
        memcpy(buf_, init_str, len_);
        buf_[len_] = '\0';
        free_ = 0;
    } else {
        buf_ = new char[1];
        buf_[0] = '\0';
        len_ = 0;
        free_ = 0;
    }
}

Sds::Sds(const std::string& init) : Sds(init.c_str()) {}

Sds::Sds(const Sds& other) {
    init();
    len_ = other.len_;
    free_ = other.free_;
    size_t total_len = len_ + free_ + 1;
    buf_ = new char[total_len];
    memcpy(buf_, other.buf_, len_ + free_ + 1);
}

Sds::Sds(Sds&& other) noexcept {
    buf_ = other.buf_;
    len_ = other.len_;
    free_ = other.free_;
    other.buf_ = nullptr;
    other.len_ = 0;
    other.free_ = 0;
}

Sds::~Sds() {
    delete[] buf_;
}

// 赋值运算符
Sds& Sds::operator=(const Sds& other) {
    if (this != &other) {
        delete[] buf_;
        len_ = other.len_;
        free_ = other.free_;
        size_t total_len = len_ + free_ + 1;
        buf_ = new char[total_len];
        memcpy(buf_, other.buf_, len_ + free_ + 1);
    }
    return *this;
}

Sds& Sds::operator=(Sds&& other) noexcept {
    if (this != &other) {
        delete[] buf_;
        buf_ = other.buf_;
        len_ = other.len_;
        free_ = other.free_;
        other.buf_ = nullptr;
        other.len_ = 0;
        other.free_ = 0;
    }
    return *this;
}

// 私有方法：根据需要增长
void Sds::growIfNeeded(size_t add_len) {
    if (free_ >= add_len) return;

    size_t new_len = len_ + add_len;
    size_t newsize = new_len + 1;  // +1 for '\0'

    // 预分配策略：当新长度小于SDS_MAX_PREALLOC时，倍增空间
    if (newsize < SDS_MAX_PREALLOC) {
        newsize *= 2;
    } else {
        newsize += SDS_MAX_PREALLOC;
    }

    realloc(newsize);
}

// 私有方法：重新分配内存
void Sds::realloc(size_t newsize) {
    char* newbuf = new char[newsize];
    size_t copy_len = std::min(len_, newsize - 1);
    memcpy(newbuf, buf_, copy_len);
    newbuf[copy_len] = '\0';

    delete[] buf_;
    buf_ = newbuf;
    free_ = newsize - len_ - 1;
}

// 字符串操作
Sds& Sds::append(const char* str) {
    if (!str) return *this;
    size_t add_len = strlen(str);
    growIfNeeded(add_len);
    memcpy(buf_ + len_, str, add_len);
    len_ += add_len;
    buf_[len_] = '\0';
    free_ -= add_len;
    return *this;
}

Sds& Sds::append(const Sds& other) {
    growIfNeeded(other.len_);
    memcpy(buf_ + len_, other.buf_, other.len_);
    len_ += other.len_;
    buf_[len_] = '\0';
    free_ -= other.len_;
    return *this;
}

Sds& Sds::append(const std::string& str) {
    return append(str.c_str());
}

// 比较操作
bool Sds::operator==(const Sds& other) const {
    if (len_ != other.len_) return false;
    return memcmp(buf_, other.buf_, len_) == 0;
}

bool Sds::operator<(const Sds& other) const {
    size_t min_len = std::min(len_, other.len_);
    int cmp = memcmp(buf_, other.buf_, min_len);
    if (cmp != 0) return cmp < 0;
    return len_ < other.len_;
}

// 访问操作
char Sds::operator[](size_t index) const {
    if (index >= len_) throw std::out_of_range("Sds index out of range");
    return buf_[index];
}

char& Sds::operator[](size_t index) {
    if (index >= len_) throw std::out_of_range("Sds index out of range");
    return buf_[index];
}

// 容量操作
void Sds::clear() {
    len_ = 0;
    if (buf_) {
        buf_[0] = '\0';
    }
    // 保留已分配的内存供后续使用
}

void Sds::resize(size_t newlen) {
    if (newlen == len_) return;

    if (newlen < len_) {
        len_ = newlen;
        buf_[len_] = '\0';
        free_ += (newlen - len_);
    } else {
        growIfNeeded(newlen - len_);
        len_ = newlen;
        buf_[len_] = '\0';
        free_ -= (newlen - len_);
    }
}

void Sds::reserve(size_t newsize) {
    if (newsize <= len_ + free_ + 1) return;
    realloc(newsize);
}

// 转换操作
std::string Sds::toString() const {
    return std::string(buf_, len_);
}

// 输出操作
std::ostream& operator<<(std::ostream& os, const Sds& sds) {
    os.write(sds.buf_, sds.len_);
    return os;
}