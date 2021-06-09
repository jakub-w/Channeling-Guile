#include <memory>

#include "Channeling/Server.h"
#include "Channeling/Client.h"
#include "Channeling/PakeHandshaker.h"

#include <libguile.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include "ProtocolCommon.h"
#include "dynwind_exts.h"
#include "libguile/backtrace.h"
#include "libguile/finalizers.h"
#include "libguile/strings.h"

using namespace Channeling;

template <typename Container>
class bytes_wrapper final {
 public:
  bytes_wrapper(Container&& container) noexcept
      : container_{std::move(container)},
        bytevector_{scm_pointer_to_bytevector(
            scm_from_pointer(std::data(container_), nullptr),
            scm_from_uint(std::size(container_)),
            scm_from_int(0),
            scm_from_utf8_symbol("vu8"))} {
    scm_gc_protect_object(bytevector_);
  }

  bytes_wrapper(const bytes_wrapper& other) = delete;
  bytes_wrapper(bytes_wrapper&& other) = delete;
  const bytes_wrapper& operator=(const bytes_wrapper& other) = delete;
  const bytes_wrapper& operator=(bytes_wrapper&& other) = delete;

  ~bytes_wrapper() noexcept {
    scm_gc_unprotect_object(bytevector_);
  }

  inline constexpr typename Container::pointer data() noexcept {
    return std::data(container_);
  }
  inline constexpr typename Container::const_pointer data() const noexcept {
    return std::data(container_);
  }
  inline constexpr typename Container::size_type size() noexcept {
    return std::size(container_);
  }

  inline SCM bytevector() { return bytevector_; }

 private:
  Container container_;
  SCM bytevector_;
};

class scm_span final {
 public:
  explicit scm_span(SCM bytevector) noexcept
      : bv_{bytevector},
        data_{bv_to_ptr(bv_)},
        size_{scm_to_uint(scm_bytevector_length(bv_))} {
    scm_gc_protect_object(bv_);
  }
  scm_span(const scm_span& other) = delete;
  scm_span(scm_span&& other)
      : bv_{std::exchange(other.bv_, SCM_UNSPECIFIED)},
        data_{std::exchange(other.data_, nullptr)},
        size_{std::exchange(other.size_, 0)} {
    scm_gc_protect_object(other.bv_);
  }
  const scm_span& operator=(const scm_span& other) = delete;
  const scm_span& operator=(scm_span&& other) = delete;

  ~scm_span() noexcept {
    scm_gc_unprotect_object(bv_);
  }

  inline constexpr const unsigned char* data() const {
    return data_;
  }
  inline constexpr unsigned char* data() {
    return data_;
  }
  inline constexpr size_t size() const {
    return size_;
  }

 private:
  static unsigned char* bv_to_ptr(SCM bv) {
    return static_cast<unsigned char*>(
        scm_to_pointer(
            scm_bytevector_to_pointer(bv, scm_from_int(0))));
  }

  SCM bv_;
  unsigned char* data_ = nullptr;
  size_t size_ = 0;
};

using HandshakerType = PakeHandshaker;
using ServerType = Server<HandshakerType, std::function<scm_span(Bytes&&)>>;
using ClientType = Client<HandshakerType>;

// UTILITY FUNCTIONS

std::string scm_str_to_str(SCM scm) {
  char* str = scm_to_utf8_string(scm);
  std::string result{str};
  free(str);
  return result;
}

std::string scm_obj_to_str(SCM obj) {
  return scm_str_to_str(scm_simple_format(SCM_BOOL_F,
                                          scm_from_utf8_string("~a"),
                                          scm_list_1(obj)));
}

/// Returns a copy of bytes in a form of a bytevector.
SCM bytes_to_scm(Bytes& bytes) {
  if (bytes.empty()) {
    return scm_make_bytevector(scm_from_int(0), SCM_UNDEFINED);
  }
  SCM scm_ptr = scm_from_pointer(bytes.data(), nullptr);
  return scm_bytevector_copy(
      scm_pointer_to_bytevector(
          scm_ptr,  scm_from_int(bytes.size()),
          scm_from_int(0), scm_from_utf8_symbol("vu8")));
}

SCM bytevector_to_hex(SCM bv) {
  SCM_ASSERT_TYPE(scm_is_bytevector(bv), bv, SCM_ARG1,
                  "bytevector->hex", "bytevector");

  const auto bytes = scm_span(bv);
  const std::string hex = to_hex(bytes.data(), bytes.size());
  return scm_from_utf8_stringn(hex.data(), hex.size());
}

// SERVER foreign data type
static SCM server_type;

static void finalize_server(SCM server) {
  ServerType* scm_serv =
      static_cast<ServerType*>(scm_foreign_object_ref(server, 0));
  if (scm_serv) delete scm_serv;
}

