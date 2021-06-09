#pragma once

namespace guile_cpp_utils {
namespace detail {
template <typename Fun>
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
    arg.~Arg();
    make_destructor(rest...)();
  };
}

template <typename Arg>
inline auto make_destructor(Arg& arg) {
  return [&]{ arg.~Arg(); };
}
}

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
