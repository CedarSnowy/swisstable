#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <unordered_map>
#include "swisstable.h"
using namespace std;

// 简单断言宏
#define ASSERT(cond, msg) \
    if (!(cond)) { \
        std::cerr << "\033[31m[FAIL] " << msg << "\033[0m\n"; \
        std::exit(1); \
    } else { \
        std::cout << "\033[32m[PASS] " << msg << "\033[0m\n"; \
    }

// 生成测试数据
std::vector<std::pair<int, int>> generate_test_data(size_t size) {
    std::vector<std::pair<int, int>> data;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, size * 10);
    
    for (size_t i = 0; i < size; ++i) {
        data.emplace_back(i, i);
    }
    return data;
}

// 性能测试函数
template<typename MapType>
void test_performance(const std::string& name, const std::vector<std::pair<int, int>>& data) {
    auto start = std::chrono::high_resolution_clock::now();
    
    MapType map(data.size());
    for (const auto& [k, v] : data) {
        map.try_emplace(k, v);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << name << " insertion time: " << duration << " ns ("
              << data.size() << " elements)\n";
}

// 正确性测试
void test_correctness() {
    std::cout << "\n=== Correctness Test ===\n";
    
    UnorderedMap<int, int> map;
    
    // 测试插入
    auto [it1, inserted1] = map.try_emplace(1, 100);
    ASSERT(inserted1, "New key insertion success");
    ASSERT(map.size() == 1, "Size after insertion");
    ASSERT(it1->second == 100, "Inserted value correct");
    
    // 测试重复插入
    auto [it2, inserted2] = map.try_emplace(1, 200);
    ASSERT(!inserted2, "Duplicate key insertion failure");
    ASSERT(it2->second == 100, "Value not overwritten");
    
    // 测试查找
    auto found = map.find(1);
    ASSERT(found != map.end(), "Key found");
    ASSERT(found->second == 100, "Found value correct");
    
    // 测试删除
    map.erase(1);
    ASSERT(map.size() == 0, "Size after deletion");
    ASSERT(map.find(1) == map.end(), "Key not found after deletion");
    
    // 测试冲突处理
    const int num_keys = 1000;
    const int bucket_size = 10;
    for (int i = 0; i < num_keys; i++) {
        map.try_emplace(i % bucket_size, i);
    }
    ASSERT(map.size() == bucket_size, "Size after collision handling");
}

// 性能对比测试
void test_performance_comparison() {
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000, 10000000};
    
    std::cout << "\n=== Insertion Performance Test ===\n";
    
    for (size_t size : sizes) {
        auto data = generate_test_data(size);
        
        std::cout << "\nTest data size: " << size << " elements\n";
        test_performance<UnorderedMap<int, int>>("Your container", data);
        test_performance<std::unordered_map<int, int>>("std::unordered_map", data);
    }
}

void test_find_correctness() {
    std::cout << "\n=== Find Correctness Test ===\n";
    
    UnorderedMap<int, int> map;
    const int test_size = 1000;
    
    // 准备测试数据
    for (int i = 0; i < test_size; i++) {
        map.try_emplace(i, i * 10);
    }
    
    // 测试存在的键
    for (int i = 0; i < test_size; i++) {
        auto it = map.find(i);
        ASSERT(it != map.end(), "Existing key found");
        ASSERT(it->second == i * 10, "Found value correct");
    }
    
    // 测试不存在的键
    auto not_found = map.find(test_size + 100);
    ASSERT(not_found == map.end(), "Non-existing key not found");
    
    // 测试空容器
    UnorderedMap<int, int> empty_map;
    auto empty_find = empty_map.find(1);
    ASSERT(empty_find == empty_map.end(), "Key not found in empty map");
    
    // 测试删除后查找
    for (int i = 0; i < test_size; i += 2) {
        map.erase(i);
    }
    for (int i = 0; i < test_size; i++) {
        auto it = map.find(i);
        if (i % 2 == 0) {
            ASSERT(it == map.end(), "Deleted key not found");
        } else {
            ASSERT(it != map.end(), "Existing key found");
        }
    }
    
    // 测试高冲突场景
    UnorderedMap<int, int> collision_map;
    const int num_keys = 10000;
    const int bucket_size = 10;
    for (int i = 0; i < num_keys; i++) {
        collision_map.try_emplace(i % bucket_size, i);
    }
    for (int i = 0; i < bucket_size; i++) {
        auto it = collision_map.find(i);
        ASSERT(it != collision_map.end(), "Key found in high collision scenario");
    }
}

