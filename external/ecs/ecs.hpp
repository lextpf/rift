/*  ============================================================================================  *
 *                                                             ⠀   ⠀
 *                                                             ⠀
 *
 *
 *                      :::::::::: ::::::::   ::::::::
 *                      :+:       :+:    :+: :+:    :+:    ⠀⠀⢀⣴⣾⠿⠿⠿⠿⠿⠿⠿⠿⢷⣦⡀⠀⠀
 *                      +:+       +:+        +:+           ⠀⢠⡿⠏⢠⣸⣧⡀⠀⠀⢀⠰⢆⡄⠙⢿⡄⠀
 *                      +#++:++#  +#+        +#++:++#++    ⠀⣿⡇⠀⠙⢻⡿⠁⠀⠀⠈⢡⡌⠏⠀⢸⣿⠀
 *                      +#+       +#+               +#+    ⢠⣿⠃⠀⠀⠀⣀⣀⣀⣀⣀⣀⠀⠀⠀⠘⣿⡆
 *                      #+#       #+#    #+# #+#    #+#    ⢸⣿⠀⠀⠀⣼⡟⠉⠉⠉⠉⢻⣧⠀⠀⠀⣿⡇
 *                      ########## ########   ########     ⠈⠻⣶⣶⡾⠏⠀⠀⠀⠀⠀⠀⠹⣷⣶⣶⠟⠁
 *                                                                ⠀⠀⠀
 *                                                                ⠀⠀⠀⠀
 *                                  << E C S   L I B R A R Y >>   ⠀⠀⠀
 *
 *  ============================================================================================  *
 *
 *      A single-header C++23 Entity Component System for real-time
 *      games and simulation: plain-struct components, cache-friendly
 *      view queries, and message-driven systems. No RTTI, no exceptions.
 *
 *    ----------------------------------------------------------------------
 *
 *      Repository:   https://github.com/lextpf/ecs
 *      License:      MIT
 */

#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef ECS_CHECKS
#ifdef NDEBUG
#define ECS_CHECKS 0
#else
#define ECS_CHECKS 1
#endif
#endif

namespace ecs
{

inline constexpr bool checks_enabled = (ECS_CHECKS != 0);

template <class Traits>
class basic_registry;

template <class Traits>
class basic_command_buffer;

template <class Traits>
class basic_blueprint;

template <class Traits>
class basic_pool_ref;

template <class Traits>
class basic_runtime_selection;

template <class Traits>
class basic_scoped_hook;

template <class W>
class basic_entity_filler;

template <class Traits, class Filter, class... Ts>
class basic_view;

struct test_access;

namespace detail
{
template <class Traits>
struct basic_kin;
}

using violation_handler = void (*)(const char* message);

namespace detail
{
inline void default_violation(const char* message)
{
    std::fputs("ecs violation: ", stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

inline std::atomic<violation_handler>& violation_slot() noexcept
{
    static std::atomic<violation_handler> slot{&default_violation};
    return slot;
}

inline void violate(const char* message)
{
    violation_slot().load(std::memory_order_relaxed)(message);
}

inline void violate_pool(const char* what, std::string_view pool_name)
{
    thread_local char buffer[192];
    const int n = std::snprintf(buffer,
                                sizeof(buffer),
                                "%s '%.*s'",
                                what,
                                static_cast<int>(pool_name.size()),
                                pool_name.data());
    violate(n > 0 ? buffer : what);
}
}  // namespace detail

inline violation_handler set_violation_handler(violation_handler handler) noexcept
{
    if (handler == nullptr)
    {
        handler = &detail::default_violation;
    }
    return detail::violation_slot().exchange(handler, std::memory_order_relaxed);
}

template <auto Value>
using constant = std::integral_constant<decltype(Value), Value>;

template <class... Ts>
struct types
{
    using self = types;
    static constexpr std::size_t size = sizeof...(Ts);
};

template <class T>
inline constexpr bool is_types_v = false;

template <class... Ts>
inline constexpr bool is_types_v<types<Ts...>> = true;

template <class T>
concept type_list = is_types_v<std::remove_cvref_t<T>>;

template <std::size_t I, class List>
struct type_at;

template <std::size_t I, class T, class... Rest>
struct type_at<I, types<T, Rest...>> : type_at<I - 1, types<Rest...>>
{
};

template <class T, class... Rest>
struct type_at<0, types<T, Rest...>>
{
    using type = T;
};

template <std::size_t I, class List>
using type_at_t = type_at<I, List>::type;

template <class List>
using front_t = type_at_t<0, List>;

template <class List>
using back_t = type_at_t<List::size - 1, List>;

template <class T, class List>
inline constexpr bool contains_type = false;

template <class T, class... Ts>
inline constexpr bool contains_type<T, types<Ts...>> = (std::same_as<T, Ts> || ...);

template <class T, class... Ts>
[[nodiscard]] consteval std::size_t index_of(types<Ts...>) noexcept
{
    static_assert((std::same_as<T, Ts> || ...), "ecs: T is not in this types<> list");
    constexpr bool hits[]{std::same_as<T, Ts>...};
    for (std::size_t i = 0; i < sizeof...(Ts); ++i)
    {
        if (hits[i])
        {
            return i;
        }
    }
    return sizeof...(Ts);
}

template <class... Lists>
struct joined
{
    using type = types<>;
};

template <class... Ts>
struct joined<types<Ts...>>
{
    using type = types<Ts...>;
};

template <class... Ts, class... Us, class... Rest>
struct joined<types<Ts...>, types<Us...>, Rest...> : joined<types<Ts..., Us...>, Rest...>
{
};

template <class... Lists>
using joined_t = joined<Lists...>::type;

template <class List, class... Drop>
struct without;

template <class... Ts, class... Drop>
struct without<types<Ts...>, Drop...>
{
    template <class T>
    using keep_one = std::conditional_t<(std::same_as<T, Drop> || ...), types<>, types<T>>;
    using type = joined_t<types<>, keep_one<Ts>...>;
};

template <class List, class... Drop>
using without_t = without<List, Drop...>::type;

template <class List>
struct distinct;

template <>
struct distinct<types<>>
{
    using type = types<>;
};

template <class T, class... Rest>
struct distinct<types<T, Rest...>>
{
    using type = joined_t<types<T>, typename distinct<without_t<types<Rest...>, T>>::type>;
};

template <class List>
using distinct_t = distinct<List>::type;

template <class List>
inline constexpr bool all_unique_v = (distinct_t<List>::size == List::size);

template <class Sub, class Super>
inline constexpr bool is_subset_v = false;

template <class... Ts, class Super>
inline constexpr bool is_subset_v<types<Ts...>, Super> = (contains_type<Ts, Super> && ...);

template <class A, class B>
struct intersected;

template <class... Ts, class B>
struct intersected<types<Ts...>, B>
{
    template <class T>
    using keep_one = std::conditional_t<contains_type<T, B>, types<T>, types<>>;
    using type = joined_t<types<>, keep_one<Ts>...>;
};

template <class A, class B>
using intersection_t = intersected<A, B>::type;

template <class A, class B>
struct subtracted;

template <class... As, class... Bs>
struct subtracted<types<As...>, types<Bs...>>
{
    using type = without_t<types<As...>, Bs...>;
};

template <class A, class B>
using difference_t = subtracted<A, B>::type;

template <template <class> class Op, class List>
struct mapped;

template <template <class> class Op, class... Ts>
struct mapped<Op, types<Ts...>>
{
    using type = types<typename Op<Ts>::type...>;
};

template <template <class> class Op, class List>
using mapped_t = mapped<Op, List>::type;

template <template <class> class Pred, class List>
inline constexpr bool all_of_v = false;

template <template <class> class Pred, class... Ts>
inline constexpr bool all_of_v<Pred, types<Ts...>> = (Pred<Ts>::value && ...);

template <template <class> class Pred, class List>
inline constexpr bool any_of_v = false;

template <template <class> class Pred, class... Ts>
inline constexpr bool any_of_v<Pred, types<Ts...>> = (Pred<Ts>::value || ...);

template <template <class> class Pred, class List>
inline constexpr bool none_of_v = false;

template <template <class> class Pred, class... Ts>
inline constexpr bool none_of_v<Pred, types<Ts...>> = (!Pred<Ts>::value && ...);

template <template <class> class Pred, class List>
inline constexpr std::size_t count_if_v = 0;

template <template <class> class Pred, class... Ts>
inline constexpr std::size_t count_if_v<Pred, types<Ts...>> =
    (std::size_t{0} + ... + (Pred<Ts>::value ? std::size_t{1} : std::size_t{0}));

template <template <class> class Pred, class List>
struct filtered;

template <template <class> class Pred, class... Ts>
struct filtered<Pred, types<Ts...>>
{
    template <class T>
    using keep_one = std::conditional_t<Pred<T>::value, types<T>, types<>>;
    using type = joined_t<types<>, keep_one<Ts>...>;
};

template <template <class> class Pred, class List>
using filter_t = filtered<Pred, List>::type;

template <template <class> class Pred, class... Ts>
[[nodiscard]] consteval std::size_t find_if(types<Ts...>) noexcept
{
    if constexpr (sizeof...(Ts) == 0)
    {
        return 0;
    }
    else
    {
        constexpr bool hits[]{Pred<Ts>::value...};
        for (std::size_t i = 0; i < sizeof...(Ts); ++i)
        {
            if (hits[i])
            {
                return i;
            }
        }
        return sizeof...(Ts);
    }
}

template <class... Ts, class F>
constexpr void for_each(types<Ts...>, F&& fn)
{
    (fn.template operator()<Ts>(), ...);
}

template <auto... Vs>
struct values
{
    using self = values;
    static constexpr std::size_t size = sizeof...(Vs);
};

template <std::size_t I, auto First, auto... Rest>
[[nodiscard]] consteval auto value_at(values<First, Rest...>) noexcept
{
    static_assert(I <= sizeof...(Rest), "ecs: values<> index out of range");
    if constexpr (I == 0)
    {
        return First;
    }
    else
    {
        return value_at<I - 1>(values<Rest...>{});
    }
}

template <auto V, class List>
inline constexpr bool contains_value = false;

template <auto V, auto... Vs>
inline constexpr bool contains_value<V, values<Vs...>> = ((V == Vs) || ...);

template <auto V, auto... Vs>
[[nodiscard]] consteval std::size_t index_of_value(values<Vs...>) noexcept
{
    static_assert(((V == Vs) || ...), "ecs: V is not in this values<> list");
    constexpr bool hits[]{(V == Vs)...};
    for (std::size_t i = 0; i < sizeof...(Vs); ++i)
    {
        if (hits[i])
        {
            return i;
        }
    }
    return sizeof...(Vs);
}

namespace detail
{
inline constexpr std::uint32_t npos32 = 0xFFFFFFFFu;
inline constexpr std::uint32_t provisional_bit = 0x80000000u;
}  // namespace detail

struct default_entity_traits
{
    using index_type = std::uint32_t;
    using generation_type = std::uint32_t;
    static constexpr std::uint32_t index_bits = 31;
};

template <class Traits>
concept entity_traits =
    std::unsigned_integral<typename Traits::index_type> &&
    std::unsigned_integral<typename Traits::generation_type> && (Traits::index_bits >= 1) &&
    (Traits::index_bits <
     static_cast<std::uint32_t>(std::numeric_limits<typename Traits::index_type>::digits)) &&
    (Traits::index_bits + static_cast<std::uint32_t>(
                              std::numeric_limits<typename Traits::generation_type>::digits) <=
     64);

namespace detail
{
template <entity_traits Traits>
struct entity_limits
{
    using index_type = typename Traits::index_type;
    static constexpr index_type npos = std::numeric_limits<index_type>::max();
    static constexpr index_type provisional_bit = index_type{1} << Traits::index_bits;
    static constexpr index_type max_slots = provisional_bit - 1;
};
}  // namespace detail

template <entity_traits Traits>
class basic_entity
{
public:
    using traits_type = Traits;
    using index_type = typename Traits::index_type;
    using generation_type = typename Traits::generation_type;

    static constexpr std::uint32_t index_bits = Traits::index_bits;
    static constexpr std::uint32_t generation_bits =
        static_cast<std::uint32_t>(std::numeric_limits<generation_type>::digits);

    constexpr basic_entity() noexcept = default;

    constexpr basic_entity(index_type index, generation_type generation) noexcept
        : index_(index),
          generation_(generation)
    {
    }

    [[nodiscard]] constexpr index_type index() const noexcept { return index_; }
    [[nodiscard]] constexpr generation_type generation() const noexcept { return generation_; }

    [[nodiscard]] constexpr std::uint64_t bits() const noexcept
    {
        return (static_cast<std::uint64_t>(index_) << generation_bits) |
               static_cast<std::uint64_t>(generation_);
    }

    constexpr explicit operator bool() const noexcept
    {
        return index_ != detail::entity_limits<Traits>::npos;
    }

    friend constexpr bool operator==(basic_entity, basic_entity) noexcept = default;
    friend constexpr auto operator<=>(basic_entity, basic_entity) noexcept = default;

private:
    index_type index_ = detail::entity_limits<Traits>::npos;
    generation_type generation_ = 0;
};

using entity = basic_entity<default_entity_traits>;

inline constexpr entity no_entity{};

static_assert(sizeof(entity) == 8);
static_assert(entity::index_bits == 31 && entity::generation_bits == 32);
static_assert(entity{5, 7}.bits() == ((std::uint64_t{5} << 32) | 7));
static_assert(!entity{} && entity{0, 0}.index() == 0);
static_assert(detail::entity_limits<default_entity_traits>::npos == detail::npos32);
static_assert(detail::entity_limits<default_entity_traits>::provisional_bit ==
              detail::provisional_bit);

namespace detail
{
template <entity_traits Traits>
[[nodiscard]] constexpr bool is_provisional(basic_entity<Traits> e) noexcept
{
    return (e.index() & entity_limits<Traits>::provisional_bit) != 0 &&
           e.index() != entity_limits<Traits>::npos;
}
}  // namespace detail

enum class storage : std::uint8_t
{
    packed,
    stable,
    tag,
};

namespace detail
{
template <class T>
concept has_storage_member = requires { T::ecs_storage; };

template <class T>
consteval storage default_storage()
{
    if constexpr (has_storage_member<T>)
    {
        static_assert(std::convertible_to<decltype(T::ecs_storage), storage>,
                      "ecs: T::ecs_storage must be a ecs::storage value");
        return T::ecs_storage;
    }
    else if constexpr (std::is_empty_v<T>)
    {
        return storage::tag;
    }
    else
    {
        return storage::packed;
    }
}
}  // namespace detail

template <class T>
inline constexpr storage storage_policy = detail::default_storage<T>();

namespace detail
{
template <class T>
concept has_chunk_member = requires {
    { T::ecs_chunk_items } -> std::convertible_to<std::size_t>;
};

template <class T>
consteval std::size_t default_chunk_items()
{
    if constexpr (has_chunk_member<T>)
    {
        return T::ecs_chunk_items;
    }
    else
    {
        return std::clamp<std::size_t>(4096 / sizeof(T), 4, 1024);
    }
}
}  // namespace detail

template <class T>
inline constexpr std::size_t chunk_capacity = detail::default_chunk_items<T>();

template <class T>
concept component = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> &&
                    !std::is_volatile_v<T> && std::is_destructible_v<T>;

namespace detail
{
template <class T>
using bare = std::remove_const_t<T>;

template <class T>
inline constexpr bool is_tag_v = storage_policy<bare<T>> == storage::tag;
}  // namespace detail

namespace detail
{
template <class T>
constexpr std::string_view raw_type_string() noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
    return {__FUNCSIG__};
#else
    return {__PRETTY_FUNCTION__};
#endif
}

constexpr std::string_view strip_type_keyword(std::string_view name) noexcept
{
    for (std::string_view keyword : {"struct ", "class ", "enum ", "union "})
    {
        if (name.starts_with(keyword))
        {
            name.remove_prefix(keyword.size());
            break;
        }
    }
    return name;
}

constexpr std::string_view extract_type_name(std::string_view raw) noexcept
{
    // gcc:   "... raw_type_string() [with T = Foo; ...]"
    // clang: "... raw_type_string() [T = Foo]"
    // msvc:  "... raw_type_string<struct Foo>(void) ..."
    if (std::size_t begin = raw.find("[with T = "); begin != std::string_view::npos)
    {
        begin += 10;
        return raw.substr(begin, raw.find_first_of(";]", begin) - begin);
    }
    if (std::size_t begin = raw.find("[T = "); begin != std::string_view::npos)
    {
        begin += 5;
        return raw.substr(begin, raw.find_first_of(";]", begin) - begin);
    }
    if (std::size_t begin = raw.find("raw_type_string<"); begin != std::string_view::npos)
    {
        begin += 16;
        return strip_type_keyword(raw.substr(begin, raw.rfind('>') - begin));
    }
    return "unknown-type";
}
}  // namespace detail

template <class T>
inline constexpr std::string_view component_label{};

namespace detail
{
template <class T>
concept has_label_member = requires {
    { T::ecs_label } -> std::convertible_to<std::string_view>;
};
}  // namespace detail

template <class T>
[[nodiscard]] constexpr std::string_view name_of() noexcept
{
    if constexpr (detail::has_label_member<T>)
    {
        static_assert(
            requires { typename std::integral_constant<std::size_t, T::ecs_label.size()>; },
            "ecs: ecs_label must be a constexpr std::string_view");
        static_assert(!std::string_view{T::ecs_label}.empty(),
                      "ecs: ecs_label must not be empty (omit it for the "
                      "compiler-derived name)");
        return T::ecs_label;
    }
    else if constexpr (!component_label<T>.empty())
    {
        return component_label<T>;
    }
    else
    {
        constexpr std::string_view name = detail::extract_type_name(detail::raw_type_string<T>());
        return name;
    }
}

namespace detail
{
constexpr std::uint64_t fnv1a(std::string_view text) noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const char c : text)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}
}  // namespace detail

template <class T>
[[nodiscard]] constexpr std::uint64_t hash_of() noexcept
{
    constexpr std::uint64_t hash = detail::fnv1a(name_of<T>());
    return hash;
}

[[nodiscard]] constexpr std::uint64_t string_id(std::string_view text) noexcept
{
    return detail::fnv1a(text);
}

namespace detail
{
inline std::uint32_t next_type_id() noexcept
{
    static std::atomic<std::uint32_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

template <class T>
inline std::uint32_t type_id() noexcept
{
    static const std::uint32_t id = next_type_id();
    return id;
}

inline std::uint32_t next_buffer_nonce() noexcept
{
    static std::atomic<std::uint32_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace detail

enum class fault_code : std::uint8_t
{
    bad_handle,
    slot_occupied,
    table_out_of_sync,
    sparse_dense_desync,
    dense_entity_dead,
    slot_map_broken,
    links_broken,
    archive_mismatch,
    world_not_empty,
    globals_broken,
    archive_too_large,
};

struct fault
{
    fault_code code;
    std::string_view pool;
    const char* note = "";
};

struct pool_info
{
    std::string_view name;
    std::uint32_t id;
    std::uint64_t name_hash;
    std::size_t size;
    std::size_t capacity;
    std::size_t bytes_per_item;
    std::size_t index_bytes;
    std::size_t bookkeeping_bytes;
    storage kind;
};

struct apply_result
{
    std::uint32_t applied = 0;
    std::uint32_t skipped = 0;
};

struct duplicate_result
{
    entity clone;
    std::uint32_t copied = 0;
    std::uint32_t skipped = 0;

    operator entity() const noexcept { return clone; }  // NOLINT(google-explicit-constructor)
};

struct memory_footprint
{
    std::size_t entity_table_bytes = 0;
    std::size_t component_bytes = 0;
    std::size_t index_bytes = 0;
    std::size_t bookkeeping_bytes = 0;

    [[nodiscard]] std::size_t total() const noexcept
    {
        return entity_table_bytes + component_bytes + index_bytes + bookkeeping_bytes;
    }
};

struct globals_mark
{
};

template <class Traits>
using basic_component_hook = void (*)(basic_registry<Traits>&, basic_entity<Traits>, void* user);

using component_hook = basic_component_hook<default_entity_traits>;

template <class Traits>
using basic_relationship_hook = void (*)(basic_registry<Traits>&,
                                         basic_entity<Traits> child,
                                         basic_entity<Traits> parent,
                                         void* user);

using relationship_hook = basic_relationship_hook<default_entity_traits>;

enum class relationship_kind : std::uint8_t
{
    adopt,
    orphan,
    reorder,
};

struct relationship_token
{
    relationship_kind kind = relationship_kind::adopt;
    std::uint32_t id = 0;

    explicit operator bool() const noexcept { return id != 0; }
};

struct hook_token
{
    std::uint32_t pool = 0xFFFFFFFFu;
    std::uint32_t id = 0;

    constexpr explicit operator bool() const noexcept { return id != 0; }
};

namespace detail
{

template <class Traits, auto Candidate>
consteval basic_component_hook<Traits> free_hook_thunk()
{
    using world = basic_registry<Traits>;
    using entity = basic_entity<Traits>;
    if constexpr (std::invocable<decltype(Candidate), world&, entity>)
    {
        return +[](world& w, entity e, void*) { std::invoke(Candidate, w, e); };
    }
    else
    {
        static_assert(std::invocable<decltype(Candidate), entity>,
                      "ecs: a hook candidate must be callable as (world&, entity) or "
                      "(entity)");
        return +[](world&, entity e, void*) { std::invoke(Candidate, e); };
    }
}

template <class Traits, auto Candidate, class Inst>
consteval basic_component_hook<Traits> bound_hook_thunk()
{
    using world = basic_registry<Traits>;
    using entity = basic_entity<Traits>;
    if constexpr (std::invocable<decltype(Candidate), Inst&, world&, entity>)
    {
        return +[](world& w, entity e, void* user)
        { std::invoke(Candidate, *static_cast<Inst*>(user), w, e); };
    }
    else
    {
        static_assert(std::invocable<decltype(Candidate), Inst&, entity>,
                      "ecs: a bound hook candidate must be callable as "
                      "(Inst&, world&, entity) or (Inst&, entity)");
        return +[](world&, entity e, void* user)
        { std::invoke(Candidate, *static_cast<Inst*>(user), e); };
    }
}
}  // namespace detail

class any
{
    static constexpr std::size_t sbo_bytes = 3 * sizeof(void*);

    union storage
    {
        void* remote;
        alignas(std::max_align_t) std::byte local[sbo_bytes];
    };

    template <class T>
    static constexpr bool fits_inline =
        sizeof(T) <= sbo_bytes && alignof(T) <= alignof(std::max_align_t) &&
        std::is_nothrow_move_constructible_v<T>;

    enum class place : std::uint8_t
    {
        local,
        remote,
        ref,
    };

    struct vtable_t
    {
        std::uint64_t hash;
        place where;
        void (*destroy)(any&) noexcept;
        void (*copy)(any& dst, const any& src);
        void (*move)(any& dst, any& src) noexcept;
        void* (*address)(const any&) noexcept;
    };

    template <class T, place Where>
    static const vtable_t* table() noexcept
    {
        static constexpr vtable_t vt{
            hash_of<T>(),
            Where,
            +[](any& self) noexcept
            {
                if constexpr (Where == place::local)
                {
                    std::destroy_at(static_cast<T*>(static_cast<void*>(self.store_.local)));
                }
                else if constexpr (Where == place::remote)
                {
                    T* payload = static_cast<T*>(self.store_.remote);
                    std::destroy_at(payload);
                    ::operator delete(payload, std::align_val_t{alignof(T)});
                }
            },
            []() -> void (*)(any&, const any&)
            {
                if constexpr (Where == place::ref)
                {
                    return +[](any& dst, const any& src)
                    {
                        dst.vt_ = src.vt_;
                        dst.store_.remote = src.store_.remote;
                    };
                }
                else if constexpr (std::copy_constructible<T>)
                {
                    return +[](any& dst, const any& src)
                    { dst.emplace_value<T>(*static_cast<const T*>(src.vt_->address(src))); };
                }
                else
                {
                    return nullptr;
                }
            }(),
            +[](any& dst, any& src) noexcept
            {
                if constexpr (Where == place::local)
                {
                    std::construct_at(
                        static_cast<T*>(static_cast<void*>(dst.store_.local)),
                        std::move(*static_cast<T*>(static_cast<void*>(src.store_.local))));
                    dst.vt_ = src.vt_;
                    src.vt_->destroy(src);
                }
                else
                {
                    dst.store_.remote = src.store_.remote;
                    dst.vt_ = src.vt_;
                }
                src.vt_ = nullptr;
            },
            +[](const any& self) noexcept -> void*
            {
                if constexpr (Where == place::local)
                {
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                    return const_cast<std::byte*>(self.store_.local);
                }
                else
                {
                    return self.store_.remote;
                }
            },
        };
        return &vt;
    }

    template <class T, class... Args>
    void emplace_value(Args&&... args)
    {
        if constexpr (fits_inline<T>)
        {
            std::construct_at(static_cast<T*>(static_cast<void*>(store_.local)),
                              std::forward<Args>(args)...);
            vt_ = table<T, place::local>();
        }
        else
        {
            void* raw = ::operator new(sizeof(T), std::align_val_t{alignof(T)});
            try
            {
                store_.remote =
                    std::construct_at(static_cast<T*>(raw), std::forward<Args>(args)...);
            }
            catch (...)
            {
                ::operator delete(raw, std::align_val_t{alignof(T)});
                throw;
            }
            vt_ = table<T, place::remote>();
        }
    }

public:
    any() noexcept = default;

    template <class T, class... Args>
    [[nodiscard]] static any make(Args&&... args)
    {
        static_assert(std::same_as<T, detail::bare<T>>,
                      "ecs: any::make<T> takes a plain, unqualified value type");
        any out;
        out.emplace_value<T>(std::forward<Args>(args)...);
        return out;
    }

    template <class T>
    [[nodiscard]] static any ref(T& object) noexcept
    {
        static_assert(std::same_as<T, detail::bare<T>>,
                      "ecs: any::ref<T> views a plain, unqualified value type");
        any out;
        out.store_.remote = &object;
        out.vt_ = table<T, place::ref>();
        return out;
    }

    any(const any& other)
    {
        if (other.vt_ == nullptr)
        {
            return;
        }
        if (other.vt_->copy == nullptr)
        {
            if constexpr (checks_enabled)
            {
                detail::violate(
                    "copy of an any holding a non-copyable payload (the copy is "
                    "empty; move it or pass any::ref)");
            }
            return;
        }
        other.vt_->copy(*this, other);
    }

    any(any&& other) noexcept
    {
        if (other.vt_ != nullptr)
        {
            other.vt_->move(*this, other);
        }
    }

    any& operator=(const any& other)
    {
        if (this != &other)
        {
            any copy(other);
            reset();
            if (copy.vt_ != nullptr)
            {
                copy.vt_->move(*this, copy);
            }
        }
        return *this;
    }

    any& operator=(any&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            if (other.vt_ != nullptr)
            {
                other.vt_->move(*this, other);
            }
        }
        return *this;
    }

    ~any() { reset(); }

    void reset() noexcept
    {
        if (vt_ != nullptr)
        {
            vt_->destroy(*this);
            vt_ = nullptr;
        }
    }

    [[nodiscard]] bool holds() const noexcept { return vt_ != nullptr; }
    explicit operator bool() const noexcept { return holds(); }

    [[nodiscard]] std::uint64_t type_hash() const noexcept
    {
        return vt_ != nullptr ? vt_->hash : 0;
    }

    template <class T>
    [[nodiscard]] T* try_as() noexcept
    {
        return vt_ != nullptr && vt_->hash == hash_of<detail::bare<T>>()
                   ? static_cast<T*>(vt_->address(*this))
                   : nullptr;
    }

    template <class T>
    [[nodiscard]] const T* try_as() const noexcept
    {
        return vt_ != nullptr && vt_->hash == hash_of<detail::bare<T>>()
                   ? static_cast<const T*>(vt_->address(*this))
                   : nullptr;
    }

    template <class T>
    [[nodiscard]] T& as()
    {
        T* payload = try_as<T>();
        if (payload == nullptr)
        {
            if constexpr (checks_enabled)
            {
                detail::violate(
                    "as<T> on an any holding a different type (or nothing); "
                    "try_as<T> is the safe form");
            }
            std::abort();
        }
        return *payload;
    }

    template <class T>
    [[nodiscard]] const T& as() const
    {
        const T* payload = try_as<T>();
        if (payload == nullptr)
        {
            if constexpr (checks_enabled)
            {
                detail::violate(
                    "as<T> on an any holding a different type (or nothing); "
                    "try_as<T> is the safe form");
            }
            std::abort();
        }
        return *payload;
    }

    [[nodiscard]] void* data() noexcept { return vt_ != nullptr ? vt_->address(*this) : nullptr; }
    [[nodiscard]] const void* data() const noexcept
    {
        return vt_ != nullptr ? vt_->address(*this) : nullptr;
    }

private:
    const vtable_t* vt_ = nullptr;
    storage store_{};
};

template <class T>
concept reflectable_aggregate =
    std::is_aggregate_v<T> && !std::is_array_v<T> && !std::is_union_v<T>;

namespace detail
{

template <class T>
struct ubiq
{
    template <class U>
        requires(!std::same_as<std::remove_cvref_t<U>, T>)
    constexpr operator U() const noexcept;
};

template <class T, std::size_t N>
[[nodiscard]] consteval bool brace_initable() noexcept
{
    return []<std::size_t... I>(std::index_sequence<I...>)
    {
        return requires { T{(static_cast<void>(I), ubiq<T>{})...}; };
    }(std::make_index_sequence<N>{});
}

template <class T, std::size_t N = 0>
[[nodiscard]] consteval std::size_t aggregate_field_count() noexcept
{
    if constexpr (N < 64 && brace_initable<T, N + 1>())
    {
        return aggregate_field_count<T, N + 1>();
    }
    else
    {
        return N;
    }
}
}  // namespace detail

template <reflectable_aggregate T>
inline constexpr std::size_t field_count_v = detail::aggregate_field_count<T>();

template <class T>
    requires reflectable_aggregate<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto tie_fields(T& object) noexcept
{
    constexpr std::size_t n = field_count_v<std::remove_cvref_t<T>>;
    static_assert(n <= 16, "ecs: tie_fields supports aggregates of up to 16 members");
    if constexpr (n == 0)
    {
        return std::tuple<>{};
    }
    else if constexpr (n == 1)
    {
        auto& [a] = object;
        return std::tie(a);
    }
    else if constexpr (n == 2)
    {
        auto& [a, b] = object;
        return std::tie(a, b);
    }
    else if constexpr (n == 3)
    {
        auto& [a, b, c] = object;
        return std::tie(a, b, c);
    }
    else if constexpr (n == 4)
    {
        auto& [a, b, c, d] = object;
        return std::tie(a, b, c, d);
    }
    else if constexpr (n == 5)
    {
        auto& [a, b, c, d, e] = object;
        return std::tie(a, b, c, d, e);
    }
    else if constexpr (n == 6)
    {
        auto& [a, b, c, d, e, f] = object;
        return std::tie(a, b, c, d, e, f);
    }
    else if constexpr (n == 7)
    {
        auto& [a, b, c, d, e, f, g] = object;
        return std::tie(a, b, c, d, e, f, g);
    }
    else if constexpr (n == 8)
    {
        auto& [a, b, c, d, e, f, g, h] = object;
        return std::tie(a, b, c, d, e, f, g, h);
    }
    else if constexpr (n == 9)
    {
        auto& [a, b, c, d, e, f, g, h, i] = object;
        return std::tie(a, b, c, d, e, f, g, h, i);
    }
    else if constexpr (n == 10)
    {
        auto& [a, b, c, d, e, f, g, h, i, j] = object;
        return std::tie(a, b, c, d, e, f, g, h, i, j);
    }
    else if constexpr (n == 11)
    {
        auto& [a, b, c, d, e, f, g, h, i, j, k] = object;
        return std::tie(a, b, c, d, e, f, g, h, i, j, k);
    }
    else if constexpr (n == 12)
    {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l] = object;
        return std::tie(a, b, c, d, e, f, g, h, i, j, k, l);
    }
    else if constexpr (n == 13)
    {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m] = object;
        return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m);
    }
    else if constexpr (n == 14)
    {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, o] = object;
        return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, o);
    }
    else if constexpr (n == 15)
    {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, o, p] = object;
        return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p);
    }
    else
    {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q] = object;
        return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q);
    }
}

