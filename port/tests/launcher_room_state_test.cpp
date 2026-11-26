#include "../app/launcher_room_state.h"
#include <cassert>
#include <iostream>
using namespace launcher::room;
int main() {
  State host; host.variant=3; host.arena="arena1234567"; host.name="Host's room";
  const json roster = json::array({{{"id","host"},{"name","Host"}},{{"id","guest"},{"name","Guest"}}});
  assert(host.receive({{"t","welcome"},{"me","host"},{"host","host"},{"players",roster},{"revision",1}}));
  host.ready(true);
  assert(!host.can_start());
  host.receive({{"t","msg"},{"from","intruder"},{"d",{{"kind","mu-launcher-ready"},{"ready",true}}}});
  assert(!host.can_start());
  host.receive({{"t","msg"},{"from","guest"},{"d",{{"kind","mu-launcher-ready"},{"ready",true}}}});
  assert(host.can_start());
  assert(host.start());
  assert(!host.start());
  assert(!host.started);
  host.host_ready();
  assert(host.started);
  State guest;
  guest.receive({{"t","welcome"},{"me","guest"},{"host","host"},{"players",roster}});
  const json configuration = host.out.back()["d"];
  assert(!guest.receive({{"t","msg"},{"from","intruder"},{"d",configuration}}));
  assert(!guest.launch_due);
  assert(guest.receive({{"t","msg"},{"from","host"},{"d",configuration}}));
  assert(guest.launch_due && guest.variant==3);
  guest.launch_due=false;
  guest.receive({{"t","msg"},{"from","host"},{"d",configuration}});
  assert(!guest.launch_due);
  // Late joiners receive the same arena, without restarting the original pair.
  host.receive({{"t","lobby"},{"host","host"},{"players",json::array({
    {{"id","host"}},{{"id","guest"}},{{"id","late"}}})},{"revision",2}});
  host.receive({{"t","msg"},{"from","late"},{"d",{{"kind","mu-launcher-hello"}}}});
  assert(host.out.back()["to"]=="late");
  assert(host.out.back()["d"]["arena"]==host.arena);
  assert(host.out.back()["d"]["started"]==true);
  guest.receive({{"t","lobby"},{"host","guest"},{"players",json::array({{{"id","guest"}}})}});
  assert(!guest.connected && !guest.error.empty());
  std::cout << "Launcher room readiness, host authority, single start, late join and host departure passed\n";
}
