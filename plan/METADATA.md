# Metadata: from Longhorn 4051's task band to extended attributes in flxp

Status: steps 1, 1b, 2 and 4 are in (Details columns, the infotip, Add Tag, the 4051 band's three states with editable rows and
invitations, the Properties sheet's Metadata tab, the Filter box over name/desc/tags/comment; docs/USAGE.md "Metadata", the
catalogue under it: docs/CATALOGUE.md). Open: step 3 (tasks as data), saved filters, where a colour shows (plan/IDEAS.md),
section 4's naming questions; the small gaps are in plan/BUGS.md. See plan/STATUS.md.

aap's starting point: WinFS had some good parts, so use it for inspiration. ext4 and ZFS support xattrs; the uses are tagging,
descriptions, comments and annotations, and organising agentic workflows. xattr support in flxp first; later a metadata catalogue
synchronised with local and remote file systems (plan/CATALOGUE-REMOTE-PHOTOS.md); the existing tooling is ~/src/luxfs.
Longhorn facts from `taskpane/README.md` (data.md, native.md, managed.md) and `explorer-views/icon-views.md` in the research
workspace; screenshots lh4051-1528xx / 1523xx / 1524xx. (Thinking notes of 2026-10-31.)

## 1. What 4051 actually did (the parts that matter here)
* **One band, three states.** Nothing selected: "N items / in this folder" over a folder-type image, plus the task grid.
  One item: thumbnail left, then name (18 pt), a **metadata column** (size, date, type-specific properties) with
  **invitations** for empty editable properties ("Add a category", "Add comments", "Add keywords"), "all properties...",
  then the tasks on the right. Several items: "N items selected", a thumbnail stack, the common properties.
* **Metadata = properties from the shell's property system (WinFS-backed), per type.** Which rows appear comes from
  one per-type property list (`PKEY_PROPLIST_PREVIEWPANE_DETAILS`); rows are two 180 px columns; editable rows (flag
  0x100) open a popup editor and persist on close. Category/keywords/comments are ordinary properties with invitations.
* **Tasks = data, not code.** Tasks are rows in a store (WinFS ShellTask), each with a name per selection kind (none /
  one file / one folder / many), an icon, an entry point, an optional state check, and a list of **experiences** they
  belong to: the folder's experience (`Explorer.InPictureFolder`) and the selection's (`OnFile.Any`, `OnFolder.Any`,
  `CanonicalType.jpg`), with inheritance (`CanonicalType.jpg` extends `OnPictureFile`). Explorer asks "tasks for these
  experiences, presentation Task Pane" and shows the result in `defaultOrder`. The folder's experience comes from its
  type (desktop.ini FolderType / WinFS query), the selection's from the items.
* **Views are queries.** The left pane's "Filter by", "All Photos / All Videos / All Folders / more" are WinFS queries;
  picture folders group by DateTaken; libraries are saved queries over properties.
* **What never worked**: WinFS down = no tasks; TaskContext (camera/printer) computed, never read; pinning no code;
  help-pane links dead. Keep in mind when deciding what to copy.

## 2. The mapping to xattrs
Longhorn's unit was the **property on an item**; the pane and the views were both consumers of properties. Replace the
WinFS property store by **xattrs on the file** and the model carries over almost unchanged:

| Longhorn | flxp with xattrs |
| --- | --- |
| property (System.Keywords, System.Comment, System.Category, System.Rating, DateTaken) | `user.xdg.tags`, `user.lux.desc` (= comments), a category/keyword list, `user.xdg.comment`, `user.lux.*`; plus derived read-only ones (EXIF date, dimensions) |
| per-type property list for the pane | a small table: file type -> which xattrs/derived props to show, which are editable |
| invitation text on empty editable rows | the same ("Add a description", "Add tags") |
| popup editor per row, persist on close | inline/popup editor writing the xattr (setxattr); luxfs `x` is the CLI twin |
| "all properties..." | a properties sheet listing every user.* attribute |
| Filter by / All Photos / saved queries | tag filter box; later saved filters over tags/desc (grep-able with `lsx -t`) |
| folder experience (InPictureFolder) | folder kind from content/.directory/xattr on the dir (`user.lux.tags` of the folder itself) |
| selection experience (OnFile.Any, CanonicalType.jpg) | selection kind + mime/extension, exactly as today |
| ShellTask rows with per-selection names | a task table in Lua/ini: name per selection kind, icon, command, `show_if`, experiences |
| WinFS store | none needed: the file system IS the store (xattrs travel with cp -a/rsync -X, not with git) |

The xattr idea covers the **property** side (what the pane shows and edits). The **task** side is independent and can
stay data-driven in the Longhorn sense without WinFS: a task list keyed by experiences is just a table.

## 3. A staged plan
1. **Properties first** (done): description and tags as Details columns; the pane's metadata
   column shows desc/tags/comment with invitations when empty; hover infotip shows them (the 4051 rich infotip already
   lists properties below the preview); context menu "Edit Description", "Add Tag" (selected items), a full editor
   later. Read/write via libattr; mirror luxfs's names so `x`/`lsx` and flxp agree (docs/ANNOTATIONS.md is the shared
   convention; "author/source/build" are research-specific, "desc/tags/comment" general).
   1b. **More metadata** (done): the description one line, the comment the free text of several lines (luxfs's notes and
   todo stay plain names in the Advanced list); a colour label (`user.lux.color`, a ten-name palette; where it shows is still
   open) and Run as (`user.lux.run`, a command a double click runs instead of the type's program); the Properties sheet's
   Metadata page with a tag list and a drop-down of known tags.
2. **Pane states as in 4051**: none / one / many, with the thumbnail left, properties middle, tasks right; compact vs
   expanded (72 / 150 px) as measured; the folder band colour per folder kind (already in lux themes).
3. **Tasks as data**: move flxp's task list to a table with experiences and per-selection names; add xattr tasks
   ("Add tags" is itself a task in 4051: `Explorer.Add_keywords`). This is where the research's `TASKS-SUMMARY.txt`
   (74 tasks with their experiences and orders) can be copied almost literally for the Longhorn look.
4. **Filtering**: the "Filter by" box filters over the name, desc, tags and comment (`tag:`, `desc:`, `comment:` words;
   docs/USAGE.md "Metadata"); later saved filters; later a catalog/index only if walking xattrs gets slow
   (4051's WinFS was that index; we can start without one, `lsx -t DIR` is the measure).
5. **Agent workflows**: the same xattrs are what agents already read/write in `~/fun/longhorn`; flxp becomes the viewer
   and editor of that metadata, which is the "organise agentic workflows" use above.

## 4. Things to decide
* Names: keep `user.xdg.tags`/`user.xdg.comment` (freedesktop) for interoperability, `user.lux.desc` for the one-line
  description; or fold desc into xdg comment? (luxfs chose separate desc.)
* Where derived properties come from (EXIF, dimensions, duration): a small extractor layer; 4051 had thumbnail/property
  tasks on a background queue; flxp already has a thumbnail task.
* Folder kinds: from an xattr on the folder (explicit, like desktop.ini FolderType) with a content heuristic as default.
* Tag vocabulary: free tags (xdg style) vs Longhorn's categories (a controlled list with "Add a category"); free first.
