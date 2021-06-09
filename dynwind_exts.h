#pragma once

namespace guile_cpp_utils {
namespace detail {
struct destructor_wrapper {
  std::function<void()> dest;

  static inline void invoke(void* dest_wrapper) {
    static_cast<destructor_wrapper*>(dest_wrapper)->dest();
  }
};
}

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

template <typename... Args>
inline void scm_dynwind_cpp_destroy(Args&... args) {
  detail::destructor_wrapper* dest =
      static_cast<detail::destructor_wrapper*>(
          scm_gc_malloc_pointerless(sizeof(detail::destructor_wrapper),
                                    "cpp-destructor"));

  dest->dest = make_destructor(args...);

  scm_dynwind_unwind_handler(&detail::destructor_wrapper::invoke, dest,
                             scm_t_wind_flags(0));
};
}
