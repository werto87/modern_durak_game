#include "server.hxx"
#include "boost/asio/experimental/awaitable_operators.hpp"
#include "src/game/logic/gameEvent.hxx"
#include "src/serialization.hxx"
#include "src/util.hxx"
#include <boost/algorithm/string/predicate.hpp>
#include <iostream>
#include <range/v3/algorithm/find_if.hpp>

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
          co_spawn (executor, myWebsocket->readLoop ([myWebsocket, &games = games, &gamesToCreate = gamesToCreate, executor, accountName = std::optional<std::string>{}] (const std::string &msg) mutable {
#ifdef LOG_MSG_READ
            std::cout << "listenerUserToGameViaMatchmaking: " << msg << std::endl;
#endif
            std::vector<std::string> splitMesssage{};
            boost::algorithm::split (splitMesssage, msg, boost::is_any_of ("|"));
            if (splitMesssage.size () == 2)
              {
                auto const &typeToSearch = splitMesssage.at (0);
                auto const &objectAsString = splitMesssage.at (1);
                if (typeToSearch == "ConnectToGame")
                  {
                    auto connectToGame = stringToObject<matchmaking_game::ConnectToGame> (objectAsString);
                    if (auto gameToCreate = ranges::find (gamesToCreate, connectToGame.gameName, [] (GameToCreate const &gameToCreate) { return gameToCreate.gameName; }); gameToCreate != gamesToCreate.end ())
                      {
                        if (auto connectToGameError = gameToCreate->tryToAddUser ({ connectToGame.accountName, [myWebsocket] (std::string const &msg) { myWebsocket->sendMessage (msg); }, std::make_shared<boost::asio::system_timer> (executor) }))
                          {
                            myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::ConnectToGameError{ connectToGameError.value () }));
                          }
                        else
                          {
                            accountName = connectToGame.accountName;
                            myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::ConnectToGameSuccess{}));
                          }
                        if (gameToCreate->allUsersConnected ())
                          {
                            games.push_back (Game{ gameToCreate->startGame, gameToCreate->gameName, std::move (gameToCreate->users) });
                            gamesToCreate.erase (gameToCreate);
                          }
                      }
                    else
                      {
                        myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::ConnectToGameError{ "Could not find a game with game name: '" + connectToGame.gameName + "'" }));
                      }
                  }
                else if (typeToSearch == "DurakAttack")
                  {
                    // auto durakAttackObject = stringToObject<shared_class::DurakAttack> (objectAsString);
                    // if (auto gameMachine = ranges::find_if (gameMachines, [accountName = user->accountName.value ()] (GameMachine const &_game) { return ranges::find_if (_game.getGameUsers (), [&accountName] (auto const &gameUser) { return gameUser._user->accountName.value () == accountName; }) != _game.getGameUsers ().end (); }); gameMachine != gameMachines.end ())
                    //   {
                    //     gameMachine->durakStateMachine.process_event (attack{ .playerName = user->accountName.value (), .cards{ std::move (durakAttackObject.cards) } });
                    //   }
                    // else
                    //   {
                    //     user->sendMessageToUser (objectToStringWithObjectName (shared_class::DurakAttackError{ "Could not find a game for Account Name: " + user->accountName.value () }));
                    //   }
                  }
                else if (typeToSearch == "DurakDefend")
                  {
                    // auto durakDefendObject = stringToObject<shared_class::DurakDefend> (objectAsString);
                    // if (auto gameMachine = ranges::find_if (gameMachines, [accountName = user->accountName.value ()] (GameMachine const &_game) { return ranges::find_if (_game.getGameUsers (), [&accountName] (auto const &gameUser) { return gameUser._user->accountName.value () == accountName; }) != _game.getGameUsers ().end (); }); gameMachine != gameMachines.end ())
                    //   {
                    //     gameMachine->durakStateMachine.process_event (defend{ .playerName = user->accountName.value (), .cardToBeat{ durakDefendObject.cardToBeat }, .card{ durakDefendObject.card } });
                    //   }
                    // else
                    //   {
                    //     user->sendMessageToUser (objectToStringWithObjectName (shared_class::DurakDefendError{ "Could not find a game for Account Name: " + user->accountName.value () }));
                    //   }
                  }
                else if (typeToSearch == "DurakDefendPass")
                  {
                    // if (auto gameMachine = ranges::find_if (gameMachines, [accountName = user->accountName.value ()] (GameMachine const &_game) { return ranges::find_if (_game.getGameUsers (), [&accountName] (auto const &gameUser) { return gameUser._user->accountName.value () == accountName; }) != _game.getGameUsers ().end (); }); gameMachine != gameMachines.end ())
                    //   {
                    //     gameMachine->durakStateMachine.process_event (defendPass{ .playerName = user->accountName.value () });
                    //   }
                    // else
                    //   {
                    //     user->sendMessageToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError{ "Could not find a game for Account Name: " + user->accountName.value () }));
                    //   }
                  }
                else if (typeToSearch == "DurakAttackPass")
                  {
                    // if (auto gameMachine = ranges::find_if (gameMachines, [accountName = user->accountName.value ()] (GameMachine const &_game) { return ranges::find_if (_game.getGameUsers (), [&accountName] (auto const &gameUser) { return gameUser._user->accountName.value () == accountName; }) != _game.getGameUsers ().end (); }); gameMachine != gameMachines.end ())
                    // {
                    // gameMachine->durakStateMachine.process_event (attackPass{ .playerName = user->accountName.value () });
                    // }
                    // else
                    // {
                    // user->sendMessageToUser (objectToStringWithObjectName (shared_class::DurakAttackPassError{ "Could not find a game for Account Name: " + user->accountName.value () }));
                    // }
                  }
                else if (typeToSearch == "DurakAssistPass")
                  {
                    // if (auto gameMachine = ranges::find_if (gameMachines, [accountName = user->accountName.value ()] (GameMachine const &_game) { return ranges::find_if (_game.getGameUsers (), [&accountName] (auto const &gameUser) { return gameUser._user->accountName.value () == accountName; }) != _game.getGameUsers ().end (); }); gameMachine != gameMachines.end ())
                    // {
                    // gameMachine->durakStateMachine.process_event (assistPass{ .playerName = user->accountName.value () });
                    // }
                    // else
                    // {
                    // user->sendMessageToUser (objectToStringWithObjectName (shared_class::DurakAssistPassError{ "Could not find a game for Account Name: " + user->accountName.value () }));
                    // }
                  }
                else if (typeToSearch == "DurakAskDefendWantToTakeCardsAnswer")
                  {
                    // auto durakAskDefendWantToTakeCardsAnswerObject = stringToObject<shared_class::DurakAskDefendWantToTakeCardsAnswer> (objectAsString);
                    // if (auto gameMachine = ranges::find_if (gameMachines, [accountName = user->accountName.value ()] (GameMachine const &_game) { return ranges::find_if (_game.getGameUsers (), [&accountName] (auto const &gameUser) { return gameUser._user->accountName.value () == accountName; }) != _game.getGameUsers ().end (); }); gameMachine != gameMachines.end ())
                    // {
                    // if (durakAskDefendWantToTakeCardsAnswerObject.answer)
                    // {
                    // gameMachine->durakStateMachine.process_event (defendAnswerYes{ .playerName = user->accountName.value () });
                    // }
                    // else
                    // {
                    // gameMachine->durakStateMachine.process_event (defendAnswerNo{ .playerName = user->accountName.value () });
                    // }
                    // }
                    // else
                    // {
                    // user->sendMessageToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerError{ "Could not find a game for Account Name: " + user->accountName.value () }));
                    // }
                  }
                else if (typeToSearch == "DurakLeaveGame")
                  {
                    if (accountName)
                      {
                        if (auto game = ranges::find_if (games, [&accountName] (Game const &game) { return game.isUserInGame (accountName.value ()); }); game != games.end ())
                          {
                            game->processEvent (objectToStringWithObjectName (leaveGame{ accountName.value () }));
                          }
                        else
                          {
                            myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakLeaveGameError{ "Could not find a game for Account Name: " + accountName.value () }));
                          }
                      }
                    else
                      {
                        myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakLeaveGameError{ "Account name not set" }));
                      }
                    // if (auto gameMachine = ranges::find_if (gameMachines, [accountName = user->accountName.value ()] (GameMachine const &_game) { return ranges::find_if (_game.getGameUsers (), [&accountName] (auto const &gameUser) { return gameUser._user->accountName.value () == accountName; }) != _game.getGameUsers ().end (); }); gameMachine != gameMachines.end ())
                    //   {
                    //     gameMachine->durakStateMachine.process_event (leaveGame{ user->accountName.value () });
                    //     gameMachines.erase (gameMachine);
                    //   }
                    // else
                    //   {
                    //     user->sendMessageToUser (objectToStringWithObjectName (shared_class::DurakLeaveGameError{ "Could not find a game for Account Name: " + user->accountName.value () }));
                    //   }
                  }
              }
          }) && myWebsocket->writeLoop (),
                    printException);
        }
      catch (std::exception &e)
        {
          std::cout << "Server::listenerUserToGameViaMatchmaking () connect  Exception : " << e.what () << std::endl;
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
          co_spawn (executor, myWebsocket->readLoop ([myWebsocket, &gamesToCreate = gamesToCreate] (const std::string &msg) {
#ifdef LOG_MSG_READ
            std::cout << "listenerMatchmakingToGame: " << msg << std::endl;
#endif
            std::vector<std::string> splitMesssage{};
            boost::algorithm::split (splitMesssage, msg, boost::is_any_of ("|"));
            if (splitMesssage.size () == 2)
              {
                auto const &typeToSearch = splitMesssage.at (0);
                auto const &objectAsString = splitMesssage.at (1);
                if (typeToSearch == "StartGame")
                  {
                    // TODO this should be create game success and not start game success because game is not started it is waiting for user
                    auto &gameToCreate = gamesToCreate.emplace_back (stringToObject<matchmaking_game::StartGame> (objectAsString));
                    myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::StartGameSuccess{ gameToCreate.gameName }));
                  }
                else
                  std::cout << "not supported event msg '" << msg << "'" << std::endl;
              }
            else
              {
                std::cout << "Not supported event. event syntax: EventName|JsonObject. Not handled event: '" << msg << "'" << std::endl;
              }
          }) && myWebsocket->writeLoop (),
                    printException);
        }
      catch (std::exception &e)
        {
          std::cout << "Server::listenerMatchmakingToGame () connect  Exception : " << e.what () << std::endl;
        }
    }
}
