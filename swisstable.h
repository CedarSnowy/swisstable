#pragma once

#if defined(__x86_64__)
#include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

constexpr size_t kInvalidIdx = static_cast<size_t>(-1);

/* ---- C++20 / legacy (C++14/17) compatibility shims ---- */
#if defined(__cpp_concepts)
#define SWISSTABLE_HAS_CONCEPTS 1
#elif defined(_MSVC_LANG) ? (_MSVC_LANG >= 202002L) : (__cplusplus >= 202002L)
#define SWISSTABLE_HAS_CONCEPTS 1
#else
#define SWISSTABLE_HAS_CONCEPTS 0
#endif

#define ST_LIKELY(x) (__builtin_expect(!!(x), 1))
#define ST_UNLIKELY(x) (__builtin_expect(!!(x), 0))

#if defined(__cpp_lib_bitops)
#include <bit>
#endif
// portable trailing-zero count for 16-bit masks
static inline size_t ctz16(uint16_t x) {
#if defined(__cpp_lib_bitops)
    return static_cast<size_t>(std::countr_zero(x));
#else
    return static_cast<size_t>(__builtin_ctz(x));
#endif
}

template <typename T, typename = void>
struct DefaultHasher {
    uint64_t operator()(const T &key) const noexcept {
        return static_cast<uint64_t>(std::hash<T>{}(key));
    }
};

#if SWISSTABLE_HAS_CONCEPTS
template <typename T>
    requires std::is_integral_v<T>
struct DefaultHasher<T> {
#else
template <typename T>
struct DefaultHasher<T, std::enable_if_t<std::is_integral<T>::value>> {
#endif
    uint64_t operator()(const T &key) const noexcept {
        constexpr uint64_t prime = 0x9e3779b97f4a7c15;
        uint64_t k = static_cast<uint64_t>(key);
        k ^= k >> 33;
        k *= prime;
        k ^= k >> 29;
        return k;
    }
};

#if SWISSTABLE_HAS_CONCEPTS
template <typename T>
concept has_key = requires(const T &entry) { entry.key(); };

template <typename K, has_key T>
const K &get_key(const T &entry) { return entry.key(); }
template <typename K, typename T>
const K &get_key(const T &entry) { return entry; }
#else
// legacy SFINAE detection of entry.key()
template <typename T>
struct key_test {
    template <typename U>
    static auto test(int) -> decltype(std::declval<U>().key(), std::true_type());
    template <typename>
    static auto test(...) -> std::false_type;
};
template <typename T>
using has_key = decltype(key_test<T>::template test<T>(0));
template <typename K, typename T>
const K &get_key(const T &entry, std::true_type const &) { return entry.key(); }
template <typename K, typename T>
const K &get_key(const T &entry, std::false_type const &) { return entry; }
template <typename K, typename T>
const K &get_key(const T &entry) { return get_key<K, T>(entry, has_key<T>{}); }
#endif

template <typename Key, typename Value>
struct HashPair {
    /* stl pair interface compatible */
    Key first;
    Value second;
    HashPair(const Key &k, const Value &v) : first(k), second(v) {}
    HashPair(Key &&k, const Value &v) : first(std::move(k)), second(v) {}
    HashPair(const Key &k, Value &&v) : first(k), second(std::move(v)) {}
    HashPair(Key &&k, Value &&v) : first(std::move(k)), second(std::move(v)) {}
    template <typename ...Args>
    HashPair(const Key &k, Args &&...args) : first(k), second(std::forward<Args>(args)...) {}
    template <typename ...Args>
    HashPair(Key &&k, Args &&...args) : first(std::move(k)), second(std::forward<Args>(args)...) {}
    const Key &key() const { return first; }
    const Value &value() const { return second; }
    Value &value() { return second; }
    operator Key() const { return first; }
};

#define FORCE_INLINE __attribute__((always_inline))

template <typename entry_base, typename key_type,
          class Hasher = DefaultHasher<key_type>,
          class KeyEqual = std::equal_to<entry_base>,
          class Allocator = std::allocator<entry_base>>
class FlatTable : public Hasher, public KeyEqual, public Allocator {
    using self_type = FlatTable<entry_base, key_type, Hasher, KeyEqual, Allocator>;
    enum class Ctrl : int8_t {
        EMPTY = -128, // 0b1000 0000
        DELETED = -2, // 0b1111 1110
        END = -1, // 0b1111 1111
    };

