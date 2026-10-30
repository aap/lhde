// Image thumbnails, shared with other applications through the freedesktop
// thumbnail spec (0.9): $XDG_CACHE_HOME/thumbnails/{normal,large}/<md5 of the
// file URI>.png, 128 or 256 px at most, with the tEXt chunks Thumb::URI and
// Thumb::MTime that say which file version they show. Existing thumbnails
// (from any application) are read when they are current; missing ones are
// made from PNG/JPEG/BMP/ICO/CUR (gdi/image) and saved; files that cannot be
// read get a failure marker in thumbnails/fail/flxp-1.0 so they are not tried
// again until they change. No size limit, no clean-up (the spec leaves that
// to the user or a cleaner).
//
// Loader runs the work on background threads; results are picked up by the
// UI thread after the notify callback (called on a worker thread) woke it.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "gdi/bitmap.h"

namespace shell {
namespace thumbs {

std::string cache_dir();                                    // $XDG_CACHE_HOME/thumbnails
int cache_size_for(int view_size);                          // 128 (normal) or 256 (large)
std::string cache_path(const std::string& file, int cache_size);   // .../normal/<md5(uri)>.png
std::string md5_hex(const std::string& data);
// A file this can make a thumbnail of (by name or by its first bytes).
bool can_thumbnail(const std::string& path);

// Whether the Longhorn views show thumbnails in a folder: flxp.ini thumbnails = 1 | 0
// (default 1) and, on a network mount (reading every picture over the network is
// slow), thumbnails_network = 0 | 1 (default 0).
bool enabled_for(const std::string& folder);
// A path on a network filesystem: NFS, SMB/CIFS, 9p, AFS, Ceph, Coda, or a FUSE mount
// of sshfs, rclone, curlftpfs, gvfs, davfs2, s3fs and the like.
bool on_network(const std::string& path);
// A picture's width and height from its header (PNG, JPEG, GIF, BMP, WebP), without decoding it
bool picture_size(const std::string& path, int* w, int* h);

// The thumbnail at cache size: a current cached one, else a new one that is
// also saved. Null when the file cannot be decoded. Thread-safe.
gdi::Bitmap* get(const std::string& path, int64_t mtime, int cache_size);

class Loader {
public:
    typedef void (*Notify)(void* ctx);
    struct Result {
        uint32_t gen = 0;           // the generation it was requested in
        int id = 0;                 // the caller's id
        std::string path;
        gdi::Bitmap* bmp = nullptr; // fitted into view_size x view_size (no upscaling); null: none
        std::vector<gdi::Bitmap*> minis;   // folder requests: up to 4 pictures inside, fitted into mini_size
    };
    Loader(Notify notify, void* ctx, int threads = 0);   // 0: up to 4 by the CPU count
    ~Loader();
    // Queue a file; results come back in request order per thread. folder:
    // the first pictures inside it (by name) instead, mini_size square each.
    void request(uint32_t gen, int id, const std::string& path, int64_t mtime, int view_size, bool folder = false, int mini_size = 0);
    uint32_t cancel();              // drops everything pending, returns the new generation
    uint32_t generation() const;
    bool busy() const;              // jobs queued or being made
    bool take(Result& out);         // UI thread: next finished result (free_result it)
    struct Impl;                    // the threads (kept out of here: <thread> and win32's min/max)
private:
    Impl* impl_;
};

void free_result(Loader::Result& r);  // destroys its bitmaps

} // namespace thumbs
} // namespace shell
