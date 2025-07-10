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
        test_performance<UnorderedMap<int, int>>("swisstable", data);
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
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000, 10000000};
    
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
        test_find_performance("swisstable", your_map, keys_to_find);
        
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
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000, 10000000};
    
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
        test_erase_performance("swisstable", your_map, keys_to_erase);
        
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

template<typename MapType>
void test_iteration_performance_traverse(const std::string& name, 
                               MapType& map, 
                               size_t iteration_count) {
    auto start = std::chrono::high_resolution_clock::now();
    
    volatile size_t sum = 0;
    auto cal_sum = [&sum](HashPair<int, int> &pair) {
        sum += pair.second;
    };
    map.ctraverse(map.cbegin(), map.cend(), cal_sum);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << name << " traverse iteration time: " << duration << " ns ("
              << "container size: " << map.size() << ", "
              << "iteration count: " << iteration_count << ")\n";
}



void test_iteration_performance_comparison() {
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000, 10000000};
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
        test_iteration_performance_traverse("swisstable traverse", your_map, iteration_count);
        test_iteration_performance("swisstable operator ++", your_map, iteration_count);
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
    const std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000, 10000000};
    
    std::cout << "\n=== Clear Performance Test ===\n";
    
    for (size_t size : sizes) {
        auto data = generate_test_data(size);
        
        std::cout << "\nTest data size: " << size << " elements\n";
        
        // 测试您的容器
        UnorderedMap<int, int> your_map;
        for (const auto& [k, v] : data) {
            your_map.try_emplace(k, v);
        }
        test_clear_performance("swisstable", your_map);
        
        // 测试std容器
        std::unordered_map<int, int> std_map;
        for (const auto& [k, v] : data) {
            std_map.try_emplace(k, v);
        }
        test_clear_performance("std::unordered_map", std_map);
    }
}

enum class Operation { INSERT, FIND, ERASE, ITERATE, CLEAR };