template<typename MapType>
void test_find_performance(const std::string& name, 
                          MapType& map,
                          const std::vector<int>& keys_to_find) {
    auto start = std::chrono::high_resolution_clock::now();
    
    size_t found_count = 0;
    for (int key : keys_to_find) {
        auto it = map.find(key);
        if (it != map.end()) {
            found_count++;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << name << " find time: " << duration << " ns ("
              << "hit rate: " << (found_count * 100.0 / keys_to_find.size()) << "%)"
              << " find count: " << keys_to_find.size() << "\n";
}

void test_find_performance_comparison() {
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000};
    
    std::cout << "\n=== Find Performance Test ===\n";
    
    for (size_t size : sizes) {
        auto data = generate_test_data(size);
        std::vector<int> keys_to_find;
        
        // 50%存在的键，50%不存在的键
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, size * 2);
        for (size_t i = 0; i < size; i++) {
            keys_to_find.push_back(dist(gen));
        }
        
        std::cout << "\nTest data size: " << size << " elements\n";
        
        // 测试您的容器
        UnorderedMap<int, int> your_map;
        for (const auto& [k, v] : data) {
            your_map.try_emplace(k, v);
        }
        test_find_performance("Your container", your_map, keys_to_find);
        
        // 测试std容器
        std::unordered_map<int, int> std_map;
        for (const auto& [k, v] : data) {
            std_map.try_emplace(k, v);
        }
        test_find_performance("std::unordered_map", std_map, keys_to_find);
    }
}

void test_erase_correctness() {
    std::cout << "\n=== Erase Correctness Test ===\n";
    
    UnorderedMap<int, int> map;
    const int test_size = 1000;
    
    // 准备测试数据
    for (int i = 0; i < test_size; i++) {
        map.try_emplace(i, i);
    }
    
    // 测试存在的键
    size_t expected_size = test_size;
    for (int i = 0; i < test_size; i += 2) {
        size_t count = map.erase(i);
        ASSERT(count == 1, "Existing key erased");
        expected_size--;
        ASSERT(map.size() == expected_size, "Size after erase");
    }
    
    // 测试不存在的键
    size_t count = map.erase(test_size + 100);
    ASSERT(count == 0, "Non-existing key erase");
    ASSERT(map.size() == expected_size, "Size unchanged");
    
    // 测试重复删除
    for (int i = 0; i < test_size; i += 2) {
        size_t repeat_count = map.erase(i);
        ASSERT(repeat_count == 0, "Repeated erase");
    }
    ASSERT(map.size() == expected_size, "Size unchanged after repeated erase");
    
    // 测试删除所有元素
    for (int i = 1; i < test_size; i += 2) {
        map.erase(i);
    }
    ASSERT(map.empty(), "Map empty after all elements erased");
    
    // 测试删除后插入新元素
    map.try_emplace(1, 100);
    ASSERT(map.size() == 1, "Insert after erase");
    auto it = map.find(1);
    ASSERT(it != map.end() && it->second == 100, "Value correct after reinsertion");
    
    // 测试迭代器稳定性
    UnorderedMap<int, int> stable_map;
    for (int i = 0; i < 10; i++) {
        stable_map.try_emplace(i, i);
    }
    auto it_before = stable_map.find(5);
    stable_map.erase(3);
    stable_map.erase(4);
    auto it_after = stable_map.find(5);
    ASSERT(it_before == it_after, "Iterator stability after erase");
}

