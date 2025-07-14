#if defined(__x86_64__)
#include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#include <cstddef>
#include <cassert>
#include <cstring>
#include <iostream>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define InValidIdx SIZE_MAX

template <typename T, typename = void>
struct DefaultHasher {
    uint64_t operator()(const T &key) const noexcept {
        return static_cast<uint64_t>(std::hash<T>()(key));
    }
};

template <typename T>
struct DefaultHasher<T, typename std::enable_if<std::is_integral<T>::value>::type> {
    uint64_t operator()(const T &key) const noexcept {
        constexpr uint64_t prime = 0x9e3779b97f4a7c15;
        uint64_t k = static_cast<uint64_t>(key);
        k ^= k >> 33;
        k *= prime;
        k ^= k >> 29;
        return k;
    }
};

struct key_test {
    template <typename U>
    static auto test(int) -> decltype(std::declval<U>().key(), std::true_type());
    template <typename>
    static auto test(...) -> std::false_type;
};
template <typename T>
using has_key = decltype(key_test::template test<T>(0));
template <typename K, typename T> const K &get_key(const T &entry, std::true_type const &) { return entry.key(); }
template <typename K, typename T> const K &get_key(const T &entry, std::false_type const &) { return entry; }
template <typename K, typename T> const K &get_key(const T &entry) { return get_key<K, T>(entry, has_key<T>{}); }

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
    HashPair(const HashPair &other) : first(other.first), second(other.second) {}
    HashPair(HashPair &&other) : first(std::move(other.first)), second(std::move(other.second)) {}
    HashPair &operator=(const HashPair &other) = default;
    HashPair &operator=(HashPair &&other) = default;
    const Key &key() const { return first; }
    const Value &value() const { return second; }
    Value &value() { return second; }
    operator Key() const { return first; }
};


#define DEFAULT_ALLOCATOR std::allocator
#define FORCE_INLINE __attribute__((always_inline))

template <typename entry_base, typename key_type, 
          class Hasher = DefaultHasher<key_type>, 
          class KeyEqual = std::equal_to<entry_base>,
          class Allocator = DEFAULT_ALLOCATOR<entry_base>>
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
        size_t hash_func(const key_type &k) { return Hasher::operator()(k); }
        bool cmp(const key_type &k1, const key_type &k2) { return KeyEqual::operator()(k1, k2); }

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

    Ctrl *_ctrl;
    entry_base *_data;
    mutable _Store _store;
    size_t _capacity{0}; // must be 2^n - 1, thus lower bit is all 1, eg:127 = 0x0000 FFFF.