template <class T, class F>
    requires reflectable_aggregate<std::remove_cvref_t<T>>
constexpr void for_each(T& object, F&& fn)
{
    std::apply([&fn](auto&... members) { (static_cast<void>(fn(members)), ...); },
               tie_fields(object));
}

template <reflectable_aggregate T>
[[nodiscard]] constexpr auto as_tuple(const T& object)
{
    return std::apply([](const auto&... members)
                      { return std::tuple<std::remove_cvref_t<decltype(members)>...>(members...); },
                      tie_fields(object));
}

template <class T, class F>
    requires reflectable_aggregate<std::remove_cvref_t<T>>
constexpr void for_each_leaf(T& object, F&& fn)
{
    for_each(object,
             [&fn](auto& member)
             {
                 if constexpr (reflectable_aggregate<std::remove_cvref_t<decltype(member)>>)
                 {
                     for_each_leaf(member, fn);
                 }
                 else
                 {
                     static_cast<void>(fn(member));
                 }
             });
}

template <class Archive, class T>
    requires reflectable_aggregate<std::remove_cvref_t<T>>
void write_fields(Archive&& archive, const T& object)
{
    for_each_leaf(object, [&archive](const auto& leaf) { archive(leaf); });
}

template <class Archive, class T>
    requires reflectable_aggregate<std::remove_cvref_t<T>>
void read_fields(Archive&& archive, T& object)
{
    for_each_leaf(object, [&archive](auto& leaf) { archive(leaf); });
}

namespace detail
{
template <class M>
[[nodiscard]] constexpr bool leaf_equal(const M& a, const M& b);
}  // namespace detail

template <reflectable_aggregate T>
[[nodiscard]] constexpr bool fields_equal(const T& lhs, const T& rhs)
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>)
    {
        return (detail::leaf_equal(std::get<Is>(tie_fields(lhs)), std::get<Is>(tie_fields(rhs))) &&
                ...);
    }(std::make_index_sequence<field_count_v<T>>{});
}

namespace detail
{
template <class M>
[[nodiscard]] constexpr bool leaf_equal(const M& a, const M& b)
{
    if constexpr (reflectable_aggregate<M>)
    {
        return ecs::fields_equal(a, b);
    }
    else
    {
        return a == b;
    }
}
}  // namespace detail

template <reflectable_aggregate T>
[[nodiscard]] inline std::size_t hash_fields(const T& object)
{
    constexpr auto golden = static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
    std::size_t seed = 0;
    for_each_leaf(object,
                  [&seed](const auto& leaf)
                  {
                      const std::size_t h = std::hash<std::remove_cvref_t<decltype(leaf)>>{}(leaf);
                      seed ^= h + golden + (seed << 6) + (seed >> 2);
                  });
    return seed;
}

#if (defined(_MSC_VER) && _MSC_VER >= 1927 && !defined(__clang__)) || defined(__clang__) || \
    defined(__GNUC__)
inline constexpr bool field_names_supported = true;
#else
inline constexpr bool field_names_supported = false;
#endif

namespace detail
{

template <class T>
struct name_probe
{
    T value;
};
template <class T>

// NOLINTNEXTLINE(clang-diagnostic-undefined-internal)
extern const name_probe<T> name_probe_obj;

template <class P>
struct name_nttp
{
    const P* p;
};
template <class P>
constexpr auto name_as_nttp(const P* p) noexcept
{
#if defined(__clang__)
    return name_nttp<P>{p};
#else
    return p;
#endif
}

template <class Tag, auto Ptr>
[[nodiscard]] consteval std::string_view raw_field_name() noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
    std::string_view s = __FUNCSIG__;  // ...<...,&...->NAME>(void) noexcept
    s = s.substr(0, s.find(">(void) noexcept"));
    return s.substr(s.rfind("->") + 2);
#elif defined(__clang__)
    std::string_view s = __PRETTY_FUNCTION__;  // ...{&...NAME}]
    s = s.substr(0, s.size() - 2);
    return s.substr(s.find_last_of(":.") + 1);
#elif defined(__GNUC__)
    std::string_view s = __PRETTY_FUNCTION__;  // ...fake_object.NAME)]
    s = s.substr(0, s.size() - 2);
    return s.substr(s.find_last_of(":.") + 1);
#else
    return {};
#endif
}

template <class T, std::size_t I>
[[nodiscard]] consteval std::string_view extracted_field_name() noexcept
{
    return raw_field_name<T, name_as_nttp(&std::get<I>(tie_fields(name_probe_obj<T>.value)))>();
}
}  // namespace detail

template <reflectable_aggregate T, std::size_t I>
[[nodiscard]] consteval std::string_view field_name() noexcept
{
    if constexpr (field_names_supported)
    {
        return detail::extracted_field_name<T, I>();
    }
    else
    {
        return {};
    }
}

template <reflectable_aggregate T>
inline constexpr auto field_names_v = []<std::size_t... Is>(std::index_sequence<Is...>)
{
    return std::array<std::string_view, sizeof...(Is)>{field_name<T, Is>()...};
}(std::make_index_sequence<field_count_v<T>>{});

template <class T>
concept reflectable = reflectable_aggregate<T>;

template <std::size_t N>
struct field_key
{
    char text[N]{};

    consteval field_key(const char (&literal)[N]) noexcept  // NOLINT(google-explicit-constructor)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            text[i] = literal[i];
        }
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {text, N - 1}; }
};

namespace detail
{
template <reflectable_aggregate T, field_key Name>
[[nodiscard]] consteval bool spells_a_field() noexcept
{
    for (const std::string_view spelled : field_names_v<T>)
    {
        if (spelled == Name.view())
        {
            return true;
        }
    }
    return false;
}

template <reflectable_aggregate T, template <class> class Trait>
[[nodiscard]] consteval bool every_field_is() noexcept
{
    return []<std::size_t... Is>(std::index_sequence<Is...>)
    {
        return (Trait<std::remove_cvref_t<
                    std::tuple_element_t<Is, decltype(tie_fields(std::declval<T&>()))>>>::value &&
                ...);
    }(std::make_index_sequence<field_count_v<T>>{});
}
}  // namespace detail

template <class T, field_key Name>
concept has_field = reflectable<T> && detail::spells_a_field<T, Name>();

template <class T, template <class> class Trait>
concept fields_all = reflectable<T> && detail::every_field_is<T, Trait>();

namespace detail
{

template <class Vector>
void grow_if_full(Vector& v)
{
    if (v.size() == v.capacity())
    {
        v.reserve(std::max<std::size_t>(8, v.capacity() * 2));
    }
}

template <entity_traits Traits>
class basic_sparse_index
{
public:
    using index_type = typename Traits::index_type;

    static constexpr index_type npos = entity_limits<Traits>::npos;
    static constexpr std::uint32_t page_size = 4096;

    explicit basic_sparse_index(std::pmr::memory_resource* memory) noexcept
        : memory_(memory),
          pages_(memory)
    {
    }

    basic_sparse_index(const basic_sparse_index&) = delete;
    basic_sparse_index& operator=(const basic_sparse_index&) = delete;
    basic_sparse_index(basic_sparse_index&&) = delete;
    basic_sparse_index& operator=(basic_sparse_index&&) = delete;

    ~basic_sparse_index() { clear(); }

    [[nodiscard]] index_type get(index_type key) const noexcept
    {
        const std::size_t p = key / page_size;
        if (p >= pages_.size() || pages_[p] == nullptr)
        {
            return npos;
        }
        return pages_[p][key % page_size];
    }

    void set(index_type key, index_type value) { page(key / page_size)[key % page_size] = value; }

    void ensure(index_type key) { page(key / page_size); }

    void set_existing(index_type key, index_type value) noexcept
    {
        pages_[key / page_size][key % page_size] = value;
    }

    [[nodiscard]] std::size_t bytes() const noexcept
    {
        std::size_t pages = 0;
        for (const auto& page : pages_)
        {
            pages += page != nullptr ? 1 : 0;
        }
        return (pages * page_size * sizeof(index_type)) + (pages_.capacity() * sizeof(pages_[0]));
    }

    void erase(index_type key) noexcept
    {
        const std::size_t p = key / page_size;
        if (p < pages_.size() && pages_[p] != nullptr)
        {
            pages_[p][key % page_size] = npos;
        }
    }

    void clear() noexcept
    {
        for (index_type* page : pages_)
        {
            if (page != nullptr)
            {
                memory_->deallocate(page, page_size * sizeof(index_type), alignof(index_type));
            }
        }
        pages_.clear();
    }

    template <class F>
    void visit(F&& fn) const
    {
        for (std::size_t p = 0; p < pages_.size(); ++p)
        {
            if (pages_[p] == nullptr)
            {
                continue;
            }
            for (std::uint32_t i = 0; i < page_size; ++i)
            {
                if (pages_[p][i] != npos)
                {
                    fn(static_cast<index_type>((p * page_size) + i), pages_[p][i]);
                }
            }
        }
    }

private:
    index_type* page(std::size_t p)
    {
        if (p >= pages_.size())
        {
            pages_.resize(p + 1, nullptr);
        }
        if (pages_[p] == nullptr)
        {
            auto* fresh = static_cast<index_type*>(
                memory_->allocate(page_size * sizeof(index_type), alignof(index_type)));
            std::fill_n(fresh, page_size, npos);
            pages_[p] = fresh;
        }
        return pages_[p];
    }

    std::pmr::memory_resource* memory_;
    std::pmr::vector<index_type*> pages_;
};

template <entity_traits Traits>
class basic_entity_table
{
public:
    using entity = ecs::basic_entity<Traits>;
    using index_type = typename Traits::index_type;
    using generation_type = typename Traits::generation_type;

    static constexpr index_type max_slots = entity_limits<Traits>::max_slots;

    explicit basic_entity_table(std::pmr::memory_resource* memory) noexcept
        : generation_(memory),
          free_flag_(memory),
          free_stack_(memory)
    {
    }

    basic_entity_table(basic_entity_table&& other) noexcept
        : generation_(std::move(other.generation_)),
          free_flag_(std::move(other.free_flag_)),
          free_stack_(std::move(other.free_stack_)),
          live_(std::exchange(other.live_, 0))
    {
    }

    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor,bugprone-exception-escape)
    basic_entity_table& operator=(basic_entity_table&& other)
    {
        if (this != &other)
        {
            generation_ = std::move(other.generation_);
            free_flag_ = std::move(other.free_flag_);
            free_stack_ = std::move(other.free_stack_);
            live_ = std::exchange(other.live_, 0);
            other.generation_.clear();
            other.free_flag_.clear();
            other.free_stack_.clear();
        }
        return *this;
    }

    basic_entity_table(const basic_entity_table&) = delete;
    basic_entity_table& operator=(const basic_entity_table&) = delete;
    ~basic_entity_table() = default;

    entity create()
    {
        while (!free_stack_.empty())
        {
            const index_type index = free_stack_.back();
            free_stack_.pop_back();
            if (free_flag_[index] != 0)
            {
                free_flag_[index] = 0;
                ++live_;
                return entity(index, generation_[index]);
            }
        }
        const auto index = static_cast<index_type>(generation_.size());
        if (index >= max_slots)
        {
            violate("entity table is full (2^index_bits - 1 slots)");
            std::abort();
        }
        generation_.push_back(0);
        free_flag_.push_back(0);
        ++live_;
        ensure_destroy_slack();
        return entity(index, 0);
    }
    // NOLINTNEXTLINE(bugprone-exception-escape)
    void destroy(index_type index) noexcept
    {
        ++generation_[index];
        free_flag_[index] = 1;
        free_stack_.push_back(index);
        --live_;
    }

    [[nodiscard]] bool alive(entity e) const noexcept
    {
        const index_type index = e.index();
        return index < generation_.size() && free_flag_[index] == 0 &&
               generation_[index] == e.generation();
    }

    [[nodiscard]] bool occupied(index_type index) const noexcept
    {
        return index < generation_.size() && free_flag_[index] == 0;
    }

    [[nodiscard]] generation_type generation_at(index_type index) const noexcept
    {
        return generation_[index];
    }

    std::expected<entity, fault> restore(entity e)
    {
        if (!e || is_provisional(e) || e.index() >= max_slots)
        {
            return std::unexpected(
                fault{fault_code::bad_handle, {}, "restore_entity: invalid handle"});
        }
        const index_type index = e.index();
        while (generation_.size() <= index)
        {
            const auto fresh = static_cast<index_type>(generation_.size());
            generation_.push_back(0);
            free_flag_.push_back(1);
            free_stack_.push_back(fresh);
        }
        if (free_flag_[index] == 0)
        {
            return std::unexpected(
                fault{fault_code::slot_occupied, {}, "restore_entity: slot holds a live entity"});
        }
        free_flag_[index] = 0;
        generation_[index] = e.generation();
        ++live_;
        ensure_destroy_slack();
        return e;
    }

    void destroy_all() noexcept
    {
        for (index_type index = 0; index < generation_.size(); ++index)
        {
            if (free_flag_[index] == 0)
            {
                destroy(index);
            }
        }
    }

    void reserve(std::size_t n)
    {
        generation_.reserve(n);
        free_flag_.reserve(n);
        free_stack_.reserve(n);
    }

    void shrink()
    {
        std::erase_if(free_stack_, [this](index_type index) { return free_flag_[index] == 0; });
        std::pmr::vector<std::uint8_t> seen(generation_.size(), 0, generation_.get_allocator());
        std::pmr::vector<index_type> kept(free_stack_.get_allocator());
        kept.reserve(free_stack_.size());
        for (std::size_t i = free_stack_.size(); i-- > 0;)
        {
            const index_type index = free_stack_[i];
            if (seen[index] == 0)
            {
                seen[index] = 1;
                kept.push_back(index);
            }
        }
        std::ranges::reverse(kept);
        free_stack_ = std::move(kept);
        generation_.shrink_to_fit();
        free_flag_.shrink_to_fit();
        ensure_destroy_slack();
    }

    [[nodiscard]] std::size_t slots() const noexcept { return generation_.size(); }
    [[nodiscard]] std::size_t live() const noexcept { return live_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return generation_.capacity(); }

    [[nodiscard]] std::size_t bytes() const noexcept
    {
        return (generation_.capacity() * sizeof(generation_type)) + free_flag_.capacity() +
               (free_stack_.capacity() * sizeof(index_type));
    }

    [[nodiscard]] std::expected<void, fault> check() const
    {
        if (free_flag_.size() != generation_.size())
        {
            return std::unexpected(
                fault{fault_code::table_out_of_sync, {}, "flag/generation size"});
        }
        std::size_t free_count = 0;
        for (const std::uint8_t flag : free_flag_)
        {
            free_count += flag;
        }
        if (live_ + free_count != generation_.size())
        {
            return std::unexpected(fault{fault_code::table_out_of_sync, {}, "live count"});
        }
        std::vector<std::uint8_t> reachable(generation_.size(), 0);
        for (const index_type index : free_stack_)
        {
            if (index >= generation_.size())
            {
                return std::unexpected(
                    fault{fault_code::table_out_of_sync, {}, "free stack out of range"});
            }
            reachable[index] = 1;
        }
        for (index_type index = 0; index < generation_.size(); ++index)
        {
            if (free_flag_[index] != 0 && reachable[index] == 0)
            {
                return std::unexpected(
                    fault{fault_code::table_out_of_sync, {}, "free slot not recyclable"});
            }
        }
        return {};
    }

private:
    friend struct ecs::test_access;

    void ensure_destroy_slack()
    {
        const std::size_t needed = free_stack_.size() + live_;
        if (free_stack_.capacity() < needed)
        {
            free_stack_.reserve(std::max(needed, free_stack_.capacity() * 2));
        }
    }

    std::pmr::vector<generation_type> generation_;
    std::pmr::vector<std::uint8_t> free_flag_;
    std::pmr::vector<index_type> free_stack_;
    std::size_t live_ = 0;
};

using entity_table = basic_entity_table<default_entity_traits>;

static_assert(entity_table::max_slots == provisional_bit - 1);

template <entity_traits Traits>
class basic_pool_base;

template <entity_traits Traits>
class basic_single_pool_lock;

struct std_sort
{
    template <class It, class Cmp>
    void operator()(It first, It last, Cmp cmp) const
    {
        std::sort(first, last, cmp);
    }
};

template <entity_traits Traits>
class basic_pool_base
{
public:
    using entity = ecs::basic_entity<Traits>;
    using entity_table = basic_entity_table<Traits>;
    using index_type = typename Traits::index_type;
    using pool_base = basic_pool_base;
    using single_pool_lock = basic_single_pool_lock<Traits>;
    using world = ecs::basic_registry<Traits>;
    using component_hook = ecs::basic_component_hook<Traits>;

    static constexpr index_type npos = entity_limits<Traits>::npos;

    basic_pool_base(std::pmr::memory_resource* memory,
                    std::string_view name,
                    std::uint64_t name_hash,
                    storage kind,
                    std::size_t item_bytes) noexcept
        : sparse_(memory),
          dense_(memory),
          memory_(memory),
          name_(name),
          name_hash_(name_hash),
          kind_(kind),
          item_bytes_(item_bytes)
    {
    }

    virtual ~basic_pool_base() = default;
    basic_pool_base(const basic_pool_base&) = delete;
    basic_pool_base& operator=(const basic_pool_base&) = delete;

    [[nodiscard]] bool contains(index_type index) const noexcept
    {
        return sparse_.get(index) != npos;
    }

    [[nodiscard]] index_type position_of(index_type index) const noexcept
    {
        return sparse_.get(index);
    }

    [[nodiscard]] std::size_t size() const noexcept { return dense_.size(); }
    [[nodiscard]] entity entity_at(std::size_t pos) const noexcept { return dense_[pos]; }

    virtual bool copy_item(index_type src_index, entity dst) = 0;

    virtual void erase_if_present(index_type index) noexcept = 0;
    virtual void wipe() noexcept = 0;
    virtual void compact() = 0;

    [[nodiscard]] virtual void* item_address(index_type /*pos*/) noexcept { return nullptr; }

    [[nodiscard]] virtual std::size_t item_capacity() const noexcept = 0;
    [[nodiscard]] virtual std::size_t extra_bookkeeping_bytes() const noexcept { return 0; }
    [[nodiscard]] virtual std::expected<void, fault> check(const entity_table& table) const = 0;

    [[nodiscard]] pool_info info() const noexcept
    {
        return pool_info{name_,
                         id_,
                         name_hash_,
                         dense_.size(),
                         item_capacity(),
                         item_bytes_,
                         sparse_.bytes(),
                         (dense_.capacity() * sizeof(entity)) + extra_bookkeeping_bytes(),
                         kind_};
    }

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] std::uint64_t name_hash() const noexcept { return name_hash_; }
    [[nodiscard]] bool locked() const noexcept { return locks_ != 0; }

    enum class hook_kind : std::uint8_t
    {
        add,
        remove,
        replace,
    };

    std::uint32_t connect_hook(hook_kind kind, component_hook fn, void* user)
    {
        if (!hooks_)
        {
            hooks_ = std::make_unique<hook_lists>();
        }
        const std::uint32_t id = hooks_->next_id++;
        list_for(kind).push_back(hook_entry{fn, user, id});
        return id;
    }

    bool disconnect_hook(std::uint32_t id) noexcept
    {
        if (!hooks_)
        {
            return false;
        }
        const auto drop = [id](std::vector<hook_entry>& list)
        { return std::erase_if(list, [id](const hook_entry& h) { return h.id == id; }) > 0; };
        return drop(hooks_->on_add) || drop(hooks_->on_remove) || drop(hooks_->on_replace);
    }

    void fire_add(entity e) { dispatch_if(hooks_ ? &hooks_->on_add : nullptr, e); }
    void fire_remove(entity e) { dispatch_if(hooks_ ? &hooks_->on_remove : nullptr, e); }
    void fire_replace(entity e) { dispatch_if(hooks_ ? &hooks_->on_replace : nullptr, e); }

    void fire_remove_all()
    {
        if (hooks_ && !hooks_->on_remove.empty())
        {
            // NOLINTNEXTLINE(modernize-loop-convert)
            for (std::size_t i = 0; i < dense_.size(); ++i)
            {
                dispatch_if(&hooks_->on_remove, dense_[i]);
            }
        }
    }

protected:
    void attach(entity e)
    {
        dense_.push_back(e);
        sparse_.set(e.index(), static_cast<index_type>(dense_.size() - 1));
    }

    index_type detach(index_type index) noexcept
    {
        const index_type pos = sparse_.get(index);
        const auto last = static_cast<index_type>(dense_.size() - 1);
        if (pos != last)
        {
            dense_[pos] = dense_[last];
            sparse_.set_existing(dense_[pos].index(), pos);
        }
        dense_.pop_back();
        sparse_.erase(index);
        return pos;
    }

    void swap_dense(index_type a, index_type b) noexcept
    {
        std::swap(dense_[a], dense_[b]);
        sparse_.set_existing(dense_[a].index(), a);
        sparse_.set_existing(dense_[b].index(), b);
    }

    template <class PosCompare, class SwapPayload, class Algo = std_sort>
    void sort_dense_impl(PosCompare cmp, SwapPayload swap_payload, Algo&& algo = {})
    {
        const auto n = static_cast<index_type>(dense_.size());
        std::pmr::vector<index_type> perm(n, memory_);
        for (index_type i = 0; i < n; ++i)
        {
            perm[i] = i;
        }
        algo(perm.begin(), perm.end(), cmp);
        for (index_type i = 0; i < n; ++i)
        {
            index_type curr = i;
            index_type next = perm[curr];
            while (next != i)
            {
                swap_dense(curr, next);
                swap_payload(curr, next);
                perm[curr] = curr;
                curr = next;
                next = perm[curr];
            }
            perm[curr] = curr;
        }
    }

    [[nodiscard]] std::expected<void, fault> check_membership(const entity_table& table) const
    {
        for (std::size_t pos = 0; pos < dense_.size(); ++pos)
        {
            const entity e = dense_[pos];
            if (!table.alive(e))
            {
                return std::unexpected(fault{fault_code::dense_entity_dead, name_, "dead entity"});
            }
            if (sparse_.get(e.index()) != pos)
            {
                return std::unexpected(
                    fault{fault_code::sparse_dense_desync, name_, "dense -> sparse"});
            }
        }
        std::expected<void, fault> result{};
        sparse_.visit(
            [&](index_type key, index_type value)
            {
                if (!result)
                {
                    return;
                }
                if (value >= dense_.size() || dense_[value].index() != key)
                {
                    result = std::unexpected(
                        fault{fault_code::sparse_dense_desync, name_, "sparse -> dense"});
                }
            });
        return result;
    }

    template <class>
    friend class ecs::basic_registry;
    template <entity_traits>
    friend class basic_single_pool_lock;
    template <class>
    friend class ecs::basic_runtime_selection;
    template <class, class, class...>
    friend class ecs::basic_view;
    friend struct ecs::test_access;

    struct hook_entry
    {
        component_hook fn;
        void* user;
        std::uint32_t id;
    };

    struct hook_lists
    {
        std::vector<hook_entry> on_add;
        std::vector<hook_entry> on_remove;
        std::vector<hook_entry> on_replace;
        std::uint32_t next_id = 1;
    };

    [[nodiscard]] std::vector<hook_entry>& list_for(hook_kind kind) noexcept
    {
        switch (kind)
        {
            case hook_kind::add:
                return hooks_->on_add;
            case hook_kind::remove:
                return hooks_->on_remove;
            case hook_kind::replace:
                return hooks_->on_replace;
        }
        std::unreachable();
    }

    void dispatch_if(const std::vector<hook_entry>* list, entity e);

    basic_sparse_index<Traits> sparse_;
    std::pmr::vector<entity> dense_;
    std::unique_ptr<hook_lists> hooks_;
    world* owner_ = nullptr;
    std::pmr::memory_resource* memory_;
    std::string_view name_;
    std::uint64_t name_hash_;
    storage kind_;
    std::size_t item_bytes_;
    std::uint32_t id_ = 0;
    mutable std::uint32_t locks_ = 0;
};

using pool_base = basic_pool_base<default_entity_traits>;

template <entity_traits Traits>
class basic_single_pool_lock
{
public:
    explicit basic_single_pool_lock(const basic_pool_base<Traits>* pool) noexcept
        : pool_(pool)
    {
        if constexpr (checks_enabled)
        {
            ++pool_->locks_;
        }
    }

    ~basic_single_pool_lock()
    {
        if constexpr (checks_enabled)
        {
            --pool_->locks_;
        }
    }

    basic_single_pool_lock(const basic_single_pool_lock&) = delete;
    basic_single_pool_lock& operator=(const basic_single_pool_lock&) = delete;

private:
    const basic_pool_base<Traits>* pool_;
};

using single_pool_lock = basic_single_pool_lock<default_entity_traits>;

template <entity_traits Traits>
void basic_pool_base<Traits>::dispatch_if(const std::vector<hook_entry>* list, entity e)
{
    if (list == nullptr || list->empty())
    {
        return;
    }
    const single_pool_lock lock(this);
    // NOLINTNEXTLINE(modernize-loop-convert)
    for (std::size_t i = 0; i < list->size(); ++i)
    {
        const hook_entry h = (*list)[i];
        h.fn(*owner_, e, h.user);
    }
}

template <component T, entity_traits Traits = default_entity_traits>
class packed_pool : public basic_pool_base<Traits>
{
public:
    static_assert(std::is_move_constructible_v<T>,
                  "ecs: packed storage moves components on add/remove; give T a move "
                  "constructor or select ecs::storage::stable for it");

    using base = basic_pool_base<Traits>;
    using entity = typename base::entity;
    using entity_table = typename base::entity_table;
    using index_type = typename base::index_type;
    using base::contains;
    using base::entity_at;
    using base::npos;

protected:
    using base::attach;
    using base::check_membership;
    using base::dense_;
    using base::detach;
    using base::fire_add;
    using base::fire_remove;
    using base::fire_remove_all;
    using base::memory_;
    using base::name_;
    using base::sort_dense_impl;
    using base::sparse_;
    using base::swap_dense;

public:
    explicit packed_pool(std::pmr::memory_resource* memory) noexcept
        : base(memory, name_of<T>(), hash_of<T>(), storage::packed, sizeof(T)),
          items_(memory)
    {
    }

    ~packed_pool() override = default;

    template <class... Args>
    T& emplace(entity e, Args&&... args)
    {
        grow_if_full(items_);
        grow_if_full(dense_);
        sparse_.ensure(e.index());
        items_.emplace_back(std::forward<Args>(args)...);
        attach(e);
        fire_add(e);
        return items_[sparse_.get(e.index())];
    }

    [[nodiscard]] T* at(index_type index) noexcept
    {
        const index_type pos = sparse_.get(index);
        return pos == npos ? nullptr : items_.data() + pos;
    }

    [[nodiscard]] const T* at(index_type index) const noexcept
    {
        const index_type pos = sparse_.get(index);
        return pos == npos ? nullptr : items_.data() + pos;
    }

    [[nodiscard]] const T& at_pos(index_type pos) const noexcept { return items_[pos]; }
    [[nodiscard]] T& at_pos(index_type pos) noexcept { return items_[pos]; }

    [[nodiscard]] void* item_address(index_type pos) noexcept override { return &items_[pos]; }

    template <class PosCompare, class Algo = std_sort>
    void sort_dense(PosCompare cmp, Algo&& algo = {})
    {
        static_assert(std::is_swappable_v<T>,
                      "ecs: sorting a packed pool swaps components; T must be swappable "
                      "(stable storage sorts without touching payloads)");
        sort_dense_impl(
            cmp,
            [this](index_type a, index_type b) { std::swap(items_[a], items_[b]); },
            std::forward<Algo>(algo));
    }

    bool copy_item(index_type src_index, entity dst) override
    {
        if constexpr (std::copy_constructible<T>)
        {
            T detached(items_[sparse_.get(src_index)]);
            emplace(dst, std::move(detached));
            return true;
        }
        else
        {
            return false;
        }
    }

    void swap_positions(index_type a, index_type b)
    {
        static_assert(std::is_swappable_v<T>,
                      "ecs: reordering a packed pool swaps components; T must be swappable "
                      "(stable storage reorders without touching payloads)");
        swap_dense(a, b);
        std::swap(items_[a], items_[b]);
    }

    void erase_if_present(index_type index) noexcept override
    {
        if (!contains(index))
        {
            return;
        }
        fire_remove(dense_[sparse_.get(index)]);
        const index_type pos = detach(index);
        const auto last = static_cast<index_type>(items_.size() - 1);
        if (pos != last)
        {
            if constexpr (std::is_move_assignable_v<T>)
            {
                items_[pos] = std::move(items_[last]);
            }
            else
            {
                std::destroy_at(&items_[pos]);
                std::construct_at(&items_[pos], std::move(items_[last]));
            }
        }
        items_.pop_back();
    }

    void wipe() noexcept override
    {
        fire_remove_all();
        items_.clear();
        dense_.clear();
        sparse_.clear();
    }

    void compact() override
    {
        items_.shrink_to_fit();
        dense_.shrink_to_fit();
    }

    void reserve(std::size_t n)
    {
        items_.reserve(n);
        dense_.reserve(n);
    }

    [[nodiscard]] std::size_t item_capacity() const noexcept override { return items_.capacity(); }

