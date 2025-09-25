#include "../include/dict.h"
#include <iostream>
#include <cassert>

void test_basic_operations() {
    std::cout << "Testing basic dictionary operations..." << std::endl;

    Dict<std::string, int> dict;

    assert(dict.empty());
    assert(dict.size() == 0);
    assert(!dict.contains("key1"));

    assert(dict.insert("key1", 100));
    assert(dict.size() == 1);
    assert(dict.contains("key1"));
    assert(*dict.find("key1") == 100);

    assert(!dict.insert("key1", 200));
    assert(*dict.find("key1") == 200);
    assert(dict.size() == 1);

    std::cout << "✓ Basic operations passed" << std::endl;
}

void test_remove_operations() {
    std::cout << "Testing remove operations..." << std::endl;

    Dict<std::string, int> dict;

    dict.insert("key1", 100);
    dict.insert("key2", 200);
    dict.insert("key3", 300);

    assert(dict.size() == 3);

    assert(dict.remove("key2"));
    assert(dict.size() == 2);
    assert(!dict.contains("key2"));
    assert(dict.contains("key1"));
    assert(dict.contains("key3"));

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

    auto it = dict.iterator();
    int count = 0;
    int sum = 0;

    while (it.hasNext()) {
        auto pair = it.next();
        count++;
        sum += pair.second;
    }

    assert(count == 3);
    assert(sum == 600);

    std::cout << "✓ Iterator operations passed" << std::endl;
}

int main() {
    test_basic_operations();
    test_remove_operations();
    test_iterator();
    std::cout << "Extended test completed successfully" << std::endl;
    return 0;
}