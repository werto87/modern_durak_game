#include "server.hxx"
#include "boost/asio/experimental/awaitable_operators.hpp"
#include "example_of_a_game_server/serialization.hxx"
#include "example_of_a_game_server/util.hxx"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <range/v3/algorithm/find_if.hpp>

using namespace boost::beast;
using namespace boost::asio;
using boost::asio::ip::tcp;
using tcp_acceptor = use_awaitable_t<>::as_default_on_t<tcp::acceptor>;

template <typename T> concept hasAccountName = requires (T t) { t.accountName; };

// work around to print type for debuging
// template <typename> struct Debug;
// Debug<SomeType>{};

void
playSuggestedMove (shared_class::DurakNextMoveSuccess const &durakNextMoveSuccess, Game &game, std::string const &playerName)
{
  switch (durakNextMoveSuccess.nextMove)
    {
    case shared_class::Move::AttackAssistPass:
    case shared_class::Move::AttackAssistDoneAddingCards:
      {
        if (game.durakGame ().getRoleForName (playerName) == durak::PlayerRole::attack)
          {
            game.processEvent (objectToStringWithObjectName (shared_class::DurakAttackPass{}), playerName);
          }
        else if (game.durakGame ().getRoleForName (playerName) == durak::PlayerRole::assistAttacker)
          {
            game.processEvent (objectToStringWithObjectName (shared_class::DurakAssistPass{}), playerName);
          }
        else
          {
            throw std::logic_error{ "Next move type pass but player is not attack or assist" };
          }
        break;
      }
    case shared_class::Move::AddCards:
      {
        game.processEvent (objectToStringWithObjectName (shared_class::DurakAttack{ std::vector<durak::Card>{ *durakNextMoveSuccess.card } }), playerName);
        break;
      }
    case shared_class::Move::Defend:
      {
        game.processEvent (objectToStringWithObjectName (shared_class::DurakDefend{ { game.durakGame ().getTable ().back ().first }, *durakNextMoveSuccess.card }), playerName);
        break;
      }
    case shared_class::Move::TakeCards:
      {
        game.processEvent (objectToStringWithObjectName (shared_class::DurakDefendPass{}), playerName);
        break;
      }
    case shared_class::Move::AnswerDefendWantsToTakeCardsYes:
      {
        game.processEvent (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswer{ true }), playerName);
        break;
      }
    case shared_class::Move::AnswerDefendWantsToTakeCardsNo:
      {
        game.processEvent (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswer{ false }), playerName);
        break;
      }
    }
}