    [[nodiscard]] std::expected<void, fault> check(const entity_table& table) const override
    {
        if (items_.size() != dense_.size())
        {
            return std::unexpected(fault{fault_code::sparse_dense_desync, name_, "item count"});
        }
        return check_membership(table);
    }

private:
    std::pmr::vector<T> items_;
};

template <component T, entity_traits Traits = default_entity_traits>
class tag_pool : public basic_pool_base<Traits>
{
public:
    using base = basic_pool_base<Traits>;
    using entity = typename base::entity;
    using entity_table = typename base::entity_table;
    using index_type = typename base::index_type;
    using base::contains;
    using base::entity_at;

protected:
    using base::attach;
    using base::check_membership;
    using base::dense_;
    using base::detach;
    using base::fire_add;
    using base::fire_remove;
    using base::fire_remove_all;
    using base::sort_dense_impl;
    using base::sparse_;
    using base::swap_dense;

public:
    explicit tag_pool(std::pmr::memory_resource* memory) noexcept
        : base(memory, name_of<T>(), hash_of<T>(), storage::tag, 0)
    {
    }

    ~tag_pool() override = default;

    void emplace(entity e)
    {
        grow_if_full(dense_);
        sparse_.ensure(e.index());
        attach(e);
        fire_add(e);
    }

    template <class PosCompare, class Algo = std_sort>
    void sort_dense(PosCompare cmp, Algo&& algo = {})
    {
        sort_dense_impl(cmp, [](index_type, index_type) {}, std::forward<Algo>(algo));
    }

    bool copy_item(index_type, entity dst) override
    {
        emplace(dst);
        return true;
    }

    void swap_positions(index_type a, index_type b) noexcept { swap_dense(a, b); }

    void erase_if_present(index_type index) noexcept override
    {
        if (contains(index))
        {
            fire_remove(dense_[sparse_.get(index)]);
            detach(index);
        }
    }

    void wipe() noexcept override
    {
        fire_remove_all();
        dense_.clear();
        sparse_.clear();
    }

    void compact() override { dense_.shrink_to_fit(); }

    void reserve(std::size_t n) { dense_.reserve(n); }

    [[nodiscard]] std::size_t item_capacity() const noexcept override { return dense_.capacity(); }

    [[nodiscard]] std::expected<void, fault> check(const entity_table& table) const override
    {
        return check_membership(table);
    }
};

template <component T, entity_traits Traits = default_entity_traits>
class stable_pool : public basic_pool_base<Traits>
{
public:
    using base = basic_pool_base<Traits>;
    using entity = typename base::entity;
    using entity_table = typename base::entity_table;
    using index_type = typename base::index_type;
    using base::contains;
    using base::entity_at;
    using base::npos;

protected:
    using base::attach;
    using base::check_membership;
    using base::dense_;
    using base::detach;
    using base::fire_add;
    using base::fire_remove;
    using base::fire_remove_all;
    using base::memory_;
    using base::name_;
    using base::sort_dense_impl;
    using base::sparse_;
    using base::swap_dense;

public:
    static constexpr std::size_t chunk_items = chunk_capacity<T>;
    static_assert(chunk_items > 0, "ecs: chunk_capacity<T> must be at least 1");

    explicit stable_pool(std::pmr::memory_resource* memory) noexcept
        : base(memory, name_of<T>(), hash_of<T>(), storage::stable, sizeof(T)),
          slot_of_(memory),
          chunks_(memory),
          free_slots_(memory)
    {
    }

    ~stable_pool() override
    {
        release_all();
        release_chunks();
    }

    template <class... Args>
    T& emplace(entity e, Args&&... args)
    {
        grow_if_full(dense_);
        grow_if_full(slot_of_);
        sparse_.ensure(e.index());
        const index_type slot = peek_free_slot();
        T* item = std::construct_at(slot_ptr(slot), std::forward<Args>(args)...);
        commit_free_slot(slot);
        slot_of_.push_back(slot);
        attach(e);
        fire_add(e);
        return *item;
    }

    [[nodiscard]] T* at(index_type index) noexcept
    {
        const index_type pos = sparse_.get(index);
        return pos == npos ? nullptr : slot_ptr(slot_of_[pos]);
    }

    [[nodiscard]] const T* at(index_type index) const noexcept
    {
        const index_type pos = sparse_.get(index);
        return pos == npos ? nullptr : slot_ptr(slot_of_[pos]);
    }

    [[nodiscard]] const T& at_pos(index_type pos) const noexcept
    {
        return *slot_ptr(slot_of_[pos]);
    }

    [[nodiscard]] T& at_pos(index_type pos) noexcept { return *slot_ptr(slot_of_[pos]); }

    [[nodiscard]] void* item_address(index_type pos) noexcept override
    {
        return slot_ptr(slot_of_[pos]);
    }

    template <class PosCompare, class Algo = std_sort>
    void sort_dense(PosCompare cmp, Algo&& algo = {})
    {
        sort_dense_impl(
            cmp,
            [this](index_type a, index_type b) { std::swap(slot_of_[a], slot_of_[b]); },
            std::forward<Algo>(algo));
    }

    bool copy_item(index_type src_index, entity dst) override
    {
        if constexpr (std::copy_constructible<T>)
        {
            emplace(dst, *slot_ptr(slot_of_[sparse_.get(src_index)]));
            return true;
        }
        else
        {
            return false;
        }
    }

    void swap_positions(index_type a, index_type b) noexcept
    {
        swap_dense(a, b);
        std::swap(slot_of_[a], slot_of_[b]);
    }

    [[nodiscard]] std::size_t extra_bookkeeping_bytes() const noexcept override
    {
        return (slot_of_.capacity() * sizeof(index_type)) +
               (free_slots_.capacity() * sizeof(index_type)) +
               (chunks_.capacity() * sizeof(std::byte*));
    }

    // NOLINTNEXTLINE(bugprone-exception-escape)
    void erase_if_present(index_type index) noexcept override
    {
        if (!contains(index))
        {
            return;
        }
        fire_remove(dense_[sparse_.get(index)]);
        const index_type pos = detach(index);
        const index_type slot = slot_of_[pos];
        std::destroy_at(slot_ptr(slot));
        free_slots_.push_back(slot);
        const auto last = static_cast<index_type>(slot_of_.size() - 1);
        if (pos != last)
        {
            slot_of_[pos] = slot_of_[last];
        }
        slot_of_.pop_back();
    }

    void wipe() noexcept override
    {
        fire_remove_all();
        release_all();
        dense_.clear();
        sparse_.clear();
        slot_of_.clear();
        free_slots_.clear();
        release_chunks();
        slot_count_ = 0;
    }

    void compact() override
    {
        dense_.shrink_to_fit();
        slot_of_.shrink_to_fit();
        free_slots_.shrink_to_fit();
    }

    void reserve(std::size_t n)
    {
        dense_.reserve(n);
        slot_of_.reserve(n);
        while (chunks_.size() * chunk_items < n)
        {
            add_chunk();
        }
    }

    [[nodiscard]] std::size_t item_capacity() const noexcept override
    {
        return chunks_.size() * chunk_items;
    }

    [[nodiscard]] std::expected<void, fault> check(const entity_table& table) const override
    {
        if (slot_of_.size() != dense_.size())
        {
            return std::unexpected(fault{fault_code::slot_map_broken, name_, "slot count"});
        }
        if (dense_.size() + free_slots_.size() != slot_count_)
        {
            return std::unexpected(fault{fault_code::slot_map_broken, name_, "slot accounting"});
        }
        std::vector<std::uint8_t> used(slot_count_, 0);
        for (const index_type slot : slot_of_)
        {
            if (slot >= slot_count_ || used[slot] != 0)
            {
                return std::unexpected(fault{fault_code::slot_map_broken, name_, "dense slots"});
            }
            used[slot] = 1;
        }
        for (const index_type slot : free_slots_)
        {
            if (slot >= slot_count_ || used[slot] != 0)
            {
                return std::unexpected(fault{fault_code::slot_map_broken, name_, "free slots"});
            }
            used[slot] = 1;
        }
        return check_membership(table);
    }

private:
    [[nodiscard]] T* slot_ptr(index_type slot) noexcept
    {
        return reinterpret_cast<T*>(chunks_[slot / chunk_items]) + (slot % chunk_items);
    }

    [[nodiscard]] const T* slot_ptr(index_type slot) const noexcept
    {
        return reinterpret_cast<const T*>(chunks_[slot / chunk_items]) + (slot % chunk_items);
    }

    [[nodiscard]] index_type peek_free_slot()
    {
        if (!free_slots_.empty())
        {
            return free_slots_.back();
        }
        if (slot_count_ == chunks_.size() * chunk_items)
        {
            add_chunk();
        }
        return static_cast<index_type>(slot_count_);
    }

    void commit_free_slot(index_type slot) noexcept
    {
        if (!free_slots_.empty() && free_slots_.back() == slot)
        {
            free_slots_.pop_back();
        }
        else
        {
            ++slot_count_;
        }
    }

    void add_chunk()
    {
        if (chunks_.size() == chunks_.capacity())
        {
            chunks_.reserve(std::max<std::size_t>(4, chunks_.capacity() * 2));
        }
        free_slots_.reserve((chunks_.size() + 1) * chunk_items);
        auto* raw = static_cast<std::byte*>(memory_->allocate(chunk_items * sizeof(T), alignof(T)));
        chunks_.push_back(raw);
    }

    void release_all() noexcept
    {
        for (const index_type slot : slot_of_)
        {
            std::destroy_at(slot_ptr(slot));
        }
    }

    void release_chunks() noexcept
    {
        for (std::byte* chunk : chunks_)
        {
            memory_->deallocate(chunk, chunk_items * sizeof(T), alignof(T));
        }
        chunks_.clear();
    }

    std::pmr::vector<index_type> slot_of_;
    std::pmr::vector<std::byte*> chunks_;
    std::pmr::vector<index_type> free_slots_;
    std::size_t slot_count_ = 0;
};

template <component T, entity_traits Traits = default_entity_traits>
using pool_for = std::conditional_t<storage_policy<T> == storage::tag,
                                    tag_pool<T, Traits>,
                                    std::conditional_t<storage_policy<T> == storage::stable,
                                                       stable_pool<T, Traits>,
                                                       packed_pool<T, Traits>>>;
}  // namespace detail

using basic_pool = detail::pool_base;

template <component T, entity_traits Traits = default_entity_traits>
using packed_pool_of = detail::packed_pool<T, Traits>;
template <component T, entity_traits Traits = default_entity_traits>
using stable_pool_of = detail::stable_pool<T, Traits>;
template <component T, entity_traits Traits = default_entity_traits>
using tag_pool_of = detail::tag_pool<T, Traits>;

template <class T, entity_traits Traits = default_entity_traits>
struct pool_of
{
    using type = detail::pool_for<T, Traits>;
};

template <class T, entity_traits Traits = default_entity_traits>
using pool_of_t = pool_of<T, Traits>::type;

template <class P, class Traits = default_entity_traits>
concept component_pool = std::derived_from<P, detail::basic_pool_base<Traits>> &&
                         std::constructible_from<P, std::pmr::memory_resource*>;

namespace detail
{

template <class T, class... Us>
inline constexpr bool type_among = contains_type<bare<T>, types<bare<Us>...>>;

template <class... Ts>
inline constexpr bool all_distinct = all_unique_v<types<bare<Ts>...>>;
}  // namespace detail

template <class T>
struct maybe
{
};

template <class T>
struct exists
{
};

template <class T, class Pred>
struct where_filter
{
    Pred pred;
};

namespace detail
{
template <class T>
struct maybe_traits
{
    static constexpr bool is_maybe = false;
    using inner = T;
};

template <class T>
struct maybe_traits<maybe<T>>
{
    static constexpr bool is_maybe = true;
    using inner = T;
};

template <class T>
inline constexpr bool is_maybe_v = maybe_traits<T>::is_maybe;

template <class T>
using maybe_inner = maybe_traits<T>::inner;

template <class T>
struct not_maybe
{
    static constexpr bool value = !is_maybe_v<T>;
};

template <class T>
struct view_part
{
    using type = std::conditional_t<
        is_maybe_v<T>,
        std::tuple<std::conditional_t<std::is_const_v<maybe_inner<T>>,
                                      const bare<maybe_inner<T>>*,
                                      bare<maybe_inner<T>>*>>,
        std::conditional_t<
            is_tag_v<T>,
            std::tuple<>,
            std::tuple<std::conditional_t<std::is_const_v<T>, const bare<T>&, bare<T>&>>>>;
};

template <class T>
using view_part_t = view_part<T>::type;

template <class T>
struct as_const_part
{
    using type = const bare<T>;
};

template <class T>
struct as_const_part<maybe<T>>
{
    using type = maybe<const bare<T>>;
};

template <class F, class Entity, class Tuple>
struct callback_traits;

template <class F, class Entity, class... Refs>
struct callback_traits<F, Entity, std::tuple<Refs...>>
{
    static constexpr bool with_entity = std::invocable<F&, Entity, Refs...>;
    static constexpr bool without_entity = std::invocable<F&, Refs...>;
};

struct true_filter
{
};
template <class L, class R>
struct and_filter
{
    L lhs;
    R rhs;
};
template <class L, class R>
struct or_filter
{
    L lhs;
    R rhs;
};
template <class E>
struct not_filter
{
    E inner;
};

template <class T>
struct is_filter : std::false_type
{
};
template <>
struct is_filter<true_filter> : std::true_type
{
};
template <class T>
struct is_filter<exists<T>> : std::true_type
{
};
template <class E>
struct is_filter<not_filter<E>> : std::true_type
{
};
template <class L, class R>
struct is_filter<and_filter<L, R>> : std::true_type
{
};
template <class L, class R>
struct is_filter<or_filter<L, R>> : std::true_type
{
};

template <class T>
concept filter_expr = is_filter<std::remove_cvref_t<T>>::value;

template <class F>
struct filter_node
{
    static constexpr int kind = 0;
};
template <class T>
struct filter_node<exists<T>>
{
    static constexpr int kind = 1;
    using comp = bare<T>;
};
template <class E>
struct filter_node<not_filter<E>>
{
    static constexpr int kind = 4;
    using inner = E;
};
template <class L, class R>
struct filter_node<and_filter<L, R>>
{
    static constexpr int kind = 2;
    using lhs = L;
    using rhs = R;
};
template <class L, class R>
struct filter_node<or_filter<L, R>>
{
    static constexpr int kind = 3;
    using lhs = L;
    using rhs = R;
};

template <class F>
struct filter_leaves
{
    using type = types<>;
};
template <class T>
struct filter_leaves<exists<T>>
{
    using type = types<bare<T>>;
};
template <class E>
struct filter_leaves<not_filter<E>>
{
    using type = typename filter_leaves<E>::type;
};
template <class L, class R>
struct filter_leaves<and_filter<L, R>>
{
    using type = joined_t<typename filter_leaves<L>::type, typename filter_leaves<R>::type>;
};
template <class L, class R>
struct filter_leaves<or_filter<L, R>>
{
    using type = joined_t<typename filter_leaves<L>::type, typename filter_leaves<R>::type>;
};
template <class F>
using filter_leaves_t = typename filter_leaves<F>::type;

template <class T, class Pred>
struct is_filter<where_filter<T, Pred>> : std::true_type
{
};
template <class T, class Pred>
struct filter_node<where_filter<T, Pred>>
{
    static constexpr int kind = 5;
    using comp = bare<T>;
};
template <class T, class Pred>
struct filter_leaves<where_filter<T, Pred>>
{
    using type = types<bare<T>>;
};
}  // namespace detail

template <class L, class R>
    requires detail::filter_expr<L> && detail::filter_expr<R>
[[nodiscard]] constexpr detail::and_filter<L, R> operator&&(L lhs, R rhs) noexcept
{
    return {lhs, rhs};
}
template <class L, class R>
    requires detail::filter_expr<L> && detail::filter_expr<R>
[[nodiscard]] constexpr detail::or_filter<L, R> operator||(L lhs, R rhs) noexcept
{
    return {lhs, rhs};
}
template <class E>
    requires detail::filter_expr<E>
[[nodiscard]] constexpr detail::not_filter<E> operator!(E inner) noexcept
{
    return {inner};
}

template <class T, class Pred>
[[nodiscard]] constexpr where_filter<detail::bare<T>, Pred> where(Pred pred)
{
    return {std::move(pred)};
}

template <class Traits, class Filter, class... Ts>
class basic_view
{
    using entity = ecs::basic_entity<Traits>;
    using index_type = typename Traits::index_type;
    using pool_base = detail::basic_pool_base<Traits>;
    using view_t = basic_view;
    template <class T>
    using pool_of_t = ecs::pool_of_t<T, Traits>;

    static_assert(sizeof...(Ts) > 0, "ecs: a view needs at least one component type");
    static_assert((component<detail::bare<detail::maybe_inner<Ts>>> && ...),
                  "ecs: view component types must be plain object types "
                  "(no references, pointers, arrays, or cv-qualified types; maybe<T> is fine)");
    static_assert((!(detail::is_maybe_v<Ts> && detail::is_tag_v<detail::maybe_inner<Ts>>) && ...),
                  "ecs: maybe<T> of a tag component carries no data to point at; tags are "
                  "filter-only -- list the tag directly or use exists<T> in the filter");
    static_assert(!(detail::is_maybe_v<Ts> && ...),
                  "ecs: a view needs at least one required (non-maybe) component to drive "
                  "iteration");
    static_assert(detail::all_distinct<detail::bare<detail::maybe_inner<Ts>>...>,
                  "ecs: duplicate component type in view<...>");
    static_assert(detail::is_filter<Filter>::value,
                  "ecs: the view's second argument is not a "
                  "filter (compose exists<T> with && || !)");

    static constexpr std::array<bool, sizeof...(Ts)> optional_include{detail::is_maybe_v<Ts>...};
    static constexpr std::size_t leaf_count = detail::filter_leaves_t<Filter>::size;

    static constexpr std::size_t first_required = find_if<detail::not_maybe>(types<Ts...>{});
    template <class T>
    static consteval std::size_t slot_of()
    {
        constexpr std::array<bool, sizeof...(Ts)> match{
            std::same_as<detail::bare<detail::maybe_inner<Ts>>, detail::bare<T>>...};
        for (std::size_t i = 0; i < match.size(); ++i)
        {
            if (match[i])
            {
                return i;
            }
        }
        return sizeof...(Ts);
    }

    struct iteration_lock
    {
        explicit iteration_lock(const std::array<pool_base*, sizeof...(Ts)>& pools) noexcept
            : pools_(pools)
        {
            if constexpr (checks_enabled)
            {
                for (pool_base* pool : pools_)
                {
                    if (pool != nullptr)
                    {
                        ++pool->locks_;
                    }
                }
            }
        }

        ~iteration_lock()
        {
            if constexpr (checks_enabled)
            {
                for (pool_base* pool : pools_)
                {
                    if (pool != nullptr)
                    {
                        --pool->locks_;
                    }
                }
            }
        }

        iteration_lock(const iteration_lock&) = delete;
        iteration_lock& operator=(const iteration_lock&) = delete;

        const std::array<pool_base*, sizeof...(Ts)>& pools_;
    };

public:
    using included = types<Ts...>;
    using filter_type = Filter;

    basic_view() = default;

    template <class F>
    void each(F&& fn) const
    {
        run(fn,
            [this]<class G>(G& f, entity e, index_type index)
            { return this->invoke_with_refs(f, e, index); });
    }

    template <class F>
    void entities(F&& fn) const
    {
        run(fn,
            []<class G>(G& f, entity e, index_type)
            {
                if constexpr (std::predicate<G&, entity>)
                {
                    return static_cast<bool>(f(e));
                }
                else
                {
                    static_assert(std::invocable<G&, entity>,
                                  "ecs: entities() callback must be callable with (ecs::entity)");
                    f(e);
                    return true;
                }
            });
    }
    template <class F>
    void each_of(std::span<const entity> ids, F&& fn) const
    {
        if (includes_[first_required] == nullptr)
        {
            return;
        }
        const iteration_lock lock(includes_);
        for (const entity e : ids)
        {
            if (contains(e) && !invoke_with_refs(fn, e, e.index()))
            {
                break;
            }
        }
    }

    template <class T>
    [[nodiscard]] decltype(auto) get(entity e) const
    {
        constexpr std::size_t slot = slot_of<T>();
        static_assert(slot < sizeof...(Ts),
                      "ecs: view::get<T> needs T to be one of the view's component types");
        static_assert(slot >= sizeof...(Ts) || !optional_include[slot],
                      "ecs: view::get<T> cannot fetch a maybe<T> (it may be absent) -- use "
                      "each() or the range, which deliver maybe<> components as pointers");
        constexpr std::size_t s = slot < sizeof...(Ts) ? slot : 0;
        constexpr bool slot_is_const =
            std::array<bool, sizeof...(Ts)>{std::is_const_v<detail::maybe_inner<Ts>>...}[s];
        auto* pool = static_cast<pool_of_t<detail::bare<T>>*>(includes_[s]);
        if constexpr (checks_enabled)
        {
            if (pool == nullptr || !contains(e))
            {
                detail::violate_pool("view::get<T> for an entity the view does not match; pool",
                                     name_of<detail::bare<T>>());
                std::abort();
            }
        }
        if constexpr (slot_is_const)
        {
            return static_cast<const detail::bare<T>&>(*pool->at(e.index()));
        }
        else
        {
            return static_cast<detail::bare<T>&>(*pool->at(e.index()));
        }
    }

    template <class T, class U, class... Rest>
    [[nodiscard]] auto get(entity e) const
    {
        return std::tuple<decltype(get<T>(e)), decltype(get<U>(e)), decltype(get<Rest>(e))...>{
            get<T>(e), get<U>(e), (get<Rest>(e))...};
    }

    [[nodiscard]] std::size_t count() const noexcept
    {
        const pool_base* driver = smallest();
        if (driver == nullptr)
        {
            return 0;
        }
        if constexpr (sizeof...(Ts) == 1 && std::same_as<Filter, detail::true_filter>)
        {
            return driver->size();
        }
        else
        {
            std::size_t n = 0;
            for (std::size_t pos = 0; pos < driver->size(); ++pos)
            {
                n += matches_rest(driver->entity_at(pos).index(), driver) ? 1 : 0;
            }
            return n;
        }
    }

    [[nodiscard]] bool empty() const noexcept
    {
        const pool_base* driver = smallest();
        if (driver == nullptr || driver->size() == 0)
        {
            return true;
        }
        if constexpr (sizeof...(Ts) == 1 && std::same_as<Filter, detail::true_filter>)
        {
            return false;
        }
        else
        {
            for (std::size_t pos = 0; pos < driver->size(); ++pos)
            {
                if (matches_rest(driver->entity_at(pos).index(), driver))
                {
                    return false;
                }
            }
            return true;
        }
    }

    [[nodiscard]] bool contains(entity e) const noexcept
    {
        pool_base* first = includes_[first_required];
        if (first == nullptr)
        {
            return false;
        }
        const auto pos = first->position_of(e.index());
        if (pos == detail::entity_limits<Traits>::npos || first->entity_at(pos) != e)
        {
            return false;
        }
        return matches_rest(e.index(), first);
    }

    [[nodiscard]] entity first() const noexcept
    {
        entity result{};
        entities(
            [&](entity e)
            {
                result = e;
                return false;
            });
        return result;
    }

    [[nodiscard]] entity back() const noexcept
    {
        pool_base* driver = smallest();
        if (driver == nullptr)
        {
            return entity{};
        }
        for (std::size_t pos = driver->size(); pos-- > 0;)
        {
            const entity e = driver->entity_at(pos);
            if (matches_rest(e.index(), driver))
            {
                return e;
            }
        }
        return entity{};
    }

    [[nodiscard]] entity single() const
    {
        entity found{};
        std::size_t seen = 0;
        entities(
            [&](entity e)
            {
                if (seen == 0)
                {
                    found = e;
                }
                ++seen;
                return seen < 2;
            });
        if constexpr (checks_enabled)
        {
            if (seen != 1)
            {
                detail::violate("view::single() expected exactly one match");
            }
        }
        return seen == 1 ? found : entity{};
    }

    [[nodiscard]] std::vector<entity> collect() const
    {
        std::vector<entity> out;
        entities([&out](entity e) { out.push_back(e); });
        return out;
    }

    template <class T>
    [[nodiscard]] view_t driven_by() const noexcept
    {
        constexpr std::size_t slot = slot_of<T>();
        static_assert(slot < sizeof...(Ts),
                      "ecs: driven_by<T> needs T to be one of the view's component types");
        static_assert(slot >= sizeof...(Ts) || !optional_include[slot],
                      "ecs: driven_by<T> cannot drive from a maybe<> component");
        view_t out = *this;
        out.driver_override_ = static_cast<std::uint8_t>(slot);
        return out;
    }

    template <class Self = view_t>
    class basic_range
    {
    public:
        explicit basic_range(const Self& s) noexcept
            : self_(s),
              driver_(self_.smallest()),
              size_(driver_ != nullptr ? driver_->size() : 0)
        {
            if constexpr (checks_enabled)
            {
                if (driver_ != nullptr)
                {
                    for (pool_base* pool : self_.includes_)
                    {
                        if (pool != nullptr)
                        {
                            ++pool->locks_;
                        }
                    }
                }
            }
        }

        ~basic_range()
        {
            if constexpr (checks_enabled)
            {
                if (driver_ != nullptr)
                {
                    for (pool_base* pool : self_.includes_)
                    {
                        if (pool != nullptr)
                        {
                            --pool->locks_;
                        }
                    }
                }
            }
        }

        basic_range(const basic_range&) = delete;
        basic_range& operator=(const basic_range&) = delete;

        using row = decltype(std::tuple_cat(std::declval<std::tuple<entity>>(),
                                            std::declval<detail::view_part_t<Ts>>()...));

        class iterator
        {
        public:
            using value_type = row;
            using difference_type = std::ptrdiff_t;
            using iterator_concept = std::input_iterator_tag;

            iterator() = default;

            explicit iterator(const basic_range* owner) noexcept
                : owner_(owner)
            {
                settle();
            }

            [[nodiscard]] row operator*() const
            {
                const entity e = owner_->driver_->entity_at(pos_);
                return std::tuple_cat(std::tuple<entity>{e}, owner_->self_.value_refs(e.index()));
            }

            iterator& operator++() noexcept
            {
                ++pos_;
                settle();
                return *this;
            }

            void operator++(int) noexcept { ++*this; }

            friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept
            {
                return it.at_end();
            }

        private:
            [[nodiscard]] bool at_end() const noexcept { return pos_ >= owner_->size_; }

            void settle() noexcept
            {
                while (pos_ < owner_->size_ &&
                       !owner_->self_.matches_rest(owner_->driver_->entity_at(pos_).index(),
                                                   owner_->driver_))
                {
                    ++pos_;
                }
            }

            const basic_range* owner_ = nullptr;
            std::size_t pos_ = 0;
        };

        [[nodiscard]] iterator begin() const noexcept { return iterator(this); }
        [[nodiscard]] static std::default_sentinel_t end() noexcept { return {}; }

    private:
        Self self_;
        pool_base* driver_ = nullptr;
        std::size_t size_ = 0;
    };

    using range_t = basic_range<>;

    template <class Self = view_t>
    class basic_reverse_range
    {
    public:
        explicit basic_reverse_range(const Self& s) noexcept
            : self_(s),
              driver_(self_.smallest()),
              size_(driver_ != nullptr ? driver_->size() : 0)
        {
            if constexpr (checks_enabled)
            {
                if (driver_ != nullptr)
                {
                    for (pool_base* pool : self_.includes_)
                    {
                        if (pool != nullptr)
                        {
                            ++pool->locks_;
                        }
                    }
                }
            }
        }

        ~basic_reverse_range()
        {
            if constexpr (checks_enabled)
            {
                if (driver_ != nullptr)
                {
                    for (pool_base* pool : self_.includes_)
                    {
                        if (pool != nullptr)
                        {
                            --pool->locks_;
                        }
                    }
                }
            }
        }

        basic_reverse_range(const basic_reverse_range&) = delete;
        basic_reverse_range& operator=(const basic_reverse_range&) = delete;

        using row = decltype(std::tuple_cat(std::declval<std::tuple<entity>>(),
                                            std::declval<detail::view_part_t<Ts>>()...));

        class iterator
        {
        public:
            using value_type = row;
            using difference_type = std::ptrdiff_t;
            using iterator_concept = std::input_iterator_tag;

            iterator() = default;

            explicit iterator(const basic_reverse_range* owner) noexcept
                : owner_(owner),
                  pos_(owner->size_)
            {
                settle();
            }

            [[nodiscard]] row operator*() const
            {
                const entity e = owner_->driver_->entity_at(pos_ - 1);
                return std::tuple_cat(std::tuple<entity>{e}, owner_->self_.value_refs(e.index()));
            }

            iterator& operator++() noexcept
            {
                --pos_;
                settle();
                return *this;
            }

            void operator++(int) noexcept { ++*this; }

            friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept
            {
                return it.at_end();
            }

        private:
            [[nodiscard]] bool at_end() const noexcept { return pos_ == 0; }

            void settle() noexcept
            {
                while (pos_ > 0 &&
                       !owner_->self_.matches_rest(owner_->driver_->entity_at(pos_ - 1).index(),
                                                   owner_->driver_))
                {
                    --pos_;
                }
            }

            const basic_reverse_range* owner_ = nullptr;
            std::size_t pos_ = 0;
        };

        [[nodiscard]] iterator begin() const noexcept { return iterator(this); }
        [[nodiscard]] static std::default_sentinel_t end() noexcept { return {}; }

    private:
        Self self_;
        pool_base* driver_ = nullptr;
        std::size_t size_ = 0;
    };

    using reverse_range_t = basic_reverse_range<>;

    [[nodiscard]] auto each() const noexcept { return basic_range<>(*this); }
    [[nodiscard]] auto reversed() const noexcept { return basic_reverse_range<>(*this); }

    class entity_cursor
    {
    public:
        using value_type = entity;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        entity_cursor() = default;

        explicit entity_cursor(const basic_view* view) noexcept
            : view_(view),
              driver_(view->smallest())
        {
            settle();
        }

        [[nodiscard]] entity operator*() const noexcept { return driver_->entity_at(pos_); }

        entity_cursor& operator++() noexcept
        {
            ++pos_;
            settle();
            return *this;
        }
        void operator++(int) noexcept { ++*this; }

        friend bool operator==(const entity_cursor& it, std::default_sentinel_t) noexcept
        {
            return it.driver_ == nullptr || it.pos_ >= it.driver_->size();
        }

    private:
        void settle() noexcept
        {
            while (driver_ != nullptr && pos_ < driver_->size() &&
                   !view_->matches_rest(driver_->entity_at(pos_).index(), driver_))
            {
                ++pos_;
            }
        }

        const basic_view* view_ = nullptr;
        pool_base* driver_ = nullptr;
        std::size_t pos_ = 0;
    };

