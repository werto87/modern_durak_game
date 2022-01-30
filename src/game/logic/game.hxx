#ifndef EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0
#define EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0

#include <deque>
#include <list>
#include <memory>
namespace boost::asio
{
class io_context;
}

class Game
{
  struct StateMachineWrapper;
  struct StateMachineWrapperDeleter
  {
    void operator() (StateMachineWrapper *p);
  };

public:
  Game ();

  void process_event (std::string const &event);

  std::unique_ptr<StateMachineWrapper, StateMachineWrapperDeleter> sm; // only use this member inside of ".cxx". reason because of incomplete type
};

#endif /* EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0 */
