#pragma once

#include <cstdint>

namespace door
{
// Opening is immediate; every open sample cancels closure confirmation.
class State
{
public:
  enum class Change { None, Open, Closed };

  explicit State(unsigned required) : required_(required) {}

  Change observe(bool open)
  {
    if(open)
    {
      closedReadings_ = 0;
      const bool changed = !known_ || !open_;
      known_ = true;
      open_ = true;
      return changed ? Change::Open : Change::None;
    }
    if(closedReadings_ < required_)
      ++closedReadings_;
    if(closedReadings_ < required_)
      return Change::None;
    const bool changed = !known_ || open_;
    known_ = true;
    open_ = false;
    return changed ? Change::Closed : Change::None;
  }

  bool closed() const { return known_ && !open_; }

private:
  unsigned required_, closedReadings_ = 0;
  bool known_ = false, open_ = false;
};
} // namespace door
