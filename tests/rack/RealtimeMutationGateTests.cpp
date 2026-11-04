#include "rack/RealtimeMutationGate.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using vstengine::rack::RealtimeMutationGate;
int main() {
  int checks = 0;
  const auto check = [&](bool value) {
    ++checks;
    if (!value)
      throw std::runtime_error("mutation gate invariant failed");
  };
  RealtimeMutationGate gate;
  {
    RealtimeMutationGate::ControllerScope outer(gate);
    {
      RealtimeMutationGate::ControllerScope inner(gate);
      RealtimeMutationGate::AudioScope audio(gate);
      check(!audio);
    }
    RealtimeMutationGate::AudioScope audio(gate);
    check(!audio);
  }
  {
    RealtimeMutationGate::AudioScope audio(gate);
    check(static_cast<bool>(audio));
    check(gate.takeRecovery());
    check(!gate.takeRecovery());
  }
  try {
    RealtimeMutationGate::ControllerScope control(gate);
    throw 1;
  } catch (int) {
  }
  {
    RealtimeMutationGate::AudioScope audio(gate);
    check(static_cast<bool>(audio));
  }
  std::atomic<bool> entered{}, attempting{};
  std::thread controller;
  {
    RealtimeMutationGate::AudioScope audio(gate);
    check(static_cast<bool>(audio));
    controller = std::thread([&] {
      attempting = true;
      RealtimeMutationGate::ControllerScope control(gate);
      entered = true;
    });
    while (!attempting.load())
      std::this_thread::yield();
    for (int i = 0; i < 1000; ++i)
      std::this_thread::yield();
    check(!entered.load());
  }
  controller.join();
  check(entered.load());
  for (int i = 0; i < 10000; ++i) {
    RealtimeMutationGate::AudioScope audio(gate);
    check(static_cast<bool>(audio));
  }
  std::cout << "Realtime mutation gate tests passed (" << checks << ")\n";
}
