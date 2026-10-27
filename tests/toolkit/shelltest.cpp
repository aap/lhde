// List a folder through the shell namespace, Explorer-style.
#include <stdio.h>
#include "shell/namespace.h"
int main(int argc, char** argv)
{
    auto f = shell::folder_for(argc > 1 ? argv[1] : "::desktop");
    if (!f) { fprintf(stderr, "not a folder\n"); return 1; }
    printf("%s  [%s]  parent=%s\n", f->name().c_str(), f->path().c_str(), f->parent() ? f->parent()->name().c_str() : "(none)");
    std::vector<shell::Item> items;
    f->enumerate(items, false);
    shell::sort_items(items, shell::SortBy::Name, true);
    for (auto& it : items)
        printf("  %-10s %-36s %10s  %-26s %s\n", it.icon.c_str(), it.name.c_str(), it.is_folder() ? "" : shell::format_size(it.size).c_str(), it.type.c_str(), it.mtime ? shell::format_date(it.mtime).c_str() : "");
    return 0;
}