static void init_server_type() {
  SCM name = scm_from_utf8_symbol("server");
  SCM slots = scm_list_1(scm_from_utf8_symbol("pointer"));

  server_type = scm_make_foreign_object_type(name, slots, finalize_server);
}

SCM c_catch_handler(void* /*data*/, SCM key, SCM args) {
  scm_display_error(SCM_BOOL_F, scm_current_error_port(),
                    scm_from_utf8_string("message_handler"),
                    scm_from_utf8_string("Error while executing server "
                                         "message handler: ~a, ~a"),
                    scm_list_2(key, args), SCM_UNDEFINED);
  return SCM_BOOL_F;
}

struct message_handler_data {
  SCM handler;
  bytes_wrapper<Bytes>* data;
};
SCM run_message_handler(void* data) {
  auto [handler, bytes] = *static_cast<message_handler_data*>(data);
  return scm_call_1(handler, bytes->bytevector());
}

// SERVER functions
SCM make_server(SCM password, SCM message_handler) {
  SCM_ASSERT_TYPE(scm_string_p(password), password, SCM_ARG1,
                  "make-server", "string");
  SCM_ASSERT_TYPE(scm_procedure_p(message_handler),
                  message_handler, SCM_ARG2,
                  "make-server", "procedure taking one argument");

  scm_dynwind_begin(static_cast<scm_t_dynwind_flags>(0));
  char* password_str = scm_to_utf8_string(password);
  scm_dynwind_free(password_str);

  auto handshaker = std::make_shared<HandshakerType>(password_str);

  ServerType* server = new ServerType{
    std::move(handshaker),
    [message_handler](Bytes&& bytes){
      scm_dynwind_begin(scm_t_dynwind_flags(0));
      auto bytes_c = bytes_wrapper(std::move(bytes));
      scm_dynwind_cpp_destroy(bytes_c);

      message_handler_data handler_data{message_handler, &bytes_c};

      SCM result = scm_internal_catch(SCM_BOOL_T,
                                      run_message_handler, &handler_data,
                                      c_catch_handler, nullptr);
      scm_remember_upto_here_1(message_handler);

      if (not scm_is_bytevector(result)) {
        scm_dynwind_end();
        throw std::logic_error(
            "Server message handler returned a wrong type object. "
            "Expected bytevector, got: " + scm_obj_to_str(result));
      }

      scm_dynwind_end();

      return scm_span(result);
    }
  };

  // FIXME: on non-local exit server should be destroyed

  SCM foreign_object = scm_make_foreign_object_1(server_type, server);
  scm_dynwind_end();

  return foreign_object;
}

SCM server_bind(SCM server, SCM address) {
  scm_assert_foreign_object_type(server_type, server);
  SCM_ASSERT_TYPE(scm_string_p(address), address, SCM_ARG1,
                  "server-bind", "string");

  scm_dynwind_begin(static_cast<scm_t_dynwind_flags>(0));
  char* address_str = scm_to_utf8_string(address);
  scm_dynwind_free(address_str);

  auto* c_server =
      static_cast<ServerType*>(scm_foreign_object_ref(server, 0));

  // c_server->Bind(address_str.get());
  c_server->Bind(address_str);

  scm_dynwind_end();

  return SCM_UNSPECIFIED;
}

SCM server_run(SCM server) {
  scm_assert_foreign_object_type(server_type, server);

  auto* c_server =
      static_cast<ServerType*>(scm_foreign_object_ref(server, 0));

  c_server->Run();

  return SCM_UNSPECIFIED;
}

SCM server_close(SCM server) {
  scm_assert_foreign_object_type(server_type, server);

  auto* c_server =
      static_cast<ServerType*>(scm_foreign_object_ref(server, 0));

  c_server->Close();

  return SCM_UNSPECIFIED;
}

// CLIENT foreign data type
static SCM client_type;

static void finalize_client(SCM client) {
  ClientType* scm_client =
      static_cast<ClientType*>(scm_foreign_object_ref(client, 0));

  if (scm_client) delete scm_client;
}

static void init_client_type() {
  SCM name = scm_from_utf8_symbol("client");
  SCM slots = scm_list_1(scm_from_utf8_symbol("pointer"));

  client_type = scm_make_foreign_object_type(name, slots, finalize_client);
}

// CLIENT functions
SCM make_client(SCM password) {
  SCM_ASSERT_TYPE(scm_string_p(password), password, SCM_ARG1,
                  "make-client", "string");

  scm_dynwind_begin(static_cast<scm_t_dynwind_flags>(0));
  char* password_str = scm_to_utf8_string(password);
  scm_dynwind_free(password_str);

  ClientType* client = new ClientType{
    std::make_shared<HandshakerType>(password_str)};

  // FIXME: on non-local exit client should be destroyed

  auto foreign_object = scm_make_foreign_object_1(client_type, client);
  scm_dynwind_end();

  return foreign_object;
}

