# swisstable

Implement the UnorderedMap and UnorderedSet containers based on Swisstable, has the same interface as STL(support mostly), and is 2 to 1000 times faster than STL.  

The implementation is more concise than the official one, and you can directly include the header file to use.



## compile

`g++ -O2 -std=c++17 ./perf_test.cpp -o perf_test`

## performance test

<table align="center" border="1" cellpadding="15">
  <!-- 表头：操作 + x86信息 + arm信息 -->
  <tr>
    <th></th>
    <th>
      <strong>x86_64</strong><br>
      - cpu: 16 Intel(R) Xeon(R) Gold 6161 CPU @ 2.20GHz<br>
      - memory: 64G
    </th>
    <th>
      <strong>ARM</strong><br>
      - cpu: Neoverse N2<br>
      - memory: 32G
    </th>
  </tr>

  <!-- 插入操作行 -->
  <tr>
    <td align="center" valign="middle"><strong>insert</strong></td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/insert_performance_log.png" width="500" height="400" alt="x86 insert" />
    </td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/insert_performance_log.png" width="500" height="400" alt="arm insert" />
    </td>
  </tr>

  <!-- 查找操作行 -->
  <tr>
    <td align="center" valign="middle"><strong>find</strong></td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/find_performance_log.png" width="500" height="400" alt="x86 find" />
    </td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/find_performance_log.png" width="500" height="400" alt="arm find" />
    </td>
  </tr>

  <!-- 删除操作行 -->
  <tr>
    <td align="center" valign="middle"><strong>erase</strong></td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/erase_performance_log.png" width="500" height="400" alt="x86 erase" />
    </td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/erase_performance_log.png" width="500" height="400" alt="arm erase" />
    </td>
  </tr>

  <!-- 清空操作行 -->
  <tr>
    <td align="center" valign="middle"><strong>clear</strong></td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/clear_performance_log.png" width="500" height="400" alt="x86 clear" />
    </td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/clear_performance_log.png" width="500" height="400" alt="arm clear" />
    </td>
  </tr>

  <!-- 迭代操作行 -->
  <tr>
    <td align="center" valign="middle"><strong>iterate</strong></td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/x86_64/iterate_performance_log.png" width="500" height="400" alt="x86 iterate" />
    </td>
    <td align="center">
      <img src="https://github.com/CedarSnowy/swisstable/blob/main/img/arm/iterate_performance_log.png" width="500" height="400" alt="arm iterate" />
    </td>
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
