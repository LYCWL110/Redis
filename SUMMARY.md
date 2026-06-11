# Redis-like Server (C++)

从零实现的类 Redis 键值存储服务器。

## 功能

- **命令**: GET / SET / DEL / KEYS / EXPIRE / TTL
- **协议**: 二进制序列化 (NIL/STR/INT/ERR/ARR 五种类型)
- **存储**: 侵入式哈希表，支持增量 rehash
- **并发**: 基于 epoll 的非阻塞 I/O 事件循环
- **定时器**: 空闲连接超时断开 + 键过期 (TTL 最小堆)
- **线程池**: C++20 std::thread + condition_variable

## 数据结构

| 模块 | 实现 |
|------|------|
| 哈希表 | HMap 双表增量 rehash，FNV 哈希，链表冲突 |
| AVL 树 | 平衡二叉树，O(log n) 增删查，排名偏移查询 |
| 最小堆 | TTL 定时器，O(log n) 更新 |
| 双向链表 | DList 循环链表，空闲连接管理 |

## 文件结构

```
server.h/cpp      # 主程序入口，事件循环
conn.cpp          # 连接状态机，epoll I/O，定时器
command.cpp       # 命令路由，序列化协议
hashtable.h/cpp   # 哈希表
avl.h/cpp         # AVL 树
heap.h/cpp        # 最小堆
list.h            # 双向链表
thread_pool.h/cpp # 线程池
client.cpp        # 测试客户端
```

## 构建

```bash
g++ -std=c++20 -Wall -Wextra -O2 -o server \
    hashtable.cpp avl.cpp heap.cpp thread_pool.cpp \
    command.cpp conn.cpp server.cpp
```

## 风格

遵循 Google C++ Style Guide: PascalCase 函数, kPascalCase 常量, snake_case 变量, nullptr, 2 空格缩进。
