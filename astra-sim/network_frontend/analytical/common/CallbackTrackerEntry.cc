/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "common/CallbackTrackerEntry.hh"
#include <cassert>

using namespace NetworkAnalytical;
using namespace AstraSimAnalytical;

CallbackTrackerEntry::CallbackTrackerEntry()
    : send_event(std::nullopt),
      recv_event(std::nullopt),
      transmission_finished(false) {}

void CallbackTrackerEntry::register_send_callback(
    const Callback callback,
    const CallbackArg arg) {
  assert(!send_event.has_value());

  // register send callback
  const auto event = Event(callback, arg);
  send_event = event;
}

void CallbackTrackerEntry::register_recv_callback(
    const Callback callback,
    const CallbackArg arg) {
  assert(!recv_event.has_value());

  // register recv callback
  const auto event = Event(callback, arg);
  recv_event = event;
}

bool CallbackTrackerEntry::is_transmission_finished() const {
  return transmission_finished;
}

void CallbackTrackerEntry::set_transmission_finished() {
  transmission_finished = true;
}

bool CallbackTrackerEntry::both_callbacks_registered() const {
  // check both callback is registered
  return (send_event.has_value() && recv_event.has_value());
}

void CallbackTrackerEntry::invoke_send_handler() {
  assert(send_event.has_value());

  // invoke send event
  send_event.value().invoke_event();
}

void CallbackTrackerEntry::invoke_recv_handler() {
  assert(recv_event.has_value());

  // invoke recv event
  recv_event.value().invoke_event();
}
