#ifndef EBD66723_6B6F_4460_A3DE_00AEB1E6D6B1
#define EBD66723_6B6F_4460_A3DE_00AEB1E6D6B1
#include <confu_json/confu_json.hxx>
#include <confu_json/util.hxx>
#include <expected>
#include <filesystem>

namespace shared_class
{
struct GameOption;
}

template <typename TypeToSend>
std::string
objectToStringWithObjectName (TypeToSend const &typeToSend)
{
  std::stringstream ss {};
  ss << confu_json::type_name<TypeToSend> () << '|' << confu_json::to_json (typeToSend);
  return ss.str ();
}

template <typename T>
T
stringToObject (std::string const &objectAsString)
{
  T t {};
  boost::system::error_code ec {};
  try
    {
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
    }
  catch (...)
    {
      std::cout << "confu_json::read_json exception. Trying to parse '" << confu_json::type_name<T> () << "'. Trying to transform message: '" << objectAsString << "'" << std::endl;
      std::cout << "example json for '" << confu_json::type_name<T> () << confu_json::type_name<T> () << "': '" << objectToStringWithObjectName (T {}) << "'" << std::endl;
      throw;
    }
  return t;
}

size_t averageRating (std::vector<std::string> const &accountNames);

void createCombinationDatabase (std::filesystem::path const &databasePath);

[[nodiscard]] std::expected<shared_class::GameOption, std::string> toGameOption (std::string const &gameOptionAsString);

#endif /* EBD66723_6B6F_4460_A3DE_00AEB1E6D6B1 */
