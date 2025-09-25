#include "../include/sds.h"
#include <iostream>
#include <cassert>
#include <string>

void test_basic_operations() {
    std::cout << "Testing basic operations..." << std::endl;

    // 测试默认构造
    Sds s1;
    assert(s1.length() == 0);
    assert(s1.empty());
    assert(strcmp(s1.data(), "") == 0);

    // 测试C字符串构造
    Sds s2("hello");
    assert(s2.length() == 5);
    assert(strcmp(s2.data(), "hello") == 0);

    // 测试std::string构造
    std::string str = "world";
    Sds s3(str);
    assert(s3.length() == 5);
    assert(strcmp(s3.data(), "world") == 0);

    std::cout << "✓ Basic operations passed" << std::endl;
}

void test_copy_and_move() {
    std::cout << "Testing copy and move operations..." << std::endl;

    Sds original("test");
    Sds copy = original;
    assert(copy == original);
    assert(copy.data() != original.data());  // 深拷贝

    Sds moved = std::move(original);
    assert(moved.length() == 4);
    assert(strcmp(moved.data(), "test") == 0);
    assert(original.length() == 0);  // 原对象被清空

    std::cout << "✓ Copy and move operations passed" << std::endl;
}

void test_append_operations() {
    std::cout << "Testing append operations..." << std::endl;

    Sds s("hello");
    s.append(" world");
    assert(s.length() == 11);
    assert(strcmp(s.data(), "hello world") == 0);

    Sds s2;
    s2 += "test";
    s2 += "123";
    assert(s2.length() == 7);
    assert(strcmp(s2.data(), "test123") == 0);

    std::cout << "✓ Append operations passed" << std::endl;
}

void test_comparison_operations() {
    std::cout << "Testing comparison operations..." << std::endl;

    Sds s1("abc");
    Sds s2("abc");
    Sds s3("def");
    Sds s4("abcd");

    assert(s1 == s2);
    assert(s1 != s3);
    assert(s1 < s3);
    assert(s1 < s4);
    assert(s3 > s1);

    std::cout << "✓ Comparison operations passed" << std::endl;
}

void test_access_operations() {
    std::cout << "Testing access operations..." << std::endl;

    Sds s("hello");
    assert(s[0] == 'h');
    assert(s[4] == 'o');

    s[0] = 'H';
    assert(s[0] == 'H');
    assert(strcmp(s.data(), "Hello") == 0);

    std::cout << "✓ Access operations passed" << std::endl;
}

void test_memory_management() {
    std::cout << "Testing memory management..." << std::endl;

    Sds s;
    s.reserve(100);
    assert(s.avail() >= 99);  // 至少99字节可用空间

    s.append("short");
    size_t old_avail = s.avail();
    s.append("another string");
    // 检查是否有合理的预分配策略
    assert(s.avail() > 0);

    s.clear();
    assert(s.length() == 0);
    assert(strcmp(s.data(), "") == 0);
    // 内存应该被保留

    std::cout << "✓ Memory management passed" << std::endl;
}

void test_conversion() {
    std::cout << "Testing conversion operations..." << std::endl;

    Sds s("test");
    std::string str = s.toString();
    assert(str == "test");

    std::string str2 = s;  // 隐式转换
    assert(str2 == "test");

    std::cout << "✓ Conversion operations passed" << std::endl;
}

int main() {
    std::cout << "=== SDS Test Suite ===" << std::endl;

    try {
        test_basic_operations();
        test_copy_and_move();
        test_append_operations();
        test_comparison_operations();
        test_access_operations();
        test_memory_management();
        test_conversion();

        std::cout << "\n✅ All tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}