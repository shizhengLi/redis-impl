#include "../include/dict.h"
#include <iostream>

int main() {
    std::cout << "Debug test..." << std::endl;

    Dict<std::string, int> dict;
    std::cout << "Initial size: " << dict.size() << std::endl;
    std::cout << "Table size: " << dict.tableSize() << std::endl;

    std::cout << "Inserting key1..." << std::endl;
    dict.insert("key1", 100);
    std::cout << "After insert size: " << dict.size() << std::endl;
    std::cout << "Table size: " << dict.tableSize() << std::endl;
    std::cout << "Load factor: " << dict.loadFactor() << std::endl;

    std::cout << "Inserting key2..." << std::endl;
    dict.insert("key2", 200);
    std::cout << "After insert size: " << dict.size() << std::endl;
    std::cout << "Table size: " << dict.tableSize() << std::endl;

    std::cout << "Test completed" << std::endl;
    return 0;
}