    constexpr static size_t group_size = 16;
    constexpr static size_t default_capacity = 15u;
    constexpr static float max_load_factor = 0.875;

    struct _Store : public Hasher, public KeyEqual, public Allocator {
        size_t hash_func(const key_type &k) const { return Hasher::operator()(k); }
        bool cmp(const key_type &k1, const key_type &k2) const { return KeyEqual::operator()(k1, k2); }

        _Store(const Hasher &h, const KeyEqual &ke, const Allocator &a)
            : Hasher(h), KeyEqual(ke), Allocator(a) {}
        _Store(const _Store &) = default;
        _Store(_Store &&) = default;
        _Store &operator=(const _Store &) = default;
        _Store &operator=(_Store &&) = default;
        friend void swap(_Store &a, _Store &b) noexcept {
            using std::swap;
            swap(static_cast<Hasher &>(a), static_cast<Hasher &>(b));
            swap(static_cast<KeyEqual &>(a), static_cast<KeyEqual &>(b));
            swap(static_cast<Allocator &>(a), static_cast<Allocator &>(b));
            swap(a._available, b._available);
        }
        size_t _available{0};
    };

    Ctrl *_ctrl{nullptr};
    entry_base *_data{nullptr};
    mutable _Store _store;
    size_t _capacity{0}; // must be 2^n - 1, thus lower bit is all 1, eg:127 = 0x0000 FFFF.

public:
    using data_type = entry_base;
    using allocator_type = Allocator;

    FlatTable(size_t capacity = default_capacity, const Hasher &hasher = Hasher(),
              const KeyEqual &equal = KeyEqual(), const Allocator &alloc = Allocator())
        : _store(hasher, equal, alloc)
    {
        if (capacity == 0) {
            capacity = default_capacity;
        } else {
            capacity /= max_load_factor;
        }
        allocate_table(align_size(capacity));
    }

    FlatTable(const FlatTable &other) : _store(other._store) {
        allocate_table(other._capacity);
        copy_slots_from(other);
    }

    FlatTable(FlatTable &&other) noexcept
        : _ctrl(other._ctrl),
          _data(other._data),
          _store(std::move(other._store)),
          _capacity(other._capacity)
    {
        other._ctrl = nullptr;
        other._data = nullptr;
        other._store._available = 0;
        other._capacity = 0;
    }

    FlatTable &operator=(const FlatTable &other) {
        if (this != &other) {
            FlatTable tmp(other);
            destroy();
            swap(tmp);
        }
        return *this;
    }

    FlatTable &operator=(FlatTable &&other) noexcept {
        if (this != &other) {
            destroy();
            swap(other);
        }
        return *this;
    }

    void swap(FlatTable &other) noexcept {
        using std::swap;
        swap(_ctrl, other._ctrl);
        swap(_data, other._data);
        swap(_store, other._store);
        swap(_capacity, other._capacity);
    }

    void clear() {
        destroy_used_slots();
        size_t ctrl_size = sizeof(Ctrl) * (_capacity + group_size);
        memset(_ctrl, static_cast<int8_t>(Ctrl::EMPTY), ctrl_size);
        _ctrl[_capacity] = Ctrl::END;
        _store._available = _capacity * max_load_factor;
    }

    size_t size() const { return static_cast<size_t>(_capacity * max_load_factor) - _store._available; }
    bool empty() const { return size() == 0; }

    FORCE_INLINE const Allocator &get_allocator() const { return _store; }
    FORCE_INLINE Allocator &get_allocator() { return _store; }
    template <class U>
    typename std::allocator_traits<Allocator>::template rebind_alloc<U> get_rebind_allocator() const {
        using rebound_alloc_type = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;
        return rebound_alloc_type(get_allocator());
    }

    template <typename reference, typename pointer>
    struct iterator_template {
        using iterator_category = std::forward_iterator_tag;
        using value_type = entry_base;

