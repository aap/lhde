# luxfind: the query window over the catalogues (docs/CATALOGUE.md "luxfind", docs/USAGE.md): a search pane and the
# Explorer's list with the results as they come. Its model (model.h, model_native.cpp: the search's threads, the
# reachability checks, the commits) is plain C++ without the win32 headers. The list and the context menu are the
# Explorer's browser widget and verbs, so the file dialog's objects (they bring flxp/explorer along).
LUXFIND_OBJS = $(call app_objs,luxfind)
build/bin/luxfind: $(LUXFIND_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
	$(app_link)
APPS += luxfind
apps programs: build/bin/luxfind
