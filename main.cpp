#include <vector>
#include <iostream>
#include <string_view>

#include <boost/mysql.hpp>
#include <boost/asio.hpp>
#include <boost/asio/experimental/as_single.hpp>

using namespace std::literals;

using boost::asio::experimental::as_single_t;
using boost::asio::use_awaitable_t;
using boost::asio::ip::tcp;
using default_token = as_single_t<use_awaitable_t<>>;
using tcp_resolver = default_token::as_default_on_t<tcp::resolver>;
using mysql_connection = boost::mysql::tcp_ssl_connection;

template <class T> using awaitable = boost::asio::awaitable<T>;

constexpr auto tuple_awaitable = boost::asio::as_tuple(
    boost::asio::use_awaitable);

constexpr auto USERNAME = "root"sv;
constexpr auto PASSWORD = "Secret"sv;
constexpr auto HOST = "127.0.0.1"sv;

auto withDbConnected(boost::asio::io_context &ctx,
                     boost::asio::ssl::context &ssl_ctx,
                     std::function<awaitable<void>(mysql_connection &)> F) ->
  awaitable<void> {
  boost::system::error_code ec;
  boost::mysql::diagnostics diag;

  // Resolve the hostname to get a collection of endpoints
  tcp_resolver resolver(ctx.get_executor());
  auto [resolveError, endpoints] = co_await resolver.async_resolve(
      HOST, boost::mysql::default_port_string);
  if (resolveError) {
    std::cerr << "Host resolve failed: " << resolveError.what() << std::endl;
    co_return;
  }

  // Represents a connection to the MySQL server.
  mysql_connection conn(ctx.get_executor(), ssl_ctx);

  // The username, password and database to use
  boost::mysql::handshake_params params(
      USERNAME,
      PASSWORD,
      "CompanyDB", // database
      boost::mysql::handshake_params::default_collation,
      boost::mysql::ssl_mode::enable
      );

  std::tie(ec) = co_await conn.async_connect(*endpoints.begin(), params, diag,
                                             tuple_awaitable);
  throw_on_error(ec, diag);

  co_await F(conn);

  std::cerr << "!!!DATABASE CLOSED!!!" << std::endl;

  co_await conn.async_close(default_token{});
}

auto print_employee(boost::mysql::row_view employee) -> void {
  std::cout << "Employee '" << employee.at(0) << " " // first_name (string)
      << employee.at(1) << "' earns "                // last_name  (string)
      << employee.at(2) << " dollars yearly\n";      // salary     (double)
}

auto db_actions(mysql_connection &conn) -> awaitable<void> {
  std::error_code ec;
  boost::mysql::statement stmt;
  boost::mysql::diagnostics diag;

  std::tie(ec, stmt) = co_await conn.
      async_prepare_statement(
          "SELECT first_name, last_name, salary FROM employees",
          diag,
          tuple_awaitable
          );
  boost::mysql::throw_on_error(ec, diag);

  boost::mysql::results result;
  std::tie(ec) = co_await conn.async_execute(stmt.bind(), result, diag,
                                             tuple_awaitable);
  boost::mysql::throw_on_error(ec, diag);

  // Print all employees
  for (boost::mysql::row_view employee : result.rows()) {
    print_employee(employee);
  }
}

auto main_impl(boost::asio::io_context &ctx,
               boost::asio::ssl::context &ssl_ctx) -> awaitable<void> {
  co_await withDbConnected(ctx, ssl_ctx, [](mysql_connection &conn) {
    return db_actions(conn);
  });
}

auto main() -> int {

  // The execution context, required to run I/O operations.
  boost::asio::io_context ctx;

  // The SSL context, required to establish TLS connections.
  // The default SSL options are good enough for us at this point.
  boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tls_client);

  auto MainImpl = [&ctx, &ssl_ctx] {
    return main_impl(ctx, ssl_ctx);
  };

  auto ErrorHandler = [](std::exception_ptr Ptr) {
    if (Ptr) {
      std::rethrow_exception(Ptr);
    }
  };

  boost::asio::co_spawn(ctx.get_executor(), MainImpl, ErrorHandler);

  try {
    ctx.run();
  } catch (const boost::mysql::error_with_diagnostics &Err) {
    std::cerr << "Error: " << Err.what() << '\n'
        << "Server diagnostics: "
        << Err.get_diagnostics().server_message() << std::endl;
    return 1;
  } catch (std::exception const &E) {
    std::cout << "Exception from main(): " << E.what() << std::endl;
  }
}
