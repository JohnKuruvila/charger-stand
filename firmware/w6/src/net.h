// Wi-Fi: joining the home network, the setup network (a phone joins it to enter the Wi-Fi password), mDNS (w6.local)
// and the network time.
#pragma once
#include <Arduino.h>
#include <vector>

namespace net {

void begin(bool open_setup);  // open_setup: start in setup even if Wi-Fi is already configured
void loop();
bool online();
bool in_setup();
bool setup_requested();  // in setup because someone asked for it, not because the saved network was unreachable
// retry_saved: the stand could not reach its saved network, so keep retrying it and leave setup once it is back.
// Otherwise setup was asked for, and it waits for a phone; if none joins for 10 minutes the stand restarts.
void start_setup(bool retry_saved = false);
bool from_setup_network(const IPAddress &local);  // true for a request that came in over the setup network

// Setup steps, driven by the setup page (web.cpp)
enum class Join { Idle, Trying, Ok, Failed };
std::vector<String> networks();  // what the scan at the start of setup found, strongest first
void try_join(const String &ssid, const String &pass);
Join join_state();
String join_error();
String joined_ssid(), joined_pass();

}  // namespace net
