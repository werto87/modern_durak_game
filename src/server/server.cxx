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
          co_spawn (executor, myWebsocket->readLoop ([myWebsocket, &games = games] (const std::string &msg) {
            std::cout << "listenerUserToGameViaMatchmaking: " << msg << std::endl;
            std::vector<std::string> splitMesssage{};
            boost::algorithm::split (splitMesssage, msg, boost::is_any_of ("|"));
            if (splitMesssage.size () == 2)
              {
                auto const &typeToSearch = splitMesssage.at (0);
                auto const &objectAsString = splitMesssage.at (1);
                if (typeToSearch == "StartGame")
                  {
                    auto &game = games.emplace_back (stringToObject<matchmaking_game::StartGame> (objectAsString));
                    myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::StartGameSuccess{ game.gameName () }));
                  }
              }
            games.front ().processEvent (msg);
          }) && myWebsocket->writeLoop (),
                    printException);
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
            std::cout << "listenerMatchmakingToGame: " << msg << std::endl;
            std::vector<std::string> splitMesssage{};
            boost::algorithm::split (splitMesssage, msg, boost::is_any_of ("|"));
            if (splitMesssage.size () == 2)
              {
                auto const &typeToSearch = splitMesssage.at (0);
                auto const &objectAsString = splitMesssage.at (1);
                if (typeToSearch == "StartGame")
                  {
                    auto &game = games.emplace_back (stringToObject<matchmaking_game::StartGame> (objectAsString));
                    myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::StartGameSuccess{ game.gameName () }));
                  }
                else
                  std::cout << "not supported event msg '" << msg << "'" << std::endl;
              }
            else
              {
                std::cout << "Not supported event. event syntax: EventName|JsonObject" << std::endl;
              }
          }) && myWebsocket->writeLoop (),
                    printException);
        }
      catch (std::exception &e)
        {
          std::cout << "Server::listener () connect  Exception : " << e.what () << std::endl;
        }
    }
}
