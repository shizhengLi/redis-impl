#include "../include/dict.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Simple dictionary test..." << std::endl;

    Dict<std::string, int> dict;

    // 测试基本插入
    std::cout << "Testing insert..." << std::endl;
    bool result = dict.insert("key1", 100);
    std::cout << "Insert result: " << result << std::endl;
    std::cout << "Dict size: " << dict.size() << std::endl;

    if (dict.contains("key1")) {
        std::cout << "Key found, value: " << *dict.find("key1") << std::endl;
    } else {
        std::cout << "Key not found" << std::endl;
    }

    std::cout << "Test completed" << std::endl;
    return 0;
}