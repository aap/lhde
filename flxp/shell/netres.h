// Embedded .NET resources (System.Resources .resources streams, as an Avalon
// assembly of the Longhorn builds carries its images): find a named entry in
// any .resources blob inside a file. Only version 2 readers' ByteArray and
// Stream entries are returned (what images are stored as). No win32 here.
#pragma once
#include <string>

namespace shell {

// Looks for `name` (e.g. "framelet_back_rest.png") in every .resources stream
// (0xBEEFCACE) in a file's bytes. True with the entry's bytes in `out`.
bool net_resource_in(const std::string& data, const std::string& name, std::string& out);

}