        iterator_template(FlatTable *table, size_t group_idx, size_t slot_idx)
            : _table(table), _group_idx(group_idx), _slot_idx(slot_idx) {}
        iterator_template() : _table(nullptr), _group_idx(kInvalidIdx), _slot_idx(kInvalidIdx) {}

        // allow implicit iterator -> const_iterator conversion
        // (the reference_ indirection defers the enable_if until use)
        template <typename pointer2, typename reference_ = reference,
                  typename = std::enable_if_t<
                      std::is_const<typename std::remove_reference<reference_>::type>::value>>
        iterator_template(const iterator_template<entry_base &, pointer2> &other)
            : _table(other._table), _group_idx(other._group_idx), _slot_idx(other._slot_idx) {}

        reference operator*() { return *get_data_ptr(); }
        pointer operator->() { return get_data_ptr(); }

        iterator_template &operator++() {
            if (ST_UNLIKELY(_group_idx == kInvalidIdx)) {
                return *this;
            }
            // search in current group
            uint16_t used_slot = _table->match_full(_group_idx);
            used_slot = (used_slot >> (_slot_idx + 1)) << (_slot_idx + 1);
            if (ST_LIKELY(used_slot)) {
                _slot_idx = ctz16(used_slot);
                if (ST_UNLIKELY(_slot_idx + _group_idx > _table->_capacity)) {
                    invalidate();
                }
                return *this;
            }
            // search the next group
            for (size_t gidx = _group_idx + group_size; gidx < _table->_capacity; gidx += group_size) {
                uint16_t used_slot = _table->match_full(gidx);
                if (ST_LIKELY(used_slot)) {
                    _group_idx = gidx;
                    _slot_idx = ctz16(used_slot);
                    if (ST_UNLIKELY(_slot_idx + _group_idx > _table->_capacity)) {
                        invalidate();
                    }
                    return *this;
                }
            }
            invalidate();
            return *this;
        }

        iterator_template &operator--() = delete;
        bool operator==(const iterator_template &other) const {
            return _table == other._table && _group_idx == other._group_idx &&
                   _slot_idx == other._slot_idx;
        }
        bool operator!=(const iterator_template &other) const {
            return !operator==(other);
        }

    private:
        friend class FlatTable;
        template <typename, typename>
        friend struct iterator_template;

        FlatTable *_table;
        size_t _group_idx; /* idx of a group in the whole _data, range[0, _capacity] */
        size_t _slot_idx; /* idx of a slot in a group, range [0, 15] */

        void invalidate() {
            _table = nullptr;
            _group_idx = kInvalidIdx;
            _slot_idx = kInvalidIdx;
        }

        entry_base *get_data_ptr() const {
            if (_group_idx == kInvalidIdx || _slot_idx == kInvalidIdx) {
                return nullptr;
            }
            size_t true_idx = _table->get_true_idx(_group_idx, _slot_idx);
            return &(_table->_data[true_idx]);
        }
    };

    using iterator = iterator_template<entry_base &, entry_base *>;
    using const_iterator = iterator_template<const entry_base &, const entry_base *>;

    iterator begin() {
        if (empty()) {
            return end();
        }
        for (size_t gidx = 0; gidx < _capacity; gidx += group_size) {
            uint16_t used_slot = match_full(gidx);
            if (used_slot) {
                int slot_idx = ctz16(used_slot);
                return iterator(this, gidx, slot_idx);
            }
        }
        __builtin_unreachable();
    }

    const_iterator cbegin() const {
        if (empty()) {
            return cend();
        }
        for (size_t gidx = 0; gidx < _capacity; gidx += group_size) {
            uint16_t used_slot = match_full(gidx);
            if (used_slot) {
                int slot_idx = ctz16(used_slot);
                return const_iterator(const_cast<self_type *>(this), gidx, slot_idx);
            }
        }
        __builtin_unreachable();
    }

    iterator end() { return iterator(nullptr, kInvalidIdx, kInvalidIdx); }
    const_iterator cend() const { return const_iterator(nullptr, kInvalidIdx, kInvalidIdx); }

    iterator find(const key_type &k) {
        bool found = false;
        iterator it = find_internal<iterator>(k, get_gidx(h1(get_hash(k))), h2(get_hash(k)), found);
        return found ? it : end();
    }

