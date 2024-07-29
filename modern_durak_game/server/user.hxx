#ifndef F85705C8_6F01_4F50_98CA_5636F5F5E1C1
#define F85705C8_6F01_4F50_98CA_5636F5F5E1C1

#include <boost/asio.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast.hpp>
#include <deque>
#include <my_web_socket/myWebSocket.hxx>

struct User
{
  User (std::string accountName_, std::function<void (std::string const &msg)> sendMsgToUser_, std::shared_ptr<boost::asio::system_timer> timer_) : accountName { std::move (accountName_) }, sendMsgToUser { sendMsgToUser_ }, timer { timer_ } {}

  std::string accountName {};
  std::function<void (std::string const &msg)> sendMsgToUser {};
  std::optional<std::chrono::milliseconds> pausedTime {};
  std::shared_ptr<boost::asio::system_timer> timer;
};

#endif /* F85705C8_6F01_4F50_98CA_5636F5F5E1C1 */