    [[nodiscard]] entity_cursor begin() const noexcept { return entity_cursor(this); }
    [[nodiscard]] static std::default_sentinel_t end() noexcept { return {}; }

    class part_t
    {
    public:
        part_t() = default;

        template <class F>
        void each(F&& fn) const
        {
            if (owner_self_ == nullptr)
            {
                return;
            }
            const view_t& s = *owner_self_;
            s.run_span(
                fn,
                [&s]<class G>(G& f, entity e, index_type index)
                { return s.invoke_with_refs(f, e, index); },
                driver_,
                begin_,
                end_);
        }

        template <class F>
        void entities(F&& fn) const
        {
            if (owner_self_ == nullptr)
            {
                return;
            }
            owner_self_->run_span(
                fn,
                []<class G>(G& f, entity e, index_type)
                {
                    if constexpr (std::predicate<G&, entity>)
                    {
                        return static_cast<bool>(f(e));
                    }
                    else
                    {
                        static_assert(std::invocable<G&, entity>,
                                      "ecs: entities() callback must be callable with "
                                      "(ecs::entity)");
                        f(e);
                        return true;
                    }
                },
                driver_,
                begin_,
                end_);
        }

    private:
        friend class basic_view;

        part_t(const view_t* self, pool_base* driver, std::size_t begin, std::size_t end) noexcept
            : owner_self_(self),
              driver_(driver),
              begin_(begin),
              end_(end)
        {
        }

        const view_t* owner_self_ = nullptr;
        pool_base* driver_ = nullptr;
        std::size_t begin_ = 0;
        std::size_t end_ = 0;
    };

    template <class Self>
    class basic_split
    {
    public:
        basic_split(const Self& s, std::size_t parts) noexcept
            : self_(s),
              driver_(self_.smallest()),
              lock_(self_.includes_),
              size_(driver_ != nullptr ? driver_->size() : 0),
              parts_(parts == 0 ? 1 : parts)
        {
        }

        basic_split(const basic_split&) = delete;
        basic_split& operator=(const basic_split&) = delete;

        [[nodiscard]] std::size_t parts() const noexcept { return parts_; }

        [[nodiscard]] part_t part(std::size_t i) const noexcept
        {
            if (i >= parts_ || size_ == 0)
            {
                return {};
            }
            return part_t(&self_, driver_, (i * size_) / parts_, ((i + 1) * size_) / parts_);
        }

    private:
        Self self_;
        pool_base* driver_;
        iteration_lock lock_;
        std::size_t size_;
        std::size_t parts_;
    };

    [[nodiscard]] auto split(std::size_t parts) const noexcept
    {
        return basic_split<view_t>(*this, parts);
    }

private:
    template <class>
    friend class basic_registry;
    friend struct test_access;

    explicit basic_view(std::array<pool_base*, sizeof...(Ts)> includes,
                        std::array<pool_base*, leaf_count> filter_pools,
                        Filter filter = {}) noexcept
        : includes_(includes),
          filter_pools_(filter_pools),
          filter_(std::move(filter))
    {
    }

    [[nodiscard]] pool_base* smallest() const noexcept
    {
        if (driver_override_ != no_driver_override)
        {
            return includes_[driver_override_];
        }
        pool_base* best = nullptr;
        for (std::size_t i = 0; i < sizeof...(Ts); ++i)
        {
            if (optional_include[i])
            {
                continue;
            }
            pool_base* pool = includes_[i];
            if (pool == nullptr)
            {
                return nullptr;
            }
            if (best == nullptr || pool->size() < best->size())
            {
                best = pool;
            }
        }
        return best;
    }

    [[nodiscard]] bool matches_rest(index_type index, const pool_base* driver) const noexcept
    {
        for (std::size_t i = 0; i < sizeof...(Ts); ++i)
        {
            if (optional_include[i])
            {
                continue;
            }
            pool_base* pool = includes_[i];
            if (pool != driver && (pool == nullptr || !pool->contains(index)))
            {
                return false;
            }
        }
        return passes_filter(index);
    }

    [[nodiscard]] bool passes_filter(index_type index) const noexcept
    {
        if constexpr (std::same_as<Filter, detail::true_filter>)
        {
            return true;
        }
        else
        {
            std::size_t leaf = 0;
            return eval_filter(filter_, index, leaf);
        }
    }

    template <class F>
    [[nodiscard]] bool eval_filter(const F& node,
                                   index_type index,
                                   std::size_t& leaf) const noexcept
    {
        using fnode = detail::filter_node<F>;
        if constexpr (fnode::kind == 1)  // exists<T>
        {
            pool_base* pool = filter_pools_[leaf++];
            return pool != nullptr && pool->contains(index);
        }
        else if constexpr (fnode::kind == 5)  // where<T>(pred)
        {
            auto* pool = static_cast<pool_of_t<typename fnode::comp>*>(filter_pools_[leaf++]);
            if (pool == nullptr)
            {
                return false;
            }
            auto* value = pool->at(index);
            return value != nullptr && node.pred(*value);
        }
        else if constexpr (fnode::kind == 2)  // and
        {
            const bool l = eval_filter(node.lhs, index, leaf);
            const bool r = eval_filter(node.rhs, index, leaf);
            return l && r;
        }
        else if constexpr (fnode::kind == 3)  // or
        {
            const bool l = eval_filter(node.lhs, index, leaf);
            const bool r = eval_filter(node.rhs, index, leaf);
            return l || r;
        }
        else if constexpr (fnode::kind == 4)  // not
        {
            return !eval_filter(node.inner, index, leaf);
        }
        else  // true_filter
        {
            return true;
        }
    }

    template <class F, class Invoke>
    void run(F& fn, Invoke&& invoke) const
    {
        pool_base* driver = smallest();
        if (driver == nullptr)
        {
            return;
        }
        const iteration_lock lock(includes_);
        run_span(fn, invoke, driver, 0, driver->size());
    }

    template <class F, class Invoke>
    void run_span(
        F& fn, Invoke&& invoke, pool_base* driver, std::size_t begin, std::size_t end) const
    {
        for (std::size_t pos = begin; pos < end; ++pos)
        {
            const entity e = driver->entity_at(pos);
            if (!matches_rest(e.index(), driver))
            {
                continue;
            }
            if (!invoke(fn, e, e.index()))
            {
                break;
            }
        }
    }

    template <std::size_t I, class T>
    [[nodiscard]] detail::view_part_t<T> ref_tuple(index_type index) const
    {
        if constexpr (detail::is_maybe_v<T>)
        {
            using inner = detail::bare<detail::maybe_inner<T>>;
            auto* pool = static_cast<pool_of_t<inner>*>(includes_[I]);
            return detail::view_part_t<T>{pool == nullptr ? nullptr : pool->at(index)};
        }
        else if constexpr (detail::is_tag_v<T>)
        {
            return std::tuple<>{};
        }
        else
        {
            auto* pool = static_cast<pool_of_t<detail::bare<T>>*>(includes_[I]);
            return detail::view_part_t<T>{*pool->at(index)};
        }
    }

    [[nodiscard]] auto value_refs(index_type index) const
    {
        return [this, index]<std::size_t... Is>(std::index_sequence<Is...>)
        {
            return std::tuple_cat(this->template ref_tuple<Is, Ts>(index)...);
        }(std::index_sequence_for<Ts...>{});
    }

    template <class F>
    bool invoke_with_refs(F& fn, entity e, index_type index) const
    {
        auto refs = value_refs(index);
        using traits = detail::callback_traits<F, entity, decltype(refs)>;
        if constexpr (traits::with_entity)
        {
            return finish(fn, std::tuple_cat(std::tuple<entity>{e}, std::move(refs)));
        }
        else if constexpr (traits::without_entity)
        {
            return finish(fn, std::move(refs));
        }
        else
        {
            static_assert(traits::with_entity || traits::without_entity,
                          "ecs: each() callback must be callable as (entity, components&...) or "
                          "(components&...). Components from a const world or marked const are "
                          "passed as const&; maybe<T> as a pointer; tags are filter-only and not "
                          "passed at all.");
            return false;
        }
    }

    template <class F, class Args>
    static bool finish(F& fn, Args&& args)
    {
        using result = decltype(std::apply(fn, std::forward<Args>(args)));
        if constexpr (std::convertible_to<result, bool>)
        {
            return static_cast<bool>(std::apply(fn, std::forward<Args>(args)));
        }
        else
        {
            std::apply(fn, std::forward<Args>(args));
            return true;
        }
    }

    static constexpr std::uint8_t no_driver_override = 0xFF;
    static_assert(sizeof...(Ts) < no_driver_override, "ecs: a view caps at 254 component types");

    std::array<pool_base*, sizeof...(Ts)> includes_{};
    std::array<pool_base*, leaf_count> filter_pools_{};
    Filter filter_{};
    std::uint8_t driver_override_ = no_driver_override;
};

template <class... Ts>
using view = basic_view<default_entity_traits, detail::true_filter, Ts...>;

template <class Traits>
class basic_pool_ref
{
    using entity = ecs::basic_entity<Traits>;
    using pool_base = detail::basic_pool_base<Traits>;

public:
    basic_pool_ref() = default;

    [[nodiscard]] explicit operator bool() const noexcept { return pool_ != nullptr; }
    [[nodiscard]] std::string_view name() const noexcept
    {
        return pool_ != nullptr ? pool_->name() : std::string_view{};
    }
    [[nodiscard]] std::uint64_t name_hash() const noexcept
    {
        return pool_ != nullptr ? pool_->name_hash() : 0;
    }
    [[nodiscard]] std::size_t size() const noexcept { return pool_ != nullptr ? pool_->size() : 0; }

    [[nodiscard]] pool_info info() const noexcept
    {
        return pool_ != nullptr ? pool_->info() : pool_info{};
    }

    [[nodiscard]] bool contains(entity e) const noexcept
    {
        if (pool_ == nullptr)
        {
            return false;
        }
        const auto pos = pool_->position_of(e.index());
        return pos != detail::entity_limits<Traits>::npos && pool_->entity_at(pos) == e;
    }

    [[nodiscard]] void* raw(entity e) const noexcept
    {
        if (pool_ == nullptr)
        {
            return nullptr;
        }
        const auto pos = pool_->position_of(e.index());
        if (pos == detail::entity_limits<Traits>::npos || pool_->entity_at(pos) != e)
        {
            return nullptr;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        return const_cast<pool_base*>(pool_)->item_address(pos);
    }

    [[nodiscard]] void* raw_at(std::size_t pos) const noexcept
    {
        if (pool_ == nullptr || pos >= pool_->size())
        {
            return nullptr;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        return const_cast<pool_base*>(pool_)->item_address(
            static_cast<typename Traits::index_type>(pos));
    }

    [[nodiscard]] entity entity_at(std::size_t pos) const noexcept
    {
        return pool_ != nullptr && pos < pool_->size() ? pool_->entity_at(pos) : entity{};
    }

private:
    template <class>
    friend class basic_registry;
    template <class>
    friend class basic_runtime_selection;

    explicit basic_pool_ref(const pool_base* pool) noexcept
        : pool_(pool)
    {
    }

    const pool_base* pool_ = nullptr;
};

using pool_ref = basic_pool_ref<default_entity_traits>;

template <class Traits>
class basic_runtime_selection
{
    using entity = ecs::basic_entity<Traits>;
    using index_type = typename Traits::index_type;
    using pool_base = detail::basic_pool_base<Traits>;
    using pool_ref = basic_pool_ref<Traits>;
    using runtime_selection = basic_runtime_selection;

public:
    basic_runtime_selection() = default;

    runtime_selection& include(pool_ref pool)
    {
        if (pool.pool_ == nullptr)
        {
            impossible_ = true;
        }
        else
        {
            includes_.push_back(pool.pool_);
        }
        return *this;
    }

    runtime_selection& exclude(pool_ref pool)
    {
        if (pool.pool_ != nullptr)
        {
            excludes_.push_back(pool.pool_);
        }
        return *this;
    }

    void clear() noexcept
    {
        includes_.clear();
        excludes_.clear();
        impossible_ = false;
    }

    template <class F>
    void entities(F&& fn) const
    {
        const pool_base* driver = smallest();
        if (driver == nullptr)
        {
            return;
        }
        const std::vector<const pool_base*> includes = includes_;
        const std::vector<const pool_base*> excludes = excludes_;
        const lock_all lock(includes);
        const std::size_t n = driver->size();
        for (std::size_t pos = 0; pos < n; ++pos)
        {
            const entity e = driver->entity_at(pos);
            if (!matches_lists(includes, excludes, e.index(), driver))
            {
                continue;
            }
            if constexpr (std::predicate<F&, entity>)
            {
                if (!fn(e))
                {
                    break;
                }
            }
            else
            {
                static_assert(std::invocable<F&, entity>,
                              "ecs: runtime_selection callback must be callable with "
                              "(ecs::entity)");
                fn(e);
            }
        }
    }

    [[nodiscard]] std::size_t count() const
    {
        std::size_t n = 0;
        entities(
            [&](entity)
            {
                ++n;
                return true;
            });
        return n;
    }

    [[nodiscard]] bool contains(entity e) const noexcept
    {
        if (impossible_ || includes_.empty())
        {
            return false;
        }
        const auto pos = includes_[0]->position_of(e.index());
        if (pos == detail::entity_limits<Traits>::npos || includes_[0]->entity_at(pos) != e)
        {
            return false;
        }
        return matches_rest(e.index(), includes_[0]);
    }

private:
    struct lock_all
    {
        explicit lock_all(const std::vector<const pool_base*>& pools) noexcept
            : pools_(pools)
        {
            if constexpr (checks_enabled)
            {
                for (const pool_base* pool : pools_)
                {
                    ++pool->locks_;
                }
            }
        }

        ~lock_all()
        {
            if constexpr (checks_enabled)
            {
                for (const pool_base* pool : pools_)
                {
                    --pool->locks_;
                }
            }
        }

        lock_all(const lock_all&) = delete;
        lock_all& operator=(const lock_all&) = delete;

        const std::vector<const pool_base*>& pools_;
    };

    [[nodiscard]] static bool matches_lists(const std::vector<const pool_base*>& includes,
                                            const std::vector<const pool_base*>& excludes,
                                            index_type index,
                                            const pool_base* driver) noexcept
    {
        for (const pool_base* pool : includes)
        {
            if (pool != driver && !pool->contains(index))
            {
                return false;
            }
        }
        for (const pool_base* pool : excludes)
        {
            if (pool->contains(index))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] const pool_base* smallest() const noexcept
    {
        if (impossible_ || includes_.empty())
        {
            return nullptr;
        }
        const pool_base* best = nullptr;
        for (const pool_base* pool : includes_)
        {
            if (best == nullptr || pool->size() < best->size())
            {
                best = pool;
            }
        }
        return best;
    }

    [[nodiscard]] bool matches_rest(index_type index, const pool_base* driver) const noexcept
    {
        for (const pool_base* pool : includes_)
        {
            if (pool != driver && !pool->contains(index))
            {
                return false;
            }
        }
        for (const pool_base* pool : excludes_)
        {
            if (pool->contains(index))
            {
                return false;
            }
        }
        return true;
    }

    std::vector<const pool_base*> includes_;
    std::vector<const pool_base*> excludes_;
    bool impossible_ = false;
};

using runtime_selection = basic_runtime_selection<default_entity_traits>;

namespace detail
{
class payload_arena
{
public:
    static constexpr std::size_t bump_align = 64;
    static constexpr std::size_t chunk_bytes = 4096;

    explicit payload_arena(std::pmr::memory_resource* memory) noexcept
        : memory_(memory)
    {
    }

    payload_arena(payload_arena&& other) noexcept
        : memory_(other.memory_),
          chunks_(std::move(other.chunks_)),
          cursor_(std::exchange(other.cursor_, 0))
    {
        other.chunks_.clear();
    }

    payload_arena& operator=(payload_arena&& other) noexcept
    {
        if (this != &other)
        {
            release();
            chunks_.swap(other.chunks_);
            memory_ = other.memory_;
            cursor_ = std::exchange(other.cursor_, 0);
        }
        return *this;
    }

    payload_arena(const payload_arena&) = delete;
    payload_arena& operator=(const payload_arena&) = delete;

    ~payload_arena() { release(); }

    void* allocate(std::size_t bytes, std::size_t align)
    {
        if (align > bump_align)
        {
            for (chunk& c : chunks_)
            {
                if (c.used == 0 && c.align >= align && c.capacity >= bytes)
                {
                    c.used = bytes;
                    return c.memory;
                }
            }
            reserve_chunk_slot();
            auto* memory = static_cast<std::byte*>(memory_->allocate(bytes, align));
            chunks_.push_back(chunk{memory, bytes, bytes, align});
            return memory;
        }
        while (cursor_ < chunks_.size())
        {
            chunk& c = chunks_[cursor_];
            const std::size_t at = (c.used + align - 1) & ~(align - 1);
            if (at + bytes <= c.capacity)
            {
                c.used = at + bytes;
                return c.memory + at;
            }
            ++cursor_;
        }
        reserve_chunk_slot();
        const std::size_t capacity = std::max(chunk_bytes, bytes);
        auto* memory = static_cast<std::byte*>(memory_->allocate(capacity, bump_align));
        chunks_.push_back(chunk{memory, capacity, bytes, bump_align});
        cursor_ = chunks_.size() - 1;
        return memory;
    }

    void reset() noexcept
    {
        for (chunk& c : chunks_)
        {
            c.used = 0;
        }
        cursor_ = 0;
    }

    void release() noexcept
    {
        for (const chunk& c : chunks_)
        {
            memory_->deallocate(c.memory, c.capacity, c.align);
        }
        chunks_.clear();
        cursor_ = 0;
    }

private:
    struct chunk
    {
        std::byte* memory;
        std::size_t capacity;
        std::size_t used;
        std::size_t align;
    };

    void reserve_chunk_slot()
    {
        if (chunks_.size() == chunks_.capacity())
        {
            chunks_.reserve(std::max<std::size_t>(4, chunks_.capacity() * 2));
        }
    }

    std::pmr::memory_resource* memory_;
    std::vector<chunk> chunks_;
    std::size_t cursor_ = 0;
};
}  // namespace detail

template <class Traits>
class basic_command_buffer
{
    using entity = ecs::basic_entity<Traits>;
    using world = ecs::basic_registry<Traits>;
    using kin = detail::basic_kin<Traits>;
    using limits = detail::entity_limits<Traits>;
    using index_type = typename Traits::index_type;
    using generation_type = typename Traits::generation_type;

public:
    explicit basic_command_buffer(
        std::pmr::memory_resource* memory = std::pmr::get_default_resource()) noexcept
        : ops_(memory),
          arena_(memory),
          resolved_(memory)
    {
    }

    basic_command_buffer(basic_command_buffer&& other) noexcept
        : ops_(std::move(other.ops_)),
          arena_(std::move(other.arena_)),
          resolved_(std::move(other.resolved_)),
          ticket_count_(std::exchange(other.ticket_count_, 0)),
          nonce_(std::exchange(other.nonce_, fresh_nonce()))
    {
    }

    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor,bugprone-exception-escape)
    basic_command_buffer& operator=(basic_command_buffer&& other)
    {
        if (this != &other)
        {
            destroy_payloads();
            ops_.clear();
            ops_ = std::move(other.ops_);
            other.ops_.clear();
            arena_ = std::move(other.arena_);
            resolved_ = std::move(other.resolved_);
            other.resolved_.clear();
            ticket_count_ = std::exchange(other.ticket_count_, 0);
            nonce_ = std::exchange(other.nonce_, fresh_nonce());
        }
        return *this;
    }

    basic_command_buffer(const basic_command_buffer&) = delete;
    basic_command_buffer& operator=(const basic_command_buffer&) = delete;

    ~basic_command_buffer() { destroy_payloads(); }

    entity create()
    {
        const entity provisional(static_cast<index_type>(limits::provisional_bit | ticket_count_),
                                 nonce_);
        if constexpr (checks_enabled)
        {
            if (ticket_count_ >= limits::provisional_bit - 1)
            {
                detail::violate("command_buffer: provisional ticket overflow");
            }
        }
        ++ticket_count_;
        reserve_op();
        ops_.push_back(op{op_kind::spawn, provisional, nullptr, nullptr, nullptr});
        return provisional;
    }

    template <class... Cs>
        requires(sizeof...(Cs) > 0 && (component<std::remove_cvref_t<Cs>> && ...) &&
                 (!std::same_as<std::remove_cvref_t<Cs>, basic_blueprint<Traits>> && ...))
    entity create(Cs&&... components)
    {
        static_assert(detail::all_distinct<std::remove_cvref_t<Cs>...>,
                      "ecs: duplicate component type in create(...)");
        const entity provisional = create();
        (record_value(provisional, std::forward<Cs>(components)), ...);
        return provisional;
    }

    template <component T, class... Args>
    void add(entity target, Args&&... args)
    {
        record<T, op_kind::add>(target, std::forward<Args>(args)...);
    }

    template <component T, class... Args>
    void put(entity target, Args&&... args)
    {
        record<T, op_kind::put>(target, std::forward<Args>(args)...);
    }

    template <component T>
    void remove(entity target)
    {
        static_assert(!std::same_as<T, kin>,
                      "ecs: parent/child links are managed via adopt/orphan/kill");
        reserve_op();
        ops_.push_back(op{op_kind::remove,
                          target,
                          [](world& w, entity e, void*) { erased_remove<T>(w, e); },
                          nullptr,
                          nullptr});
    }

    void destroy(entity target)
    {
        reserve_op();
        ops_.push_back(op{op_kind::kill, target, nullptr, nullptr, nullptr});
    }

    void adopt(entity parent, entity child)
    {
        reserve_op();
        auto* slot = static_cast<entity*>(arena_.allocate(sizeof(entity), alignof(entity)));
        std::construct_at(slot, parent);
        ops_.push_back(op{op_kind::adopt, child, nullptr, nullptr, slot});
    }

    void orphan(entity child)
    {
        reserve_op();
        ops_.push_back(op{op_kind::orphan, child, nullptr, nullptr, nullptr});
    }

    void destroy_subtree(entity root)
    {
        reserve_op();
        ops_.push_back(op{op_kind::subtree_kill, root, nullptr, nullptr, nullptr});
    }

    [[nodiscard]] bool empty() const noexcept { return ops_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return ops_.size(); }

    void clear() noexcept
    {
        destroy_payloads();
        ops_.clear();
        ticket_count_ = 0;
        resolved_.clear();
        arena_.reset();
        nonce_ = fresh_nonce();
    }

private:
    template <class>
    friend class basic_registry;

    [[nodiscard]] static generation_type fresh_nonce() noexcept
    {
        return static_cast<generation_type>(detail::next_buffer_nonce());
    }

    enum class op_kind : std::uint8_t
    {
        spawn,
        add,
        put,
        remove,
        kill,
        adopt,
        orphan,
        subtree_kill,
    };

    struct op
    {
        op_kind kind;
        entity target;
        void (*apply_fn)(world&, entity, void*);
        void (*destroy_fn)(void*) noexcept;
        void* payload;
    };

    void destroy_payloads() noexcept
    {
        for (const op& o : ops_)
        {
            if (o.destroy_fn != nullptr)
            {
                o.destroy_fn(o.payload);
            }
        }
    }

    void reserve_op()
    {
        if (ops_.size() == ops_.capacity())
        {
            ops_.reserve(std::max<std::size_t>(16, ops_.capacity() * 2));
        }
    }

    template <class C>
    void record_value(entity target, C&& value)
    {
        using T = std::remove_cvref_t<C>;
        if constexpr (detail::is_tag_v<T>)
        {
            record<T, op_kind::add>(target);
        }
        else
        {
            record<T, op_kind::add>(target, std::forward<C>(value));
        }
    }

    template <component T, op_kind Kind, class... Args>
    void record(entity target, Args&&... args)
    {
        reserve_op();
        if constexpr (detail::is_tag_v<T>)
        {
            static_assert(sizeof...(Args) == 0,
                          "ecs: T uses tag storage and carries no data; record add<T>(e) "
                          "with no arguments");
            ops_.push_back(op{Kind,
                              target,
                              [](world& w, entity e, void*) { erased_apply_tag<T, Kind>(w, e); },
                              nullptr,
                              nullptr});
        }
        else
        {
            static_assert(std::is_move_constructible_v<T>,
                          "ecs: command_buffer payloads are moved into the world at apply(); "
                          "add non-movable components directly via world::add");
            T* payload = std::construct_at(static_cast<T*>(arena_.allocate(sizeof(T), alignof(T))),
                                           std::forward<Args>(args)...);
            void (*destroy_fn)(void*) noexcept = nullptr;
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                destroy_fn = [](void* p) noexcept { std::destroy_at(static_cast<T*>(p)); };
            }
            ops_.push_back(op{Kind,
                              target,
                              [](world& w, entity e, void* p) { erased_apply<T, Kind>(w, e, p); },
                              destroy_fn,
                              payload});
        }
    }

    template <component T, op_kind Kind>
    static void erased_apply(world& w, entity target, void* payload)
    {
        T& value = *static_cast<T*>(payload);
        if constexpr (Kind == op_kind::add)
        {
            w.template add<T>(target, std::move(value));
        }
        else
        {
            w.template put<T>(target, std::move(value));
        }
    }

    template <component T, op_kind Kind>
    static void erased_apply_tag(world& w, entity target)
    {
        if constexpr (Kind == op_kind::add)
        {
            w.template add<T>(target);
        }
        else
        {
            w.template put<T>(target);
        }
    }

    template <component T>
    static void erased_remove(world& w, entity target)
    {
        w.template remove<T>(target);
    }

    std::pmr::vector<op> ops_;
    detail::payload_arena arena_;
    std::pmr::vector<entity> resolved_;
    index_type ticket_count_ = 0;
    generation_type nonce_ = fresh_nonce();
};

using command_buffer = basic_command_buffer<default_entity_traits>;

template <class Traits>
class basic_blueprint
{
    using entity = ecs::basic_entity<Traits>;
    using world = ecs::basic_registry<Traits>;
    using blueprint = basic_blueprint;

public:
    explicit basic_blueprint(
        std::pmr::memory_resource* memory = std::pmr::get_default_resource()) noexcept
        : ops_(memory),
          arena_(memory)
    {
    }

    template <class... Cs>
        requires(sizeof...(Cs) > 0 && (component<std::remove_cvref_t<Cs>> && ...) &&
                 (!std::same_as<std::remove_cvref_t<Cs>, basic_blueprint> && ...) &&
                 (!std::convertible_to<Cs &&, std::pmr::memory_resource*> && ...))
    explicit basic_blueprint(Cs&&... components)
        : basic_blueprint()
    {
        static_assert(detail::all_distinct<std::remove_cvref_t<Cs>...>,
                      "ecs: duplicate component type in blueprint{...}");
        (record_value(std::forward<Cs>(components)), ...);
    }

    template <class... Cs>
        requires(sizeof...(Cs) > 0 && (component<std::remove_cvref_t<Cs>> && ...) &&
                 (!std::same_as<std::remove_cvref_t<Cs>, basic_blueprint> && ...) &&
                 (!std::convertible_to<Cs &&, std::pmr::memory_resource*> && ...))
    basic_blueprint(std::pmr::memory_resource* memory, Cs&&... components)
        : basic_blueprint(memory)
    {
        static_assert(detail::all_distinct<std::remove_cvref_t<Cs>...>,
                      "ecs: duplicate component type in blueprint{...}");
        (record_value(std::forward<Cs>(components)), ...);
    }

    basic_blueprint(basic_blueprint&& other) noexcept
        : ops_(std::move(other.ops_)),
          arena_(std::move(other.arena_))
    {
        other.ops_.clear();
    }

    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor,bugprone-exception-escape)
    basic_blueprint& operator=(basic_blueprint&& other)
    {
        if (this != &other)
        {
            destroy_payloads();
            ops_.clear();
            ops_ = std::move(other.ops_);
            other.ops_.clear();
            arena_ = std::move(other.arena_);
        }
        return *this;
    }

    basic_blueprint(const basic_blueprint&) = delete;
    basic_blueprint& operator=(const basic_blueprint&) = delete;

    ~basic_blueprint() { destroy_payloads(); }

    template <component T, class... Args>
    basic_blueprint& add(Args&&... args)
    {
        if constexpr (checks_enabled)
        {
            for (const op& o : ops_)
            {
                if (o.type == detail::type_id<T>())
                {
                    detail::violate_pool("blueprint already contains component", name_of<T>());
                    return *this;
                }
            }
        }
        reserve_op();
        if constexpr (detail::is_tag_v<T>)
        {
            static_assert(sizeof...(Args) == 0,
                          "ecs: T uses tag storage and carries no data; record add<T>() "
                          "with no arguments");
            ops_.push_back(op{detail::type_id<T>(),
                              [](world& w, entity e, const void*) { erased_stamp<T>(w, e); },
                              nullptr,
                              nullptr});
        }
        else
        {
            static_assert(std::copy_constructible<T>,
                          "ecs: blueprint components are copied at every spawn; move-only "
                          "types cannot be stamped repeatedly -- add them per entity instead");
            static_assert(std::constructible_from<T, Args&&...>,
                          "ecs: T cannot be constructed from these blueprint arguments");
            T* payload = std::construct_at(static_cast<T*>(arena_.allocate(sizeof(T), alignof(T))),
                                           std::forward<Args>(args)...);
            void (*destroy_fn)(void*) noexcept = nullptr;
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                destroy_fn = [](void* p) noexcept { std::destroy_at(static_cast<T*>(p)); };
            }
            ops_.push_back(op{detail::type_id<T>(),
                              [](world& w, entity e, const void* p)
                              { erased_stamp_value<T>(w, e, p); },
                              destroy_fn,
                              payload});
        }
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept { return ops_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return ops_.size(); }

    void clear() noexcept
    {
        destroy_payloads();
        ops_.clear();
        arena_.reset();
    }

private:
    template <class>
    friend class basic_registry;

    struct op
    {
        std::uint32_t type;
        void (*apply_fn)(world&, entity, const void*);
        void (*destroy_fn)(void*) noexcept;
        void* payload;
    };

    void destroy_payloads() noexcept
    {
        for (const op& o : ops_)
        {
            if (o.destroy_fn != nullptr)
            {
                o.destroy_fn(o.payload);
            }
        }
    }

    void reserve_op()
    {
        if (ops_.size() == ops_.capacity())
        {
            ops_.reserve(std::max<std::size_t>(8, ops_.capacity() * 2));
        }
    }

    template <class C>
    void record_value(C&& value)
    {
        using T = std::remove_cvref_t<C>;
        if constexpr (detail::is_tag_v<T>)
        {
            add<T>();
        }
        else
        {
            add<T>(std::forward<C>(value));
        }
    }

    template <component T>
    static void erased_stamp(world& w, entity e)
    {
        w.template add<T>(e);
    }

