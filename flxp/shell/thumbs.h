// Image thumbnails, shared with other applications through the freedesktop
// thumbnail spec (0.9): $XDG_CACHE_HOME/thumbnails/{normal,large}/<md5 of the
// file URI>.png, 128 or 256 px at most, with the tEXt chunks Thumb::URI and
// Thumb::MTime that say which file version they show. Existing thumbnails
// (from any application) are read when they are current; missing ones are
// made from PNG/JPEG/BMP/ICO/CUR/WebP (gdi/image), or by a thumbnailer entry's
// program for any other type an entry names (a PDF, a PostScript file, a video),
// and saved; files that cannot be read get a failure marker in
// thumbnails/fail/flxp-1.0 so they are not tried again until they change. No
// size limit, no clean-up (the spec leaves that to the user or a cleaner).
//
// Loader runs the work on background threads; results are picked up by the
// UI thread after the notify callback (called on a worker thread) woke it.
#pragma once
#include <stdint.h>
#include <set>
#include <string>
#include <vector>
#include "gdi/bitmap.h"

namespace shell {
namespace thumbs {

std::string cache_dir();                                    // $XDG_CACHE_HOME/thumbnails
int cache_size_for(int view_size);                          // 128 (normal) or 256 (large)
std::string cache_path(const std::string& file, int cache_size);   // .../normal/<md5(uri)>.png
std::string md5_hex(const std::string& data);
// A file this can make a thumbnail of: a picture flxp decodes itself, or a file whose type (by its name) a thumbnailer
// entry takes. UI thread (the MIME database is read without a lock).
bool can_thumbnail(const std::string& path);
// A picture flxp decodes itself (JPEG, PNG, BMP, ICO/CUR, WebP), by its name.
bool own_picture(const std::string& path);

// Thumbnailer entries (the freedesktop thumbnailer spec): [Thumbnailer Entry] files named *.thumbnailer with TryExec, Exec
// and MimeType, read from $XDG_DATA_HOME/thumbnailers, each $XDG_DATA_DIRS/thumbnailers, then lhde's own
// (<prefix>/share/thumbnailers; the source tree's share/thumbnailers uninstalled). A file name seen in an earlier directory
// hides the later ones; lhde's own entries (lhde-*.thumbnailer) come after every other, so a distribution's thumbnailer
// for a type wins over ours. Within a directory, by file name.
struct Thumbnailer {
    std::string file;                   // the entry's path
    std::string name;                   // its file name
    std::string exec, try_exec;
    std::vector<std::string> types;     // MimeType, canonical
};
std::vector<std::string> thumbnailer_dirs();
std::vector<const Thumbnailer*> thumbnailers();     // every entry, in the order they are tried
// The entry for a file: the first whose program is there (TryExec, else the Exec's command, looked up once per entry)
// listing the file's type, else its nearest parent type that has one (application/octet-stream never). Null: none, or
// the file is one of flxp's own pictures. UI thread.
const Thumbnailer* thumbnailer_for(const std::string& path);
void reload_thumbnailers();                         // read the entries again (earlier pointers stay valid)
// Exec's argument list for a file: %i the path, %o the output, %s the size, %u the URI, %% a percent sign, each a
// whole argument; the line is split as a shell would ('...', "..." with backslash escapes, a backslash outside quotes),
// with no expansion.
std::vector<std::string> thumbnailer_argv(const Thumbnailer& t, const std::string& path, const std::string& out, int size);
// A thumbnailer runs as a child process in its own process group, stdin and stdout on /dev/null, stderr too (flxp's
// stderr with FLXP_TRACE_THUMBS), with only PATH, HOME, LANG, LC_ALL and TMPDIR of the environment; it writes into a
// temporary file in the thumbnail directory. Its group is killed after timeout_ms (10 s) and when it ends, and its
// output is not read above max_bytes (16 MB). Tests change them.
void set_thumbnailer_limits(int timeout_ms, size_t max_bytes);
int thumbnailer_runs();                             // children started so far

// Whether the views show thumbnails in a folder: flxp.ini thumbnails = 1 | 0 (default 1) and, on a network mount,
// thumbnails_network = 1 | 0 (default 1; 0: nothing is read over the network for a thumbnail).
bool enabled_for(const std::string& folder);
// On a network mount a thumbnail comes in three stages: the cache (nothing else is read while it has a current one), the
// JPEG's own EXIF thumbnail (exif_preview: at most EXIF_READ bytes of the file), and the picture's full decode (or a
// thumbnailer's run) last, only while the files read whole for a folder stay within thumbnails_network_budget MB
// (default 64; 0: none read whole) - the rest keep their EXIF thumbnail, or their icon. A local folder: the cache, then
// the full decode, as always.
enum { EXIF_READ = 64 * 1024 };
uint64_t network_budget();                                  // thumbnails_network_budget in bytes (UI thread)
// A JPEG's EXIF thumbnail (APP1 Exif, IFD1's JPEGInterchangeFormat; usually 160 x 120): read from the first EXIF_READ bytes
// of the file in one read, decoded, turned as its Orientation tag says and fitted into size x size (enlarged too; 0: as
// it is). *w, *h: the picture's own size as shown (its frame header's, turned), 0 when not in those bytes. Null: none.
// Never stored in the thumbnail cache (it would stand for the full thumbnail there). Thread-safe.
gdi::Bitmap* exif_preview(const std::string& path, int size, int* w = nullptr, int* h = nullptr);
int previews();                                             // EXIF thumbnails made so far
// The current cached thumbnail at cache size (a larger one resampled), nothing else tried; null when there is none
gdi::Bitmap* cached_thumbnail(const std::string& path, int cache_size);
// A path on a network filesystem: NFS, SMB/CIFS, 9p, AFS, Ceph, Coda, or a FUSE mount
// of sshfs, rclone, curlftpfs, gvfs, davfs2, s3fs and the like. Asked for every item a view shows, on the UI thread:
// answered from what is known of the folder the path is in (one statfs per folder, the Folder's own slow() included:
// a round trip on a network mount); only an entry of a local folder is asked about itself (a mount point under it).
bool on_network(const std::string& path);
// A picture's width and height from its header (PNG, JPEG, GIF, BMP, WebP), without decoding it
bool picture_size(const std::string& path, int* w, int* h);

// The thumbnail at cache size: a current cached one, else a new one that is
// also saved, made by the thumbnailer when one is given (its child counts as one
// of the decodes at once). Null when the file cannot be decoded. Thread-safe.
gdi::Bitmap* get(const std::string& path, int64_t mtime, int cache_size, const Thumbnailer* by = nullptr);
// Thumbnails made from their pictures so far (decodes; not those read from the cache). FLXP_TRACE_THUMBS=1 prints a line
// for each on stderr: the picture's size and the size it was decoded at.
int decodes();
// Pictures decoding at once in the process, whichever loader asks (the others wait their turn), and the most there were.
enum { DECODES_AT_ONCE = 2 };
int decodes_at_once_most();
// One of those turns for a decode of the caller's own (an image viewer's whole picture): held while it lives, waiting
// while DECODES_AT_ONCE others decode.
struct DecodeTurn {
    DecodeTurn();
    ~DecodeTurn();
    DecodeTurn(const DecodeTurn&) = delete;
    DecodeTurn& operator=(const DecodeTurn&) = delete;
};

class Loader {
public:
    typedef void (*Notify)(void* ctx);
    struct Result {
        uint32_t gen = 0;           // the generation it was requested in
        int id = 0;                 // the caller's id
        std::string path;
        gdi::Bitmap* bmp = nullptr; // fitted into view_size x view_size (no upscaling); null: none
        std::vector<gdi::Bitmap*> minis;   // folder requests: up to 4 pictures inside, fitted into mini_size
        // a network file's EXIF thumbnail (enlarged to view_size too): the same request's result follows (null when the
        // full decode was past the folder's budget: the preview stays)
        bool preview = false;
        bool over_budget = false;   // not read whole: past thumbnails_network_budget for its folder
    };
    Loader(Notify notify, void* ctx, int threads = 0);   // 0: up to 4 by the CPU count
    ~Loader();
    // Queue a file; results come back in request order per thread. folder:
    // the first pictures inside it (by name; flxp's own pictures only) instead, mini_size square each.
    // A file that is not one of flxp's own pictures gets its thumbnailer here (UI thread), none on a
    // network mount with thumbnails_network = 0. A file on a network mount goes through the three stages
    // (enabled_for above): every request's cache and EXIF stage before any full decode, the full decodes
    // within the budget of the request's folder (it starts again when a request is for another folder).
    void request(uint32_t gen, int id, const std::string& path, int64_t mtime, int view_size, bool folder = false, int mini_size = 0);
    uint32_t cancel();              // drops everything pending, returns the new generation
    uint32_t generation() const;
    bool busy() const;              // jobs queued or being made
    bool take(Result& out);         // UI thread: next finished result (free_result it)
    uint64_t spent() const;         // bytes of network files read whole for the current folder
    // No budget: every network file's full decode is made (still after every request's cache and EXIF stage), for a
    // caller that asks only for what it shows (luxview's filmstrip), where a cell past the budget would stay empty
    void unbudgeted();
    struct Impl;                    // the threads (kept out of here: <thread> and win32's min/max)
private:
    Impl* impl_;
};

void free_result(Loader::Result& r);  // destroys its bitmaps

// The images a view keeps, bounded by bytes: each key (a file and its mtime) has a slot (the view's image list index
// space for thumbnails, slots reused), the least recently used are evicted when an insert would pass the budget, never
// the pinned ones (the items on screen), so a view holds at most budget bytes plus what it shows.
class Slots {
public:
    static const size_t DEFAULT_BUDGET = 64u << 20;
    explicit Slots(size_t budget = DEFAULT_BUDGET);
    ~Slots();
    void reset(size_t item_bytes);            // drops everything; every image is item_bytes from now on
    int find(const std::string& key);         // its slot (now the most recently used), -1: none
    // a slot for key: its own, a free one, the least recently used unpinned one's (its key into evicted) or a new one
    int insert(const std::string& key, std::vector<std::string>* evicted);
    void erase(const std::string& key);
    void pin(const std::set<std::string>& keys);   // the keys not to evict (replaces the earlier set)
    size_t bytes() const;
    size_t count() const;
    size_t budget() const;
    size_t item_bytes() const;
    int slots() const;                        // slots ever handed out (the image list's thumbnail images)
    struct Impl;                              // (std::list and std::unordered_map kept out of here: win32's min/max)
private:
    Impl* impl_;
    Slots(const Slots&);
    Slots& operator=(const Slots&);
};

} // namespace thumbs
} // namespace shell
