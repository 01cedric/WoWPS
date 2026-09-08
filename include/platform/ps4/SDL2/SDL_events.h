#pragma once
// PS4 shim for <SDL2/SDL_events.h>: there is no SDL on the console. When the PS4
// include path precedes the system one, upstream '#include <SDL2/SDL_events.h>' lands
// here and gets the compat slice instead.
#include "../sdl_scancode_compat.h"
