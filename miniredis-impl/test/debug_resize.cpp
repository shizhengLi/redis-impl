#include "../include/dict.h"
#include <iostream>

int main() {
    Dict<int, std::string> dict;

    std::cout << "Inserting elements..." << std::endl;
    for (int i = 0; i < 20; i++) {
        bool result = dict.insert(i, "value" + std::to_string(i));
        std::cout << "Inserted " << i << ": " << result << ", size: " << dict.size() << ", table_size: " << dict.tableSize() << std::endl;
    }

    std::cout << "Checking elements..." << std::endl;
    for (int i = 0; i < 20; i++) {
        bool found = dict.contains(i);
        std::cout << "Element " << i << ": " << (found ? "found" : "NOT FOUND") << std::endl;
        if (!found) break;
    }

    return 0;
}