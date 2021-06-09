#pragma once

inline void call_destructors(void* destructor) {
  std::invoke(*static_cast<std::function<void()>*>(destructor));
}

template <typename Arg, typename... Args>
auto make_destructor(Arg& arg, Args&... rest) {
  return [&]{
    arg.~Arg();
    make_destructor(rest...)();
  };
}

template <typename Arg>
auto make_destructor(Arg& arg) {
  return [&]{ arg.~Arg(); };
}

template <typename... Args>
inline void scm_dynwind_cpp_destroy(Args&... args){
  static thread_local std::function<void()> destr;
  destr = make_destructor(args...);
  scm_dynwind_unwind_handler(&call_destructors, &destr, scm_t_wind_flags(0));
};
