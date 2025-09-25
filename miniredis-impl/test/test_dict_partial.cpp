#include "../include/dict.h"
#include <iostream>
#include <cassert>

void test_basic_operations() {
    std::cout << "Testing basic dictionary operations..." << std::endl;

    Dict<std::string, int> dict;

    std::cout << "Test empty dict..." << std::endl;
    assert(dict.empty());
    assert(dict.size() == 0);
    assert(!dict.contains("key1"));

    std::cout << "Test insert..." << std::endl;
    assert(dict.insert("key1", 100));
    assert(dict.size() == 1);
    assert(dict.contains("key1"));
    assert(*dict.find("key1") == 100);

    std::cout << "Test duplicate insert..." << std::endl;
    assert(!dict.insert("key1", 200));
    assert(*dict.find("key1") == 200);
    assert(dict.size() == 1);

    std::cout << "✓ Basic operations passed" << std::endl;
}

int main() {
    test_basic_operations();
    std::cout << "Partial test completed" << std::endl;
    return 0;
}