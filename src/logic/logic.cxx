#include "src/logic/logic.hxx"
#include "src/serialization.hxx"
#include <boost/algorithm/string/split.hpp>
#include <boost/hana/fwd/for_each.hpp>
#include <confu_json/confu_json.hxx>
#include <confu_json/to_json.hxx>
#include <iostream>
#include <set>

std::set<std::string>
getApiTypes ()
{
  auto result = std::set<std::string>{};
  boost::hana::for_each (matchmaking_game::sharedClasses, [&] (const auto &x) { result.insert (confu_json::type_name<typename std::decay<decltype (x)>::type> ()); });
  return result;
}
auto const apiTypes = getApiTypes ();

template <typename TypeToSend>
std::string
objectToStringWithObjectName (TypeToSend const &typeToSend)
{
  std::stringstream ss{};
  ss << confu_json::type_name<TypeToSend> () << '|' << confu_json::to_json (typeToSend);
  return ss.str ();
}

template <typename T>
T
stringToObject (std::string const &objectAsString)
{
  T t{};
  boost::json::error_code ec{};
  auto jsonValue = confu_json::read_json (objectAsString, ec);
  if (ec)
    {
      std::cerr << "error while parsing string: error code: " << ec << std::endl;
      std::cerr << "error while parsing string: stringToParse: " << objectAsString << std::endl;
    }
  else
    {
      t = confu_json::to_object<T> (jsonValue);
    }
  return t;
}

void
startGame (std::string const &msg)
{
}

void
handleMessage (std::string const &msg, std::list<std::shared_ptr<User> > &, std::shared_ptr<User> user)
{
  std::vector<std::string> splitMesssage{};
  boost::algorithm::split (splitMesssage, msg, boost::is_any_of ("|"));
  if (splitMesssage.size () == 2)
    {
      auto const &typeToSearch = splitMesssage.at (0);
      auto const &objectAsString = splitMesssage.at (1);
      if (not apiTypes.contains (typeToSearch))
        {
          user->sendMessageToUser (objectToStringWithObjectName (matchmaking_game::UnhandledMessageError{ msg, "Message type is not handled by server api" }));
        }
      else if (typeToSearch == "StartGame")
        {
          user->sendMessageToUser (objectToStringWithObjectName (matchmaking_game::StartGameSuccess{}));
        }
      else if (typeToSearch == "LeaveGameServer")
        {

          user->sendMessageToUser (objectToStringWithObjectName (matchmaking_game::LeaveGameSuccess{}));
        }
      else
        {
          user->sendMessageToUser ("Message type not supported. Check for. Message should have exactly one |");
        }
    }
  else
    {
      user->sendMessageToUser ("NOT HANDLED MESSAGE. Check for. Message should have exactly one |");
    }
}