    template <component T>
    static void erased_stamp_value(world& w, entity e, const void* payload)
    {
        w.template add<T>(e, *static_cast<const T*>(payload));
    }

    std::pmr::vector<op> ops_;
    detail::payload_arena arena_;
};

using blueprint = basic_blueprint<default_entity_traits>;

namespace detail
{

template <class Traits>
struct basic_kin
{
    using entity = ecs::basic_entity<Traits>;

    entity parent{};
    entity first_child{};
    entity last_child{};
    entity prev_sibling{};
    entity next_sibling{};

    static constexpr auto ecs_storage = storage::stable;
};

namespace kin_links
{
template <class Traits>
void unlink(stable_pool<basic_kin<Traits>, Traits>& pool, basic_kin<Traits>& child_k) noexcept
{
    constexpr ecs::basic_entity<Traits> no_entity{};
    basic_kin<Traits>* parent_k = pool.at(child_k.parent.index());
    if (child_k.prev_sibling != no_entity)
    {
        pool.at(child_k.prev_sibling.index())->next_sibling = child_k.next_sibling;
    }
    else
    {
        parent_k->first_child = child_k.next_sibling;
    }
    if (child_k.next_sibling != no_entity)
    {
        pool.at(child_k.next_sibling.index())->prev_sibling = child_k.prev_sibling;
    }
    else
    {
        parent_k->last_child = child_k.prev_sibling;
    }
}

template <class Traits>
void sever(stable_pool<basic_kin<Traits>, Traits>& pool, ecs::basic_entity<Traits> e) noexcept
{
    using entity = ecs::basic_entity<Traits>;
    using kin = basic_kin<Traits>;
    constexpr entity no_entity{};
    kin* k = pool.at(e.index());
    if (k == nullptr)
    {
        return;
    }
    if (k->parent != no_entity)
    {
        unlink(pool, *k);
    }
    for (entity child = k->first_child; child != no_entity;)
    {
        kin* child_k = pool.at(child.index());
        const entity next = child_k->next_sibling;
        child_k->parent = no_entity;
        child_k->prev_sibling = no_entity;
        child_k->next_sibling = no_entity;
        child = next;
    }
}

template <class Traits>
[[nodiscard]] std::expected<void, fault> check(const stable_pool<basic_kin<Traits>, Traits>& pool,
                                               const basic_entity_table<Traits>& table)
{
    using entity = ecs::basic_entity<Traits>;
    using kin = basic_kin<Traits>;
    constexpr entity no_entity{};
    const auto broken = [&](const char* note)
    { return std::unexpected(fault{fault_code::links_broken, pool.name(), note}); };
    for (std::size_t pos = 0; pos < pool.size(); ++pos)
    {
        const entity e = pool.entity_at(pos);
        const kin* k = pool.at(e.index());
        if (k->parent != no_entity)
        {
            if (!table.alive(k->parent) || pool.at(k->parent.index()) == nullptr)
            {
                return broken("dangling parent");
            }
            if (k->prev_sibling == no_entity && pool.at(k->parent.index())->first_child != e)
            {
                return broken("head child not referenced by parent");
            }
        }
        if (k->next_sibling != no_entity)
        {
            const kin* next_k = pool.at(k->next_sibling.index());
            if (next_k == nullptr || next_k->prev_sibling != e || next_k->parent != k->parent)
            {
                return broken("sibling chain mismatch");
            }
        }
        if (k->first_child != no_entity)
        {
            std::size_t steps = 0;
            for (entity child = k->first_child; child != no_entity;)
            {
                const kin* child_k = pool.at(child.index());
                if (child_k == nullptr || child_k->parent != e || ++steps > pool.size())
                {
                    return broken("child list mismatch");
                }
                child = child_k->next_sibling;
            }
        }
        if ((k->first_child == no_entity) != (k->last_child == no_entity))
        {
            return broken("first_child/last_child presence mismatch");
        }
        if (k->last_child != no_entity)
        {
            const kin* tail_k = pool.at(k->last_child.index());
            if (tail_k == nullptr || tail_k->parent != e || tail_k->next_sibling != no_entity)
            {
                return broken("last_child not the tail of the child list");
            }
        }
    }
    return {};
}
}  // namespace kin_links
}  // namespace detail

namespace detail
{
struct system_root
{
    system_root() = default;
    system_root(const system_root&) = delete;
    system_root& operator=(const system_root&) = delete;
    system_root(system_root&&) = delete;
    system_root& operator=(system_root&&) = delete;
    virtual ~system_root() = default;
};

template <entity_traits Traits, class E>
struct system_handler
{
    virtual void process(basic_registry<Traits>& world, const E& event) = 0;

protected:
    ~system_handler() = default;
};

template <entity_traits Traits>
struct dispatch_bucket_base
{
    bool order_dirty = false;

    virtual ~dispatch_bucket_base() = default;

    virtual void add_after(std::uint32_t system_id, std::uint32_t dep) = 0;

    virtual void sort_by_dependencies() = 0;

    virtual void remove_listener(std::uint32_t listener) = 0;
};

template <entity_traits Traits, class E>
struct dispatch_bucket : dispatch_bucket_base<Traits>
{
    struct entry
    {
        std::uint32_t feature = 0;
        std::uint32_t system_id = 0;
        std::vector<std::uint32_t> after;
        std::move_only_function<void(basic_registry<Traits>&, const E&)> fn;
        std::uint32_t listener = 0;
    };

    std::vector<entry> entries;

    void add_after(std::uint32_t system_id, std::uint32_t dep) override
    {
        for (entry& e : entries)
        {
            if (e.system_id == system_id && e.listener == 0)
            {
                e.after.push_back(dep);
            }
        }
        this->order_dirty = true;
    }

    void sort_by_dependencies() override
    {
        const std::size_t n = entries.size();
        std::vector<std::size_t> indegree(n, 0);
        std::vector<std::vector<std::size_t>> successors(n);
        const auto index_of = [&](std::uint32_t sid) -> std::size_t
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                if (entries[i].system_id == sid && entries[i].listener == 0)
                {
                    return i;
                }
            }
            return n;
        };
        for (std::size_t i = 0; i < n; ++i)
        {
            for (const std::uint32_t dep : entries[i].after)
            {
                const std::size_t j = index_of(dep);
                if (j < n && j != i)
                {
                    successors[j].push_back(i);
                    ++indegree[i];
                }
            }
        }
        std::vector<entry> ordered;
        ordered.reserve(n);
        std::vector<bool> emitted(n, false);
        for (std::size_t step = 0; step < n; ++step)
        {
            std::size_t pick = n;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (!emitted[i] && indegree[i] == 0)
                {
                    pick = i;
                    break;
                }
            }
            if (pick == n)
            {
                if constexpr (checks_enabled)
                {
                    violate("system ordering cycle in an event bucket");
                }
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (!emitted[i])
                    {
                        ordered.push_back(std::move(entries[i]));
                        emitted[i] = true;
                    }
                }
                break;
            }
            emitted[pick] = true;
            ordered.push_back(std::move(entries[pick]));
            for (const std::size_t s : successors[pick])
            {
                --indegree[s];
            }
        }
        entries = std::move(ordered);
    }

    void remove_listener(std::uint32_t listener) override
    {
        std::erase_if(entries, [listener](const entry& e) { return e.listener == listener; });
    }
};
}  // namespace detail

template <entity_traits Traits, class... Events>
class basic_system : public detail::system_root, public detail::system_handler<Traits, Events>...
{
public:
    static_assert(sizeof...(Events) > 0, "ecs: system<> needs at least one event type");
    static_assert(detail::all_distinct<Events...>, "ecs: duplicate event type in system<...>");

    using events = types<Events...>;
    using traits_type = Traits;
};

template <class... Events>
using system = basic_system<default_entity_traits, Events...>;

namespace detail
{
template <entity_traits Traits>
class basic_dispatcher;

template <entity_traits Traits>
struct event_queue_base
{
    virtual ~event_queue_base() = default;
    virtual void flush(basic_dispatcher<Traits>& disp, basic_registry<Traits>& w) = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

template <entity_traits Traits, class E>
struct event_queue;

template <entity_traits Traits>
class basic_dispatcher
{
    using world = basic_registry<Traits>;
    using command_buffer = basic_command_buffer<Traits>;

    static constexpr std::uint32_t max_dispatch_depth = 64;

public:
    explicit basic_dispatcher(std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : deferred_(memory),
          memory_(memory)
    {
    }

    basic_dispatcher(basic_dispatcher&&) noexcept = default;
    basic_dispatcher& operator=(basic_dispatcher&&) = default;
    basic_dispatcher(const basic_dispatcher&) = delete;
    basic_dispatcher& operator=(const basic_dispatcher&) = delete;
    ~basic_dispatcher() = default;

    std::uint32_t ensure_feature(std::uint32_t feature_id)
    {
        for (std::uint32_t i = 0; i < feature_ids_.size(); ++i)
        {
            if (feature_ids_[i] == feature_id)
            {
                return i;
            }
        }
        feature_ids_.push_back(feature_id);
        feature_enabled_.push_back(std::uint8_t{1});
        return static_cast<std::uint32_t>(feature_ids_.size() - 1);
    }

    void set_feature_enabled(std::uint32_t feature_id, bool on)
    {
        const std::uint32_t f = ensure_feature(feature_id);
        feature_enabled_[f] = static_cast<std::uint8_t>(on ? 1 : 0);
        any_disabled_ = any_disabled_ || !on;
    }

    template <class Sys, class... Args>
        requires std::derived_from<Sys, system_root>
    void add_system(std::uint32_t feature, Args&&... args)
    {
        static_assert(std::same_as<typename Sys::traits_type, Traits>,
                      "ecs: system traits do not match registry traits");
        if (depth_ != 0)
        {
            if constexpr (checks_enabled)
            {
                violate("add_system during dispatch()");
            }
            return;
        }
        auto owned = std::make_unique<Sys>(std::forward<Args>(args)...);
        Sys* ptr = owned.get();
        const std::uint32_t sid = type_id<Sys>();
        last_system_id_ = sid;
        last_event_ids_.clear();
        for_each(typename Sys::events{},
                 [this, ptr, feature, sid]<class E>()
                 {
                     auto* handler = static_cast<system_handler<Traits, E>*>(ptr);
                     this->template ensure_bucket<E>().entries.push_back(
                         {feature,
                          sid,
                          {},
                          [handler](world& w, const E& ev) { handler->process(w, ev); }});
                     last_event_ids_.push_back(type_id<E>());
                 });
        owned_.push_back(std::move(owned));
    }

    void depend_after(std::uint32_t dep)
    {
        for (const std::uint32_t eid : last_event_ids_)
        {
            if (eid < buckets_.size() && buckets_[eid])
            {
                buckets_[eid]->add_after(last_system_id_, dep);
            }
        }
    }

    template <class E>
    void process(world& w, const E& ev)
    {
        dispatch_bucket<Traits, E>* bucket = find_bucket<E>();
        if (bucket == nullptr)
        {
            return;
        }

        if constexpr (checks_enabled)
        {
            if (depth_ >= max_dispatch_depth)
            {
                violate("dispatch nested beyond the maximum dispatch depth");
                return;
            }
        }

        if (bucket->order_dirty)
        {
            bucket->sort_by_dependencies();
            bucket->order_dirty = false;
        }

        const bool nested = depth_ != 0;
        command_buffer parent_pending(memory_);
        if (nested)
        {
            using std::swap;
            swap(deferred_, parent_pending);
        }

        struct level_guard
        {
            std::uint32_t* depth;
            command_buffer* live;
            command_buffer* stash;
            bool nested;
            ~level_guard()
            {
                if (nested)
                {
                    using std::swap;
                    swap(*live, *stash);
                }
                --*depth;
            }
        } guard{&depth_, &deferred_, &parent_pending, nested};
        ++depth_;

        for (auto& entry : bucket->entries)
        {
            if (any_disabled_ && entry.listener == 0 && !feature_enabled_[entry.feature])
            {
                continue;
            }
            entry.fn(w, ev);
            if (!deferred_.empty())
            {
                w.apply(deferred_);
            }
        }
    }

    [[nodiscard]] command_buffer& deferred() noexcept { return deferred_; }

    template <class E, class Fn>
        requires(std::invocable<Fn&, world&, const E&> || std::invocable<Fn&, const E&>)
    std::uint32_t connect(Fn&& fn)
    {
        if (depth_ != 0)
        {
            if constexpr (checks_enabled)
            {
                violate("connect during dispatch()");
            }
            return 0;
        }
        const std::uint32_t id = ++last_listener_id_;
        dispatch_bucket<Traits, E>& bucket = ensure_bucket<E>();
        if constexpr (std::invocable<Fn&, world&, const E&>)
        {
            bucket.entries.push_back({0U, 0U, {}, std::forward<Fn>(fn), id});
        }
        else
        {
            bucket.entries.push_back({0U,
                                      0U,
                                      {},
                                      [call = std::forward<Fn>(fn)](world&, const E& ev) mutable
                                      { call(ev); },
                                      id});
        }
        return id;
    }

    void disconnect(std::uint32_t bucket_id, std::uint32_t listener)
    {
        if (depth_ != 0)
        {
            if constexpr (checks_enabled)
            {
                violate("disconnect during dispatch()");
            }
            return;
        }
        if (listener != 0 && bucket_id < buckets_.size() && buckets_[bucket_id])
        {
            buckets_[bucket_id]->remove_listener(listener);
        }
    }

    template <class E>
    void enqueue(E ev)
    {
        ensure_queue<E>().items.push_back(std::move(ev));
    }

    template <class E>
    void flush(world& w)
    {
        const std::uint32_t id = type_id<E>();
        if (id < queues_.size() && queues_[id])
        {
            queues_[id]->flush(*this, w);
        }
    }

    void flush(world& w)
    {
        for (std::size_t i = 0; i < queues_.size(); ++i)
        {
            if (queues_[i])
            {
                queues_[i]->flush(*this, w);
            }
        }
    }

    template <class E>
    [[nodiscard]] std::size_t queued() const noexcept
    {
        const std::uint32_t id = type_id<E>();
        return id < queues_.size() && queues_[id] ? queues_[id]->size() : 0;
    }

private:
    template <class E>
    dispatch_bucket<Traits, E>& ensure_bucket()
    {
        const std::uint32_t id = type_id<E>();
        if (id >= buckets_.size())
        {
            buckets_.resize(static_cast<std::size_t>(id) + 1U);
        }
        if (!buckets_[id])
        {
            buckets_[id] = std::make_unique<dispatch_bucket<Traits, E>>();
        }
        return static_cast<dispatch_bucket<Traits, E>&>(*buckets_[id]);
    }

    template <class E>
    dispatch_bucket<Traits, E>* find_bucket() noexcept
    {
        const std::uint32_t id = type_id<E>();
        if (id >= buckets_.size() || !buckets_[id])
        {
            return nullptr;
        }
        return static_cast<dispatch_bucket<Traits, E>*>(buckets_[id].get());
    }

    template <class E>
    event_queue<Traits, E>& ensure_queue()
    {
        const std::uint32_t id = type_id<E>();
        if (id >= queues_.size())
        {
            queues_.resize(static_cast<std::size_t>(id) + 1U);
        }
        if (!queues_[id])
        {
            queues_[id] = std::make_unique<event_queue<Traits, E>>(memory_);
        }
        return static_cast<event_queue<Traits, E>&>(*queues_[id]);
    }

    std::vector<std::unique_ptr<dispatch_bucket_base<Traits>>> buckets_;
    std::vector<std::unique_ptr<event_queue_base<Traits>>> queues_;
    std::vector<std::unique_ptr<system_root>> owned_;
    std::vector<std::uint32_t> feature_ids_;
    std::vector<std::uint8_t> feature_enabled_;
    std::vector<std::uint32_t> last_event_ids_;
    command_buffer deferred_;
    std::pmr::memory_resource* memory_;
    std::uint32_t last_system_id_ = 0;
    std::uint32_t last_listener_id_ = 0;
    std::uint32_t depth_ = 0;
    bool any_disabled_ = false;
};

template <entity_traits Traits, class E>
struct event_queue : event_queue_base<Traits>
{
    std::pmr::vector<E> items;

    explicit event_queue(std::pmr::memory_resource* memory)
        : items(memory)
    {
    }

    void flush(basic_dispatcher<Traits>& disp, basic_registry<Traits>& w) override
    {
        std::pmr::vector<E> batch(items.get_allocator());
        using std::swap;
        swap(batch, items);
        for (E& ev : batch)
        {
            disp.template process<E>(w, ev);
        }
    }

    [[nodiscard]] std::size_t size() const noexcept override { return items.size(); }
};
}  // namespace detail

struct listener_token
{
    std::uint32_t bucket = 0;
    std::uint32_t id = 0;
};

template <entity_traits Traits>
class basic_system_ref
{
public:
    basic_system_ref(detail::basic_dispatcher<Traits>* dispatcher, std::uint32_t feature) noexcept
        : dispatcher_(dispatcher),
          feature_(feature)
    {
    }

    template <class Other>
    basic_system_ref& after()
    {
        dispatcher_->depend_after(detail::type_id<Other>());
        return *this;
    }

    template <class Sys, class... Args>
    basic_system_ref add_system(Args&&... args)
    {
        dispatcher_->template add_system<Sys>(feature_, std::forward<Args>(args)...);
        return basic_system_ref(dispatcher_, feature_);
    }

private:
    detail::basic_dispatcher<Traits>* dispatcher_;
    std::uint32_t feature_;
};

template <entity_traits Traits>
class basic_feature_builder
{
public:
    basic_feature_builder(detail::basic_dispatcher<Traits>* dispatcher,
                          std::uint32_t feature) noexcept
        : dispatcher_(dispatcher),
          feature_(feature)
    {
    }

    template <class Sys, class... Args>
    basic_system_ref<Traits> add_system(Args&&... args)
    {
        dispatcher_->template add_system<Sys>(feature_, std::forward<Args>(args)...);
        return basic_system_ref<Traits>(dispatcher_, feature_);
    }

private:
    detail::basic_dispatcher<Traits>* dispatcher_;
    std::uint32_t feature_;
};

template <class Traits>
class basic_registry
{
public:
    using traits_type = Traits;
    using entity = ecs::basic_entity<Traits>;

private:
    using world = basic_registry;
    using entity_table = detail::basic_entity_table<Traits>;
    using index_type = typename Traits::index_type;
    using pool_base = detail::basic_pool_base<Traits>;
    using single_pool_lock = detail::basic_single_pool_lock<Traits>;
    using kin = detail::basic_kin<Traits>;
    using component_hook = ecs::basic_component_hook<Traits>;
    using command_buffer = basic_command_buffer<Traits>;
    using blueprint = basic_blueprint<Traits>;
    using pool_ref = basic_pool_ref<Traits>;
    using runtime_selection = basic_runtime_selection<Traits>;
    using entity_filler = basic_entity_filler<basic_registry>;
    using const_entity_filler = basic_entity_filler<const basic_registry>;
    using limits = detail::entity_limits<Traits>;
    template <class T>
    using pool_of_t = ecs::pool_of_t<T, Traits>;
    static constexpr entity no_entity{};

public:
    explicit basic_registry(
        std::pmr::memory_resource* memory = std::pmr::get_default_resource()) noexcept
        : table_(memory),
          pools_(memory),
          active_(memory),
          memory_(memory),
          dispatcher_(memory)
    {
    }

    basic_registry(basic_registry&& other) noexcept
        : table_(std::move(other.table_)),
          pools_(std::move(other.pools_)),
          active_(std::move(other.active_)),
          memory_(other.memory_),
          globals_(std::exchange(other.globals_, entity{})),
          dispatcher_(std::move(other.dispatcher_))
    {
        other.pools_.clear();
        other.active_.clear();
        repoint_pools();
    }

    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor,bugprone-exception-escape)
    basic_registry& operator=(basic_registry&& other)
    {
        if (this != &other)
        {
            if constexpr (checks_enabled)
            {
                if (any_locked())
                {
                    detail::violate("world move-assigned over while an iteration is running");
                }
            }
            table_ = std::move(other.table_);
            active_.clear();
            pools_ = std::move(other.pools_);
            active_ = std::move(other.active_);
            memory_ = other.memory_;
            globals_ = std::exchange(other.globals_, no_entity);
            dispatcher_ = std::move(other.dispatcher_);
            other.pools_.clear();
            other.active_.clear();
            repoint_pools();
        }
        return *this;
    }

    basic_registry(const basic_registry&) = delete;
    basic_registry& operator=(const basic_registry&) = delete;

    ~basic_registry()
    {
        if constexpr (checks_enabled)
        {
            if (any_locked())
            {
                detail::violate("world destroyed while an iteration is running");
            }
        }
    }

    template <class F>
    basic_feature_builder<Traits> feature()
    {
        return basic_feature_builder<Traits>(&dispatcher_,
                                             dispatcher_.ensure_feature(detail::type_id<F>()));
    }

    template <class E>
    void dispatch(const E& ev)
    {
        dispatcher_.template process<E>(*this, ev);
    }

    template <class F>
    void enable_feature()
    {
        dispatcher_.set_feature_enabled(detail::type_id<F>(), true);
    }
    template <class F>
    void disable_feature()
    {
        dispatcher_.set_feature_enabled(detail::type_id<F>(), false);
    }

    [[nodiscard]] command_buffer& deferred() noexcept { return dispatcher_.deferred(); }
    template <class E>
    void enqueue(E ev)
    {
        dispatcher_.template enqueue<E>(std::move(ev));
    }

    template <class E>
    void flush()
    {
        dispatcher_.template flush<E>(*this);
    }

    void flush() { dispatcher_.flush(*this); }

    template <class E>
    [[nodiscard]] std::size_t queued() const noexcept
    {
        return dispatcher_.template queued<E>();
    }

    template <class E, class Fn>
        requires(std::invocable<Fn&, basic_registry&, const E&> || std::invocable<Fn&, const E&>)
    listener_token connect(Fn&& fn)
    {
        return {detail::type_id<E>(), dispatcher_.template connect<E>(std::forward<Fn>(fn))};
    }

    void disconnect(listener_token token) { dispatcher_.disconnect(token.bucket, token.id); }

    entity_filler create() { return ref(table_.create()); }

    template <class... Cs>
        requires(sizeof...(Cs) > 0 && (component<std::remove_cvref_t<Cs>> && ...) &&
                 (!std::same_as<std::remove_cvref_t<Cs>, blueprint> && ...))
    entity_filler create(Cs&&... components)
    {
        static_assert(detail::all_distinct<std::remove_cvref_t<Cs>...>,
                      "ecs: duplicate component type in create(...)");
        const entity e = table_.create();
        (add_value(e, std::forward<Cs>(components)), ...);
        return ref(e);
    }

    entity_filler create(const blueprint& recipe)
    {
        const entity e = table_.create();
        for (const blueprint::op& o : recipe.ops_)
        {
            o.apply_fn(*this, e, o.payload);
        }
        return ref(e);
    }

    void create(const blueprint& recipe, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            create(recipe);
        }
    }

