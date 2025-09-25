#ifndef MINIREDIS_DICT_H
#define MINIREDIS_DICT_H

#include "sds.h"
#include <functional>
#include <utility>
#include <vector>
#include <memory>
#include <stdexcept>

// 哈希表节点
template<typename K, typename V>
class DictEntry {
public:
    K key;
    V value;
    std::shared_ptr<DictEntry<K, V>> next;  // 链地址法

    DictEntry(const K& k, const V& v)
        : key(k), value(v), next(nullptr) {}
};

// 字典迭代器
template<typename K, typename V>
class DictIterator {
private:
    std::vector<std::shared_ptr<DictEntry<K, V>>>* table_;
    size_t table_size_;
    size_t index_;
    std::shared_ptr<DictEntry<K, V>> current_;
    bool safe_;  // 安全迭代器标志

public:
    DictIterator(std::vector<std::shared_ptr<DictEntry<K, V>>>& table, bool safe = true)
        : table_(&table), table_size_(table.size()), index_(0), current_(nullptr), safe_(safe) {
        advance();
    }

    bool hasNext() const {
        return current_ != nullptr;
    }

    std::pair<K, V> next() {
        if (!hasNext()) {
            throw std::out_of_range("Iterator out of range");
        }

        auto result = std::make_pair(current_->key, current_->value);
        advance();
        return result;
    }

    std::shared_ptr<DictEntry<K, V>> currentEntry() const {
        return current_;
    }

private:
    void advance() {
        if (current_ && current_->next) {
            current_ = current_->next;
            return;
        }

        while (index_ < table_size_) {
            if ((*table_)[index_]) {
                current_ = (*table_)[index_];
                index_++;
                return;
            }
            index_++;
        }

        current_ = nullptr;
    }
};

// 字典主类
template<typename K, typename V>
class Dict {
private:
    std::vector<std::shared_ptr<DictEntry<K, V>>> table_;
    size_t size_;      // 已存储的键值对数量
    size_t size_mask_; // 掩码，用于计算索引
    size_t rehash_index_;  // 渐进式rehash的索引，-1表示不在rehash中
    std::vector<std::shared_ptr<DictEntry<K, V>>> rehash_table_;  // rehash目标表

    // 哈希函数
    std::function<size_t(const K&)> hash_func_;

    // 扩容和缩容的阈值
    static constexpr size_t MIN_SIZE = 4;
    static constexpr double FORCE_RESIZE_RATIO = 10.0;
    static constexpr double RESIZE_RATIO = 1.0;

public:
    Dict() : size_(0), rehash_index_(static_cast<size_t>(-1)) {
        table_.resize(MIN_SIZE);
        hash_func_ = std::hash<K>();
    }

    explicit Dict(size_t initial_size) : size_(0), rehash_index_(static_cast<size_t>(-1)) {
        size_t size = std::max(initial_size, MIN_SIZE);
        table_.resize(size);
        hash_func_ = std::hash<K>();
    }

    // 基本操作
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t tableSize() const { return table_.size(); }

    // 插入操作
    bool insert(const K& key, const V& value);
    bool insert(K&& key, V&& value);

    // 查找操作
    V* find(const K& key);
    const V* find(const K& key) const;
    bool contains(const K& key) const { return find(key) != nullptr; }

    // 删除操作
    bool remove(const K& key);

    // 清空操作
    void clear();

    // 迭代器
    DictIterator<K, V> iterator(bool safe = true) {
        return DictIterator<K, V>(table_, safe);
    }

    // 获取所有键
    std::vector<K> keys() const;

    // 获取所有值
    std::vector<V> values() const;

    // 负载因子
    double loadFactor() const { return static_cast<double>(size_) / table_.size(); }

private:
    size_t hashKey(const K& key) const {
        return hash_func_(key);
    }

    size_t getIndex(const K& key, const std::vector<std::shared_ptr<DictEntry<K, V>>>& t) const {
        return hashKey(key) & (t.size() - 1);
    }

    void resize(size_t new_size);
    void rehashStep();  // 执行一步渐进式rehash
    bool isRehashing() const { return rehash_index_ != static_cast<size_t>(-1); }

    // 查找节点（用于内部操作）
    std::shared_ptr<DictEntry<K, V>> findEntry(const K& key,
        const std::vector<std::shared_ptr<DictEntry<K, V>>>& t) const;
};

// 模板实现
template<typename K, typename V>
bool Dict<K, V>::insert(const K& key, const V& value) {
    // 如果需要扩容且不在rehash中，直接扩容
    if (!isRehashing() && size_ >= table_.size() * RESIZE_RATIO) {
        resize(table_.size() * 2);
    }

    // 如果正在rehash，执行一步rehash
    if (isRehashing()) {
        rehashStep();
    }

    // 检查是否已存在
    if (auto* existing = find(key)) {
        *existing = value;
        return false;  // 已存在，更新值
    }

    // 在主表中插入（即使正在rehash，新元素也插入到主表）
    size_t index = getIndex(key, table_);
    auto new_entry = std::make_shared<DictEntry<K, V>>(key, value);
    new_entry->next = table_[index];
    table_[index] = new_entry;

    size_++;
    return true;
}

