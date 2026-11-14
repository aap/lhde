// org.freedesktop.FileManager1 on the session bus (the freedesktop file manager interface): browsers' "Show in Folder" and
// other programs ask it to show items in their folder, folders, or an item's Properties. flxp-desktop owns the name while it
// runs (it is there in every session) and answers on its own message loop; flxp-desktop --filemanager1 is the same service
// without the desktop, which the bus starts from the service file when no session runs. Every request starts an flxp
// process (one window per process, as everywhere): ShowItems `flxp --select ITEM` (one window per folder, its first item
// selected), ShowFolders `flxp DIR...`, ShowItemProperties `flxp --properties ITEM...`. Plain libdbus, no win32.
#pragma once
#include <string>

namespace fm1 {

// Connects to the session bus and asks for the name: replace takes it from another owner that allows it (the activated
// service when flxp-desktop starts); the name is kept replaceable unless replace. The connection's fd to watch, or -1
// (no bus, or another program owns the name: logged).
int start(const std::string& flxp, bool replace);
// The fd is readable: read and answer what arrived.
void handle();
// flxp-desktop --filemanager1: start(flxp, false) and a poll loop, until the name goes to another owner, the bus goes away
// or nothing came for a few minutes. The exit status.
int run_service(const std::string& flxp);

} // namespace fm1