    template <class F>
        requires std::invocable<F&, basic_entity<Traits>>
    void create(const blueprint& recipe, std::size_t count, F&& fn)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            fn(create(recipe));
        }
    }

    template <class OutputIt>
        requires std::output_iterator<OutputIt, basic_entity<Traits>>
    OutputIt create_n(std::size_t count, OutputIt out)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            *out = table_.create();
            ++out;
        }
        return out;
    }

    template <class F>
        requires std::invocable<F&, basic_entity<Traits>>
    void create_n(std::size_t count, F&& fn)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            fn(table_.create());
        }
    }

    duplicate_result duplicate(entity src)
    {
        if (!table_.alive(src))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("duplicate of a dead, stale, or null entity handle");
            }
            return {};
        }
        if (is_globals(src))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("duplicate of the globals entity");
            }
            return {};
        }
        if constexpr (checks_enabled)
        {
            for (pool_base* pool : active_)
            {
                if (pool->locked() && pool->contains(src.index()))
                {
                    detail::violate_pool("duplicate during iteration over pool", pool->name());
                    return {};
                }
            }
        }
        duplicate_result result{table_.create()};
        const pool_base* links = peek_pool<kin>();

        // NOLINTNEXTLINE(modernize-loop-convert)
        for (std::size_t i = 0; i < active_.size(); ++i)
        {
            pool_base* pool = active_[i];
            if (pool == links || !pool->contains(src.index()) ||
                pool->contains(result.clone.index()))
            {
                continue;
            }
            if (pool->copy_item(src.index(), result.clone))
            {
                ++result.copied;
            }
            else
            {
                ++result.skipped;
            }
        }
        return result;
    }

    void destroy(entity e) noexcept
    {
        if (!table_.alive(e) || e == dying_)
        {
            if constexpr (checks_enabled)
            {
                detail::violate("destroy on a dead, dying, stale, or null entity handle");
            }
            return;
        }
        if (is_globals(e))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("destroy of the globals entity (reset() clears state)");
            }
            return;
        }
        const index_type index = e.index();
        if constexpr (checks_enabled)
        {
            // NOLINTNEXTLINE(modernize-loop-convert)
            for (std::size_t i = 0; i < active_.size(); ++i)
            {
                pool_base* pool = active_[i];
                if (pool->locked() && pool->contains(index))
                {
                    detail::violate_pool("destroy during iteration over pool", pool->name());
                    return;
                }
            }
        }

        const entity previous_dying = std::exchange(dying_, e);
        sever_links(e);
        // NOLINTNEXTLINE(modernize-loop-convert)
        for (std::size_t i = 0; i < active_.size(); ++i)
        {
            active_[i]->erase_if_present(index);
        }
        dying_ = previous_dying;
        table_.destroy(index);
    }

    template <class EntityIt>
        requires(std::input_iterator<EntityIt> &&
                 std::convertible_to<std::iter_value_t<EntityIt>, basic_entity<Traits>>)
    void destroy(EntityIt first, EntityIt last) noexcept
    {
        for (; first != last; ++first)
        {
            destroy(static_cast<entity>(*first));
        }
    }

    [[nodiscard]] bool alive(entity e) const noexcept { return table_.alive(e); }

    [[nodiscard]] entity current_handle(index_type slot) const noexcept
    {
        if (!table_.occupied(slot))
        {
            return no_entity;
        }
        return entity(slot, table_.generation_at(slot));
    }

    [[nodiscard]] entity_filler ref(entity e) noexcept;
    [[nodiscard]] const_entity_filler ref(entity e) const noexcept;

    [[nodiscard]] entity_filler globals();
    [[nodiscard]] const_entity_filler globals() const noexcept;

    void reserve_entities(std::size_t n) { table_.reserve(n); }

    template <component T, class... Args>
    decltype(auto) add(entity e, Args&&... args)
    {
        auto& pool = ensure_pool<T>();
        if (!table_.alive(e))
        {
            if constexpr (checks_enabled)
            {
                detail::violate_pool("add on a dead, stale, or null entity; pool", pool.name());
            }
            std::abort();
        }
        if constexpr (detail::is_tag_v<T>)
        {
            static_assert(sizeof...(Args) == 0,
                          "ecs: T uses tag storage and carries no data; call add<T>(e) with "
                          "no arguments (or give T members / a non-tag storage policy)");
            if (clearing_ || e == dying_)
            {
                if constexpr (checks_enabled)
                {
                    detail::violate_pool("add to a dying entity or during reset; pool",
                                         pool.name());
                }
                return;
            }
            if constexpr (checks_enabled)
            {
                if (pool.locked())
                {
                    detail::violate_pool("add during iteration over pool", pool.name());
                    return;
                }
            }
            if (pool.contains(e.index()))
            {
                if constexpr (checks_enabled)
                {
                    detail::violate_pool("add of a component the entity already has; pool",
                                         pool.name());
                }
                return;
            }
            pool.emplace(e);
        }
        else
        {
            static_assert(std::constructible_from<T, Args&&...>,
                          "ecs: T cannot be constructed from these add() arguments (note: "
                          "passing a T value requires T to be move-constructible; non-movable "
                          "components must be constructed in place from constructor arguments)");
            if (clearing_ || e == dying_)
            {
                if constexpr (checks_enabled)
                {
                    detail::violate_pool("add to a dying entity or during reset; pool",
                                         pool.name());
                }
                std::abort();
            }
            if constexpr (checks_enabled)
            {
                // Add during iteration is flagged but still performed: the emplace appends
                // (unlike the swap-removes of destroy/remove, which are refused). If that
                // append reallocates the pool being iterated, the in-flight loop is corrupted
                // -- reserve ahead or defer the add via a command_buffer.
                if (pool.locked())
                {
                    detail::violate_pool("add during iteration over pool", pool.name());
                }
            }
            if (T* existing = pool.at(e.index()); existing != nullptr)
            {
                if constexpr (checks_enabled)
                {
                    detail::violate_pool("add of a component the entity already has; pool",
                                         pool.name());
                }
                return *existing;
            }
            return pool.emplace(e, std::forward<Args>(args)...);
        }
    }

    // Single-value overload: lets braced-init deduce, e.g. add<T>(e, {x, y}).
    // Multi-arg emplace, lvalues, and zero-arg fall through to the variadic above.
    template <component T>
        requires(!detail::is_tag_v<T>)
    decltype(auto) add(entity e, T&& value)
    {
        return add<T, T>(e, std::forward<T>(value));
    }

    template <component T, class... Args>
    T& replace(entity e, Args&&... args)
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: T uses tag storage and carries no data; there is nothing to "
                      "replace -- use has<T>/add<T>/remove<T>");
        T* existing = lookup<T>(e);
        if (existing == nullptr)
        {
            if constexpr (checks_enabled)
            {
                detail::violate_pool("replace of a component the entity does not have; pool",
                                     name_of<T>());
            }
            std::abort();
        }
        auto* pool = peek_pool<T>();
        if constexpr (std::is_move_assignable_v<T>)
        {
            *existing = T(std::forward<Args>(args)...);
        }
        else
        {
            std::destroy_at(existing);
            std::construct_at(existing, std::forward<Args>(args)...);
        }
        pool->fire_replace(e);
        return *pool->at(e.index());
    }

    template <component T, class F>
        requires std::invocable<F&, T&>
    T& amend(entity e, F&& fn)
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: T uses tag storage and carries no data; there is nothing to "
                      "amend -- use has<T>/add<T>/remove<T>");
        T* existing = lookup<T>(e);
        if (existing == nullptr)
        {
            if constexpr (checks_enabled)
            {
                detail::violate_pool("amend of a component the entity does not have; pool",
                                     name_of<T>());
            }
            std::abort();
        }
        auto* pool = peek_pool<T>();
        std::invoke(fn, *existing);
        pool->fire_replace(e);
        return *pool->at(e.index());
    }

    template <component T>
    void touch(entity e)
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: T uses tag storage and carries no data; there is nothing to "
                      "touch -- use has<T>/add<T>/remove<T>");
        if (lookup<T>(e) == nullptr)
        {
            if constexpr (checks_enabled)
            {
                detail::violate_pool("touch of a component the entity does not have; pool",
                                     name_of<T>());
            }
            std::abort();
        }
        peek_pool<T>()->fire_replace(e);
    }

    template <component T>
        requires(!detail::is_tag_v<T>)
    T& replace(entity e, T&& value)
    {
        return replace<T, T>(e, std::forward<T>(value));
    }

    template <component T, class... Args>
    decltype(auto) put(entity e, Args&&... args)
    {
        if constexpr (detail::is_tag_v<T>)
        {
            static_assert(sizeof...(Args) == 0,
                          "ecs: T uses tag storage and carries no data; call put<T>(e) with "
                          "no arguments");
            if (!has<T>(e))
            {
                add<T>(e);
            }
        }
        else
        {
            if (T* existing = lookup<T>(e); existing != nullptr)
            {
                auto* pool = peek_pool<T>();
                if constexpr (std::is_move_assignable_v<T>)
                {
                    *existing = T(std::forward<Args>(args)...);
                }
                else
                {
                    std::destroy_at(existing);
                    std::construct_at(existing, std::forward<Args>(args)...);
                }
                pool->fire_replace(e);
                return *pool->at(e.index());
            }
            return add<T>(e, std::forward<Args>(args)...);
        }
    }

    template <component T>
        requires(!detail::is_tag_v<T>)
    decltype(auto) put(entity e, T&& value)
    {
        return put<T, T>(e, std::forward<T>(value));
    }

    template <component T>
    bool remove(entity e)
    {
        static_assert(!std::same_as<T, kin>,
                      "ecs: parent/child links are managed via adopt/orphan/kill");
        if (!table_.alive(e))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("remove on a dead, stale, or null entity handle");
            }
            return false;
        }
        auto* pool = peek_pool<T>();
        if (pool == nullptr || !pool->contains(e.index()))
        {
            return false;
        }
        if constexpr (checks_enabled)
        {
            if (pool->locked())
            {
                detail::violate_pool("remove during iteration over pool", pool->name());
                return false;
            }
        }
        pool->erase_if_present(e.index());
        return true;
    }

    template <component T>
    [[nodiscard]] bool has(entity e) const noexcept
    {
        if (!table_.alive(e))
        {
            return false;
        }
        return probe<T>(e.index());
    }

    template <component T, component U, component... Rest>
    [[nodiscard]] bool has_all(entity e) const noexcept
    {
        static_assert(detail::all_distinct<T, U, Rest...>,
                      "ecs: duplicate component type in has_all<...>");
        if (!table_.alive(e))
        {
            return false;
        }
        return probe<T>(e.index()) && probe<U>(e.index()) && (probe<Rest>(e.index()) && ...);
    }

    template <component T, component U, component... Rest>
    [[nodiscard]] bool has_any(entity e) const noexcept
    {
        static_assert(detail::all_distinct<T, U, Rest...>,
                      "ecs: duplicate component type in has_any<...>");
        if (!table_.alive(e))
        {
            return false;
        }
        return probe<T>(e.index()) || probe<U>(e.index()) || (probe<Rest>(e.index()) || ...);
    }

    template <component T, class Self>
    [[nodiscard]] auto& get(this Self&& self, entity e)
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: T uses tag storage and carries no data; use has<T>(e)");
        auto* p = self.template lookup<T>(e);
        if constexpr (checks_enabled)
        {
            if (p == nullptr)
            {
                detail::violate_pool(
                    "get of a missing component (dead entity or never added); "
                    "pool",
                    name_of<T>());
                std::abort();
            }
        }
        return *p;
    }

    template <component T, component U, component... Rest, class Self>
    [[nodiscard]] auto get(this Self&& self, entity e)
    {
        return std::tuple<decltype(self.template get<T>(e)),
                          decltype(self.template get<U>(e)),
                          decltype(self.template get<Rest>(e))...>{
            self.template get<T>(e), self.template get<U>(e), self.template get<Rest>(e)...};
    }

    template <component T, class Self>
    [[nodiscard]] auto* find(this Self&& self, entity e) noexcept
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: T uses tag storage and carries no data; use has<T>(e)");
        return self.template lookup<T>(e);
    }

    template <component T, component U, component... Rest, class Self>
    [[nodiscard]] auto find_all(this Self&& self, entity e) noexcept
    {
        using row = std::tuple<decltype(self.template find_in_pool<T>(e)),
                               decltype(self.template find_in_pool<U>(e)),
                               decltype(self.template find_in_pool<Rest>(e))...>;
        if (!self.table_.alive(e))
        {
            return row{};
        }
        return row{self.template find_in_pool<T>(e),
                   self.template find_in_pool<U>(e),
                   self.template find_in_pool<Rest>(e)...};
    }

    template <component T, class... Args>
    decltype(auto) obtain(entity e, Args&&... args)
    {
        if constexpr (detail::is_tag_v<T>)
        {
            static_assert(sizeof...(Args) == 0,
                          "ecs: T uses tag storage and carries no data; call obtain<T>(e) "
                          "with no arguments");
            if (!has<T>(e))
            {
                add<T>(e);
            }
        }
        else
        {
            if (T* existing = lookup<T>(e); existing != nullptr)
            {
                return *existing;
            }
            return add<T>(e, std::forward<Args>(args)...);
        }
    }

    template <component T>
        requires(!detail::is_tag_v<T>)
    decltype(auto) obtain(entity e, T&& value)
    {
        return obtain<T, T>(e, std::forward<T>(value));
    }

    template <component T>
    void purge()
    {
        static_assert(!std::same_as<T, kin>,
                      "ecs: parent/child links are managed via adopt/orphan/kill");
        auto* pool = peek_pool<T>();
        if (pool == nullptr)
        {
            return;
        }
        if constexpr (checks_enabled)
        {
            if (pool->locked())
            {
                detail::violate_pool("purge during iteration over pool", pool->name());
                return;
            }
        }
        pool->wipe();
    }

    template <component T, class EntityIt>
        requires(std::input_iterator<EntityIt> && !detail::is_tag_v<T>)
    void insert(EntityIt first, EntityIt last, const T& value)
    {
        if constexpr (std::random_access_iterator<EntityIt>)
        {
            const auto* pool = peek_pool<T>();
            reserve<T>((pool != nullptr ? pool->size() : 0) +
                       static_cast<std::size_t>(last - first));
        }
        for (; first != last; ++first)
        {
            add<T>(*first, value);
        }
    }

    template <component T, class EntityIt, class ValueIt>
        requires(std::input_iterator<EntityIt> && std::input_iterator<ValueIt> &&
                 !detail::is_tag_v<T>)
    void insert(EntityIt first, EntityIt last, ValueIt vfirst)
    {
        if constexpr (std::random_access_iterator<EntityIt>)
        {
            const auto* pool = peek_pool<T>();
            reserve<T>((pool != nullptr ? pool->size() : 0) +
                       static_cast<std::size_t>(last - first));
        }
        for (; first != last; ++first, static_cast<void>(++vfirst))
        {
            add<T>(*first, *vfirst);
        }
    }

    template <component T>
    void reserve(std::size_t n)
    {
        ensure_pool<T>().reserve(n);
    }

    template <component T, class Compare, class Algorithm = detail::std_sort>
    void sort(Compare cmp, Algorithm&& algo = {})
    {
        auto* pool = peek_pool<T>();
        if (pool == nullptr || pool->size() < 2)
        {
            return;
        }
        if constexpr (checks_enabled)
        {
            if (pool->locked())
            {
                detail::violate_pool("sort during iteration over pool", pool->name());
                return;
            }
        }
        if constexpr (std::invocable<Compare&, const T&, const T&> && !detail::is_tag_v<T>)
        {
            pool->sort_dense([&](index_type a, index_type b)
                             { return static_cast<bool>(cmp(pool->at_pos(a), pool->at_pos(b))); },
                             std::forward<Algorithm>(algo));
        }
        else if constexpr (std::invocable<Compare&, entity, entity>)
        {
            pool->sort_dense(
                [&](index_type a, index_type b)
                { return static_cast<bool>(cmp(pool->entity_at(a), pool->entity_at(b))); },
                std::forward<Algorithm>(algo));
        }
        else
        {
            static_assert(std::invocable<Compare&, entity, entity>,
                          "ecs: sort<T> comparator must be callable as (const T&, const T&) "
                          "for value sorting (non-tag components only) or as (entity, entity)");
        }
    }

    template <component T>
    hook_token on_add(component_hook fn, void* user = nullptr)
    {
        return connect_hook<T>(pool_base::hook_kind::add, fn, user);
    }

    template <component T, auto Candidate>
    hook_token on_add()
    {
        return connect_hook<T>(
            pool_base::hook_kind::add, detail::free_hook_thunk<Traits, Candidate>(), nullptr);
    }

    template <component T, auto Candidate, class Inst>
    hook_token on_add(Inst* instance)
    {
        return connect_hook<T>(pool_base::hook_kind::add,
                               detail::bound_hook_thunk<Traits, Candidate, Inst>(),
                               // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                               const_cast<void*>(static_cast<const void*>(instance)));
    }
    template <component T>
    hook_token on_remove(component_hook fn, void* user = nullptr)
    {
        return connect_hook<T>(pool_base::hook_kind::remove, fn, user);
    }

    template <component T, auto Candidate>
    hook_token on_remove()
    {
        return connect_hook<T>(
            pool_base::hook_kind::remove, detail::free_hook_thunk<Traits, Candidate>(), nullptr);
    }

    template <component T, auto Candidate, class Inst>
    hook_token on_remove(Inst* instance)
    {
        return connect_hook<T>(pool_base::hook_kind::remove,
                               detail::bound_hook_thunk<Traits, Candidate, Inst>(),
                               // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                               const_cast<void*>(static_cast<const void*>(instance)));
    }
    template <component T>
    hook_token on_replace(component_hook fn, void* user = nullptr)
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: tags carry no data and are never replaced; use on_add/on_remove");
        return connect_hook<T>(pool_base::hook_kind::replace, fn, user);
    }

    template <component T, auto Candidate>
    hook_token on_replace()
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: tags carry no data and are never replaced; use on_add/on_remove");
        return connect_hook<T>(
            pool_base::hook_kind::replace, detail::free_hook_thunk<Traits, Candidate>(), nullptr);
    }

    template <component T, auto Candidate, class Inst>
    hook_token on_replace(Inst* instance)
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: tags carry no data and are never replaced; use on_add/on_remove");
        return connect_hook<T>(pool_base::hook_kind::replace,
                               detail::bound_hook_thunk<Traits, Candidate, Inst>(),
                               // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                               const_cast<void*>(static_cast<const void*>(instance)));
    }

    bool unhook(hook_token token) noexcept
    {
        if (!token || token.pool >= pools_.size() || !pools_[token.pool])
        {
            return false;
        }
        if constexpr (checks_enabled)
        {
            if (pools_[token.pool]->locked())
            {
                detail::violate_pool("hook change during iteration over pool",
                                     pools_[token.pool]->name());
                return false;
            }
        }
        return pools_[token.pool]->disconnect_hook(token.id);
    }

    template <class... Ts, class Self>
    [[nodiscard]] auto view(this Self&& self)
    {
        return self.template view<Ts...>(detail::true_filter{});
    }

    template <class... Ts, class Filter, class Self>
        requires detail::filter_expr<Filter>
    [[nodiscard]] auto view(this Self&& self, Filter filter)
    {
        if constexpr (std::is_const_v<std::remove_reference_t<Self>>)
        {
            // NOLINTNEXTLINE(readability-redundant-typename)
            using result = basic_view<Traits, Filter, typename detail::as_const_part<Ts>::type...>;
            return result{self.template include_pools<Ts...>(),
                          self.template filter_pools<Filter>(),
                          std::move(filter)};
        }
        else
        {
            using result = basic_view<Traits, Filter, Ts...>;
            return result{self.template include_pools<Ts...>(),
                          self.template filter_pools<Filter>(),
                          std::move(filter)};
        }
    }

    template <class... Ts, class Filter, class Self>
        requires detail::filter_expr<Filter>
    [[nodiscard]] auto view(this Self&& self, types<Ts...>, Filter f)
    {
        return self.template view<Ts...>(f);
    }

    template <class... Ts, class Self>
    [[nodiscard]] auto view(this Self&& self, types<Ts...>)
    {
        return self.template view<Ts...>(detail::true_filter{});
    }

    template <class... Ts, class F, class Self>
        requires(sizeof...(Ts) >= 1 && !detail::filter_expr<F>)
    void each(this Self&& self, F&& fn)
    {
        self.template view<Ts...>().each(std::forward<F>(fn));
    }

    template <class... Ts, class F, class Filter, class Self>
        requires(sizeof...(Ts) >= 1 && detail::filter_expr<Filter>)
    void each(this Self&& self, F&& fn, Filter f)
    {
        self.template view<Ts...>(f).each(std::forward<F>(fn));
    }

    template <class... Ts, class F, class Self>
        requires(!detail::filter_expr<F>)
    void each(this Self&& self, types<Ts...> list, F&& fn)
    {
        self.view(list).each(std::forward<F>(fn));
    }

    template <class... Ts, class F, class Filter, class Self>
        requires detail::filter_expr<Filter>
    void each(this Self&& self, types<Ts...> list, F&& fn, Filter f)
    {
        self.view(list, f).each(std::forward<F>(fn));
    }

    apply_result apply(command_buffer& buffer)
    {
        if (!apply_allowed())
        {
            return {};
        }
        const apply_result result = apply_replay(buffer);
        buffer.clear();
        return result;
    }

    template <class F>
    apply_result apply(command_buffer& buffer, F&& on_spawn)
    {
        static_assert(std::invocable<F&, entity, entity>,
                      "ecs: the apply() spawn callback must be callable as "
                      "(entity provisional, entity real)");
        if (!apply_allowed())
        {
            return {};
        }
        const apply_result result = apply_replay(buffer);
        std::size_t ticket = 0;
        for (std::size_t i = 0; i < buffer.ops_.size(); ++i)
        {
            const command_buffer::op o = buffer.ops_[i];
            if (o.kind == command_buffer::op_kind::spawn)
            {
                on_spawn(o.target, buffer.resolved_[ticket++]);
            }
        }
        buffer.clear();
        return result;
    }

    relationship_token on_adopt(basic_relationship_hook<Traits> fn, void* user = nullptr)
    {
        return connect_relationship(relationship_kind::adopt, fn, user);
    }
    relationship_token on_orphan(basic_relationship_hook<Traits> fn, void* user = nullptr)
    {
        return connect_relationship(relationship_kind::orphan, fn, user);
    }
    relationship_token on_reorder(basic_relationship_hook<Traits> fn, void* user = nullptr)
    {
        return connect_relationship(relationship_kind::reorder, fn, user);
    }

    bool unhook(relationship_token token) noexcept
    {
        if (!token)
        {
            return false;
        }
        return std::erase_if(list_for(token.kind),
                             [id = token.id](const rel_hook_entry& e) { return e.id == id; }) > 0;
    }

    void adopt(entity parent, entity child)
    {
        if (!table_.alive(parent) || !table_.alive(child) || parent == child || clearing_ ||
            parent == dying_ || child == dying_)
        {
            if constexpr (checks_enabled)
            {
                detail::violate(
                    "adopt with dead/dying/stale handles, during reset, or "
                    "parent == child");
            }
            return;
        }
        if constexpr (checks_enabled)
        {
            for (entity up = parent_of(parent); up != no_entity; up = parent_of(up))
            {
                if (up == child)
                {
                    detail::violate("adopt would create a parent/child cycle");
                    return;
                }
            }
        }
        auto& pool = ensure_pool<kin>();
        if constexpr (checks_enabled)
        {
            if (pool.locked())
            {
                detail::violate("adopt while children_of is iterating");
                return;
            }
        }
        kin* child_k = pool.at(child.index());
        entity old_parent = no_entity;
        if (child_k == nullptr)
        {
            child_k = &pool.emplace(child);
        }
        else if (child_k->parent != no_entity)
        {
            old_parent = child_k->parent;
            detail::kin_links::unlink(pool, *child_k);
        }
        kin* parent_k = pool.at(parent.index());
        if (parent_k == nullptr)
        {
            parent_k = &pool.emplace(parent);
        }
        child_k->parent = parent;
        child_k->next_sibling = no_entity;
        child_k->prev_sibling = parent_k->last_child;
        if (parent_k->last_child != no_entity)
        {
            pool.at(parent_k->last_child.index())->next_sibling = child;
        }
        else
        {
            parent_k->first_child = child;
        }
        parent_k->last_child = child;
        if (old_parent != no_entity)
        {
            fire_orphan(child, old_parent);
        }
        fire_adopt(child, parent);
    }

    void orphan(entity child)
    {
        if (!table_.alive(child))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("orphan on a dead, stale, or null entity handle");
            }
            return;
        }
        auto* pool = peek_pool<kin>();
        if (pool == nullptr)
        {
            return;
        }
        kin* k = pool->at(child.index());
        if (k == nullptr || k->parent == no_entity)
        {
            return;
        }
        if constexpr (checks_enabled)
        {
            if (pool->locked())
            {
                detail::violate("orphan while children_of is iterating");
                return;
            }
        }
        const entity old_parent = k->parent;
        detail::kin_links::unlink(*pool, *k);
        k->parent = no_entity;
        k->prev_sibling = no_entity;
        k->next_sibling = no_entity;
        fire_orphan(child, old_parent);
    }

    void reorder_child(entity child, entity before = no_entity)
    {
        if (!table_.alive(child))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("reorder_child on a dead, stale, or null entity handle");
            }
            return;
        }
        auto* pool = peek_pool<kin>();
        kin* child_k = (pool != nullptr) ? pool->at(child.index()) : nullptr;
        if (child_k == nullptr || child_k->parent == no_entity)
        {
            if constexpr (checks_enabled)
            {
                detail::violate("reorder_child on a child that has no parent");
            }
            return;
        }
        if constexpr (checks_enabled)
        {
            if (pool->locked())
            {
                detail::violate("reorder_child while children_of is iterating");
                return;
            }
        }
        if (before != no_entity)
        {
            const kin* before_k = pool->at(before.index());
            if (before == child || before_k == nullptr || before_k->parent != child_k->parent)
            {
                if constexpr (checks_enabled)
                {
                    detail::violate("reorder_child: `before` is not a sibling of child");
                }
                return;
            }
        }
        detail::kin_links::unlink(*pool, *child_k);
        kin* parent_k = pool->at(child_k->parent.index());
        if (before == no_entity)
        {
            child_k->next_sibling = no_entity;
            child_k->prev_sibling = parent_k->last_child;
            if (parent_k->last_child != no_entity)
            {
                pool->at(parent_k->last_child.index())->next_sibling = child;
            }
            else
            {
                parent_k->first_child = child;
            }
            parent_k->last_child = child;
        }
        else
        {
            kin* before_k = pool->at(before.index());
            child_k->next_sibling = before;
            child_k->prev_sibling = before_k->prev_sibling;
            if (before_k->prev_sibling != no_entity)
            {
                pool->at(before_k->prev_sibling.index())->next_sibling = child;
            }
            else
            {
                parent_k->first_child = child;
            }
            before_k->prev_sibling = child;
        }
        fire_reorder(child, child_k->parent);
    }

    [[nodiscard]] entity parent_of(entity child) const noexcept
    {
        const auto* pool = peek_pool<kin>();
        if (pool == nullptr || !table_.alive(child))
        {
            return no_entity;
        }
        const kin* k = pool->at(child.index());
        return k == nullptr ? no_entity : k->parent;
    }

    template <class F>
        requires std::invocable<F&, entity>
    void children_of(entity parent, F&& fn) const
    {
        const auto* pool = peek_pool<kin>();
        if (pool == nullptr || !table_.alive(parent))
        {
            return;
        }
        const kin* k = pool->at(parent.index());
        if (k == nullptr)
        {
            return;
        }
        const single_pool_lock lock(pool);
        for (entity child = k->first_child; child != no_entity;)
        {
            const entity next = pool->at(child.index())->next_sibling;
            if constexpr (std::predicate<F&, entity>)
            {
                if (!fn(child))
                {
                    break;
                }
            }
            else
            {
                fn(child);
            }
            child = next;
        }
    }

    [[nodiscard]] std::size_t child_count(entity parent) const noexcept
    {
        std::size_t n = 0;
        children_of(parent, [&](entity) { ++n; });
        return n;
    }

    template <class F>
        requires std::invocable<F&, entity>
    void descendants_of(entity root, F&& fn) const
    {
        const auto* pool = peek_pool<kin>();
        if (pool == nullptr || !table_.alive(root))
        {
            return;
        }
        const kin* rk = pool->at(root.index());
        if (rk == nullptr)
        {
            return;
        }
        const single_pool_lock lock(pool);
        std::pmr::vector<entity> stack(memory_);
        for (entity c = rk->last_child; c != no_entity; c = pool->at(c.index())->prev_sibling)
        {
            stack.push_back(c);
        }
        while (!stack.empty())
        {
            const entity e = stack.back();
            stack.pop_back();
            if constexpr (std::predicate<F&, entity>)
            {
                if (!fn(e))
                {
                    break;
                }
            }
            else
            {
                fn(e);
            }
            const kin* k = pool->at(e.index());
            for (entity c = k->last_child; c != no_entity; c = pool->at(c.index())->prev_sibling)
            {
                stack.push_back(c);
            }
        }
    }

    template <class F>
        requires std::invocable<F&, entity>
    void ancestors_of(entity e, F&& fn) const
    {
        const auto* pool = peek_pool<kin>();
        if (pool == nullptr || !table_.alive(e))
        {
            return;
        }
        const kin* k = pool->at(e.index());
        if (k == nullptr)
        {
            return;
        }
        const single_pool_lock lock(pool);
        for (entity up = k->parent; up != no_entity;)
        {
            const kin* uk = pool->at(up.index());
            const entity next = (uk != nullptr) ? uk->parent : no_entity;
            if constexpr (std::predicate<F&, entity>)
            {
                if (!fn(up))
                {
                    break;
                }
            }
            else
            {
                fn(up);
            }
            up = next;
        }
    }

    [[nodiscard]] entity root_of(entity e) const noexcept
    {
        const auto* pool = peek_pool<kin>();
        if (!table_.alive(e))
        {
            return no_entity;
        }
        if (pool == nullptr)
        {
            return e;
        }
        entity cur = e;
        for (const kin* k = pool->at(cur.index()); k != nullptr && k->parent != no_entity;
             k = pool->at(cur.index()))
        {
            cur = k->parent;
        }
        return cur;
    }

    [[nodiscard]] std::size_t depth_of(entity e) const noexcept
    {
        const auto* pool = peek_pool<kin>();
        if (pool == nullptr || !table_.alive(e))
        {
            return 0;
        }
        std::size_t d = 0;
        for (const kin* k = pool->at(e.index()); k != nullptr && k->parent != no_entity;
             k = pool->at(k->parent.index()))
        {
            ++d;
        }
        return d;
    }

    template <class F>
        requires std::invocable<F&, entity>
    void roots(F&& fn) const
    {
        const auto* pool = peek_pool<kin>();
        if (pool == nullptr)
        {
            return;
        }
        const single_pool_lock lock(pool);
        for (std::size_t pos = 0; pos < pool->size(); ++pos)
        {
            const entity e = pool->entity_at(pos);
            if (pool->at(e.index())->parent != no_entity)
            {
                continue;
            }
            if constexpr (std::predicate<F&, entity>)
            {
                if (!fn(e))
                {
                    break;
                }
            }
            else
            {
                fn(e);
            }
        }
    }

    std::size_t destroy_subtree(entity root)
    {
        if (!table_.alive(root))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("destroy_subtree on a dead, stale, or null entity handle");
            }
            return 0;
        }
        std::pmr::vector<entity> doomed(memory_);
        doomed.push_back(root);
        descendants_of(root, [&](entity e) { doomed.push_back(e); });
        std::size_t n = 0;
        for (const entity e : doomed)
        {
            if (table_.alive(e))
            {
                destroy(e);
                ++n;
            }
        }
        return n;
    }

    entity duplicate_subtree(entity root)
    {
        if (!table_.alive(root))
        {
            if constexpr (checks_enabled)
            {
                detail::violate("duplicate_subtree on a dead, stale, or null entity handle");
            }
            return no_entity;
        }
        std::pmr::vector<entity> originals(memory_);
        originals.push_back(root);
        descendants_of(root, [&](entity e) { originals.push_back(e); });
        std::pmr::vector<std::pair<entity, entity>> map(memory_);
        map.reserve(originals.size());
        for (const entity o : originals)
        {
            map.emplace_back(o, duplicate(o).clone);
        }
        const auto clone_of = [&](entity o) -> entity
        {
            for (const auto& [orig, cl] : map)
            {
                if (orig == o)
                {
                    return cl;
                }
            }
            return no_entity;
        };
        for (const entity o : originals)
        {
            if (o == root)
            {
                continue;
            }
            const entity cp = clone_of(parent_of(o));
            if (cp != no_entity)
            {
                adopt(cp, clone_of(o));
            }
        }
        return map.front().second;
    }

    template <class F>
        requires std::invocable<F&, entity>
    void each(F&& fn) const
    {
        for (index_type slot = 0; slot < table_.slots(); ++slot)
        {
            if (!table_.occupied(slot))
            {
                continue;
            }
            const entity e(slot, table_.generation_at(slot));
            if constexpr (std::predicate<F&, entity>)
            {
                if (!fn(e))
                {
                    break;
                }
            }
            else
            {
                fn(e);
            }
        }
    }

    std::expected<entity, fault> restore_entity(entity e) { return table_.restore(e); }

    void reset()
    {
        if constexpr (checks_enabled)
        {
            if (any_locked())
            {
                detail::violate("reset while an iteration is running");
                return;
            }
        }

        clearing_ = true;
        // NOLINTNEXTLINE(modernize-loop-convert)
        for (std::size_t i = 0; i < active_.size(); ++i)
        {
            active_[i]->wipe();
        }
        clearing_ = false;
        table_.destroy_all();
    }

    void shrink()
    {
        if constexpr (checks_enabled)
        {
            if (any_locked())
            {
                detail::violate("shrink while an iteration is running");
                return;
            }
        }
        for (pool_base* pool : active_)
        {
            pool->compact();
        }
        table_.shrink();
    }

    [[nodiscard]] std::size_t slot_count() const noexcept { return table_.slots(); }
    [[nodiscard]] std::size_t live_count() const noexcept { return table_.live(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return table_.capacity(); }
    template <class F>
        requires std::invocable<F&, const pool_info&>
    void each_pool(F&& fn) const
    {
        for (const pool_base* pool : active_)
        {
            fn(pool->info());
        }
    }

    template <class F>
        requires std::invocable<F&, const pool_info&>
    void components_of(entity e, F&& fn) const
    {
        if (!table_.alive(e))
        {
            return;
        }
        for (const pool_base* pool : active_)
        {
            if (pool->contains(e.index()))
            {
                fn(pool->info());
            }
        }
    }

    template <class F>
    void visit_components(entity e, F&& visitor) const;

    [[nodiscard]] memory_footprint footprint() const noexcept
    {
        memory_footprint f{};
        f.entity_table_bytes = table_.bytes();
        for (const pool_base* pool : active_)
        {
            const pool_info info = pool->info();
            f.component_bytes += info.capacity * info.bytes_per_item;
            f.index_bytes += info.index_bytes;
            f.bookkeeping_bytes += info.bookkeeping_bytes;
        }
        return f;
    }

    template <component T>
    [[nodiscard]] pool_ref find_pool() const noexcept
    {
        return pool_ref{peek_pool<T>()};
    }

    [[nodiscard]] pool_ref find_pool(std::uint32_t type_id) const noexcept
    {
        return pool_ref{type_id < pools_.size() ? pools_[type_id].get() : nullptr};
    }

    [[nodiscard]] pool_ref find_pool_by_hash(std::uint64_t name_hash) const noexcept
    {
        for (const pool_base* pool : active_)
        {
            if (pool->name_hash() == name_hash)
            {
                return pool_ref{pool};
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<void, fault> validate() const
    {
        if (auto r = table_.check(); !r)
        {
            return r;
        }
        for (const pool_base* pool : active_)
        {
            if (auto r = pool->check(table_); !r)
            {
                return r;
            }
        }
        if (const auto* marks = peek_pool<globals_mark>(); marks != nullptr && marks->size() > 1)
        {
            return std::unexpected(
                fault{fault_code::globals_broken, marks->name(), "more than one globals entity"});
        }
        return check_links();
    }

private:
    friend struct test_access;
    template <class>
    friend class basic_scoped_hook;
    [[nodiscard]] pool_base* pool_for_token(hook_token token) noexcept
    {
        if (!token || token.pool >= pools_.size() || !pools_[token.pool])
        {
            return nullptr;
        }
        return pools_[token.pool].get();
    }

    [[nodiscard]] bool is_globals(entity e) const noexcept
    {
        if (e == globals_)
        {
            return true;
        }
        const auto* marks = peek_pool<globals_mark>();
        return marks != nullptr && marks->contains(e.index());
    }

    template <component T>
    pool_of_t<T>& ensure_pool()
    {
        static_assert(component_pool<pool_of_t<T>, Traits>,
                      "ecs: pool_of<T> specializations must derive ecs::basic_pool (via "
                      "packed_pool_of / stable_pool_of / tag_pool_of, or from scratch) and "
                      "construct from a std::pmr::memory_resource* -- see the pool_of seam "
                      "block");
        const std::uint32_t id = detail::type_id<T>();
        if (id >= pools_.size())
        {
            pools_.resize(id + 1);
        }
        if (!pools_[id])
        {
            auto pool = std::make_unique<pool_of_t<T>>(memory_);
            pool->id_ = id;
            pool->owner_ = this;
            if constexpr (checks_enabled)
            {
                for (const pool_base* existing : active_)
                {
                    if (existing->name_hash() == pool->name_hash())
                    {
                        detail::violate_pool("component name hash collision with pool",
                                             existing->name());
                    }
                }
            }
            active_.push_back(pool.get());
            pools_[id] = std::move(pool);
        }
        return static_cast<pool_of_t<T>&>(*pools_[id]);
    }

    template <component T>
    [[nodiscard]] pool_of_t<T>* peek_pool() const noexcept
    {
        const std::uint32_t id = detail::type_id<T>();
        if (id >= pools_.size())
        {
            return nullptr;
        }
        return static_cast<pool_of_t<T>*>(pools_[id].get());
    }

    template <component T>
    [[nodiscard]] pool_base* pool_base_of() const noexcept
    {
        return peek_pool<T>();
    }

    template <component T>
    [[nodiscard]] bool probe(index_type index) const noexcept
    {
        const auto* pool = peek_pool<T>();
        return pool != nullptr && pool->contains(index);
    }

    template <class... Es, class Self>
    [[nodiscard]] auto include_pools(this Self&& self)
    {
        if constexpr (std::is_const_v<std::remove_reference_t<Self>>)
        {
            return std::array<pool_base*, sizeof...(Es)>{
                self.template pool_base_of<detail::bare<detail::maybe_inner<Es>>>()...};
        }
        else
        {
            return std::array<pool_base*, sizeof...(Es)>{
                &self.template ensure_pool<detail::bare<detail::maybe_inner<Es>>>()...};
        }
    }

    template <class Filter, class Self>
    [[nodiscard]] auto filter_pools(this Self&& self)
    {
        return [&self]<class... Ls>(types<Ls...>)
        {
            if constexpr (std::is_const_v<std::remove_reference_t<Self>>)
            {
                return std::array<pool_base*, sizeof...(Ls)>{self.template pool_base_of<Ls>()...};
            }
            else
            {
                return std::array<pool_base*, sizeof...(Ls)>{&self.template ensure_pool<Ls>()...};
            }
        }(detail::filter_leaves_t<Filter>{});
    }

    template <component T, class Self>
    [[nodiscard]] auto* lookup(this Self&& self, entity e) noexcept
    {
        using pointer =
            std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>;
        if (!self.table_.alive(e))
        {
            return pointer{nullptr};
        }
        auto* pool = self.template peek_pool<T>();
        return pool == nullptr ? pointer{nullptr} : pointer{pool->at(e.index())};
    }

    template <component T, class Self>
    [[nodiscard]] auto* find_in_pool(this Self&& self, entity e) noexcept
    {
        static_assert(!detail::is_tag_v<T>,
                      "ecs: T uses tag storage and carries no data; use has<T>(e)");
        using pointer =
            std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>;
        auto* pool = self.template peek_pool<T>();
        return pool == nullptr ? pointer{nullptr} : pointer{pool->at(e.index())};
    }

    template <class C>
    void add_value(entity e, C&& value)
    {
        using T = std::remove_cvref_t<C>;
        if constexpr (detail::is_tag_v<T>)
        {
            add<T>(e);
        }
        else
        {
            add<T>(e, std::forward<C>(value));
        }
    }

    [[nodiscard]] bool any_locked() const noexcept
    {
        for (const pool_base* pool : active_)
        {
            if (pool->locked())
            {
                return true;
            }
        }
        return false;
    }

    void repoint_pools() noexcept
    {
        for (pool_base* pool : active_)
        {
            pool->owner_ = this;
        }
    }

    template <component T>
    hook_token connect_hook(pool_base::hook_kind kind, component_hook fn, void* user)
    {
        auto& pool = ensure_pool<T>();
        if constexpr (checks_enabled)
        {
            if (fn == nullptr)
            {
                detail::violate_pool("null hook for pool", pool.name());
                return {};
            }
            if (pool.locked())
            {
                detail::violate_pool("hook change during iteration over pool", pool.name());
                return {};
            }
        }
        return hook_token{detail::type_id<T>(), pool.connect_hook(kind, fn, user)};
    }

    [[nodiscard]] bool apply_allowed() const
    {
        if constexpr (checks_enabled)
        {
            for (const pool_base* pool : active_)
            {
                if (pool->locked())
                {
                    detail::violate_pool("apply while iterating pool", pool->name());
                    return false;
                }
            }
        }
        return true;
    }

    apply_result apply_replay(command_buffer& buffer)
    {
        buffer.resolved_.clear();
        apply_result result{};
        for (std::size_t i = 0; i < buffer.ops_.size(); ++i)
        {
            const command_buffer::op o = buffer.ops_[i];
            if (o.kind == command_buffer::op_kind::spawn)
            {
                buffer.resolved_.push_back(table_.create());
                ++result.applied;
                continue;
            }
            bool ok = true;
            const auto resolve = [&](entity h) -> entity
            {
                if (!detail::is_provisional(h))
                {
                    return h;
                }
                const auto ticket = static_cast<index_type>(h.index() & ~limits::provisional_bit);
                if (h.generation() != buffer.nonce_ || ticket >= buffer.resolved_.size())
                {
                    ok = false;
                    return no_entity;
                }
                return buffer.resolved_[ticket];
            };
            const entity target = resolve(o.target);
            if (!ok)
            {
                if constexpr (checks_enabled)
                {
                    detail::violate(
                        "command_buffer: provisional handle from another buffer "
                        "or from before clear()");
                }
                ++result.skipped;
                continue;
            }
            if (!table_.alive(target))
            {
                ++result.skipped;
                continue;
            }
            switch (o.kind)
            {
                case command_buffer::op_kind::kill:
                    destroy(target);
                    break;
                case command_buffer::op_kind::orphan:
                    orphan(target);
                    break;
                case command_buffer::op_kind::subtree_kill:
                    destroy_subtree(target);
                    break;
                case command_buffer::op_kind::adopt:
                {
                    const entity parent = resolve(*static_cast<const entity*>(o.payload));
                    if (!ok)
                    {
                        ++result.skipped;
                        continue;
                    }
                    adopt(parent, target);
                    break;
                }
                default:
                    o.apply_fn(*this, target, o.payload);
                    break;
            }
            ++result.applied;
        }
        return result;
    }

    void sever_links(entity e) noexcept
    {
        if (auto* pool = peek_pool<kin>())
        {
            detail::kin_links::sever(*pool, e);
        }
    }

    [[nodiscard]] std::expected<void, fault> check_links() const
    {
        const auto* pool = peek_pool<kin>();
        return pool == nullptr ? std::expected<void, fault>{}
                               : detail::kin_links::check(*pool, table_);
    }

    entity_table table_;
    std::pmr::vector<std::unique_ptr<pool_base>> pools_;
    std::pmr::vector<pool_base*> active_;
    std::pmr::memory_resource* memory_;
    entity dying_;
    bool clearing_ = false;
    entity globals_;
    detail::basic_dispatcher<Traits> dispatcher_;

    struct rel_hook_entry
    {
        basic_relationship_hook<Traits> fn;
        void* user;
        std::uint32_t id;
    };

    std::vector<rel_hook_entry>& list_for(relationship_kind kind) noexcept
    {
        switch (kind)
        {
            case relationship_kind::orphan:
                return rel_orphan_;
            case relationship_kind::reorder:
                return rel_reorder_;
            default:
                return rel_adopt_;
        }
    }

    relationship_token connect_relationship(relationship_kind kind,
                                            basic_relationship_hook<Traits> fn,
                                            void* user)
    {
        if constexpr (checks_enabled)
        {
            if (fn == nullptr)
            {
                detail::violate("null relationship hook");
                return {};
            }
        }
        const std::uint32_t id = rel_next_id_++;
        list_for(kind).push_back(rel_hook_entry{fn, user, id});
        return relationship_token{kind, id};
    }

    void fire_relationship(std::vector<rel_hook_entry>& list, entity child, entity parent)
    {
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const rel_hook_entry e = list[i];
            e.fn(*this, child, parent, e.user);
        }
    }

    void fire_adopt(entity child, entity parent) { fire_relationship(rel_adopt_, child, parent); }
    void fire_orphan(entity child, entity parent) { fire_relationship(rel_orphan_, child, parent); }
    void fire_reorder(entity child, entity parent)
    {
        fire_relationship(rel_reorder_, child, parent);
    }

    std::vector<rel_hook_entry> rel_adopt_;
    std::vector<rel_hook_entry> rel_orphan_;
    std::vector<rel_hook_entry> rel_reorder_;
    std::uint32_t rel_next_id_ = 1;
};

using registry = basic_registry<default_entity_traits>;

using entity_filler = basic_entity_filler<registry>;
using const_entity_filler = basic_entity_filler<const registry>;

template <class Traits>
class basic_scoped_hook
{
public:
    basic_scoped_hook() = default;

    basic_scoped_hook(basic_registry<Traits>& w, hook_token token) noexcept
        : pool_(w.pool_for_token(token)),
          token_(token)
    {
    }

    basic_scoped_hook(basic_scoped_hook&& other) noexcept
        : pool_(std::exchange(other.pool_, nullptr)),
          token_(std::exchange(other.token_, hook_token{}))
    {
    }

    basic_scoped_hook& operator=(basic_scoped_hook&& other) noexcept
    {
        if (this != &other)
        {
            release();
            pool_ = std::exchange(other.pool_, nullptr);
            token_ = std::exchange(other.token_, hook_token{});
        }
        return *this;
    }

    basic_scoped_hook(const basic_scoped_hook&) = delete;
    basic_scoped_hook& operator=(const basic_scoped_hook&) = delete;

    ~basic_scoped_hook() { release(); }

    void release() noexcept
    {
        if (pool_ == nullptr || !token_)
        {
            pool_ = nullptr;
            token_ = hook_token{};
            return;
        }
        if constexpr (checks_enabled)
        {
            // Disconnecting during iteration is refused (not deferred): mutating the hook
            // list while a pool is locked could corrupt an in-flight dispatch. The hook stays
            // connected and a later release() outside iteration cleans it up. If the owning
            // tracker/watcher is destroyed here, its still-connected member-fn hook dangles --
            // do not destroy reactive objects mid-iteration over a pool they watch.
            if (pool_->locked())
            {
                detail::violate_pool("hook change during iteration over pool", pool_->name());
                return;
            }
        }
        pool_->disconnect_hook(token_.id);
        pool_ = nullptr;
        token_ = hook_token{};
    }

    [[nodiscard]] hook_token token() const noexcept { return token_; }
    explicit operator bool() const noexcept { return static_cast<bool>(token_); }

private:
    detail::basic_pool_base<Traits>* pool_ = nullptr;
    hook_token token_{};
};

using scoped_hook = basic_scoped_hook<default_entity_traits>;

template <class Traits>
class basic_scoped_relationship_hook
{
public:
    basic_scoped_relationship_hook() = default;

    basic_scoped_relationship_hook(basic_registry<Traits>& w, relationship_token token) noexcept
        : world_(&w),
          token_(token)
    {
    }

    basic_scoped_relationship_hook(basic_scoped_relationship_hook&& other) noexcept
        : world_(std::exchange(other.world_, nullptr)),
          token_(std::exchange(other.token_, relationship_token{}))
    {
    }

    basic_scoped_relationship_hook& operator=(basic_scoped_relationship_hook&& other) noexcept
    {
        if (this != &other)
        {
            release();
            world_ = std::exchange(other.world_, nullptr);
            token_ = std::exchange(other.token_, relationship_token{});
        }
        return *this;
    }

    basic_scoped_relationship_hook(const basic_scoped_relationship_hook&) = delete;
    basic_scoped_relationship_hook& operator=(const basic_scoped_relationship_hook&) = delete;

    ~basic_scoped_relationship_hook() { release(); }

    void release() noexcept
    {
        if (world_ != nullptr && token_)
        {
            world_->unhook(token_);
        }
        world_ = nullptr;
        token_ = relationship_token{};
    }

    [[nodiscard]] relationship_token token() const noexcept { return token_; }
    explicit operator bool() const noexcept { return static_cast<bool>(token_); }

private:
    basic_registry<Traits>* world_ = nullptr;
    relationship_token token_{};
};

using scoped_relationship_hook = basic_scoped_relationship_hook<default_entity_traits>;

enum class track : std::uint8_t
{
    added = 1,
    replaced = 2,
    removed = 4,
    all = added | replaced | removed,
};

[[nodiscard]] constexpr track operator|(track a, track b) noexcept
{
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<track>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr track operator&(track a, track b) noexcept
{
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<track>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

template <entity_traits Traits, component T>
class basic_tracker
{
    using entity = ecs::basic_entity<Traits>;
    using world = basic_registry<Traits>;
    using scoped_hook = basic_scoped_hook<Traits>;

public:
    explicit basic_tracker(world& w,
                           track which = track::all,
                           std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : seen_added_(memory),
          seen_replaced_(memory),
          seen_removed_(memory),
          added_(memory),
          replaced_(memory),
          removed_(memory)
    {
        if ((which & track::added) == track::added)
        {
            on_added_ = scoped_hook(w, w.template on_add<T>(&basic_tracker::record_added, this));
        }
        if constexpr (!detail::is_tag_v<T>)
        {
            if ((which & track::replaced) == track::replaced)
            {
                on_replaced_ =
                    scoped_hook(w, w.template on_replace<T>(&basic_tracker::record_replaced, this));
            }
        }
        if ((which & track::removed) == track::removed)
        {
            on_removed_ =
                scoped_hook(w, w.template on_remove<T>(&basic_tracker::record_removed, this));
        }
    }

    basic_tracker(const basic_tracker&) = delete;
    basic_tracker(basic_tracker&&) = delete;
    basic_tracker& operator=(const basic_tracker&) = delete;
    basic_tracker& operator=(basic_tracker&&) = delete;
    ~basic_tracker() = default;

    [[nodiscard]] std::span<const entity> added() const noexcept { return added_; }
    [[nodiscard]] std::span<const entity> replaced() const noexcept { return replaced_; }
    [[nodiscard]] std::span<const entity> removed() const noexcept { return removed_; }

    void clear() noexcept
    {
        drain(added_, seen_added_);
        drain(replaced_, seen_replaced_);
        drain(removed_, seen_removed_);
    }

private:
    static void record_added(world&, entity e, void* user)
    {
        auto* self = static_cast<basic_tracker*>(user);
        self->record(self->added_, self->seen_added_, e, false);
    }

    static void record_replaced(world&, entity e, void* user)
    {
        auto* self = static_cast<basic_tracker*>(user);
        self->record(self->replaced_, self->seen_replaced_, e, false);
    }

    static void record_removed(world&, entity e, void* user)
    {
        auto* self = static_cast<basic_tracker*>(user);
        self->record(self->removed_, self->seen_removed_, e, true);
    }

    void record(std::pmr::vector<entity>& list,
                detail::basic_sparse_index<Traits>& seen,
                entity e,
                bool keep_history)
    {
        const auto pos = seen.get(e.index());
        if (pos != detail::entity_limits<Traits>::npos)
        {
            if (!keep_history || list[pos] == e)
            {
                list[pos] = e;
                return;
            }
        }
        seen.ensure(e.index());
        list.push_back(e);
        seen.set_existing(e.index(), static_cast<typename Traits::index_type>(list.size() - 1));
    }

    static void drain(std::pmr::vector<entity>& list,
                      detail::basic_sparse_index<Traits>& seen) noexcept
    {
        for (const entity e : list)
        {
            seen.erase(e.index());
        }
        list.clear();
    }

    detail::basic_sparse_index<Traits> seen_added_;
    detail::basic_sparse_index<Traits> seen_replaced_;
    detail::basic_sparse_index<Traits> seen_removed_;
    std::pmr::vector<entity> added_;
    std::pmr::vector<entity> replaced_;
    std::pmr::vector<entity> removed_;
    scoped_hook on_added_;
    scoped_hook on_replaced_;
    scoped_hook on_removed_;
};

template <component T>
using tracker = basic_tracker<default_entity_traits, T>;

template <class... Es>
struct except
{
};

template <class T>
struct changed
{
};

namespace detail
{
template <class S>
struct watcher_spec
{
    static constexpr bool is_includes = false;
    static constexpr bool is_excludes = false;
    static constexpr bool is_trigger = false;
    using list = types<>;
};

template <class... Is>
struct watcher_spec<types<Is...>>
{
    static constexpr bool is_includes = true;
    static constexpr bool is_excludes = false;
    static constexpr bool is_trigger = false;
    using list = types<Is...>;
};

template <class... Es>
struct watcher_spec<except<Es...>>
{
    static constexpr bool is_includes = false;
    static constexpr bool is_excludes = true;
    static constexpr bool is_trigger = false;
    using list = types<Es...>;
};

template <class C>
struct watcher_spec<changed<C>>
{
    static constexpr bool is_includes = false;
    static constexpr bool is_excludes = false;
    static constexpr bool is_trigger = true;
    using list = types<C>;
};

template <class A, class B>
struct lists_disjoint;

template <class... As, class... Bs>
struct lists_disjoint<types<As...>, types<Bs...>>
{
    static constexpr bool value = (!type_among<As, Bs...> && ...);
};

template <class List>
struct watcher_conditions_ok;

template <class... Is>
struct watcher_conditions_ok<types<Is...>>
{
    static_assert((component<Is> && ...),
                  "ecs: watcher condition types must be plain, non-const component types");
    static_assert(all_distinct<Is...>, "ecs: duplicate component type in a watcher list");
    static constexpr bool value = true;
};

template <class List>
struct watcher_triggers_ok;

template <class... Cs>
struct watcher_triggers_ok<types<Cs...>>
{
    static_assert((component<Cs> && ...),
                  "ecs: changed<T> takes a plain, non-const component type");
    static_assert((!is_tag_v<Cs> && ...),
                  "ecs: changed<T> on a tag is inert -- tags carry no data and are never "
                  "replaced");
    static_assert(all_distinct<Cs...>, "ecs: duplicate changed<> trigger");
    static constexpr bool value = true;
};
}  // namespace detail

template <class Traits, class... Specs>
class basic_watcher
{
    using entity = ecs::basic_entity<Traits>;
    using world = basic_registry<Traits>;
    using scoped_hook = basic_scoped_hook<Traits>;
    using watcher = basic_watcher;

    static_assert((std::size_t{0} + ... + std::size_t{detail::watcher_spec<Specs>::is_includes}) ==
                      1,
                  "ecs: a watcher takes exactly one types<...> condition list");
    static_assert((std::size_t{0} + ... + std::size_t{detail::watcher_spec<Specs>::is_excludes}) <=
                      1,
                  "ecs: at most one except<...> list per watcher");
    static_assert(((detail::watcher_spec<Specs>::is_includes ||
                    detail::watcher_spec<Specs>::is_excludes ||
                    detail::watcher_spec<Specs>::is_trigger) &&
                   ...),
                  "ecs: watcher specs are types<...>, except<...>, and changed<T>");

    using include_list = joined_t<std::conditional_t<detail::watcher_spec<Specs>::is_includes,
                                                     typename detail::watcher_spec<Specs>::list,
                                                     types<>>...>;
    using exclude_list = joined_t<std::conditional_t<detail::watcher_spec<Specs>::is_excludes,
                                                     typename detail::watcher_spec<Specs>::list,
                                                     types<>>...>;
    using trigger_list = joined_t<std::conditional_t<detail::watcher_spec<Specs>::is_trigger,
                                                     typename detail::watcher_spec<Specs>::list,
                                                     types<>>...>;

    static_assert(include_list::size >= 1, "ecs: a watcher needs at least one condition component");
    static_assert(detail::watcher_conditions_ok<include_list>::value);
    static_assert(detail::watcher_conditions_ok<exclude_list>::value);
    static_assert(detail::watcher_triggers_ok<trigger_list>::value);
    static_assert(detail::lists_disjoint<include_list, exclude_list>::value,
                  "ecs: a component type appears in both the watcher's types<...> and "
                  "except<...> lists");

    static constexpr std::size_t hook_count =
        (include_list::size * 2) + (exclude_list::size * 2) + trigger_list::size;

public:
    explicit basic_watcher(world& w,
                           std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : seen_(memory),
          matched_(memory)
    {
        std::size_t k = 0;
        connect_includes(w, k, include_list{});
        connect_excludes(w, k, exclude_list{});
        connect_triggers(w, k, trigger_list{});
    }

    basic_watcher(const basic_watcher&) = delete;
    basic_watcher(basic_watcher&&) = delete;
    basic_watcher& operator=(const basic_watcher&) = delete;
    basic_watcher& operator=(basic_watcher&&) = delete;
    ~basic_watcher() = default;

    [[nodiscard]] std::span<const entity> matched() const noexcept { return matched_; }
    [[nodiscard]] std::size_t count() const noexcept { return matched_.size(); }
    [[nodiscard]] bool empty() const noexcept { return matched_.empty(); }

    [[nodiscard]] bool contains(entity e) const noexcept
    {
        const auto pos = seen_.get(e.index());
        return pos != detail::entity_limits<Traits>::npos && matched_[pos] == e;
    }

    void clear() noexcept
    {
        for (const entity e : matched_)
        {
            seen_.erase(e.index());
        }
        matched_.clear();
    }

private:
    template <class... Is>
    void connect_includes(world& w, std::size_t& k, types<Is...>)
    {
        ((hooks_[k] = scoped_hook(w, w.template on_add<Is>(&watcher::probe_edge, this)),
          hooks_[k + 1] = scoped_hook(w, w.template on_remove<Is>(&watcher::evict_edge, this)),
          k += 2),
         ...);
    }

    template <class... Es>
    void connect_excludes(world& w, std::size_t& k, types<Es...>)
    {
        ((hooks_[k] = scoped_hook(w, w.template on_add<Es>(&watcher::evict_edge, this)),
          hooks_[k + 1] =
              scoped_hook(w, w.template on_remove<Es>(&watcher::unshielded_edge<Es>, this)),
          k += 2),
         ...);
    }

    template <class... Cs>
    void connect_triggers(world& w, std::size_t& k, types<Cs...>)
    {
        ((hooks_[k++] = scoped_hook(w, w.template on_replace<Cs>(&watcher::probe_edge, this))),
         ...);
    }

    static void probe_edge(world& w, entity e, void* user)
    {
        auto* self = static_cast<watcher*>(user);
        if (matches<void>(w, e))
        {
            self->insert(e);
        }
    }

    template <class SkipX>
    static void unshielded_edge(world& w, entity e, void* user)
    {
        auto* self = static_cast<watcher*>(user);
        if (matches<SkipX>(w, e))
        {
            self->insert(e);
        }
    }

    static void evict_edge(world&, entity e, void* user) { static_cast<watcher*>(user)->evict(e); }

    template <class SkipX>
    [[nodiscard]] static bool matches(world& w, entity e)
    {
        const bool held = []<class... Is>(types<Is...>, world& ww, entity ee)
        { return (ww.template has<Is>(ee) && ...); }(include_list{}, w, e);
        if (!held)
        {
            return false;
        }
        return []<class... Es>(types<Es...>, world& ww, entity ee)
        { return ((std::same_as<Es, SkipX> || !ww.template has<Es>(ee)) && ...); }(
            exclude_list{}, w, e);
    }

    void insert(entity e)
    {
        if (seen_.get(e.index()) != detail::entity_limits<Traits>::npos)
        {
            return;
        }
        seen_.ensure(e.index());
        matched_.push_back(e);
        seen_.set_existing(e.index(),
                           static_cast<typename Traits::index_type>(matched_.size() - 1));
    }

    void evict(entity e) noexcept
    {
        const auto pos = seen_.get(e.index());
        if (pos == detail::entity_limits<Traits>::npos)
        {
            return;
        }
        const entity last = matched_.back();
        matched_[pos] = last;
        seen_.set_existing(last.index(), pos);
        matched_.pop_back();
        seen_.erase(e.index());
    }

    detail::basic_sparse_index<Traits> seen_;
    std::pmr::vector<entity> matched_;
    std::array<scoped_hook, hook_count> hooks_;
};

template <class... Specs>
using watcher = basic_watcher<default_entity_traits, Specs...>;

template <class W, class Traits = default_entity_traits>
concept archive_writer = requires(W& w, std::uint64_t n, basic_entity<Traits> e) {
    w(n);
    w(e);
};

template <class W, class T, class Traits = default_entity_traits>
concept archive_writer_for =
    archive_writer<W, Traits> && (detail::is_tag_v<T> || requires(W& w, const T& v) { w(v); });

template <class R, class Traits = default_entity_traits>
concept archive_reader = requires(R& r, std::uint64_t& n, basic_entity<Traits>& e) {
    r(n);
    r(e);
};

template <class R, class T, class Traits = default_entity_traits>
concept archive_reader_for =
    archive_reader<R, Traits> && (detail::is_tag_v<T> || requires(R& r, T& v) { r(v); });

namespace detail
{
struct archive_access;

template <class Traits>
[[nodiscard]] consteval std::uint64_t layout_stamp() noexcept
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto mix = [&h](std::uint64_t v)
    {
        h ^= v;
        h *= 0x100000001b3ull;
    };
    mix(static_cast<std::uint64_t>(std::numeric_limits<typename Traits::index_type>::digits));
    mix(Traits::index_bits);
    mix(static_cast<std::uint64_t>(std::numeric_limits<typename Traits::generation_type>::digits));
    return h;
}
}  // namespace detail

template <class Traits>
class basic_graft_map
{
    using entity = ecs::basic_entity<Traits>;

public:
    explicit basic_graft_map(std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : pairs_(memory)
    {
    }

    [[nodiscard]] entity resolve(entity old) const noexcept
    {
        const auto it = std::ranges::partition_point(pairs_,
                                                     [&](const std::pair<entity, entity>& p)
                                                     { return p.first.index() < old.index(); });
        if (it != pairs_.end() && it->first == old)
        {
            return it->second;
        }
        return entity{};
    }

    [[nodiscard]] std::size_t size() const noexcept { return pairs_.size(); }
    [[nodiscard]] bool empty() const noexcept { return pairs_.empty(); }

    template <class F>
        requires std::invocable<F&, entity, entity>
    void each(F&& fn) const
    {
        for (const auto& [old, fresh] : pairs_)
        {
            fn(old, fresh);
        }
    }

private:
    friend struct detail::archive_access;
    std::pmr::vector<std::pair<entity, entity>> pairs_;
};

using graft_map = basic_graft_map<default_entity_traits>;

namespace detail
{
struct archive_access
{
    template <class Traits>
    static auto& pairs(basic_graft_map<Traits>& map) noexcept
    {
        return map.pairs_;
    }
};

template <class T, class Traits>
concept has_member_relink = requires(T& v, const basic_graft_map<Traits>& m) { v.ecs_relink(m); };
}  // namespace detail

template <class T, class Traits = default_entity_traits>
struct relink_traits
{
    static constexpr bool links = detail::has_member_relink<T, Traits>;

    static void relink(T& value, const basic_graft_map<Traits>& map)
        requires detail::has_member_relink<T, Traits>
    {
        value.ecs_relink(map);
    }
};

namespace detail
{
template <class T>
[[nodiscard]] consteval std::uint64_t schema_hash() noexcept
{
    std::uint64_t h = hash_of<T>();
    const auto mix = [&h](std::uint64_t v)
    {
        h ^= v;
        h *= 0x100000001b3ULL;
    };
    mix(sizeof(T));
    mix(alignof(T));
    if constexpr (reflectable_aggregate<T>)
    {
        mix(field_count_v<T>);
        [&]<std::size_t... Is>(std::index_sequence<Is...>)
        { (mix(fnv1a(field_name<T, Is>())), ...); }(std::make_index_sequence<field_count_v<T>>{});
    }
    return h;
}

template <component T, class Traits, class W>
void pack_one(const basic_registry<Traits>& w, W& out)
{
    using entity = ecs::basic_entity<Traits>;
    out(schema_hash<T>());
    const auto sel = w.template view<T>();
    out(static_cast<std::uint64_t>(sel.count()));
    if constexpr (is_tag_v<T>)
    {
        sel.entities([&](entity e) { out(e); });
    }
    else
    {
        sel.each(
            [&](entity e, const T& value)
            {
                out(e);
                out(value);
            });
    }
}

template <component T, class Traits, class R>
[[nodiscard]] std::expected<void, fault> unpack_one(basic_registry<Traits>& w,
                                                    R& in,
                                                    std::uint64_t max_entities)
{
    using entity = ecs::basic_entity<Traits>;
    std::uint64_t hash{};
    in(hash);
    if (hash != schema_hash<T>())
    {
        return std::unexpected(
            fault{fault_code::archive_mismatch, name_of<T>(), "type hash or layout differs"});
    }
    std::uint64_t count{};
    in(count);
    if (count > max_entities)
    {
        return std::unexpected(
            fault{fault_code::archive_too_large, name_of<T>(), "row count exceeds cap"});
    }
    for (std::uint64_t i = 0; i < count; ++i)
    {
        entity e;
        in(e);
        if (!w.alive(e))
        {
            return std::unexpected(
                fault{fault_code::archive_mismatch, name_of<T>(), "row owner not restored"});
        }
        if constexpr (is_tag_v<T>)
        {
            w.template add<T>(e);
        }
        else
        {
            static_assert(std::default_initializable<T> && std::is_move_constructible_v<T>,
                          "ecs: unpack/graft default-construct a row, read into it, and "
                          "move it into the world; give T those operations or load manually");
            T value{};
            in(value);
            w.template add<T>(e, std::move(value));
        }
    }
    return {};
}

template <component T, class Traits, class R>
[[nodiscard]] std::expected<void, fault> graft_one(basic_registry<Traits>& w,
                                                   R& in,
                                                   const basic_graft_map<Traits>& map,
                                                   std::uint64_t max_entities)
{
    using entity = ecs::basic_entity<Traits>;
    constexpr entity no_entity{};
    std::uint64_t hash{};
    in(hash);
    if (hash != schema_hash<T>())
    {
        return std::unexpected(
            fault{fault_code::archive_mismatch, name_of<T>(), "type hash or layout differs"});
    }
    std::uint64_t count{};
    in(count);
    if (count > max_entities)
    {
        return std::unexpected(
            fault{fault_code::archive_too_large, name_of<T>(), "row count exceeds cap"});
    }
    for (std::uint64_t i = 0; i < count; ++i)
    {
        entity old;
        in(old);
        const entity owner = map.resolve(old);
        if (owner == no_entity)
        {
            return std::unexpected(
                fault{fault_code::archive_mismatch, name_of<T>(), "row owner outside archive"});
        }
        if constexpr (is_tag_v<T>)
        {
            w.template add<T>(owner);
        }
        else
        {
            static_assert(std::default_initializable<T> && std::is_move_constructible_v<T>,
                          "ecs: unpack/graft default-construct a row, read into it, and "
                          "move it into the world; give T those operations or load manually");
            T value{};
            in(value);
            if constexpr (relink_traits<T, Traits>::links)
            {
                relink_traits<T, Traits>::relink(value, map);
            }
            w.template add<T>(owner, std::move(value));
        }
    }
    return {};
}
}  // namespace detail

inline constexpr std::uint64_t archive_unbounded = std::numeric_limits<std::uint64_t>::max();
template <component... Ts, class Traits, class W>
void pack(const basic_registry<Traits>& w, W& out)
{
    using entity = ecs::basic_entity<Traits>;
    static_assert(sizeof...(Ts) > 0, "ecs: pack needs at least one component type");
    static_assert((archive_writer_for<W, Ts, Traits> && ...),
                  "ecs: the writer must be callable with std::uint64_t, ecs::entity, and "
                  "each non-tag component as (const T&)");
    static_assert((std::same_as<Ts, detail::bare<Ts>> && ...),
                  "ecs: pack/unpack/graft take plain component types");
    static_assert(detail::all_distinct<Ts...>, "ecs: duplicate component type in pack");
    out(detail::layout_stamp<Traits>());
    out(static_cast<std::uint64_t>(w.live_count()));
    w.each([&](entity e) { out(e); });
    (detail::pack_one<Ts>(w, out), ...);
}

template <class Traits, class W, component... Ts>
void pack(const basic_registry<Traits>& w, W& out, types<Ts...>)
{
    pack<Ts...>(w, out);
}

template <component... Ts, class Traits, class R>
[[nodiscard]] std::expected<void, fault> unpack(basic_registry<Traits>& w,
                                                R& in,
                                                std::uint64_t max_entities = archive_unbounded)
{
    using entity = ecs::basic_entity<Traits>;
    static_assert(sizeof...(Ts) > 0, "ecs: unpack needs at least one component type");
    static_assert((archive_reader_for<R, Ts, Traits> && ...),
                  "ecs: the reader must be callable with std::uint64_t&, ecs::entity&, "
                  "and each non-tag component as (T&)");
    static_assert((std::same_as<Ts, detail::bare<Ts>> && ...),
                  "ecs: pack/unpack/graft take plain component types");
    if (w.live_count() != 0)
    {
        if constexpr (checks_enabled)
        {
            detail::violate("unpack into a non-empty world (reset() it, or use graft)");
        }
        return std::unexpected(fault{fault_code::world_not_empty, {}, "unpack target"});
    }
    std::uint64_t stamp{};
    in(stamp);
    if (stamp != detail::layout_stamp<Traits>())
    {
        return std::unexpected(
            fault{fault_code::archive_mismatch, {}, "stream packed with different entity traits"});
    }
    std::uint64_t count{};
    in(count);
    if (count > max_entities)
    {
        return std::unexpected(
            fault{fault_code::archive_too_large, {}, "entity count exceeds cap"});
    }
    for (std::uint64_t i = 0; i < count; ++i)
    {
        entity e;
        in(e);
        if (static_cast<std::uint64_t>(e.index()) >= max_entities)
        {
            return std::unexpected(
                fault{fault_code::archive_too_large, {}, "entity index exceeds cap"});
        }
        if (auto restored = w.restore_entity(e); !restored)
        {
            return std::unexpected(restored.error());
        }
    }
    std::expected<void, fault> result{};
    static_cast<void>(((result = detail::unpack_one<Ts>(w, in, max_entities)).has_value() && ...));
    return result;
}
template <class Traits, class R, component... Ts>
[[nodiscard]] std::expected<void, fault> unpack(basic_registry<Traits>& w,
                                                R& in,
                                                types<Ts...>,
                                                std::uint64_t max_entities = archive_unbounded)
{
    return unpack<Ts...>(w, in, max_entities);
}

template <component... Ts, class Traits, class R>
[[nodiscard]] std::expected<basic_graft_map<Traits>, fault> graft(
    basic_registry<Traits>& w,
    R& in,
    std::pmr::memory_resource* memory = std::pmr::get_default_resource(),
    std::uint64_t max_entities = archive_unbounded)
{
    using entity = ecs::basic_entity<Traits>;
    static_assert(sizeof...(Ts) > 0, "ecs: graft needs at least one component type");
    static_assert((archive_reader_for<R, Ts, Traits> && ...),
                  "ecs: the reader must be callable with std::uint64_t&, ecs::entity&, "
                  "and each non-tag component as (T&)");
    static_assert((std::same_as<Ts, detail::bare<Ts>> && ...),
                  "ecs: pack/unpack/graft take plain component types");
    basic_graft_map<Traits> map(memory);
    auto& pairs = detail::archive_access::pairs(map);
    std::uint64_t stamp{};
    in(stamp);
    if (stamp != detail::layout_stamp<Traits>())
    {
        return std::unexpected(
            fault{fault_code::archive_mismatch, {}, "stream packed with different entity traits"});
    }
    std::uint64_t count{};
    in(count);
    if (count > max_entities)
    {
        return std::unexpected(
            fault{fault_code::archive_too_large, {}, "entity count exceeds cap"});
    }
    pairs.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i)
    {
        entity old;
        in(old);
        pairs.emplace_back(old, w.create().id());
    }
    std::expected<void, fault> result{};
    static_cast<void>(
        ((result = detail::graft_one<Ts>(w, in, map, max_entities)).has_value() && ...));
    if (!result)
    {
        return std::unexpected(result.error());
    }
    return map;
}

template <class Traits, class R, component... Ts>
[[nodiscard]] std::expected<basic_graft_map<Traits>, fault> graft(
    basic_registry<Traits>& w,
    R& in,
    types<Ts...>,
    std::pmr::memory_resource* memory = std::pmr::get_default_resource(),
    std::uint64_t max_entities = archive_unbounded)
{
    return graft<Ts...>(w, in, memory, max_entities);
}

template <class Traits, class W>
void pack_links(const basic_registry<Traits>& w, W& out)
{
    using entity = ecs::basic_entity<Traits>;
    std::vector<std::pair<entity, entity>> edges;
    w.roots([&](entity r)
            { w.descendants_of(r, [&](entity e) { edges.emplace_back(e, w.parent_of(e)); }); });
    out(static_cast<std::uint64_t>(edges.size()));
    for (const auto& [child, parent] : edges)
    {
        out(child);
        out(parent);
    }
}

template <class Traits, class R>
[[nodiscard]] std::expected<void, fault> unpack_links(
    basic_registry<Traits>& w, R& in, std::uint64_t max_entities = archive_unbounded)
{
    using entity = ecs::basic_entity<Traits>;
    std::uint64_t count{};
    in(count);
    if (count > max_entities)
    {
        return std::unexpected(
            fault{fault_code::archive_too_large, {}, "link edge count exceeds cap"});
    }
    for (std::uint64_t i = 0; i < count; ++i)
    {
        entity child;
        entity parent;
        in(child);
        in(parent);
        if (!w.alive(child) || !w.alive(parent))
        {
            return std::unexpected(
                fault{fault_code::archive_mismatch, {}, "link endpoint not restored"});
        }
        w.adopt(parent, child);
    }
    return {};
}

template <class Traits, class R>
[[nodiscard]] std::expected<void, fault> graft_links(basic_registry<Traits>& w,
                                                     R& in,
                                                     const basic_graft_map<Traits>& map,
                                                     std::uint64_t max_entities = archive_unbounded)
{
    using entity = ecs::basic_entity<Traits>;
    constexpr entity no_entity{};
    std::uint64_t count{};
    in(count);
    if (count > max_entities)
    {
        return std::unexpected(
            fault{fault_code::archive_too_large, {}, "link edge count exceeds cap"});
    }
    for (std::uint64_t i = 0; i < count; ++i)
    {
        entity child;
        entity parent;
        in(child);
        in(parent);
        const entity nc = map.resolve(child);
        const entity np = map.resolve(parent);
        if (nc == no_entity || np == no_entity)
        {
            return std::unexpected(
                fault{fault_code::archive_mismatch, {}, "link endpoint outside archive"});
        }
        w.adopt(np, nc);
    }
    return {};
}

template <class W>
class basic_entity_filler
{
    static constexpr bool writable = !std::is_const_v<W>;
    using entity = typename std::remove_const_t<W>::entity;

public:
    basic_entity_filler() = default;

    basic_entity_filler(W& w, entity e) noexcept
        : world_(&w),
          entity_(e)
    {
    }

    template <class V>
        requires(std::is_const_v<W> && std::same_as<V, std::remove_const_t<W>>)
    basic_entity_filler(const basic_entity_filler<V>& other) noexcept
        : world_(other.world_),
          entity_(other.entity_)
    {
    }

    [[nodiscard]] entity id() const noexcept { return entity_; }
    operator entity() const noexcept { return entity_; }  // NOLINT(google-explicit-constructor)
    [[nodiscard]] W& owner() const noexcept { return *world_; }

    [[nodiscard]] bool alive() const noexcept
    {
        return world_ != nullptr && world_->alive(entity_);
    }
    explicit operator bool() const noexcept { return alive(); }

    template <component T, class... Args>
    decltype(auto) add(Args&&... args) const
        requires writable
    {
        return bound("entity_filler::add").template add<T>(entity_, std::forward<Args>(args)...);
    }

    template <component T>
        requires(writable && !detail::is_tag_v<T>)
    decltype(auto) add(T&& value) const
    {
        return bound("entity_filler::add").template add<T, T>(entity_, std::forward<T>(value));
    }

    template <component T, class... Args>
    decltype(auto) put(Args&&... args) const
        requires writable
    {
        return bound("entity_filler::put").template put<T>(entity_, std::forward<Args>(args)...);
    }

    template <component T>
        requires(writable && !detail::is_tag_v<T>)
    decltype(auto) put(T&& value) const
    {
        return bound("entity_filler::put").template put<T, T>(entity_, std::forward<T>(value));
    }

    template <component T, class... Args>
    T& replace(Args&&... args) const
        requires writable
    {
        return bound("entity_filler::replace")
            .template replace<T>(entity_, std::forward<Args>(args)...);
    }

    template <component T>
        requires(writable && !detail::is_tag_v<T>)
    T& replace(T&& value) const
    {
        return bound("entity_filler::replace")
            .template replace<T, T>(entity_, std::forward<T>(value));
    }

    template <component T, class... Args>
    decltype(auto) obtain(Args&&... args) const
        requires writable
    {
        return bound("entity_filler::obtain")
            .template obtain<T>(entity_, std::forward<Args>(args)...);
    }

    template <component T>
        requires(writable && !detail::is_tag_v<T>)
    decltype(auto) obtain(T&& value) const
    {
        return bound("entity_filler::obtain")
            .template obtain<T, T>(entity_, std::forward<T>(value));
    }

    template <component T>
    bool remove() const
        requires writable
    {
        return world_ != nullptr && world_->template remove<T>(entity_);
    }

    template <component T>
    [[nodiscard]] bool has() const noexcept
    {
        return world_ != nullptr && world_->template has<T>(entity_);
    }

    template <component T>
    [[nodiscard]] decltype(auto) get() const
    {
        return bound("entity_filler::get").template get<T>(entity_);
    }

    template <component T, component U, component... Rest>
    [[nodiscard]] auto get() const
    {
        return bound("entity_filler::get").template get<T, U, Rest...>(entity_);
    }

    template <component T>
    [[nodiscard]] auto* find() const noexcept
    {
        using pointer = decltype(world_->template find<T>(entity_));
        return world_ != nullptr ? world_->template find<T>(entity_) : pointer{nullptr};
    }

    template <component T, component U, component... Rest>
    [[nodiscard]] bool has_all() const noexcept
    {
        return world_ != nullptr && world_->template has_all<T, U, Rest...>(entity_);
    }

    template <component T, component U, component... Rest>
    [[nodiscard]] bool has_any() const noexcept
    {
        return world_ != nullptr && world_->template has_any<T, U, Rest...>(entity_);
    }

    template <component T, component U, component... Rest>
    [[nodiscard]] auto find_all() const noexcept
    {
        using row =
            decltype(std::declval<W&>().template find_all<T, U, Rest...>(std::declval<entity>()));
        return world_ != nullptr ? world_->template find_all<T, U, Rest...>(entity_) : row{};
    }

    template <component T, class F>
        requires(writable && std::invocable<F&, T&>)
    T& amend(F&& fn) const
    {
        return bound("entity_filler::amend").template amend<T>(entity_, std::forward<F>(fn));
    }

    template <component T, class... Args>
    const basic_entity_filler& component(Args&&... args) const
        requires writable
    {
        bound("entity_filler::component").template add<T>(entity_, std::forward<Args>(args)...);
        return *this;
    }

    template <ecs::component T>
        requires(writable && !detail::is_tag_v<T>)
    const basic_entity_filler& component(T&& value) const
    {
        bound("entity_filler::component").template add<T, T>(entity_, std::forward<T>(value));
        return *this;
    }

    void destroy() const noexcept
        requires writable
    {
        if (world_ != nullptr)
        {
            world_->destroy(entity_);
        }
    }

    void orphan() const
        requires writable
    {
        if (world_ != nullptr)
        {
            world_->orphan(entity_);
        }
    }

    [[nodiscard]] basic_entity_filler parent() const noexcept
    {
        return world_ != nullptr ? basic_entity_filler{*world_, world_->parent_of(entity_)}
                                 : basic_entity_filler{};
    }

private:
    template <class>
    friend class basic_entity_filler;

    [[nodiscard]] W& bound(const char* what) const
    {
        if (world_ == nullptr)
        {
            if constexpr (checks_enabled)
            {
                detail::violate_pool("call on an empty (default-constructed) entity_filler:", what);
            }
            std::abort();
        }
        return *world_;
    }

    W* world_ = nullptr;
    entity entity_;
};

template <class Traits>
typename basic_registry<Traits>::entity_filler basic_registry<Traits>::ref(entity e) noexcept
{
    return entity_filler{*this, e};
}

template <class Traits>
typename basic_registry<Traits>::const_entity_filler basic_registry<Traits>::ref(
    entity e) const noexcept
{
    return const_entity_filler{*this, e};
}

template <class Traits>
typename basic_registry<Traits>::entity_filler basic_registry<Traits>::globals()
{
    if (!table_.alive(globals_))
    {
        globals_ = view<globals_mark>().first();
        if (globals_ == no_entity)
        {
            globals_ = table_.create();
        }
    }
    if (!has<globals_mark>(globals_))
    {
        add<globals_mark>(globals_);
    }
    return entity_filler{*this, globals_};
}

template <class Traits>
typename basic_registry<Traits>::const_entity_filler basic_registry<Traits>::globals()
    const noexcept
{
    if (table_.alive(globals_))
    {
        return const_entity_filler{*this, globals_};
    }
    return const_entity_filler{*this, view<globals_mark>().first()};
}

namespace detail
{
struct field_node
{
    std::string_view name;
    std::uint64_t owner_hash;
    std::uint64_t type_hash;
    any (*get)(const void* object);
    bool (*set)(void* object, const any& value);
};

struct method_node
{
    std::string_view name;
    std::uint64_t owner_hash;
    bool is_const;
    any (*invoke)(void* object, std::span<any> args);
};

struct ctor_node
{
    std::size_t arity;
    any (*construct)(std::span<any> args);
};

struct ecs_bridge
{
    bool (*add_to)(registry& w, entity e, any& value) = nullptr;
    bool (*remove_from)(registry& w, entity e) = nullptr;
    bool (*present_on)(const registry& w, entity e) = nullptr;
};

struct type_node
{
    std::uint64_t hash = 0;
    std::string_view name;
    std::size_t size_bytes = 0;
    std::size_t align = 0;
    std::deque<field_node> fields;
    std::deque<method_node> methods;
    std::deque<ctor_node> ctors;
    ecs_bridge ecs;
};

inline std::vector<std::unique_ptr<type_node>>& reflection_registry()
{
    static std::vector<std::unique_ptr<type_node>> nodes;
    return nodes;
}

[[nodiscard]] inline type_node* find_type_node(std::uint64_t hash) noexcept
{
    auto& nodes = reflection_registry();
    const auto it = std::lower_bound(nodes.begin(),
                                     nodes.end(),
                                     hash,
                                     [](const std::unique_ptr<type_node>& node, std::uint64_t h)
                                     { return node->hash < h; });
    return it != nodes.end() && (*it)->hash == hash ? it->get() : nullptr;
}

template <class M>
struct member_object_traits;

template <class C, class M>
struct member_object_traits<M C::*>
{
    using owner = C;
    using value = M;
};

template <class F>
struct member_function_traits;

template <class C, class R, class... As>
struct member_function_traits<R (C::*)(As...)>
{
    using owner = C;
    using result = R;
    using args = types<As...>;
    static constexpr bool is_const = false;
};

template <class C, class R, class... As>
struct member_function_traits<R (C::*)(As...) const>
{
    using owner = C;
    using result = R;
    using args = types<As...>;
    static constexpr bool is_const = true;
};
}  // namespace detail

class field
{
public:
    field() = default;

    explicit operator bool() const noexcept { return node_ != nullptr; }
    [[nodiscard]] std::string_view name() const noexcept
    {
        return node_ != nullptr ? node_->name : std::string_view{};
    }
    [[nodiscard]] std::uint64_t type_hash() const noexcept
    {
        return node_ != nullptr ? node_->type_hash : 0;
    }

    [[nodiscard]] any get(const any& object) const
    {
        if (node_ == nullptr || object.type_hash() != node_->owner_hash)
        {
            return {};
        }
        return node_->get(object.data());
    }

    bool set(any& object, const any& value) const
    {
        if (node_ == nullptr || object.type_hash() != node_->owner_hash)
        {
            return false;
        }
        return node_->set(object.data(), value);
    }

    [[nodiscard]] any get_at(const void* object) const
    {
        return node_ != nullptr ? node_->get(object) : any{};
    }

    bool set_at(void* object, const any& value) const
    {
        return node_ != nullptr && node_->set(object, value);
    }

private:
    friend class reflection;

    explicit field(const detail::field_node* node) noexcept
        : node_(node)
    {
    }

    const detail::field_node* node_ = nullptr;
};

class method
{
public:
    method() = default;

    explicit operator bool() const noexcept { return node_ != nullptr; }
    [[nodiscard]] std::string_view name() const noexcept
    {
        return node_ != nullptr ? node_->name : std::string_view{};
    }

    any invoke(any& object, std::span<any> args) const
    {
        if (node_ == nullptr || object.type_hash() != node_->owner_hash)
        {
            return {};
        }
        return node_->invoke(object.data(), args);
    }

    any invoke(const any& object, std::span<any> args) const
    {
        if (node_ == nullptr || !node_->is_const || object.type_hash() != node_->owner_hash)
        {
            return {};
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        return node_->invoke(const_cast<void*>(object.data()), args);
    }

private:
    friend class reflection;

    explicit method(const detail::method_node* node) noexcept
        : node_(node)
    {
    }

    const detail::method_node* node_ = nullptr;
};

class reflection
{
public:
    reflection() = default;

    explicit operator bool() const noexcept { return node_ != nullptr; }
    [[nodiscard]] std::string_view name() const noexcept
    {
        return node_ != nullptr ? node_->name : std::string_view{};
    }
    [[nodiscard]] std::uint64_t hash() const noexcept { return node_ != nullptr ? node_->hash : 0; }
    [[nodiscard]] std::size_t size_bytes() const noexcept
    {
        return node_ != nullptr ? node_->size_bytes : 0;
    }
    [[nodiscard]] std::size_t align() const noexcept { return node_ != nullptr ? node_->align : 0; }

    [[nodiscard]] field find_field(std::string_view name) const noexcept
    {
        if (node_ != nullptr)
        {
            for (const detail::field_node& f : node_->fields)
            {
                if (f.name == name)
                {
                    return field(&f);
                }
            }
        }
        return {};
    }

    [[nodiscard]] method find_method(std::string_view name) const noexcept
    {
        if (node_ != nullptr)
        {
            for (const detail::method_node& m : node_->methods)
            {
                if (m.name == name)
                {
                    return method(&m);
                }
            }
        }
        return {};
    }

    template <class F>
        requires std::invocable<F&, const field&>
    void each_field(F&& fn) const
    {
        if (node_ != nullptr)
        {
            for (const detail::field_node& f : node_->fields)
            {
                fn(field(&f));
            }
        }
    }

    [[nodiscard]] any construct(std::span<any> args) const
    {
        if (node_ != nullptr)
        {
            for (const detail::ctor_node& c : node_->ctors)
            {
                if (c.arity != args.size())
                {
                    continue;
                }
                any made = c.construct(args);
                if (made.holds())
                {
                    return made;
                }
            }
        }
        return {};
    }

    bool add_to(registry& w, entity e, any value) const
    {
        return node_ != nullptr && node_->ecs.add_to != nullptr && node_->ecs.add_to(w, e, value);
    }

    bool remove_from(registry& w, entity e) const
    {
        return node_ != nullptr && node_->ecs.remove_from != nullptr &&
               node_->ecs.remove_from(w, e);
    }

    [[nodiscard]] bool present_on(const registry& w, entity e) const noexcept
    {
        return node_ != nullptr && node_->ecs.present_on != nullptr && node_->ecs.present_on(w, e);
    }

private:
    template <class T>
    friend class reflect_builder;
    friend reflection reflection_of(std::uint64_t hash) noexcept;

    explicit reflection(const detail::type_node* node) noexcept
        : node_(node)
    {
    }

    const detail::type_node* node_ = nullptr;
};

[[nodiscard]] inline reflection reflection_of(std::uint64_t hash) noexcept
{
    return reflection(detail::find_type_node(hash));
}

[[nodiscard]] inline reflection reflection_of(std::string_view name) noexcept
{
    return reflection_of(detail::fnv1a(name));
}

template <class T>
[[nodiscard]] reflection reflection_of() noexcept
{
    return reflection_of(hash_of<detail::bare<T>>());
}

template <class F>
    requires std::invocable<F&, const reflection&>
void for_each(F&& fn)
{
    for (const std::unique_ptr<detail::type_node>& node : detail::reflection_registry())
    {
        fn(reflection_of(node->hash));
    }
}
[[nodiscard]] inline any get(const any& object, std::string_view field_name)
{
    return reflection_of(object.type_hash()).find_field(field_name).get(object);
}

inline bool set(any& object, std::string_view field_name, const any& value)
{
    return reflection_of(object.type_hash()).find_field(field_name).set(object, value);
}

inline any invoke(any& object, std::string_view method_name, std::span<any> args = {})
{
    return reflection_of(object.type_hash()).find_method(method_name).invoke(object, args);
}

inline any invoke(const any& object, std::string_view method_name, std::span<any> args = {})
{
    return reflection_of(object.type_hash()).find_method(method_name).invoke(object, args);
}

struct component_view
{
    pool_info info;
    reflection reflect;
    void* bytes = nullptr;

    [[nodiscard]] std::string_view name() const noexcept { return info.name; }
    [[nodiscard]] std::uint64_t name_hash() const noexcept { return info.name_hash; }
    [[nodiscard]] bool reflected() const noexcept { return static_cast<bool>(reflect); }

    [[nodiscard]] any value(std::string_view field_name) const
    {
        const field f = reflect.find_field(field_name);
        return (f && bytes != nullptr) ? f.get_at(bytes) : any{};
    }

    template <class F>
    void each_field(F&& visitor) const
    {
        if (bytes != nullptr)
        {
            reflect.each_field([&](const field& f) { visitor(f, f.get_at(bytes)); });
        }
    }
};

template <class Traits>
template <class F>
void basic_registry<Traits>::visit_components(entity e, F&& visitor) const
{
    if (!table_.alive(e))
    {
        return;
    }
    for (const pool_base* pool : active_)
    {
        if (!pool->contains(e.index()))
        {
            continue;
        }
        const pool_info info = pool->info();
        auto ref = find_pool(info.id);
        const component_view view{info, reflection_of(info.name_hash), ref.raw(e)};
        visitor(view);
    }
}

template <class T>
class reflect_builder;

template <class T>
reflect_builder<T> reflect();

template <class T>
class reflect_builder
{
public:
    template <auto Member>
    reflect_builder& field(std::string_view name)
    {
        using traits = detail::member_object_traits<decltype(Member)>;
        static_assert(std::same_as<typename traits::owner, T>,
                      "ecs: field<&U::m> must name a member of the reflected type");
        using value = typename traits::value;
        node_->fields.push_back(detail::field_node{
            name,
            hash_of<T>(),
            hash_of<detail::bare<value>>(),
            +[](const void* object) -> any
            { return any::make<detail::bare<value>>(static_cast<const T*>(object)->*Member); },
            +[](void* object, const any& incoming) -> bool
            {
                const auto* payload = incoming.template try_as<detail::bare<value>>();
                if (payload == nullptr)
                {
                    return false;
                }
                static_cast<T*>(object)->*Member = *payload;
                return true;
            },
        });
        return *this;
    }

    template <class... Names>
        requires reflectable_aggregate<T>
    reflect_builder& fields(Names... names)
    {
        constexpr std::size_t n = field_count_v<T>;
        static_assert(sizeof...(Names) == 0 || sizeof...(Names) == n,
                      "ecs: fields(...) takes no names (positional) or exactly "
                      "field_count_v<T> of them");
        [&]<std::size_t... Is>(std::index_sequence<Is...>)
        {
            if constexpr (sizeof...(Names) == 0)
            {
                if constexpr (field_names_supported)
                {
                    (auto_field<Is>(field_names_v<T>[Is]), ...);  // real "x","y",...
                }
                else
                {
                    (auto_field<Is>(positional_name<Is>()), ...);  // "0","1",...
                }
            }
            else
            {
                const std::array<std::string_view, n> labels{std::string_view{names}...};
                (auto_field<Is>(labels[Is]), ...);
            }
        }(std::make_index_sequence<n>{});
        return *this;
    }

    template <auto Method>
    reflect_builder& method(std::string_view name)
    {
        using traits = detail::member_function_traits<decltype(Method)>;
        static_assert(std::same_as<typename traits::owner, T>,
                      "ecs: method<&U::fn> must name a member of the reflected type");
        node_->methods.push_back(detail::method_node{
            name,
            hash_of<T>(),
            traits::is_const,
            &invoke_thunk<Method>,
        });
        return *this;
    }

    template <class... Args>
    reflect_builder& construct()
    {
        static_assert(std::constructible_from<T, Args...>,
                      "ecs: construct<Args...> must name a real constructor of T");
        node_->ctors.push_back(detail::ctor_node{
            sizeof...(Args),
            +[](std::span<any> args) -> any
            {
                return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> any
                {
                    const bool typed =
                        ((args[Is].template try_as<detail::bare<Args>>() != nullptr) && ...);
                    if (!typed)
                    {
                        return {};
                    }
                    return any::make<T>(*args[Is].template try_as<detail::bare<Args>>()...);
                }(std::index_sequence_for<Args...>{});
            },
        });
        return *this;
    }

private:
    friend reflect_builder<T> reflect<T>();

    template <auto Method>
    static any invoke_thunk(void* object, std::span<any> args)
    {
        using traits = detail::member_function_traits<decltype(Method)>;
        return [&]<class... As>(types<As...>) -> any
        {
            if (args.size() != sizeof...(As))
            {
                return {};
            }
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> any
            {
                const bool typed =
                    ((args[Is].template try_as<detail::bare<As>>() != nullptr) && ...);
                if (!typed)
                {
                    return {};
                }
                using result = typename traits::result;
                if constexpr (std::is_void_v<result>)
                {
                    (static_cast<T*>(object)->*Method)(
                        *args[Is].template try_as<detail::bare<As>>()...);
                    return {};
                }
                else
                {
                    return any::make<detail::bare<result>>((static_cast<T*>(object)->*Method)(
                        *args[Is].template try_as<detail::bare<As>>()...));
                }
            }(std::index_sequence_for<As...>{});
        }(typename traits::args{});
    }

    template <std::size_t I>
    void auto_field(std::string_view name)
    {
        using value =
            std::remove_cvref_t<std::tuple_element_t<I, decltype(tie_fields(std::declval<T&>()))>>;
        node_->fields.push_back(detail::field_node{
            name,
            hash_of<T>(),
            hash_of<detail::bare<value>>(),
            +[](const void* object) -> any
            {
                return any::make<detail::bare<value>>(
                    std::get<I>(tie_fields(*static_cast<const T*>(object))));
            },
            +[](void* object, const any& incoming) -> bool
            {
                const auto* payload = incoming.template try_as<detail::bare<value>>();
                if (payload == nullptr)
                {
                    return false;
                }
                std::get<I>(tie_fields(*static_cast<T*>(object))) = *payload;
                return true;
            },
        });
    }

    template <std::size_t I>
    static std::string_view positional_name()
    {
        static const std::string label = std::to_string(I);
        return label;
    }

    explicit reflect_builder(detail::type_node* node) noexcept
        : node_(node)
    {
    }

    detail::type_node* node_;
};

template <class T>
reflect_builder<T> reflect()
{
    static_assert(std::same_as<T, detail::bare<T>>,
                  "ecs: reflect<T> takes a plain, unqualified type");
    constexpr std::uint64_t hash = hash_of<T>();
    if (detail::type_node* standing = detail::find_type_node(hash); standing != nullptr)
    {
        if constexpr (checks_enabled)
        {
            detail::violate_pool("reflect<T> on an already registered type", name_of<T>());
        }
        return reflect_builder<T>(standing);
    }
    auto& nodes = detail::reflection_registry();
    const auto at = std::lower_bound(nodes.begin(),
                                     nodes.end(),
                                     hash,
                                     [](const std::unique_ptr<detail::type_node>& node,
                                        std::uint64_t h) { return node->hash < h; });
    auto node = std::make_unique<detail::type_node>();
    node->hash = hash;
    node->name = name_of<T>();
    node->size_bytes = sizeof(T);
    node->align = alignof(T);
    if constexpr (component<T>)
    {
        node->ecs.add_to = +[](registry& w, entity e, any& value) -> bool
        {
            if (!w.alive(e) || w.has<T>(e))
            {
                return false;
            }
            if constexpr (detail::is_tag_v<T>)
            {
                w.add<T>(e);
                return true;
            }
            else
            {
                if (T* payload = value.try_as<T>(); payload != nullptr)
                {
                    if constexpr (std::move_constructible<T>)
                    {
                        w.add<T>(e, std::move(*payload));
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                if (!value.holds())
                {
                    if constexpr (std::default_initializable<T>)
                    {
                        w.add<T>(e);
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                return false;
            }
        };
        node->ecs.remove_from =
            +[](registry& w, entity e) -> bool { return w.alive(e) && w.remove<T>(e); };
        node->ecs.present_on = +[](const registry& w, entity e) -> bool { return w.has<T>(e); };
    }
    detail::type_node* raw = node.get();
    nodes.insert(at, std::move(node));
    return reflect_builder<T>(raw);
}

template <class... Ts>
void reflect_all(types<Ts...>)
{
    ((detail::find_type_node(hash_of<detail::bare<Ts>>()) == nullptr
          ? static_cast<void>(reflect<detail::bare<Ts>>())
          : void()),
     ...);
}

#if ECS_CHECKS
struct test_access
{
    template <component T>
    static void corrupt_sparse(registry& w, entity e)
    {
        static_cast<detail::pool_base*>(w.peek_pool<T>())
            ->sparse_.set(e.index(), detail::npos32 - 1);
    }

    static void corrupt_generation(registry& w, entity e) { ++w.table_.generation_[e.index()]; }

    template <component T>
    static std::uint32_t lock_count(const registry& w)
    {
        const detail::pool_base* pool = w.peek_pool<T>();
        return pool == nullptr ? 0 : pool->locks_;
    }
};
#endif

template <entity_traits Traits>
struct basic_entity_hash
{
    [[nodiscard]] std::size_t operator()(basic_entity<Traits> e) const noexcept
    {
        return std::hash<std::uint64_t>{}(e.bits());
    }
};

using entity_hash = basic_entity_hash<default_entity_traits>;

}  // namespace ecs

template <ecs::entity_traits Traits>
struct std::hash<ecs::basic_entity<Traits>>
{
    [[nodiscard]] std::size_t operator()(ecs::basic_entity<Traits> e) const noexcept
    {
        return std::hash<std::uint64_t>{}(e.bits());
    }
};