public:
    using data_type = entry_base;
    using allocator_type = Allocator;

    FlatTable(size_t capacity = default_capacity, const Hasher &hasher = Hasher(),
              const KeyEqual &equal = KeyEqual(), const Allocator &alloc = Allocator())
        :  _store(hasher, equal, alloc)
    {
        if (capacity == 0) {
            _capacity = default_capacity;
        } else {
            capacity = capacity / max_load_factor;
            _capacity = align_size(capacity);
        }
        size_t ctrl_size = sizeof(Ctrl) * (_capacity + group_size);
        size_t align_ctrl_size = align(ctrl_size, group_size);
        size_t slot_size = sizeof(entry_base) * _capacity;
        size_t all_size = align_ctrl_size + align(slot_size, group_size);
        _store._available = _capacity * max_load_factor;
        char *p = get_rebind_allocator<char>().allocate(all_size);
        _ctrl = (Ctrl *)p;
        _data = (entry_base *)(p + align_ctrl_size);
        memset(_ctrl, (int8_t)Ctrl::EMPTY, ctrl_size);
        _ctrl[_capacity] = Ctrl::END;
    }

    FlatTable(const FlatTable &other) : _store(other._store), _capacity(other._capacity) {
        size_t ctrl_size = sizeof(Ctrl) * (_capacity + group_size);
        size_t align_ctrl_size = align(ctrl_size, group_size);
        size_t slot_size = sizeof(entry_base) * _capacity;
        size_t all_size = align_ctrl_size + align(slot_size, group_size);
        char *p = get_rebind_allocator<char>().allocate(all_size);
        _ctrl = (Ctrl *)p;
        _data = (entry_base *)(p + align_ctrl_size);
        memcpy(_ctrl, other._ctrl, ctrl_size);
        for (size_t gidx = 0; gidx < _capacity; gidx += group_size) {
            size_t used_slot = match_full(gidx);
            while (used_slot) {
                size_t slot_idx = __builtin_ctz(used_slot);
                size_t true_idx = gidx + slot_idx;
                new (&_data[true_idx]) entry_base(other._data[true_idx]);
                used_slot &= used_slot - 1;
            }
        }
        _ctrl[_capacity] = Ctrl::END;
    }

    FlatTable(FlatTable &&other)
        : _ctrl(other._ctrl),
          _data(other._data),
          _store(std::move(other._store)),
          _capacity(other._capacity)
    {
        other._ctrl = NULL;
        other._data = NULL;
        other._store._available = 0;
        other._capacity = 0;
    }

    FlatTable &operator=(const FlatTable &other)
    {
        _capacity = other._capacity;
        _store = other._store;
        size_t ctrl_size = sizeof(Ctrl) * (_capacity + group_size);
        size_t align_ctrl_size = align(ctrl_size, group_size);
        size_t slot_size = sizeof(entry_base) * _capacity;
        size_t all_size = align_ctrl_size + align(slot_size, group_size);
        char *p = get_rebind_allocator<char>().allocate(all_size);
        _ctrl = (Ctrl *)p;
        _data = (entry_base *)(p + align_ctrl_size);
        memcpy(_ctrl, other._ctrl, ctrl_size);
        for (size_t gidx = 0; gidx < _capacity; gidx += group_size) {
            size_t used_slot = match_full(gidx);
            while (used_slot) {
                size_t slot_idx = __builtin_ctz(used_slot);
                size_t true_idx = gidx + slot_idx;
                new (&_data[true_idx]) entry_base(other._data[true_idx]);
                used_slot &= used_slot - 1;
            }
        }
        _ctrl[_capacity] = Ctrl::END;
    }

    FlatTable &operator=(FlatTable &&other) {
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
        memset(_ctrl, (int8_t)Ctrl::EMPTY, ctrl_size);
        _ctrl[_capacity] = Ctrl::END;
        _store._available = _capacity * max_load_factor;
    }

    size_t size() const { return (size_t)(_capacity * max_load_factor) - _store._available; }
    bool empty() const { return size() == 0; }

    FORCE_INLINE const Allocator &get_allocator() const { return _store; }
    FORCE_INLINE Allocator &get_allocator() { return _store; }
    template <class U>
    typename Allocator::template rebind<U>::other get_rebind_allocator() const {
        using rebound_alloc_type = typename Allocator::template rebind<U>::other;
        return rebound_alloc_type(get_allocator());
    }


    template <typename reference, typename pointer>
    struct iterator_template {
        using iterator_category = std::forward_iterator_tag;
        using value_type = entry_base;

        iterator_template(FlatTable *table, size_t group_idx, size_t slot_idx)
            : _table(table), _group_idx(group_idx), _slot_idx(slot_idx) {}
        iterator_template() : _table(NULL), _group_idx(InValidIdx), _slot_idx(InValidIdx) {}
        reference operator*() { return *get_data_ptr(); }
        pointer operator->() { return get_data_ptr(); }

        iterator_template &operator++() {
            if (unlikely(_group_idx == InValidIdx)) {
                return *this;
            }
            // search in current group
            uint16_t used_slot = _table->match_full(_group_idx);
            used_slot = (used_slot >> (_slot_idx + 1)) << (_slot_idx + 1);
            if (likely(used_slot)) {
                _slot_idx = __builtin_ctz(used_slot);
                if (unlikely(_slot_idx + _group_idx > _table->_capacity)) {
                    _table = NULL;
                    _slot_idx = InValidIdx;
                    _group_idx = InValidIdx;
                }
                return *this;
            }
            // search the next group
            for (size_t gidx = _group_idx + group_size; gidx < _table->_capacity; gidx += group_size) {
                uint16_t used_slot = _table->match_full(gidx);
                if (likely(used_slot)) {
                    _group_idx = gidx;
                    _slot_idx = __builtin_ctz(used_slot);
                    if (unlikely(_slot_idx + _group_idx > _table->_capacity)) {
                        _table = NULL;
                        _slot_idx = InValidIdx;
                        _group_idx = InValidIdx;
                    }
                    return *this;
                }
            }
            _table = NULL;
            _group_idx = InValidIdx;
            _slot_idx = InValidIdx;
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
        FlatTable *_table;
        size_t _group_idx; /* idx of a group in the whole _data, range[0, _capacity] */
        size_t _slot_idx; /* idx of a slot in a group, range [0, 15] */

        entry_base *get_data_ptr() const {
            if (_group_idx == InValidIdx || _slot_idx == InValidIdx) {
                return NULL;
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
                int slot_idx = __builtin_ctz(used_slot);
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
                int slot_idx = __builtin_ctz(used_slot);
                return const_iterator(const_cast<self_type *>(this), gidx, slot_idx);
            }
        }
        __builtin_unreachable();
    }

    iterator end() { return iterator(NULL, InValidIdx, InValidIdx); }
    const_iterator cend() const { return const_iterator(NULL, InValidIdx, InValidIdx); }

    iterator find(const key_type &k) {
        size_t hash_32 = get_hash(k);
        size_t hash_h1 = h1(hash_32);
        int8_t hash_h2 = h2(hash_32);
        size_t gidx = get_gidx(hash_h1);
        bool found;
        iterator it = find_internal<iterator>(k, gidx, hash_h2, found);
        if (found) {
            return it;
        } else {
            return end();
        }
    }

    const_iterator cfind(const key_type &k) const {
        size_t hash_32 = get_hash(k);
        size_t hash_h1 = h1(hash_32);
        int8_t hash_h2 = h2(hash_32);
        size_t gidx = get_gidx(hash_h1);
        bool found;
        const_iterator it = find_internal<const_iterator>(k, gidx, hash_h2, found);
        if (found) {
            return it;
        } else {
            return cend();
        }
    }

    bool contains(const key_type& key) { return cfind(key) != cend(); }

    template <typename ...Args>
    std::pair<iterator, bool> emplace(Args &&...args) {
        entry_base entry(std::forward<Args>(args)...);
        const key_type &k = get_key<key_type,entry_base>(entry);
        size_t hash_32 = get_hash(k);
        size_t hash_h1 = h1(hash_32);
        int8_t hash_h2 = h2(hash_32);
        size_t gidx = get_gidx(hash_h1);
        bool found;
        iterator it = find_internal<iterator>(k, gidx, hash_h2, found);
        if (found) {
            return {it, false};
        } else {
            if (should_grow()) {
                rehash(next_capacity(_capacity));
                gidx = get_gidx(hash_h1);
                it = find_internal<iterator>(k, gidx, hash_h2, found);
            }
            entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx, hash_h2);
            get_allocator().construct(entry_ptr, std::move(entry));
            return {it, true};
        }
    }

    iterator insert_reserve(key_type &&k, bool &found){
        size_t hash_32 = get_hash(k);
        size_t hash_h1 = h1(hash_32);
        int8_t hash_h2 = h2(hash_32);
        size_t gidx = get_gidx(hash_h1);
        iterator it = find_internal<iterator>(k, gidx, hash_h2, found);
        if (!found) {
            if (should_grow()) {
                rehash(next_capacity(_capacity));
                gidx = get_gidx(hash_h1);
                it = find_internal<iterator>(k, gidx, hash_h2, found);
            }
            entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx, hash_h2);
            memset((char *)entry_ptr + sizeof(key_type), 0, sizeof(entry_base) - sizeof(key_type));
            get_allocator().construct((key_type *)entry_ptr, std::move(k));
        }
        return it;
    }

    iterator insert_reserve(const key_type &k, bool &found){
        size_t hash_32 = get_hash(k);
        size_t hash_h1 = h1(hash_32);
        int8_t hash_h2 = h2(hash_32);
        size_t gidx = get_gidx(hash_h1);
        iterator it = find_internal<iterator>(k, gidx, hash_h2, found);
        if (!found) {
            if (should_grow()) {
                rehash(next_capacity(_capacity));
                gidx = get_gidx(hash_h1);
                it = find_internal<iterator>(k, gidx, hash_h2, found);
            }
            entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx, hash_h2);
            memset((char *)entry_ptr + sizeof(key_type), 0, sizeof(entry_base) - sizeof(key_type));
            get_allocator().construct((key_type *)entry_ptr, k);
        }
        return it;
    }

    std::pair<iterator, bool> insert(const entry_base &entry) {
        const key_type &k = get_key<key_type,entry_base>(entry);
        size_t hash_32 = get_hash(k);
        size_t hash_h1 = h1(hash_32);
        int8_t hash_h2 = h2(hash_32);
        size_t gidx = get_gidx(hash_h1);
        bool found;
        iterator it = find_internal<iterator>(k, gidx, hash_h2, found);
        if (found) {
            return {it, false};
        } else {
            if (should_grow()) {
                rehash(next_capacity(_capacity));
                gidx = get_gidx(hash_h1);
                it = find_internal<iterator>(k, gidx, hash_h2, found);
            }
            entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx, hash_h2);
            get_allocator().construct(entry_ptr, entry);
            return {it, true};
        }
    }

    std::pair<iterator, bool> insert(entry_base &&entry) {
        const key_type &k = get_key<key_type,entry_base>(entry);
        size_t hash_32 = get_hash(k);
        size_t hash_h1 = h1(hash_32);
        int8_t hash_h2 = h2(hash_32);
        size_t gidx = get_gidx(hash_h1);
        bool found;
        iterator it = find_internal<iterator>(k, gidx, hash_h2, found);
        if (found) {
            return {it, false};
        } else {
            if (should_grow()) {
                rehash(next_capacity(_capacity));
                gidx = get_gidx(hash_h1);
                it = find_internal<iterator>(k, gidx, hash_h2, found);
            }
            entry_base *entry_ptr = prepare_construct(it._group_idx, it._slot_idx, hash_h2);
            get_allocator().construct(entry_ptr, std::move(entry));
            return {it, true};
        }
    }
    
    iterator erase(iterator it) {
        if (it == end()) {
            return it;
        }
        ++_store._available;
        size_t true_idx = get_true_idx(it._group_idx, it._slot_idx);
        set_meta(true_idx, (int8_t)Ctrl::DELETED);
        entry_base *entry_ptr = &_data[true_idx];
        get_allocator().destroy(entry_ptr);
        ++it;
        return it;
    }

    const_iterator erase(const_iterator it) {
        if (it == cend()) {
            return it;
        }
        ++_store._available;
        size_t true_idx = get_true_idx(it._group_idx, it._slot_idx);
        set_meta(true_idx, (int8_t)Ctrl::DELETED);
        entry_base *entry_ptr = &_data[true_idx];
        get_allocator().destroy(entry_ptr);
        ++it;
        return it;
    }

    size_t erase(const key_type &k){
        iterator it = find(k);
        if (it == end()) {
            return 0;
        }
        ++_store._available;
        size_t true_idx = get_true_idx(it._group_idx, it._slot_idx);
        set_meta(true_idx, (int8_t)Ctrl::DELETED);
        entry_base *entry_ptr = &_data[true_idx];
        get_allocator().destroy(entry_ptr);
        return 1ul;
    }

    ~FlatTable() { destroy(); }

    void destroy() {
        destroy_used_slots();
        destroy_table();
    }

    template <typename Callback>
    void ctraverse(const_iterator start, const_iterator dest, Callback &&callback) {
        size_t start_true_idx = 0;
        size_t dest_true_idx = 0;
        if (start == cend()) {
            return;
        }
        start_true_idx = get_true_idx(start._group_idx, start._slot_idx); 
        if (dest == cend()) {
            dest_true_idx = _capacity;
        } else {
            dest_true_idx = get_true_idx(dest._group_idx, dest._slot_idx);
        }
        if (start_true_idx > dest_true_idx) {
            return;
        }
        size_t gidx = start_true_idx;
        uint16_t used_slot = 0;
        size_t sidx = 0;
        for (; gidx < dest_true_idx; gidx += group_size) {
            used_slot = match_full(gidx);
            while(used_slot) {
                sidx = __builtin_ctz(used_slot);
                if (unlikely(gidx + sidx > dest_true_idx) || unlikely(gidx + sidx == _capacity)) {
                    return;
                }
                entry_base &entry_ptr = _data[gidx + sidx];
                callback(entry_ptr);
                used_slot &= used_slot - 1;
            }
        }
        return;
    }

    template <typename Callback>
    void traverse(iterator start, iterator dest, Callback &&callback) {
        size_t start_true_idx = 0;
        size_t dest_true_idx = 0;
        if (start == end()) {
            return;
        }
        start_true_idx = get_true_idx(start._group_idx, start._slot_idx); 
        if (dest == end()) {
            dest_true_idx = _capacity;
        } else {
            dest_true_idx = get_true_idx(dest._group_idx, dest._slot_idx);
        }
        if (start_true_idx > dest_true_idx) {
            return;
        }
        size_t gidx = start_true_idx;
        uint16_t used_slot = 0;
        size_t sidx = 0;
        for (; gidx < dest_true_idx; gidx += group_size) {
            used_slot = match_full(gidx);
            while(used_slot) {
                sidx = __builtin_ctz(used_slot);
                if (unlikely(gidx + sidx > dest_true_idx) || unlikely(gidx + sidx == _capacity)) {
                    return;
                }
                entry_base &entry_ptr = _data[gidx + sidx];
                callback(entry_ptr);
                used_slot &= used_slot - 1;
            }
        }
        return;
    }

    void reserve(size_t size) { rehash(size); }

