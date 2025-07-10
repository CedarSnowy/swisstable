# swisstable

Implement the UnorderedMap and UnorderedSet containers based on Swisstable, has the same interface as STL(support mostly), and is 2 to 1000 times faster than STL.  

The implementation is more concise than the official one, and you can directly include the header file to use.



## compile

`g++ -O2 -std=c++17 ./perf_test.cpp -o perf_test`

## test setting

### x86_64
- cpu:16  Intel(R) Xeon(R) Gold 6161 CPU @ 2.20GHz
- memory:64G

<table align="center">
  <tr>
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/insert_performance_log.png" width = "350" height = "300" alt="insert"  />
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/find_performance_log.png" width = "350" height = "300" alt="find"  />
  </tr>
</table>
<table align="center">
  <tr>
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/erase_performance_log.png" width = "400" height = "300" alt="erase"  />
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/clear_performance_log.png" width = "400" height = "300" alt="clear"  />
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/iterate_performance_log.png" width = "400" height = "300" alt="iterate"  />
  </tr>
</table>

### arm
- cpu:Neoverse N2
- memory:32G

<table align="center">
  <tr>
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/insert_performance_log.png" width = "350" height = "300" alt="insert"  />
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/find_performance_log.png" width = "350" height = "300" alt="find"  />
  </tr>
</table>
<table align="center">
  <tr>
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/erase_performance_log.png" width = "400" height = "300" alt="erase"  />
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/clear_performance_log.png" width = "400" height = "300" alt="clear"  />
<td><img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/iterate_performance_log.png" width = "400" height = "300" alt="iterate"  />
  </tr>
</table>

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

    size_t sum = 0;
    auto cal_sum = [&sum](HashPair<int, int> &pair) {
        sum += pair.second;
    };
    map.ctraverse(map.cbegin(), map.cend(), cal_sum);

    for (size_t i = 0; i < 100; ++i) {
        m.erase(i);
    }
}
```
## TODO
### merge c++20 pr