    const_iterator cfind(const key_type &k) const {
        bool found = false;
        const_iterator it =
            find_internal<const_iterator>(k, get_gidx(h1(get_hash(k))), h2(get_hash(k)), found);
        return found ? it : cend();
    }

    bool contains(const key_type &key) const { return cfind(key) != cend(); }

    template <typename ...Args>
    std::pair<iterator, bool> emplace(Args &&...args) {
        entry_base entry(std::forward<Args>(args)...);
        bool found = false;
        iterator it = locate(get_key<key_type, entry_base>(entry), found);
        if (found) {
            return {it, false};
        }
        entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx, h2_of(get_key<key_type, entry_base>(entry)));
        std::allocator_traits<Allocator>::construct(get_allocator(), entry_ptr, std::move(entry));
        return {it, true};
    }

    iterator insert_reserve(key_type &&k, bool &found) {
        iterator it = locate(k, found);
        if (!found) {
            entry_base *entry_ptr = prepare_slot(it._group_idx, it._slot_idx, h2_of(k));
            memset(reinterpret_cast<char *>(entry_ptr) + sizeof(key_type), 0,
                   sizeof(entry_base) - sizeof(key_type));
            std::allocator_traits<Allocator>::construct(get_allocator(), reinterpret_cast<key_type *>(entry_ptr), std::move(k));
        }
        return it;
    }

    iterator insert_reserve(const key_type &k, bool &found) {
        iterator it = locate(k, found);
        if (!found) {
            entry_base *entry_ptr = prepare_slot(it._group_idx, it._slot_idx, h2_of(k));
            memset(reinterpret_cast<char *>(entry_ptr) + sizeof(key_type), 0,
                   sizeof(entry_base) - sizeof(key_type));
            std::allocator_traits<Allocator>::construct(get_allocator(), reinterpret_cast<key_type *>(entry_ptr), k);
        }
        return it;
    }

    std::pair<iterator, bool> insert(const entry_base &entry) {
        bool found = false;
        iterator it = locate(get_key<key_type, entry_base>(entry), found);
        if (found) {
            return {it, false};
        }
        entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx,
                                                  h2_of(get_key<key_type, entry_base>(entry)));
        std::allocator_traits<Allocator>::construct(get_allocator(), entry_ptr, entry);
        return {it, true};
    }

    std::pair<iterator, bool> insert(entry_base &&entry) {
        bool found = false;
        iterator it = locate(get_key<key_type, entry_base>(entry), found);
        if (found) {
            return {it, false};
        }
        entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx,
                                                  h2_of(get_key<key_type, entry_base>(entry)));
        std::allocator_traits<Allocator>::construct(get_allocator(), entry_ptr, std::move(entry));
        return {it, true};
    }

    iterator erase(iterator it) {
        if (it == end()) {
            return it;
        }
        erase_at(it._group_idx, it._slot_idx);
        ++it;
        return it;
    }

    const_iterator erase(const_iterator it) {
        if (it == cend()) {
            return it;
        }
        erase_at(it._group_idx, it._slot_idx);
        ++it;
        return it;
    }

    size_t erase(const key_type &k) {
        iterator it = find(k);
        if (it == end()) {
            return 0;
        }
        erase_at(it._group_idx, it._slot_idx);
        return 1;
    }

    ~FlatTable() { destroy(); }

    void destroy() {
        destroy_used_slots();
        destroy_table();
    }

    template <typename Iter, typename Callback>
    void traverse_impl(Iter start, Iter dest, Callback &&callback) {
        if (start == Iter(nullptr, kInvalidIdx, kInvalidIdx)) {
            return;
        }
        size_t start_true_idx = get_true_idx(start._group_idx, start._slot_idx);
        size_t dest_true_idx = (dest == Iter(nullptr, kInvalidIdx, kInvalidIdx))
                                   ? _capacity
                                   : get_true_idx(dest._group_idx, dest._slot_idx);
        if (start_true_idx > dest_true_idx) {
            return;
        }
        for (size_t gidx = start_true_idx; gidx < dest_true_idx; gidx += group_size) {
            uint16_t used_slot = match_full(gidx);
            while (used_slot) {
                size_t sidx = ctz16(used_slot);
                if (ST_UNLIKELY(gidx + sidx > dest_true_idx || gidx + sidx == _capacity)) {
                    return;
                }
                callback(_data[gidx + sidx]);
                used_slot &= used_slot - 1;
            }
        }
    }

    template <typename Callback>
    void ctraverse(const_iterator start, const_iterator dest, Callback &&callback) {
        traverse_impl(start, dest, std::forward<Callback>(callback));
    }

    template <typename Callback>
    void traverse(iterator start, iterator dest, Callback &&callback) {
        traverse_impl(start, dest, std::forward<Callback>(callback));
    }

    void reserve(size_t size) { rehash(size); }

