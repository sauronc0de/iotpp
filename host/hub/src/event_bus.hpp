#pragma once

// Fan-out of live updates to Server-Sent Events clients. Each subscriber has
// a bounded queue; slow clients drop their oldest frames instead of stalling.

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace hub
{
class EventBus
{
public:
  struct Subscriber
  {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> frames;
    bool closed = false;
  };

  std::shared_ptr<Subscriber> subscribe()
  {
    auto subscriber = std::make_shared<Subscriber>();
    std::lock_guard lock(mutex_);
    subscribers_.insert(subscriber);
    return subscriber;
  }

  void unsubscribe(const std::shared_ptr<Subscriber> &subscriber)
  {
    std::lock_guard lock(mutex_);
    subscribers_.erase(subscriber);
  }

  void publish(const std::string &type, const nlohmann::ordered_json &data)
  {
    const auto frame = "event: " + type + "\ndata: " + data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n\n";
    std::lock_guard lock(mutex_);
    for(const auto &subscriber : subscribers_)
    {
      std::lock_guard guard(subscriber->mutex);
      if(subscriber->frames.size() >= MaxQueued)
        subscriber->frames.pop_front();
      subscriber->frames.push_back(frame);
      subscriber->ready.notify_one();
    }
  }

  // Next frame, or an empty string after `wait` (caller sends a keep-alive).
  static bool next(Subscriber &subscriber, std::string &frame, std::chrono::milliseconds wait)
  {
    std::unique_lock lock(subscriber.mutex);
    subscriber.ready.wait_for(lock, wait, [&] { return !subscriber.frames.empty() || subscriber.closed; });
    if(subscriber.closed)
      return false;
    frame.clear();
    while(!subscriber.frames.empty() && frame.size() < 256 * 1024)
    {
      frame += subscriber.frames.front();
      subscriber.frames.pop_front();
    }
    return true;
  }

  void close_all()
  {
    std::lock_guard lock(mutex_);
    for(const auto &subscriber : subscribers_)
    {
      std::lock_guard guard(subscriber->mutex);
      subscriber->closed = true;
      subscriber->ready.notify_all();
    }
  }

  std::size_t clients()
  {
    std::lock_guard lock(mutex_);
    return subscribers_.size();
  }

private:
  static constexpr std::size_t MaxQueued = 2000;
  std::mutex mutex_;
  std::set<std::shared_ptr<Subscriber>> subscribers_;
};
} // namespace hub
