// wux.h's entry points for applications: WuxInit (the start-up every flxp program does by hand: the fonts, the session's
// visual style, the shim) and WuxLoadResources (an embedded .res as a module).
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include "w32.h"
#include "user.h"
#include "shell/runtime.h"

extern "C" {

void WINAPI WuxInit(void)
{
    static bool done;
    if (done) return;
    done = true;
    shell::setup_fonts();
    shell::setup_theme();
    w32::init();
}

HINSTANCE WINAPI WuxLoadResources(const void* data, size_t size)
{
    if (!data || !size) return nullptr;
    return (HINSTANCE)res::load_memory((const uint8_t*)data, size);
}

}
