#include "../include/dict.h"
#include <iostream>
#include <cassert>

void test_resize() {
    std::cout << "Testing dictionary resize..." << std::endl;

    Dict<int, std::string> dict;

    for (int i = 0; i < 100; i++) {
        dict.insert(i, "value" + std::to_string(i));
    }

    assert(dict.size() == 100);
    assert(dict.loadFactor() < 2.0);

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

    dict.insert("key3", 300);
    assert(dict.size() == 1);
    assert(dict.contains("key3"));

    std::cout << "✓ Clear operations passed" << std::endl;
}

void test_collision_handling() {
    std::cout << "Testing hash collision handling..." << std::endl;

    Dict<int, int> dict(4);

    dict.insert(1, 100);
    dict.insert(5, 500);
    dict.insert(9, 900);

    assert(dict.size() == 3);
    assert(*dict.find(1) == 100);
    assert(*dict.find(5) == 500);
    assert(*dict.find(9) == 900);

    assert(dict.remove(5));
    assert(dict.size() == 2);
    assert(!dict.contains(5));
    assert(dict.contains(1));
    assert(dict.contains(9));

    std::cout << "✓ Collision handling passed" << std::endl;
}

int main() {
    test_resize();
    test_string_keys();
    test_clear();
    test_collision_handling();
    std::cout << "Full test completed successfully" << std::endl;
    return 0;
}