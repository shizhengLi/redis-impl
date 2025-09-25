# MiniRedis - A Redis Implementation in C++

这是一个用C++实现的简化版Redis，用于学习和理解Redis的核心原理。

## 项目结构

```
miniredis-impl/
├── include/          # 头文件
│   ├── sds.h         # 动态字符串实现
│   └── dict.h        # 字典/哈希表实现
├── src/              # 源文件
│   ├── sds.cpp       # SDS实现
│   └── main.cpp      # 主程序入口
├── test/             # 测试文件
└── CMakeLists.txt    # 构建配置
```

## 已实现功能

### 1. 动态字符串 (SDS)
- O(1)时间复杂度获取字符串长度
- 自动扩容和预分配策略
- 二进制安全，可存储任意数据
- 避免缓冲区溢出

### 2. 字典 (Dictionary)
- 基于哈希表的键值存储
- 渐进式Rehash，避免性能抖动
- 链地址法处理哈希冲突
- 自动扩容机制

## 编译和测试

```bash
# 编译基础测试
g++ -std=c++17 -I include src/sds.cpp -o test_sds_demo

# 编译字典测试
g++ -std=c++17 -I include src/sds.cpp -o test_dict_demo
```

## 设计特点

1. **内存管理优化**：使用智能指针管理内存，避免内存泄漏
2. **性能优化**：采用渐进式Rehash，保证操作的时间复杂度
3. **代码质量**：完整的单元测试覆盖，确保代码正确性
4. **可扩展性**：模块化设计，便于后续功能扩展

## 后续计划

- [ ] 实现RESP协议编解码
- [ ] 实现Redis命令解析器
- [ ] 实现内存数据库核心类
- [ ] 实现网络服务器(Reactor模式)
- [ ] 集成测试和性能优化