template<typename K, typename V>
bool Dict<K, V>::insert(K&& key, V&& value) {
    if (!isRehashing() && size_ >= table_.size() * RESIZE_RATIO) {
        resize(table_.size() * 2);
    }

    if (isRehashing()) {
        rehashStep();
    }

    if (auto* existing = find(key)) {
        *existing = std::move(value);
        return false;
    }

    // 在主表中插入（即使正在rehash，新元素也插入到主表）
    size_t index = getIndex(key, table_);
    auto new_entry = std::make_shared<DictEntry<K, V>>(std::move(key), std::move(value));
    new_entry->next = table_[index];
    table_[index] = new_entry;

    size_++;
    return true;
}

template<typename K, typename V>
V* Dict<K, V>::find(const K& key) {
    // 先在主表中查找
    auto entry = findEntry(key, table_);
    if (entry) return &entry->value;

    // 如果在rehash中，在新表中查找
    if (isRehashing()) {
        entry = findEntry(key, rehash_table_);
        if (entry) return &entry->value;
    }

    return nullptr;
}

template<typename K, typename V>
const V* Dict<K, V>::find(const K& key) const {
    return const_cast<Dict<K, V>*>(this)->find(key);
}

template<typename K, typename V>
bool Dict<K, V>::remove(const K& key) {
    if (isRehashing()) {
        rehashStep();
    }

    auto removeFromTable = [&](std::vector<std::shared_ptr<DictEntry<K, V>>>& t) -> bool {
        size_t index = getIndex(key, t);
        auto current = t[index];
        std::shared_ptr<DictEntry<K, V>> prev = nullptr;

        while (current) {
            if (current->key == key) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    t[index] = current->next;
                }
                size_--;
                return true;
            }
            prev = current;
            current = current->next;
        }
        return false;
    };

    if (removeFromTable(table_)) {
        return true;
    }

    if (isRehashing() && removeFromTable(rehash_table_)) {
        return true;
    }

    return false;
}

template<typename K, typename V>
void Dict<K, V>::clear() {
    table_.clear();
    rehash_table_.clear();
    size_ = 0;
    rehash_index_ = static_cast<size_t>(-1);
    resize(MIN_SIZE);
}

template<typename K, typename V>
std::vector<K> Dict<K, V>::keys() const {
    std::vector<K> result;
    result.reserve(size_);

    auto collectKeys = [&](const std::vector<std::shared_ptr<DictEntry<K, V>>>& t) {
        for (const auto& entry : t) {
            auto current = entry;
            while (current) {
                result.push_back(current->key);
                current = current->next;
            }
        }
    };

    collectKeys(table_);
    if (isRehashing()) {
        collectKeys(rehash_table_);
    }

    return result;
}

template<typename K, typename V>
std::vector<V> Dict<K, V>::values() const {
    std::vector<V> result;
    result.reserve(size_);

    auto collectValues = [&](const std::vector<std::shared_ptr<DictEntry<K, V>>>& t) {
        for (const auto& entry : t) {
            auto current = entry;
            while (current) {
                result.push_back(current->value);
                current = current->next;
            }
        }
    };

    collectValues(table_);
    if (isRehashing()) {
        collectValues(rehash_table_);
    }

    return result;
}

template<typename K, typename V>
void Dict<K, V>::resize(size_t new_size) {
    if (new_size < MIN_SIZE) new_size = MIN_SIZE;

    // 如果已经在rehashing，不进行新的resize
    if (isRehashing()) {
        return;
    }

    if (table_.size() == new_size) return;

    // 创建新表
    std::vector<std::shared_ptr<DictEntry<K, V>>> new_table(new_size);

    // 开始rehash过程
    rehash_table_ = std::move(table_);
    table_ = std::move(new_table);
    rehash_index_ = 0;
}

template<typename K, typename V>
void Dict<K, V>::rehashStep() {
    if (!isRehashing()) return;

    // 每次至少迁移一个bucket
    size_t steps = std::max(static_cast<size_t>(1), rehash_table_.size() / 10);

    for (size_t i = 0; i < steps && rehash_index_ < rehash_table_.size(); i++, rehash_index_++) {
        auto entry = rehash_table_[rehash_index_];
        while (entry) {
            auto next = entry->next;
            size_t new_index = getIndex(entry->key, table_);

            entry->next = table_[new_index];
            table_[new_index] = entry;

            entry = next;
        }
        rehash_table_[rehash_index_] = nullptr;
    }

    // 如果rehash完成，清理
    if (rehash_index_ >= rehash_table_.size()) {
        rehash_table_.clear();
        rehash_index_ = static_cast<size_t>(-1);
    }
}

template<typename K, typename V>
std::shared_ptr<DictEntry<K, V>> Dict<K, V>::findEntry(const K& key,
    const std::vector<std::shared_ptr<DictEntry<K, V>>>& t) const {
    size_t index = getIndex(key, t);
    auto current = t[index];

    while (current) {
        if (current->key == key) {
            return current;
        }
        current = current->next;
    }

    return nullptr;
}

#endif // MINIREDIS_DICT_H