awaitable<void>
Server::listenerUserToGameViaMatchmaking (boost::asio::ip::tcp::endpoint userToGameViaMatchmakingEndpoint, boost::asio::io_context &ioContext, std::string matchmakingHost, std::string matchmakingPort, std::filesystem::path databasePath)
{
  auto executor = co_await this_coro::executor;
  tcp_acceptor acceptor (ioContext, userToGameViaMatchmakingEndpoint);
  for (;;)
    {
      try
        {
          auto socket = co_await acceptor.async_accept ();
          auto connection = std::make_shared<Websocket> (Websocket{ std::move (socket) });
          connection->set_option (websocket::stream_base::timeout::suggested (role_type::server));
          connection->set_option (websocket::stream_base::decorator ([] (websocket::response_type &res) { res.set (http::field::server, std::string (BOOST_BEAST_VERSION_STRING) + " websocket-server-async"); }));
          co_await connection->async_accept ();
          static size_t id = 0;
          auto myWebsocket = std::make_shared<MyWebsocket<Websocket> > (MyWebsocket<Websocket>{ connection, "UserToGameViaMatchmaking", fmt::fg (fmt::color::red), std::to_string (id++) });
          auto accountName = std::make_shared<std::optional<std::string> > ();
          using namespace boost::asio::experimental::awaitable_operators;
          tcp::resolver resolv{ ioContext };
          auto resolvedGameToMatchmakingEndpoint = co_await resolv.async_resolve (ip::tcp::v4 (), matchmakingHost, matchmakingPort, use_awaitable);
          co_spawn (executor, myWebsocket->readLoop ([myWebsocket, &games = games, &gamesToCreate = gamesToCreate, executor, accountName, &ioContext, gameToMatchmakingEndpoint = resolvedGameToMatchmakingEndpoint->endpoint (), databasePath] (const std::string &msg) mutable {
            std::vector<std::string> splitMessage{};
            boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
            if (splitMessage.size () == 2)
              {
                auto const &typeToSearch = splitMessage.at (0);
                auto const &objectAsString = splitMessage.at (1);
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
                            *accountName = connectToGame.accountName;
                            myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::ConnectToGameSuccess{}));
                          }
                        if (gameToCreate->allUsersConnected ())
                          {
                            auto computerControlledPlayerNames = std::vector<std::string> (gameToCreate->startGame.gameOption.computerControlledPlayerCount);
                            ranges::generate (computerControlledPlayerNames, [] () { return boost::uuids::to_string (boost::uuids::random_generator () ()); });
                            ranges::for_each (computerControlledPlayerNames, [gameName = gameToCreate->gameName, &games = games, &gameToCreate, &executor] (auto const &id) {
                              gameToCreate->users.push_back ({ id,
                                                               [id, gameName, &games = games] (auto const &msg) {
#ifdef LOG_COMPUTER_CONTROLLED_OPPONENT_MASSAGE_RECIVED
                                                                 std::cout << "msg to computer controlled opponent: " << msg << std::endl;
#endif
                                                                 std::vector<std::string> splitMessage{};
                                                                 boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
                                                                 auto result = std::optional<std::string>{};
                                                                 if (splitMessage.size () == 2)
                                                                   {
                                                                     auto const &typeToSearch = splitMessage.at (0);
                                                                     auto const &objectAsString = splitMessage.at (1);
                                                                     if (typeToSearch == confu_json::type_name<durak::GameData> ())
                                                                       {
                                                                         if (auto gameWithPlayer = ranges::find (games, gameName, &Game::gameName); gameWithPlayer != games.end ())
                                                                           {
                                                                             gameWithPlayer->processEvent (objectToStringWithObjectName (shared_class::DurakNextMove{}), id);
                                                                           }
                                                                       }
                                                                     else if (typeToSearch == confu_json::type_name<shared_class::DurakNextMoveSuccess> ())
                                                                       {
                                                                         if (auto gameWithPlayer = ranges::find (games, gameName, &Game::gameName); gameWithPlayer != games.end ())
                                                                           {
                                                                             playSuggestedMove (stringToObject<shared_class::DurakNextMoveSuccess> (objectAsString), *gameWithPlayer, id);
                                                                           }
                                                                       }
                                                                     else if (typeToSearch == confu_json::type_name<shared_class::DurakAskDefendWantToTakeCards> ())
                                                                       {
                                                                         if (auto gameWithPlayer = ranges::find (games, gameName, &Game::gameName); gameWithPlayer != games.end ())
                                                                           {
                                                                             auto nextSuggestedMove = shared_class::DurakNextMoveSuccess{};
                                                                             // TODO right now defend discards cards all the time when asked. There could be a constellation where this is not a good idea
                                                                             // TODO Find a game where this is the case
                                                                             // TODO ignoring blunder ofcourse
                                                                             nextSuggestedMove.nextMove = shared_class::Move::AnswerDefendWantsToTakeCardsNo;
                                                                             playSuggestedMove (nextSuggestedMove, *gameWithPlayer, id);
                                                                           }
                                                                       }
                                                                   }
                                                               },
                                                               std::make_shared<boost::asio::system_timer> (executor) });
                            });
                            auto &game = games.emplace_back (Game{ gameToCreate->startGame, gameToCreate->gameName, std::move (gameToCreate->users), ioContext, gameToMatchmakingEndpoint, databasePath });
                            game.startGame ();
                            gamesToCreate.erase (gameToCreate);
                          }
                      }
                    else
                      {
                        myWebsocket->sendMessage (objectToStringWithObjectName (matchmaking_game::ConnectToGameError{ "Could not find a game with game name: '" + connectToGame.gameName + "'" }));
                      }
                  }
                else if (accountName && accountName->has_value ())
                  {
                    if (auto game = ranges::find_if (games, [&accountName] (Game const &game) { return game.isUserInGame (accountName->value ()); }); game != games.end ())
                      {
                        if (auto const &error = game->processEvent (msg, accountName->value ()))
                          {
                            myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::UnhandledEventError{ msg, error.value () }));
                          }
                      }
                  }
                else if (accountName && not accountName->has_value ())
                  {
                    bool typeFound = false;
                    boost::hana::for_each (shared_class::gameTypes, [&] (const auto &x) {
                      if (typeToSearch + "Error" == confu_json::type_name<typename std::decay<decltype (x)>::type> ())
                        {
                          typeFound = true;
                          if constexpr (requires { x.error; })
                            {
                              auto errorToSend = std::decay_t<decltype (x)>{};
                              errorToSend.error = "Account name not set please call: ConnectToGame with account name and game name";
                              myWebsocket->sendMessage (objectToStringWithObjectName (errorToSend));
                            }
                          return;
                        }
                    });
                    if (not typeFound) std::cout << "could not find a match for typeToSearch in shared_class::gameTypes or matchmaking_game::gameMessages '" << typeToSearch << "'" << std::endl;
                  }
              }
          }) && myWebsocket->writeLoop (),
                    [&games = games, accountName] (auto eptr) {
                      printException (eptr);
                      if (accountName && accountName->has_value ())
                        {
                          if (auto gameItr = ranges::find_if (games, [accountName] (Game &game) { return accountName->has_value () && game.isUserInGame (accountName->value ()); }); gameItr != games.end ())
                            {
                              gameItr->removeUser (accountName->value ());
                              if (gameItr->usersInGame () == 0)
                                {
                                  games.erase (gameItr);
                                }
                            }
                          else
                            {
                              std::cout << "remove user from game error can not find a game with this user:" << accountName->value () << std::endl;
                            }
                        }
                      else
                        {
                          std::cout << "remove user from game error name of user not known" << std::endl;
                        }
                    });
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
          static size_t id = 0;
          auto myWebsocket = std::make_shared<MyWebsocket<Websocket> > (MyWebsocket<Websocket>{ connection, "MatchmakingToGame", fmt::fg (fmt::color::blue_violet), std::to_string (id++) });
          using namespace boost::asio::experimental::awaitable_operators;
          co_spawn (executor, myWebsocket->readLoop ([myWebsocket, &gamesToCreate = gamesToCreate] (const std::string &msg) {
            std::vector<std::string> splitMessage{};
            boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
            if (splitMessage.size () == 2)
              {
                auto const &typeToSearch = splitMessage.at (0);
                auto const &objectAsString = splitMessage.at (1);
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
                    [myWebsocket] (auto eptr) { printException (eptr); });
        }
      catch (std::exception &e)
        {
          std::cout << "Server::listenerMatchmakingToGame () connect  Exception : " << e.what () << std::endl;
        }
    }
}
