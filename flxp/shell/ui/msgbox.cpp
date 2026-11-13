// shell::message_box (shell/ui.h): wux's MessageBoxW with the shell layer's UTF-8 strings.
#include "w32/w32.h"
#include "shell/ui.h"

namespace shell {

int message_box(HWND owner, const std::string& title, const std::string& text, unsigned type)
{
    std::u16string t = w32::utf16_of(title.c_str()), m = w32::utf16_of(text.c_str());
    return MessageBoxW(owner, (LPCWSTR)m.c_str(), (LPCWSTR)t.c_str(), type);
}

} // namespace shell
