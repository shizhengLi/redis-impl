#include "../include/dict.h"
#include <iostream>
#include <cassert>
#include <string>

void test_basic_operations() {
    std::cout << "Testing basic dictionary operations..." << std::endl;

    Dict<std::string, int> dict;

    // 测试空字典
    assert(dict.empty());
    assert(dict.size() == 0);
    assert(!dict.contains("key1"));

    // 测试插入
    assert(dict.insert("key1", 100));
    assert(dict.size() == 1);
    assert(dict.contains("key1"));
    assert(*dict.find("key1") == 100);

    // 测试重复插入
    assert(!dict.insert("key1", 200));  // 应该返回false，key已存在
    assert(*dict.find("key1") == 200);  // 值应该被更新
    assert(dict.size() == 1);

    // 测试移动语义插入
    assert(dict.insert(std::string("key2"), 300));
    assert(dict.size() == 2);
    assert(*dict.find("key2") == 300);

    std::cout << "✓ Basic operations passed" << std::endl;
}

void test_remove_operations() {
    std::cout << "Testing remove operations..." << std::endl;

    Dict<std::string, int> dict;

    dict.insert("key1", 100);
    dict.insert("key2", 200);
    dict.insert("key3", 300);

    assert(dict.size() == 3);

    // 测试删除存在的键
    assert(dict.remove("key2"));
    assert(dict.size() == 2);
    assert(!dict.contains("key2"));
    assert(dict.contains("key1"));
    assert(dict.contains("key3"));

    // 测试删除不存在的键
    assert(!dict.remove("nonexistent"));
    assert(dict.size() == 2);

    std::cout << "✓ Remove operations passed" << std::endl;
}

void test_iterator() {
    std::cout << "Testing dictionary iterator..." << std::endl;

    Dict<std::string, int> dict;
    dict.insert("key1", 100);
    dict.insert("key2", 200);
    dict.insert("key3", 300);

    // 测试迭代器
    auto it = dict.iterator();
    int count = 0;
    int sum = 0;

    while (it.hasNext()) {
        auto pair = it.next();
        count++;
        sum += pair.second;
    }

    assert(count == 3);
    assert(sum == 600);  // 100 + 200 + 300

    // 测试获取所有键和值
    auto keys = dict.keys();
    auto values = dict.values();

    assert(keys.size() == 3);
    assert(values.size() == 3);

    // 验证键值对应关系
    for (size_t i = 0; i < keys.size(); i++) {
        assert(*dict.find(keys[i]) == values[i]);
    }

    std::cout << "✓ Iterator operations passed" << std::endl;
}

void test_resize() {
    std::cout << "Testing dictionary resize..." << std::endl;

    Dict<int, std::string> dict;

    // 插入足够多的元素触发扩容
    for (int i = 0; i < 100; i++) {
        dict.insert(i, "value" + std::to_string(i));
    }

    assert(dict.size() == 100);
    assert(dict.loadFactor() < 2.0);  // 负载因子应该小于2

    // 验证所有元素仍然存在
    for (int i = 0; i < 100; i++) {
        assert(dict.contains(i));
        assert(*dict.find(i) == "value" + std::to_string(i));
    }

    std::cout << "✓ Resize operations passed" << std::endl;
}

void test_string_keys() {
    std::cout << "Testing with string keys..." << std::endl;

    Dict<Sds, Sds> dict;

    Sds key1("hello");
    Sds value1("world");
    Sds key2("foo");
    Sds value2("bar");

    dict.insert(key1, value1);
    dict.insert(key2, value2);

    assert(dict.size() == 2);
    assert(dict.contains(key1));
    assert(dict.contains(key2));

    assert(*dict.find(key1) == value1);
    assert(*dict.find(key2) == value2);

    // 测试迭代器
    auto it = dict.iterator();
    int count = 0;
    while (it.hasNext()) {
        it.next();
        count++;
    }
    assert(count == 2);

    std::cout << "✓ String keys operations passed" << std::endl;
}

void test_clear() {
    std::cout << "Testing clear operation..." << std::endl;

    Dict<std::string, int> dict;
    dict.insert("key1", 100);
    dict.insert("key2", 200);

    assert(dict.size() == 2);

    dict.clear();

    assert(dict.empty());
    assert(dict.size() == 0);
    assert(!dict.contains("key1"));
    assert(!dict.contains("key2"));

    // 清空后应该能重新插入
    dict.insert("key3", 300);
    assert(dict.size() == 1);
    assert(dict.contains("key3"));

    std::cout << "✓ Clear operations passed" << std::endl;
}

void test_collision_handling() {
    std::cout << "Testing hash collision handling..." << std::endl;

    // 创建一个很小的表来强制碰撞
    Dict<int, int> dict(4);

    // 插入多个元素，确保它们会被映射到同一个bucket
    // 通过选择特定的键值来制造碰撞
    dict.insert(1, 100);
    dict.insert(5, 500);  // 在大小为4的表中，1和5会有相同的索引
    dict.insert(9, 900);  // 同样会碰撞

    assert(dict.size() == 3);
    assert(*dict.find(1) == 100);
    assert(*dict.find(5) == 500);
    assert(*dict.find(9) == 900);

    // 测试在碰撞链中的删除
    assert(dict.remove(5));
    assert(dict.size() == 2);
    assert(!dict.contains(5));
    assert(dict.contains(1));
    assert(dict.contains(9));

    std::cout << "✓ Collision handling passed" << std::endl;
}

int main() {
    std::cout << "=== Dictionary Test Suite ===" << std::endl;

    try {
        test_basic_operations();
        test_remove_operations();
        test_iterator();
        test_resize();
        test_string_keys();
        test_clear();
        test_collision_handling();

        std::cout << "\n✅ All dictionary tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}