# swisstable
It implements the UnorderedMap and UnorderedSet containers based on Swisstable in a streamlined way, has the same interface as STL, and is 2 to 4 times faster than STL.
精简地实现了底层为swisstable的Unorderedmap和UnorderedSet容器，拥有和stl相同的接口，性能比stl快2到4倍。
# Usage
支持以下接口
您的容器库提供了以下接口，主要分为两类：**底层容器接口（FlatTable）** 和**高级容器接口（HashTable/HashSet）**。以下是详细的接口总结：

---

### **1. 底层容器 FlatTable**
#### **核心功能**
- **构造/析构**
  - 构造函数：支持容量/哈希函数/比较器/分配器
  - 拷贝/移动构造
  - 析构函数：自动销毁元素并释放内存

- **容量查询**
  - `size()`：返回元素数量
  - `empty()`：判断是否为空
  - `capacity`：底层存储容量（成员变量）

- **迭代器**
  - `begin()`/`end()`：正向迭代器
  - `cbegin()`/`cend()`：const 迭代器
  - 支持 `++` 遍历操作

- **元素访问**
  - `find(key)`：返回迭代器
  - `cfind(key)`：返回 const 迭代器
  - `contains(key)`：检查存在性

- **修改操作**
  - `insert(entry)`：插入元素（左值/右值）
  - `emplace(args...)`：原位构造元素
  - `erase(iterator)`：删除迭代器指向的元素
  - `erase(key)`：按键删除元素
  - `clear()`：清空容器

- **内存管理**
  - `get_allocator()`：获取分配器
  - `swap(other)`：交换容器内容

### **2. 高级容器 HashTable (UnorderedMap)**
#### **扩展接口**
- **元素访问**
  - `operator[](key)`：按键访问值（不存在时自动插入）
  - `operator[](std::move(key))`：移动键版本

- **原位构造**
  - `try_emplace(key, args...)`：避免不必要的拷贝
  - 支持左值/右值键版本

- **底层适配**
  - 继承自 `FlatTable<HashPair<Key, Value>>`
  - 键值对存储为 `std::pair<Key, Value>`

---

### **3. 高级容器 HashSet (UnorderedSet)**
#### **扩展接口**
- **范围构造**
  - `HashSet(first, last)`：迭代器范围初始化

- **集合操作**
  - 继承 `FlatTable<T>` 的所有接口
  - 元素即键值（`T = Key`）


