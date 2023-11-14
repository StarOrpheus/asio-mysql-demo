#include <iostream>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/experimental/as_single.hpp>
#include <boost/mysql.hpp>

using namespace std::literals;

using boost::asio::experimental::as_single_t;
using boost::asio::use_awaitable_t;
using boost::asio::ip::tcp;

using default_token = as_single_t<use_awaitable_t<>>;
using tcp_resolver = default_token::as_default_on_t<tcp::resolver>;
using mysql_connection = boost::mysql::tcp_ssl_connection;
using io_context = boost::asio::io_context;
using ssl_context = boost::asio::ssl::context;


template <class T> using awaitable = boost::asio::awaitable<T>;

constexpr auto TupleAwaitable = as_tuple(boost::asio::use_awaitable);

constexpr auto USERNAME = "root"sv;
constexpr auto PASSWORD = "Secret"sv;
constexpr auto HOST = "127.0.0.1"sv;

namespace {

auto withDbConnected(io_context &Ctx,
                     ssl_context &SslCtx,
                     std::function<awaitable<void>(mysql_connection &)> F) ->
  awaitable<void> {
  boost::system::error_code Ec;
  boost::mysql::diagnostics Diag;

  // Resolve the hostname to get a collection of endpoints
  tcp_resolver Resolver(Ctx.get_executor());
  auto [resolveError, endpoints] = co_await Resolver.async_resolve(
      HOST, boost::mysql::default_port_string);
  if (resolveError) {
    std::cerr << "Host resolve failed: " << resolveError.what() << std::endl;
    co_return;
  }

  // Represents a connection to the MySQL server.
  mysql_connection Conn(Ctx.get_executor(), SslCtx);

  // The username, password and database to use
  boost::mysql::handshake_params Params(
      USERNAME,
      PASSWORD,
      "CompanyDB", // database
      boost::mysql::handshake_params::default_collation,
      boost::mysql::ssl_mode::enable
      );

  std::tie(Ec) = co_await Conn.async_connect(*endpoints.begin(), Params, Diag,
                                             TupleAwaitable);
  throw_on_error(Ec, Diag);

  co_await F(Conn);

  std::cerr << "!!!DATABASE CLOSED!!!" << std::endl;

  co_await Conn.async_close(default_token{});
}

auto printEmployee(boost::mysql::row_view Employee) -> void {
  std::cout << "Employee '" << Employee.at(0) << " " // first_name (string)
      << Employee.at(1) << "' earns "                // last_name  (string)
      << Employee.at(2) << " dollars yearly\n";      // salary     (double)
}

auto dbActions(mysql_connection &Conn) -> awaitable<void> {
  std::error_code Ec;
  boost::mysql::statement Stmt;
  boost::mysql::diagnostics Diag;

  std::tie(Ec, Stmt) = co_await Conn.
      async_prepare_statement(
          "SELECT first_name, last_name, salary FROM employees",
          Diag,
          TupleAwaitable
          );
  throw_on_error(Ec, Diag);

  boost::mysql::results Result;
  std::tie(Ec) = co_await Conn.async_execute(Stmt.bind(), Result, Diag,
                                             TupleAwaitable);
  throw_on_error(Ec, Diag);

  // Print all employees
  for (const boost::mysql::row_view Employee : Result.rows()) {
    printEmployee(Employee);
  }
}

auto mainImpl(io_context &Ctx,
              ssl_context &SslContext) -> awaitable<void> {
  co_await withDbConnected(Ctx, SslContext, [](mysql_connection &conn) {
    return dbActions(conn);
  });
}
} // namespace

auto main() -> int {

  // The execution context, required to run I/O operations.
  io_context Ctx;

  // The SSL context, required to establish TLS connections.
  // The default SSL options are good enough for us at this point.
  ssl_context SslContext(ssl_context::tls_client);

  auto MainImpl = [&Ctx, &SslContext] {
    return mainImpl(Ctx, SslContext);
  };

  auto ErrorHandler = [](std::exception_ptr Ptr) {
    if (Ptr) {
      std::rethrow_exception(Ptr);
    }
  };

  co_spawn(Ctx.get_executor(), MainImpl, ErrorHandler);

  try {
    Ctx.run();
  } catch (const boost::mysql::error_with_diagnostics &Err) {
    std::cerr << "Error: " << Err.what() << '\n'
        << "Server diagnostics: "
        << Err.get_diagnostics().server_message() << std::endl;
    return 1;
  } catch (std::exception const &E) {
    std::cout << "Exception from main(): " << E.what() << std::endl;
  }
}