template<typename MapType>
void test_erase_performance(const std::string& name, 
                           MapType& map,
                           const std::vector<int>& keys_to_erase) {
    auto start = std::chrono::high_resolution_clock::now();
    
    size_t erased_count = 0;
    for (int key : keys_to_erase) {
        erased_count += map.erase(key);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << name << " erase time: " << duration << " ns ("
              << "erased: " << erased_count << " elements) "
              << "erase count: " << keys_to_erase.size() << "\n";
}

void test_erase_performance_comparison() {
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000};
    
    std::cout << "\n=== Erase Performance Test ===\n";
    
    for (size_t size : sizes) {
        auto data = generate_test_data(size);
        
        // 准备要删除的键（50%存在，50%不存在）
        std::vector<int> keys_to_erase;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, size * 2);
        for (size_t i = 0; i < size; i++) {
            keys_to_erase.push_back(dist(gen));
        }
        
        std::cout << "\nTest data size: " << size << " elements\n";
        
        // 测试您的容器
        UnorderedMap<int, int> your_map;
        for (const auto& [k, v] : data) {
            your_map.try_emplace(k, v);
        }
        test_erase_performance("Your container", your_map, keys_to_erase);
        
        // 测试std容器
        std::unordered_map<int, int> std_map;
        for (const auto& [k, v] : data) {
            std_map.try_emplace(k, v);
        }
        test_erase_performance("std::unordered_map", std_map, keys_to_erase);
    }
}

template<typename MapType>
void test_iteration_performance(const std::string& name, 
                               MapType& map, 
                               size_t iteration_count) {
    auto start = std::chrono::high_resolution_clock::now();
    
    volatile size_t sum = 0;
    for (size_t i = 0; i < iteration_count; i++) {
        for (auto it = map.begin(); it != map.end(); ++it) {
            sum += it->second;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << name << " iteration time: " << duration << " ns ("
              << "container size: " << map.size() << ", "
              << "iteration count: " << iteration_count << ")\n";
}

void test_iteration_performance_comparison() {
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000};
    const size_t iteration_count = 10;
    
    std::cout << "\n=== Iteration Performance Test ===\n";
    
    for (size_t size : sizes) {
        auto data = generate_test_data(size);
        
        std::cout << "\nTest data size: " << size << " elements\n";
        
        // 准备两个容器
        UnorderedMap<int, int> your_map;
        std::unordered_map<int, int> std_map;
        
        for (const auto& [k, v] : data) {
            your_map.try_emplace(k, v);
            std_map.try_emplace(k, v);
        }
        
        // 预热缓存
        for (auto it = your_map.begin(); it != your_map.end(); ++it) {}
        for (auto it = std_map.begin(); it != std_map.end(); ++it) {}
        
        // 测试迭代性能
        test_iteration_performance("Your container", your_map, iteration_count);
        test_iteration_performance("std::unordered_map", std_map, iteration_count);
    }
}

template<typename MapType>
void test_clear_performance(const std::string& name, MapType& map) {
    auto start = std::chrono::high_resolution_clock::now();
    
    map.clear();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << name << " clear time: " << duration << " ns ("
              << "container size: " << map.size() << ")\n";
}

// 清除性能对比测试
void test_clear_performance_comparison() {
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000};
    
    std::cout << "\n=== Clear Performance Test ===\n";
    
    for (size_t size : sizes) {
        auto data = generate_test_data(size);
        
        std::cout << "\nTest data size: " << size << " elements\n";
        
        // 测试您的容器
        UnorderedMap<int, int> your_map;
        for (const auto& [k, v] : data) {
            your_map.try_emplace(k, v);
        }
        test_clear_performance("Your container", your_map);
        
        // 测试std容器
        std::unordered_map<int, int> std_map;
        for (const auto& [k, v] : data) {
            std_map.try_emplace(k, v);
        }
        test_clear_performance("std::unordered_map", std_map);
    }
}

int main() {
    UnorderedMap<int, int> m;
    for (size_t i = 0; i < 100; ++i) {
        m.try_emplace(i, i);
    }
    for (size_t i = 0; i < 100; ++i) {
        m.find(i);
    }
    for (size_t i = 0; i < 100; ++i) {
        m.erase(i);
    }
    // test correctness
    // test_correctness();
    // test_find_correctness();
    // test_erase_correctness();

    // // perfermance test
    // test_performance_comparison();        // insert
    // test_find_performance_comparison();   // find
    // test_erase_performance_comparison();  // erase
    // test_iteration_performance_comparison(); // iteration
    // test_clear_performance_comparison();  // clear
    
    // std::cout << "\nAll tests completed!\n";
    // return 0;
}