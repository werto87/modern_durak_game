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
  boost::hana::for_each (shared_class::sharedClasses, [&] (const auto &x) { result.insert (confu_json::type_name<typename std::decay<decltype (x)>::type> ()); });
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
          user->sendMessageToUser (objectToStringWithObjectName (shared_class::UnhandledMessageError{ msg, "Message type is not handled by server api" }));
        }
      else if (typeToSearch == "StartGame")
        {
          user->sendMessageToUser (objectToStringWithObjectName (shared_class::GameStarted{}));
        }
    }
}