private:
    static constexpr size_t align(size_t x, size_t a) { return (x + a - 1) & (~(a - 1)); }
    size_t get_hash(const key_type &key) const { return _store.hash_func(key); }
    bool cmp_key(const key_type &k1, const key_type &k2) const { return _store.cmp(k1, k2); }
    static constexpr size_t h1(const size_t hash) { return hash >> 7; }
    static constexpr int8_t h2(const size_t hash) { return static_cast<int8_t>(hash & 0x7F); }
    int8_t h2_of(const key_type &key) const { return h2(get_hash(key)); }
    static bool is_full(int8_t meta) { return meta >= 0; }
    static constexpr size_t next_capacity(size_t capacity) { return 2 * capacity + 1; }
    bool should_grow() const { return _store._available == 0; }
    size_t get_groupnum() const { return _capacity / group_size + 1; }
    size_t get_gidx(const size_t hash_h1) const { return hash_h1 & _capacity; }

    static size_t align_size(size_t size) {
        /* get the nearest 2^n - 1 */
        size_t v = size + 1;
        size_t n = v - 1;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n;
    }

    size_t get_allsize() const {
        return align(sizeof(Ctrl) * (_capacity + group_size), group_size)
             + align(sizeof(entry_base) * _capacity, group_size);
    }

    /* allocate raw memory for ctrl + data of the given capacity (2^n - 1) */
    void allocate_table(size_t capacity) {
        _capacity = capacity;
        size_t ctrl_size = sizeof(Ctrl) * (_capacity + group_size);
        size_t align_ctrl_size = align(ctrl_size, group_size);
        size_t slot_size = sizeof(entry_base) * _capacity;
        size_t all_size = align_ctrl_size + align(slot_size, group_size);
        _store._available = _capacity * max_load_factor;
        char *p = get_rebind_allocator<char>().allocate(all_size);
        _ctrl = reinterpret_cast<Ctrl *>(p);
        _data = reinterpret_cast<entry_base *>(p + align_ctrl_size);
        memset(_ctrl, static_cast<int8_t>(Ctrl::EMPTY), ctrl_size);
        _ctrl[_capacity] = Ctrl::END;
    }

    /* copy all used slots (already constructed entries) from another table */
    void copy_slots_from(const FlatTable &other) {
        _store._available = other._store._available;
        memcpy(_ctrl, other._ctrl, sizeof(Ctrl) * (_capacity + group_size));
        for (size_t gidx = 0; gidx < _capacity; gidx += group_size) {
            uint16_t used_slot = match_full(gidx);
            while (used_slot) {
                size_t true_idx = gidx + ctz16(used_slot);
                new (&_data[true_idx]) entry_base(other._data[true_idx]);
                used_slot &= used_slot - 1;
            }
        }
    }

    /* idx of a slot in the whole _data. */
    size_t get_true_idx(size_t group_idx, size_t slot_idx) const {
        return (group_idx + slot_idx) & _capacity;
    }

    void set_meta(size_t true_idx, int8_t value) {
        _ctrl[true_idx] = static_cast<Ctrl>(value);
        if (true_idx < group_size - 1) {
            _ctrl[true_idx + _capacity + 1] = static_cast<Ctrl>(value);
        }
    }

    uint16_t match_full(size_t group_idx) const {
    #if defined(__x86_64__)
        __m128i group_meta = _mm_loadu_si128(reinterpret_cast<const __m128i *>(_ctrl + group_idx));
        uint16_t res = _mm_movemask_epi8(group_meta);
        return static_cast<uint16_t>(~res);
    #elif defined(__ARM_NEON) || defined(__aarch64__)
        uint8x16_t group_meta = vld1q_u8(reinterpret_cast<const uint8_t *>(_ctrl + group_idx));

        static const uint8x16_t mask = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        uint8x16_t masked = vandq_u8(mask, reinterpret_cast<uint8x16_t>(vshrq_n_s8(reinterpret_cast<int8x16_t>(group_meta), 7)));
        uint8x16_t maskedhi = vextq_u8(masked, masked, 8);
        int16_t res = vaddvq_u16(reinterpret_cast<uint16x8_t>(vzip1q_u8(masked, maskedhi)));
        return static_cast<uint16_t>(~res);
    #else
        const int8_t *group_meta = _ctrl + group_idx;
        uint16_t res = 0;
        for (size_t i = 0; i < group_size; ++i) {
            if (group_meta[i] >= 0) {
                res |= (1 << i);
            }
        }
        return res;
    #endif
    }

    uint16_t match_empty_or_delete(size_t group_idx) const {
    #if defined(__x86_64__)
        __m128i group_meta = _mm_loadu_si128(reinterpret_cast<const __m128i *>(_ctrl + group_idx));
        __m128i special = _mm_set1_epi8(static_cast<char>(Ctrl::END)); // 0xFF (-1)
        __m128i cmp_res = _mm_cmpgt_epi8(special, group_meta); // cannot swap order!!!!
        return static_cast<uint16_t>(_mm_movemask_epi8(cmp_res));
    #elif defined(__ARM_NEON) || defined(__aarch64__)
        uint8x16_t group_meta = vld1q_u8(reinterpret_cast<const uint8_t *>(_ctrl + group_idx));
        uint8x16_t special = vdupq_n_u8(static_cast<uint8_t>(Ctrl::END));
        uint8x16_t cmp_res = vcgtq_s8(vreinterpretq_s8_u8(special), vreinterpretq_s8_u8(group_meta));

        static const uint8x16_t mask = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        uint8x16_t masked = vandq_u8(mask, reinterpret_cast<uint8x16_t>(vshrq_n_s8(reinterpret_cast<int8x16_t>(cmp_res), 7)));
        uint8x16_t maskedhi = vextq_u8(masked, masked, 8);
        int16_t res = vaddvq_u16(reinterpret_cast<uint16x8_t>(vzip1q_u8(masked, maskedhi)));
        return static_cast<uint16_t>(res);
    #else
        const int8_t *group_meta = _ctrl + group_idx;
        uint16_t res = 0;
        for (size_t i = 0; i < group_size; ++i) {
            if (group_meta[i] < static_cast<int8_t>(Ctrl::END)) {
                res |= (1 << i);
            }
        }
        return res;
    #endif
    }

    uint16_t match_meta(size_t group_idx, int8_t key) const {
    #if defined(__x86_64__)
        __m128i key_128 = _mm_set1_epi8(key);
        __m128i group_meta = _mm_loadu_si128(reinterpret_cast<const __m128i *>(_ctrl + group_idx));
        __m128i cmp_res = _mm_cmpeq_epi8(group_meta, key_128);
        return static_cast<uint16_t>(_mm_movemask_epi8(cmp_res));
    #elif defined(__ARM_NEON) || defined(__aarch64__)
        uint8x16_t key_128 = vdupq_n_u8(static_cast<uint8_t>(key));
        uint8x16_t group_meta = vld1q_u8(reinterpret_cast<const uint8_t *>(_ctrl + group_idx));
        uint8x16_t cmp_res = vceqq_u8(group_meta, key_128);

        static const uint8x16_t mask = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        uint8x16_t masked = vandq_u8(mask, reinterpret_cast<uint8x16_t>(vshrq_n_s8(reinterpret_cast<int8x16_t>(cmp_res), 7)));
        uint8x16_t maskedhi = vextq_u8(masked, masked, 8);
        int16_t res = vaddvq_u16(reinterpret_cast<uint16x8_t>(vzip1q_u8(masked, maskedhi)));
        return static_cast<uint16_t>(res);
    #else
        const int8_t *group_meta = _ctrl + group_idx;
        uint16_t res = 0;
        for (size_t i = 0; i < group_size; ++i) {
            if (group_meta[i] == key) {
                res |= (1 << i);
            }
        }
        return res;
    #endif
    }

    // check whether the first slot is empty.
    // if the first slot is empty, can ensure the whole group is empty,
    // sometimes can help skip probing
    bool check_first(size_t group_idx) const {
        return _ctrl[group_idx] == Ctrl::EMPTY;
    }

    // used by find.
    // if exists, return the match iterator.
    // if not exists, return the iterator of an empty or deleted slot.
    template <typename Iter>
    Iter find_internal(const key_type &key, size_t group_main_idx, int8_t hash_h2, bool &found) const {
        Iter it;
        bool record = false;
        size_t group_num = get_groupnum();
        for (size_t gidx = group_main_idx; gidx < group_main_idx + group_num * group_size; gidx += group_size) {
            size_t group_idx = get_true_idx(gidx, 0);
            size_t slot_idx = 0; // idx of the slot in a group, range [0, 15]
            size_t true_idx = 0; // idx of the slot in the whole _data.
            if (check_first(group_idx)) {
                found = false;
                if (!record) {
                    it._table = const_cast<self_type *>(this);
                    it._group_idx = group_idx;
                    it._slot_idx = 0;
                }
                return it;
            }
            uint16_t mask = match_meta(group_idx, hash_h2);
            while (mask) {
                slot_idx = ctz16(mask);
                true_idx = get_true_idx(group_idx, slot_idx);
                entry_base *entry_ptr = &_data[true_idx];
                if (cmp_key(get_key<key_type, entry_base>(*entry_ptr), key)) {
                    found = true;
                    it._table = const_cast<self_type *>(this);
                    it._group_idx = group_idx;
                    it._slot_idx = slot_idx;
                    return it;
                }
                mask &= mask - 1;
            }
            // current group doesn't contain the key, try to find empty or delete.
            // if found, record it and no need to search in following loops.
            if (!record) {
                mask = match_empty_or_delete(group_idx);
                if (mask) {
                    record = true;
                    it._table = const_cast<self_type *>(this);
                    it._group_idx = group_idx;
                    it._slot_idx = ctz16(mask);
                }
            }
        }
        found = false;
        return it;
    }

    // hash the key, find its slot; grow and re-find if the table is full.
    // if found, return the match iterator; otherwise return an empty/deleted slot.
    iterator locate(const key_type &k, bool &found) {
        size_t hash_32 = get_hash(k);
        size_t gidx = get_gidx(h1(hash_32));
        iterator it = find_internal<iterator>(k, gidx, h2(hash_32), found);
        if (!found && should_grow()) {
            rehash(next_capacity(_capacity));
            gidx = get_gidx(h1(hash_32));
            it = find_internal<iterator>(k, gidx, h2(hash_32), found);
        }
        return it;
    }

    // mark the slot used and bookkeep the load, without constructing anything.
    entry_base *prepare_slot(size_t group_idx, size_t slot_idx, int8_t hash_h2) {
        size_t true_idx = get_true_idx(group_idx, slot_idx);
        set_meta(true_idx, hash_h2);
        --_store._available;
        return &_data[true_idx];
    }

    entry_base *prepare_construct(size_t group_idx, size_t slot_idx, int8_t hash_h2) {
        return prepare_slot(group_idx, slot_idx, hash_h2);
    }

    void insert_internal(entry_base &&entry, size_t group_main_idx, int8_t hash_h2) {
        size_t group_num = get_groupnum();
        for (size_t gidx = group_main_idx; gidx < group_main_idx + group_num * group_size; gidx += group_size) {
            size_t group_idx = get_true_idx(gidx, 0);
            uint16_t free_slot = match_empty_or_delete(group_idx);
            if (free_slot) {
                size_t slot_idx = ctz16(free_slot);
                entry_base *entry_ptr = prepare_construct(group_idx, slot_idx, hash_h2);
                std::allocator_traits<Allocator>::construct(get_allocator(), entry_ptr, std::move(entry));
                return;
            }
        }
        assert(0);
        __builtin_unreachable();
    }

    // you can also use simd here, but it will not be faster since most slots are full
    void rehash(size_t new_capacity) {
        FlatTable new_table(new_capacity, _store, _store, get_allocator());
        for (size_t i = 0; i < _capacity; ++i) {
            if (is_full(static_cast<int8_t>(_ctrl[i]))) {
                entry_base &entry = _data[i];
                size_t hash_32 = get_hash(get_key<key_type>(entry));
                new_table.insert_internal(std::move(entry), new_table.get_gidx(h1(hash_32)), h2(hash_32));
            }
        }
        destroy_table();
        swap(new_table);
    }

    void erase_at(size_t group_idx, size_t slot_idx) {
        ++_store._available;
        size_t true_idx = get_true_idx(group_idx, slot_idx);
        set_meta(true_idx, static_cast<int8_t>(Ctrl::DELETED));
        std::allocator_traits<Allocator>::destroy(get_allocator(), &_data[true_idx]);
    }

    void destroy_used_slots() {
        for (size_t gidx = 0; gidx < _capacity; gidx += group_size) {
            uint16_t used_slot = match_full(gidx);
            while (used_slot) {
                std::allocator_traits<Allocator>::destroy(get_allocator(), &_data[gidx + ctz16(used_slot)]);
                used_slot &= used_slot - 1;
            }
        }
    }

    void destroy_table() {
        get_rebind_allocator<char>().deallocate(reinterpret_cast<char *>(_ctrl), get_allsize());
        _ctrl = nullptr;
        _data = nullptr;
        _capacity = 0;
        _store._available = 0;
    }
};


