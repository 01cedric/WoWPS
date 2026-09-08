#pragma once
// PS4 shim for '#include <SDL.h>' (the SDL2 include-directory spelling, used
// by src/addons/lua_engine.cpp): there is no SDL on the console.
#include "sdl_scancode_compat.h"