SCM client_connect(SCM client, SCM address) {
  scm_assert_foreign_object_type(client_type, client);
  SCM_ASSERT_TYPE(scm_string_p(address), address, SCM_ARG2,
                  "client-connect", "string");

  scm_dynwind_begin(static_cast<scm_t_dynwind_flags>(0));
  char* address_str = scm_to_utf8_string(address);
  scm_dynwind_free(address_str);

  ClientType* client_c =
      static_cast<ClientType*>(scm_foreign_object_ref(client, 0));

  // FIXME: Handle errors
  SCM ok = scm_from_bool(client_c->Connect(address_str));

  scm_dynwind_end();

  return ok;
}

SCM client_start(SCM client) {
  scm_assert_foreign_object_type(client_type, client);

  ClientType* client_c =
      static_cast<ClientType*>(scm_foreign_object_ref(client, 0));

  scm_dynwind_begin(scm_t_dynwind_flags(0));
  auto ec = client_c->Start();
  scm_dynwind_cpp_destroy(ec);

  if (ec) {
    SCM arg;
    if (ec == std::errc::operation_not_permitted) {
      arg = scm_from_utf8_string("Wrong state. Not connected to the server?");
    } else if (ec == std::errc::protocol_error) {
      arg = scm_from_utf8_string("Internal error");
    } else {
      arg = scm_from_utf8_string("Unhandled error type.");
    }

    scm_misc_error("client-start", "Failed to start the client: ~A",
                   scm_list_1(arg));
  }

  scm_dynwind_end();

  return SCM_UNSPECIFIED;
}

SCM client_stop(SCM client) {
  scm_assert_foreign_object_type(client_type, client);

  ClientType* client_c =
      static_cast<ClientType*>(scm_foreign_object_ref(client, 0));

  client_c->Stop();

  // delete client_c;
  // scm_foreign_object_set_x(client, 0, nullptr);

  return SCM_UNSPECIFIED;
}

SCM client_request(SCM client, SCM data) {
  scm_assert_foreign_object_type(client_type, client);
  SCM_ASSERT_TYPE(scm_is_bytevector(data), data, SCM_ARG2,
                  "client-request", "bytevector");

  ClientType* client_c =
      static_cast<ClientType*>(scm_foreign_object_ref(client, 0));

  scm_dynwind_begin(scm_t_dynwind_flags(0));
  auto maybe_result = client_c->Request(scm_span{data});
  scm_dynwind_cpp_destroy(maybe_result);

  if (not maybe_result) {
    const auto& ec = maybe_result.error();
    SCM arg;
    if (ec == std::errc::operation_not_permitted) {
      arg = scm_from_utf8_string("Client is not running.");
    } else if (ec == std::errc::protocol_error) {
      arg = scm_from_utf8_string("Internal error.");
    } else {
      arg = scm_from_utf8_string("Unhandled error type.");
    }

    scm_misc_error("client-request", "Failed to issue a request: ~A",
                   scm_list_1(arg));
  }

  scm_dynwind_end();

  return bytes_to_scm(maybe_result.value());
}

SCM channeling_cleanup() {
  scm_run_finalizers();

  return SCM_UNSPECIFIED;
}

extern "C" {
void init_channeling_wrapper() {
  spdlog::should_log(spdlog::level::debug);

  init_server_type();
  init_client_type();

  scm_set_automatic_finalization_enabled(true);

  scm_c_define_gsubr("make-server", 2, 0, 0, (void*) make_server);
  scm_c_define_gsubr("server-bind", 2, 0, 0, (void*) server_bind);
  scm_c_define_gsubr("server-run", 1, 0, 0, (void*) server_run);
  scm_c_define_gsubr("server-close", 1, 0, 0, (void*) server_close);

  scm_c_define_gsubr("make-client", 1, 0, 0, (void*) make_client);
  scm_c_define_gsubr("client-connect", 2, 0, 0, (void*) client_connect);
  scm_c_define_gsubr("client-start", 1, 0, 0, (void*) client_start);
  scm_c_define_gsubr("client-stop", 1, 0, 0, (void*) client_stop);
  scm_c_define_gsubr("client-request", 2, 0, 0, (void*) client_request);

  scm_c_define_gsubr("bytevector->hex", 1, 0, 0, (void*) bytevector_to_hex);

  scm_c_define_gsubr("channeling-cleanup", 0, 0, 0,
                     (void*) channeling_cleanup);

}
}
// TODO: Check if destructors will run on guile's non-local exits

// TODO: Make a SCM foreign type for Bytes.
//       This type should be expected to be the return value of
//       message_handler.
