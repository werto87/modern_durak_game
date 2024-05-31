#ifndef FDE41782_20C3_436A_B415_E198F593F0AE
#define FDE41782_20C3_436A_B415_E198F593F0AE

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/optional.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <iostream>
#include <list>
#include <memory>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
typedef boost::asio::use_awaitable_t<>::as_default_on_t<boost::asio::basic_waitable_timer<boost::asio::chrono::system_clock> > CoroTimer;

template <class T> class MyWebsocket
{
public:
  explicit MyWebsocket (std::shared_ptr<T> webSocket_) : webSocket { webSocket_ } {}
  MyWebsocket (std::shared_ptr<T> webSocket_, std::string loggingName_, fmt::text_style loggingTextStyleForName_, std::string id_) : webSocket { webSocket_ }, loggingName { std::move (loggingName_) }, loggingTextStyleForName { std::move (loggingTextStyleForName_) }, id { std::move (id_) } {}
  boost::asio::awaitable<std::string> async_read_one_message ();

  boost::asio::awaitable<void> readLoop (std::function<void (std::string const &readResult)> onRead);

  boost::asio::awaitable<void> async_write_one_message (std::string message);

  boost::asio::awaitable<void> writeLoop ();

  void sendMessage (std::string message);
  boost::asio::awaitable<void> async_close ();

private:
  std::shared_ptr<T> webSocket {};
  std::string loggingName {};
  fmt::text_style loggingTextStyleForName {};
  std::string id {};
  std::deque<std::string> msgQueue {};
  std::shared_ptr<CoroTimer> timer {};
};

inline void
printTagWithPadding (std::string const &tag, fmt::text_style const &style, size_t maxLength)
{
  if (maxLength < 3) throw std::logic_error { "maxLength should be min 3" };
  if (tag.length () > maxLength)
    {
      fmt::print (style, "[{:<" + std::to_string (maxLength) + "}]", std::string { tag.begin (), tag.begin () + boost::numeric_cast<int> (maxLength) - 3 } + "...");
    }
  else
    {
      fmt::print (style, "[{}]{}", tag, std::string (maxLength - tag.size (), '-'));
    }
}

template <class T>
inline boost::asio::awaitable<std::string>
MyWebsocket<T>::async_read_one_message ()
{

  boost::beast::flat_buffer buffer;
  co_await webSocket->async_read (buffer, boost::asio::use_awaitable);
  auto msg = boost::beast::buffers_to_string (buffer.data ());
#ifdef LOG_MY_WEBSOCKET
  printTagWithPadding (loggingName + (loggingName.empty () ? "" : " ") + id, loggingTextStyleForName, 30);
  fmt::print ("[r] {}", msg);
  std::cout << std::endl;
#endif
  co_return msg;
}

template <class T>
inline boost::asio::awaitable<void>
MyWebsocket<T>::readLoop (std::function<void (std::string const &readResult)> onRead)
{
  try
    {
      for (;;)
        {
          auto oneMsg = co_await async_read_one_message ();
          onRead (std::move (oneMsg));
        }
    }
  catch (...)
    {
      webSocket.reset ();
      if (timer) timer->cancel ();
      throw;
    }
}
template <class T>
inline boost::asio::awaitable<void>
MyWebsocket<T>::async_write_one_message (std::string message)
{
#ifdef LOG_MY_WEBSOCKET
  printTagWithPadding (loggingName + (loggingName.empty () ? "" : " ") + id, loggingTextStyleForName, 30);
  fmt::print ("[w] {}", message);
  std::cout << std::endl;
#endif
  co_await webSocket->async_write (boost::asio::buffer (std::move (message)), boost::asio::use_awaitable);
}
template <class T>
inline boost::asio::awaitable<void>
MyWebsocket<T>::writeLoop ()
{
  auto connection = std::weak_ptr<T> { webSocket };
  try
    {
      while (not connection.expired ())
        {
          timer = std::make_shared<CoroTimer> (CoroTimer { co_await boost::asio::this_coro::executor });
          timer->expires_after (std::chrono::system_clock::time_point::max () - std::chrono::system_clock::now ());
          try
            {
              co_await timer->async_wait ();
            }
          catch (boost::system::system_error &e)
            {
              using namespace boost::system::errc;
              if (operation_canceled == e.code ())
                {
                  //  swallow cancel
                }
              else
                {
                  std::cout << "error in timer boost::system::errc: " << e.code () << std::endl;
                  abort ();
                }
            }
          while (not connection.expired () && not msgQueue.empty ())
            {
              auto tmpMsg = std::move (msgQueue.front ());
              msgQueue.pop_front ();
              co_await async_write_one_message (std::move (tmpMsg));
            }
        }
    }
  catch (std::exception const &e)
    {
      webSocket.reset ();
      if (timer) timer->cancel ();
      throw;
    }
}
template <class T>
inline void
MyWebsocket<T>::sendMessage (std::string message)
{
  msgQueue.push_back (std::move (message));
  if (timer) timer->cancel ();
}
template <class T>
inline boost::asio::awaitable<void>
MyWebsocket<T>::async_close ()
{
  co_await webSocket->async_close ("User left game");
}

#endif /* FDE41782_20C3_436A_B415_E198F593F0AE */
