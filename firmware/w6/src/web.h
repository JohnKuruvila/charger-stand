// The HTTP server on port 80: the setup page (only over the setup network) and the token-protected API. See
// README.md for the API.
#pragma once
#include <stdint.h>

namespace web {

void begin();
void loop();

}  // namespace web
