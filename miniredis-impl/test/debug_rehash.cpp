#include "../include/dict.h"
#include <iostream>

int main() {
    Dict<int, std::string> dict;

    std::cout << "Initial table size: " << dict.tableSize() << std::endl;

    // 插入前4个元素（应该触发resize）
    for (int i = 0; i < 5; i++) {
        dict.insert(i, "value" + std::to_string(i));
        std::cout << "Inserted " << i << ", size: " << dict.size()
                  << ", table_size: " << dict.tableSize()
                  << ", isRehashing: " << (dict.loadFactor() > 1.0 ? "true" : "false") << std::endl;
    }

    std::cout << "\nChecking all elements:" << std::endl;
    for (int i = 0; i < 5; i++) {
        bool found = dict.contains(i);
        std::cout << "Element " << i << ": " << (found ? "found" : "NOT FOUND") << std::endl;
    }

    return 0;
}