// infotip FILE: the Explorer's infotip of FILE (shell::details::infotip of the item its folder lists, the stored
// properties read as the views read them), one line each. tests/headless/luxshot.sh reads luxshot's annotations with it.
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "shell/config.h"
#include "shell/details.h"
#include "shell/namespace.h"

int main(int argc, char** argv)
{
    char abs[PATH_MAX];
    if (argc != 2 || !realpath(argv[1], abs)) { fprintf(stderr, "usage: infotip FILE\n"); return 2; }
    std::string path = abs;
    auto folder = shell::folder_for(shell::dir_of(path));
    std::vector<shell::Item> items;
    if (!folder || !folder->enumerate(items, true)) { fprintf(stderr, "infotip: cannot list %s\n", shell::dir_of(path).c_str()); return 1; }
    for (shell::Item& it : items) {
        if (it.path != path) continue;
        shell::details::read_stored(it);
        printf("%s\n", shell::details::infotip(it).c_str());
        return 0;
    }
    fprintf(stderr, "infotip: %s is not in its folder's list\n", path.c_str());
    return 1;
}
