// What winecfg would list: run uxtheme's EnumThemes over a themes directory.
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "w32/w32.h"
#include "w32/user.h"
extern "C" {
#include "uxtheme.h"
typedef BOOL (CALLBACK *EnumThemeProc)(LPVOID, LPCWSTR, LPCWSTR, LPCWSTR, LPVOID, LPVOID);
HRESULT WINAPI EnumThemes(LPCWSTR, EnumThemeProc, LPVOID);
}
static BOOL CALLBACK cb(LPVOID, LPCWSTR file, LPCWSTR name, LPCWSTR tip, LPVOID, LPVOID)
{
    printf("theme: %s\n  name: %s\n  tip:  %s\n", w32::utf8_of(file).c_str(), w32::utf8_of(name).c_str(), w32::utf8_of(tip).c_str());
    return TRUE;
}
int main(int argc, char** argv)
{
    w32::init();
    const char* home = getenv("HOME");
    std::string deflt = std::string(home ? home : "") + "/.wine/drive_c/windows/resources/themes";
    const char* dir = argc > 1 ? argv[1] : deflt.c_str();
    HRESULT hr = EnumThemes((LPCWSTR)w32::utf16_of(dir).c_str(), cb, nullptr);
    printf("EnumThemes: %08x\n", (unsigned)hr);
    return 0;
}
