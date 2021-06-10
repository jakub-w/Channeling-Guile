#pragma once

namespace guile_cpp_utils {
namespace detail {
template <typename Fun,
          typename = std::enable_if_t<std::is_trivially_destructible_v<Fun>>>
struct destructor_wrapper {
  const Fun dest;

  destructor_wrapper(Fun&& f) : dest{std::move(f)} {}

  static inline void invoke(void* dest_wrapper) {
    static_cast<destructor_wrapper*>(dest_wrapper)->dest();
  }
};

template <typename Arg, typename... Args>
inline auto make_destructor(Arg& arg, Args&... rest) {
  return [&]{
    make_destructor(rest...)();
    arg.~Arg();
  };
}

template <typename Arg>
inline auto make_destructor(Arg& arg) {
  return [&]{ arg.~Arg(); };
}
}

// Register ARGS objects to be destroyed on a non-local exit from the current
// dynamic extent.
// Must be used inside of a dynamic wind context.
template <typename... Args>
inline void scm_dynwind_cpp_destroy(Args&... args) {
  auto d = detail::make_destructor(args...);
  using dest_t = detail::destructor_wrapper<decltype(d)>;

  dest_t* dest = static_cast<dest_t*>(
      scm_gc_malloc_pointerless(sizeof(dest_t), "cpp-destructor"));
  new (dest) dest_t(std::move(d));

  scm_dynwind_unwind_handler(&dest_t::invoke, dest, scm_t_wind_flags(0));
}
}