template <typename Key, typename Value,
          class Hasher = DefaultHasher<Key>,
          class KeyEqual = std::equal_to<Key>,
          class Allocator = std::allocator<HashPair<Key, Value>>>
class HashTable : public FlatTable<HashPair<Key, Value>, Key, Hasher, KeyEqual, Allocator> {
    using super = FlatTable<HashPair<Key, Value>, Key, Hasher, KeyEqual, Allocator>;
public:
    using super::super;
    using iterator = typename super::iterator;
    using super::operator=;

    Value &operator[](const Key &key)
        { bool unused; return this->insert_reserve(key, unused)->value(); }
    Value &operator[](Key &&key)
        { bool unused; return this->insert_reserve(std::move(key), unused)->value(); }
    template <typename ...Args>
    std::pair<iterator, bool> try_emplace(const Key &key, Args &&...args)
    {
        bool found;
        auto it = this->insert_reserve(key, found);
        if (found) {
            return std::make_pair(it, false);
        }
        std::allocator_traits<Allocator>::construct(this->get_allocator(), &it->value(), std::forward<Args>(args)...);
        return std::make_pair(it, true);
    }
    template <typename ...Args>
    std::pair<iterator, bool> try_emplace(Key &&key, Args &&...args)
    {
        bool found;
        auto it = this->insert_reserve(std::move(key), found);
        if (found) {
            return std::make_pair(it, false);
        }
        std::allocator_traits<Allocator>::construct(this->get_allocator(), &it->value(), std::forward<Args>(args)...);
        return std::make_pair(it, true);
    }
};

template <typename T,
          class Hasher = DefaultHasher<T>,
          class KeyEqual = std::equal_to<T>,
          class Allocator = std::allocator<T>>
class HashSet : public FlatTable<T, T, Hasher, KeyEqual, Allocator> {
    using super = FlatTable<T, T, Hasher, KeyEqual, Allocator>;
public:
    using super::super;
    template <typename InputIt>
    HashSet(InputIt first, InputIt last, const Allocator &alloc = Allocator())
        : super(std::distance(first, last), alloc)
    {
        for (auto it = first; it != last; ++it) {
            this->emplace(*it);
        }
    }
    using iterator = typename super::iterator;
    using const_iterator = typename super::const_iterator;
    using super::operator=;
};


template <typename Key, typename Value,
          class Hasher = DefaultHasher<Key>,
          class KeyEqual = std::equal_to<Key>,
          class Allocator = std::allocator<HashPair<Key, Value>>>
using UnorderedMap = HashTable<Key, Value, Hasher, KeyEqual, Allocator>;
template <typename T,
          class Hasher = DefaultHasher<T>,
          class KeyEqual = std::equal_to<T>,
          class Allocator = std::allocator<T>>
using UnorderedSet = HashSet<T, Hasher, KeyEqual, Allocator>;
