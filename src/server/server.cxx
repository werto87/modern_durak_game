#include "server.hxx"
#include "boost/asio/experimental/awaitable_operators.hpp"
#include "src/util.hxx"
#include <boost/algorithm/string/predicate.hpp>
#include <iostream>

using namespace boost::beast;
using namespace boost::asio;
using boost::asio::ip::tcp;
using tcp_acceptor = use_awaitable_t<>::as_default_on_t<tcp::acceptor>;

awaitable<void>
Server::listenerUserToGameViaMatchmaking (boost::asio::ip::tcp::endpoint const &endpoint)
{
  auto executor = co_await this_coro::executor;
  tcp_acceptor acceptor (executor, endpoint);
  for (;;)
    {
      try
        {
          auto socket = co_await acceptor.async_accept ();
          auto connection = std::make_shared<Websocket> (Websocket{ std::move (socket) });
          connection->set_option (websocket::stream_base::timeout::suggested (role_type::server));
          connection->set_option (websocket::stream_base::decorator ([] (websocket::response_type &res) { res.set (http::field::server, std::string (BOOST_BEAST_VERSION_STRING) + " websocket-server-async"); }));
          co_await connection->async_accept ();
          auto myWebsocket = std::make_shared<MyWebsocket<Websocket> > (MyWebsocket<Websocket>{ connection });
          using namespace boost::asio::experimental::awaitable_operators;
          co_spawn (executor, myWebsocket->readLoop ([myWebsocket] (const std::string &msg) {
            //
            // TODO connect user and add him to game
            if (boost::starts_with (msg, "StartGame"))
              {
                // games.emplace_back ();
                // myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::StartGameSuccess{}));
              }
          }) && myWebsocket->writeLoop (),
                    [] (auto eptr) {
                      // TODO remove user from list
                    });
        }
      catch (std::exception &e)
        {
          std::cout << "Server::listener () connect  Exception : " << e.what () << std::endl;
        }
    }
}

boost::asio::awaitable<void>
Server::listenerMatchmakingToGame (boost::asio::ip::tcp::endpoint const &endpoint)
{
  auto executor = co_await this_coro::executor;
  tcp_acceptor acceptor (executor, endpoint);
  for (;;)
    {
      try
        {
          auto socket = co_await acceptor.async_accept ();
          auto connection = std::make_shared<Websocket> (Websocket{ std::move (socket) });
          connection->set_option (websocket::stream_base::timeout::suggested (role_type::server));
          connection->set_option (websocket::stream_base::decorator ([] (websocket::response_type &res) { res.set (http::field::server, std::string (BOOST_BEAST_VERSION_STRING) + " websocket-server-async"); }));
          co_await connection->async_accept ();
          auto myWebsocket = std::make_shared<MyWebsocket<Websocket> > (MyWebsocket<Websocket>{ connection });
          using namespace boost::asio::experimental::awaitable_operators;
          co_spawn (executor, myWebsocket->readLoop ([myWebsocket, &games = games] (const std::string &msg) {
            if (boost::starts_with (msg, "StartGame"))
              {
                games.emplace_back ();
                myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::StartGameSuccess{}));
              }
          }) && myWebsocket->writeLoop (),
                    detached);
        }
      catch (std::exception &e)
        {
          std::cout << "Server::listener () connect  Exception : " << e.what () << std::endl;
        }
    }
}
