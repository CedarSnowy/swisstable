# swisstable

Implement the UnorderedMap and UnorderedSet containers based on Swisstable in a streamlined way, has the same interface as STL(support mostly), and is 2 to 5 times faster than STL.

## compile

`g++ -O2 -std=c++17 ./perf_test.cpp -o perf_test`

## test setting

- cpu:16  Intel(R) Xeon(R) Gold 6161 CPU @ 2.20GHz
- memory:64G

## usage

```c++
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