private:
    size_t align(size_t x, size_t a) { return (x + a - 1) & (~(a - 1)); }
    size_t get_hash(const key_type &key) const { return _store.hash_func(key); }
    bool cmp_key(const key_type &k1, const key_type &k2) const { return _store.cmp(k1, k2); }
    size_t h1(const size_t hash) const { return hash >> 7; }
    int8_t h2(const size_t hash) const { return (int8_t)(hash & 0x7F); }
    bool is_full(int8_t meta) const { return meta >= 0; }
    size_t next_capacity(size_t capacity) { return 2 * capacity + 1; }
    bool should_grow() { return _store._available == 0; }
    size_t get_groupnum() const { return _capacity / group_size + 1; }
    size_t get_gidx(const size_t hash_h1) const { return hash_h1 & _capacity; }

    size_t align_size(size_t size) {
        /* get the nearest 2^n - 1 */
        size_t v = size + 1;
        size_t n = v - 1;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        return n;
    }
    
    size_t get_allsize() {
        return align(sizeof(Ctrl) * (_capacity + group_size), group_size)
             + align(sizeof(entry_base) * _capacity, group_size);
    }

    /* idx of a slot in the whole _data. */
    size_t get_true_idx(size_t group_idx, size_t slot_idx) const {
        return (group_idx + slot_idx) & _capacity;
    }

    void set_meta(size_t true_idx, int8_t value) {
        _ctrl[true_idx] = (Ctrl)value;
        if (true_idx < group_size - 1) {
            _ctrl[true_idx + _capacity + 1] = (Ctrl)value;
        }
    }

    uint16_t match_full(size_t group_idx) const {
    #if defined(__x86_64__)
        __m128i group_meta = _mm_loadu_si128((__m128i *)(_ctrl + group_idx));
        uint16_t res = _mm_movemask_epi8(group_meta);
        return ~res;
    #elif defined(__ARM_NEON) || defined(__aarch64__)
        uint8x16_t group_meta = vld1q_u8(reinterpret_cast<const uint8_t*>(_ctrl + group_idx));

        static const uint8x16_t mask = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        uint8x16_t masked = vandq_u8(mask, (uint8x16_t)vshrq_n_s8((int8x16_t)group_meta, 7));
        uint8x16_t maskedhi = vextq_u8(masked, masked, 8);
        int16_t res = vaddvq_u16((uint16x8_t)vzip1q_u8(masked, maskedhi));
        return ~res;
    #else
        const int8_t* group_meta = _ctrl + group_idx;
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
        __m128i group_meta = _mm_loadu_si128((__m128i *)(_ctrl + group_idx));
        __m128i special = _mm_set1_epi8(static_cast<char>(Ctrl::END)); // 0xFF (-1)
        __m128i cmp_res = _mm_cmpgt_epi8(special, group_meta); // cannot swap order!!!!
        uint16_t res = _mm_movemask_epi8(cmp_res);
        return res;
    #elif defined(__ARM_NEON) || defined(__aarch64__)
        uint8x16_t group_meta = vld1q_u8(reinterpret_cast<const uint8_t*>(_ctrl + group_idx));
        uint8x16_t special = vdupq_n_u8(static_cast<char>(Ctrl::END));
        uint8x16_t cmp_res = vcgtq_s8(vreinterpretq_s8_u8(special), vreinterpretq_s8_u8(group_meta));

        static const uint8x16_t mask = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        uint8x16_t masked = vandq_u8(mask, (uint8x16_t)vshrq_n_s8((int8x16_t)cmp_res, 7));
        uint8x16_t maskedhi = vextq_u8(masked, masked, 8);
        int16_t res = vaddvq_u16((uint16x8_t)vzip1q_u8(masked, maskedhi));
        return (uint16_t)res;
    #else
        const int8_t *group_meta = (const int8_t *)_ctrl + group_idx;
        uint16_t res = 0;
        for (size_t i = 0; i < 16; ++i) {
            const int8_t meta = group_meta[i];
            if (meta < static_cast<int8_t>(Ctrl::END)) {
                res |= (1 << i);
            }
        }
        return res;
    #endif
    }
    
    uint16_t match_meta(size_t group_idx, int8_t key) const {
    #if defined(__x86_64__)
        __m128i key_128 = _mm_set1_epi8(key);
        __m128i group_meta = _mm_loadu_si128((__m128i *)(_ctrl + group_idx));
        __m128i cmp_res = _mm_cmpeq_epi8(group_meta, key_128);
        uint16_t res = _mm_movemask_epi8(cmp_res);
        return res;
    #elif defined(__ARM_NEON) || defined(__aarch64__)
        uint8x16_t key_128 = vdupq_n_u8(key);
        uint8x16_t group_meta = vld1q_u8(reinterpret_cast<const uint8_t*>(_ctrl + group_idx));
        uint8x16_t cmp_res = vceqq_u8(group_meta, key_128);

        static const uint8x16_t mask = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        uint8x16_t masked = vandq_u8(mask, (uint8x16_t)vshrq_n_s8((int8x16_t)cmp_res, 7));
        uint8x16_t maskedhi = vextq_u8(masked, masked, 8);
        int16_t res = vaddvq_u16((uint16x8_t)vzip1q_u8(masked, maskedhi));
        return (uint16_t)res;
    #else        
        const int8_t *group_meta = (const int8_t *)_ctrl + group_idx;
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
    // if the first slot is empty,can ensure the whole group is empty,
    // some time can help skip
    bool check_first(size_t group_idx) const {
        Ctrl first_meta = _ctrl[group_idx];
        return first_meta == Ctrl::EMPTY;
    }

    // used by find.
    // if exists, return the match iterator.
    // if not exists, return the iterator of an empty or delete slot.
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
                slot_idx = __builtin_ctz(mask);
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
            // current group not find key, try to find empty or delete.
            // if find, record and don't need to find in following loop.
            if (!record) {
                mask = match_empty_or_delete(group_idx);
                if (mask) {
                    record = true;
                    slot_idx = __builtin_ctz(mask);
                    it._table = const_cast<self_type *>(this);
                    it._group_idx = group_idx;
                    it._slot_idx = slot_idx;
                }
            }
        }
        found = false;
        return it;
    }

    entry_base *prepare_construct(size_t group_idx, size_t slot_idx, int8_t hash_h2) {
        size_t true_idx = get_true_idx(group_idx, slot_idx);
        set_meta(true_idx, hash_h2);
        --_store._available;
        return &_data[true_idx];
    }

    void insert_internal(entry_base &&entry, size_t group_main_idx, int8_t hash_h2) {
        size_t group_num = get_groupnum();
        for (size_t gidx = group_main_idx; gidx < group_main_idx + group_num * group_size; gidx += group_size) {
            size_t group_idx = get_true_idx(gidx, 0);
            uint16_t free_slot = match_empty_or_delete(group_idx);
            size_t slot_idx = 0;
            if (free_slot) {
                slot_idx = __builtin_ctz(free_slot);
                entry_base *entry_ptr = prepare_construct(group_idx, slot_idx, hash_h2);
                get_allocator().construct(entry_ptr, std::move(entry));
                return;
            }
        }
        assert(0);
        __builtin_unreachable();
    }


    // you can also use simd here, but will not be faster since most slot is full
    void rehash(size_t new_capacity) {
        FlatTable new_table = self_type (new_capacity, _store, _store, get_allocator());
        for (size_t i = 0; i < _capacity; ++i) {
            if (is_full((int8_t)_ctrl[i])) {
                entry_base &entry = _data[i];
                size_t hash_32 = get_hash(get_key<key_type>(entry));
                size_t hash_h1 = h1(hash_32);
                int8_t hash_h2 = h2(hash_32);
                size_t new_gidx = new_table.get_gidx(hash_h1);
                new_table.insert_internal(std::move(entry), new_gidx, hash_h2);
            }
        }
        destroy_table();
        swap(new_table);
    }

    void destroy_used_slots() {
        size_t true_idx = 0;
        uint16_t used_slot = 0;
        int slot_idx = 0;
        for (size_t gidx = 0; gidx < _capacity; gidx += group_size) {
            used_slot = match_full(gidx);
            while (used_slot) {
                slot_idx = __builtin_ctz(used_slot);
                true_idx = gidx + slot_idx;
                get_allocator().destroy(&_data[true_idx]); 
                used_slot &= used_slot - 1;
            }
        }
    }

    void destroy_table() {
        get_rebind_allocator<char>().deallocate((char *)_ctrl, get_allsize());
        _ctrl = NULL;
        _data = NULL;
        _capacity = 0;
        _store._available = 0;
    }
};


template <typename Key, typename Value,
          class Hasher = DefaultHasher<Key>, 
          class KeyEqual = std::equal_to<Key>,
          class Allocator = DEFAULT_ALLOCATOR<HashPair<Key, Value>>>
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
        super::get_allocator().construct(&it->value(), std::forward<Args>(args)...);
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
        super::get_allocator().construct(&it->value(), std::forward<Args>(args)...);
        return std::make_pair(it, true);
    }
};

template <typename T,
          class Hasher = DefaultHasher<T>, 
          class KeyEqual = std::equal_to<T>,
          class Allocator = DEFAULT_ALLOCATOR<T>>
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
          class Allocator = DEFAULT_ALLOCATOR<HashPair<Key, Value>>>
using UnorderedMap = HashTable<Key, Value, Hasher, KeyEqual, Allocator>;
template <typename T,
          class Hasher = DefaultHasher<T>, 
          class KeyEqual = std::equal_to<T>,
          class Allocator = DEFAULT_ALLOCATOR<T>>
using UnorderedSet = HashSet<T, Hasher, KeyEqual, Allocator>;