void test_random_operations(size_t num_ops = 1000000, int key_range = 10000) {
    cout << "===== Randomized Test: " << num_ops << " operations =====" << endl;
    
    UnorderedMap<int, int> test_map;
    unordered_map<int, int> ref_map; // 参考映射
    mt19937 rng(random_device{}());
    uniform_int_distribution<int> key_dist(0, key_range);
    uniform_int_distribution<int> op_dist(0, 4); // 增加CLEAR操作
    
    size_t inserts = 0, finds = 0, erases = 0, iterates = 0, clears = 0;
    vector<string> op_history; // 记录操作历史
    
    // 错误处理函数
    auto log_error = [&](const string& error_msg) {
        cerr << "\nERROR: " << error_msg << endl;
        cerr << "Operation history (up to error):" << endl;
        for (size_t i = 0; i < op_history.size(); ++i) {
            cerr << "  Step " << i << ": " << op_history[i] << endl;
        }
        cerr << "FAIL" << endl;
        exit(1);
    };
    
    for (size_t i = 0; i < num_ops; ++i) {
        int key = key_dist(rng);
        Operation op = static_cast<Operation>(op_dist(rng));
        string op_desc;
        
        switch (op) {
            case Operation::INSERT: {
                op_desc = "INSERT key=" + to_string(key);
                op_history.push_back(op_desc);
                
                auto [test_it, test_inserted] = test_map.try_emplace(key, key);
                bool ref_inserted = ref_map.try_emplace(key, key).second;
                
                if (test_inserted != ref_inserted) {
                    log_error("Insert mismatch! Key: " + to_string(key) + 
                              " Test: " + (test_inserted ? "inserted" : "not inserted") + 
                              " Ref: " + (ref_inserted ? "inserted" : "not inserted"));
                }
                
                if (test_inserted) {
                    if (test_it->second != key) {
                        log_error("Insert value mismatch! Key: " + to_string(key) +
                                  " Value: " + to_string(test_it->second) +
                                  " Expected: " + to_string(key));
                    }
                }
                inserts++;
                break;
            }
                
            case Operation::FIND: {
                op_desc = "FIND key=" + to_string(key);
                op_history.push_back(op_desc);
                
                auto test_it = test_map.find(key);
                auto ref_it = ref_map.find(key);
                
                if ((test_it == test_map.end()) != (ref_it == ref_map.end())) {
                    log_error("Find presence mismatch! Key: " + to_string(key));
                }
                
                if (test_it != test_map.end() && ref_it != ref_map.end()) {
                    if (test_it->second != ref_it->second) {
                        log_error("Find value mismatch! Key: " + to_string(key) + 
                                 " Test: " + to_string(test_it->second) + 
                                 " Ref: " + to_string(ref_it->second));
                    }
                }
                finds++;
                break;
            }
                
            case Operation::ERASE: {
                op_desc = "ERASE key=" + to_string(key);
                op_history.push_back(op_desc);
                
                size_t test_erased = test_map.erase(key);
                size_t ref_erased = ref_map.erase(key);
                
                if (test_erased != ref_erased) {
                    log_error("Erase mismatch! Key: " + to_string(key) + 
                             " Test: " + to_string(test_erased) + 
                             " Ref: " + to_string(ref_erased));
                }
                erases++;
                break;
            }
                
            case Operation::ITERATE: {
                op_desc = "ITERATE";
                op_history.push_back(op_desc);
                
                unordered_map<int, int> test_elements;
                for (auto it = test_map.begin(); it != test_map.end(); ++it) {
                    if (test_elements.count(it->first)) {
                        log_error("Duplicate key in iteration: " + to_string(it->first));
                    }
                    test_elements[it->first] = it->second;
                }
                
                if (test_elements.size() != ref_map.size()) {
                    for (auto it = test_map.begin(); it != test_map.end(); ++it) {
                            cout << it->second <<" ";
                        }
                    cout << endl;
                    log_error("Size mismatch! Iteration: " + to_string(test_elements.size()) +
                              " Expected: " + to_string(ref_map.size()));
                }
                
                for (const auto& [k, v] : ref_map) {
                    if (!test_elements.count(k)) {
                        log_error("Missing key in iteration: " + to_string(k));
                    }
                    if (test_elements[k] != v) {
                        log_error("Value mismatch for key " + to_string(k) + 
                                 " Iteration: " + to_string(test_elements[k]) +
                                 " Expected: " + to_string(v));
                    }
                }
                iterates++;
                break;
            }
                
            case Operation::CLEAR: { // 添加CLEAR操作
                op_desc = "CLEAR";
                op_history.push_back(op_desc);
                
                test_map.clear();
                ref_map.clear();
                
                // 验证clear后的状态
                if (test_map.size() != 0) {
                    log_error("Size after clear should be 0, got " + to_string(test_map.size()));
                }
                if (test_map.begin() != test_map.end()) {
                    log_error("Begin should equal end after clear");
                }
                if (!test_map.empty()) {
                    log_error("Map should be empty after clear");
                }
                
                clears++;
                break;
            }
        }
        
        // 定期检查大小一致性
        if (i % 100 == 0) {
            if (test_map.size() != ref_map.size()) {
                log_error("Size mismatch at op " + to_string(i) + 
                         " Test: " + to_string(test_map.size()) + 
                         " Ref: " + to_string(ref_map.size()));
            }
        }
    }
    
    cout << "Operations summary:\n"
         << "  Inserts: " << inserts << "\n"
         << "  Finds: " << finds << "\n"
         << "  Erases: " << erases << "\n"
         << "  Iterates: " << iterates << "\n"
         << "  Clears: " << clears << endl; // 添加清除操作统计
    
    // 最终完整性检查
    unordered_map<int, int> final_elements;
    for (auto it = test_map.begin(); it != test_map.end(); ++it) {
        final_elements[it->first] = it->second;
    }
    
    if (final_elements.size() != ref_map.size()) {
        log_error("Final size mismatch! Test: " + to_string(final_elements.size()) + 
                 " Ref: " + to_string(ref_map.size()));
    }
    for (const auto& [k, v] : ref_map) {
        if (!final_elements.count(k)) {
            log_error("Missing key in final: " + to_string(k));
        }
        if (final_elements[k] != v) {
            log_error("Value mismatch for key " + to_string(k) + 
                     " Final: " + to_string(final_elements[k]) +
                     " Expected: " + to_string(v));
        }
    }
    
    // 额外测试clear函数
    cout << "Testing final clear..." << endl;
    test_map.clear();
    ref_map.clear();
    
    // 检查clear后的状态
    if (test_map.size() != 0) {
        log_error("Final clear: size should be 0, got " + to_string(test_map.size()));
    }
    if (test_map.begin() != test_map.end()) {
        log_error("Final clear: begin should equal end");
    }
    if (!test_map.empty()) {
        log_error("Final clear: map should be empty");
    }
    
    // 插入新元素验证结构仍然有效
    test_map.try_emplace(0, 0);
    if (test_map.size() != 1 || test_map.find(0) == test_map.end()) {
        log_error("Insertion failed after final clear");
    }
    
    cout << "ALL TESTS PASSED" << endl;
}

int main() {
    // correctness test
    test_random_operations();

    // perfermance test
    test_performance_comparison();        // insert
    test_find_performance_comparison();   // find
    test_erase_performance_comparison();  // erase
    test_iteration_performance_comparison(); // iteration
    test_clear_performance_comparison();  // clear
}