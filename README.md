# swisstable

Implement the UnorderedMap and UnorderedSet containers based on Swisstable, has the same interface as STL(support mostly), and is 2 to 1000 times faster than STL.  

The implementation is more concise than the official one, and you can directly include the header file to use.



## compile

`g++ -O2 -std=c++17 ./perf_test.cpp -o perf_test`

## test setting

- cpu:16  Intel(R) Xeon(R) Gold 6161 CPU @ 2.20GHz
- memory:64G

<img src="https://github.com/CedarSnowy/swisstable/blob/main/cmp_res/insert_performance_log.png" width = "600" height = "500" alt="insert" align=center />
<img src="https://github.com/CedarSnowy/swisstable/blob/main/cmp_res/find_performance_log.png" width = "600" height = "500" alt="find" align=center />
<img src="https://github.com/CedarSnowy/swisstable/blob/main/cmp_res/erase_performance_log.png" width = "600" height = "500" alt="erase" align=center />
<img src="https://github.com/CedarSnowy/swisstable/blob/main/cmp_res/clear_performance_log.png" width = "600" height = "500" alt="clear" align=center />
<img src="https://github.com/CedarSnowy/swisstable/blob/main/cmp_res/iterate_performance_log.png" width = "600" height = "500" alt="iterate" align=center />


## usage

```c++
#include "swisstable.h"
int main() {
    UnorderedMap<int, int> m;
    for (size_t i = 0; i < 100; ++i) {
        m.try_empace(i, i);
    }
    for (size_t i = 0; i < 100; ++i) {
        m.find(i);
    }
    for (size_t i = 0; i < 100; ++i) {
        m.erase(i);
    }
}
```
## TODO
### add ARM support
