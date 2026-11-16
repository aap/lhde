#!/bin/sh
# make check-liblhcat: lhde-cat over a scratch root (docs/CATALOGUE.md). 100 files with attributes and 20 JPEGs with EXIF
# dates (Pillow; the picture checks skip without it): import-du of a listing, index (rows, EXIF, attributes), a second
# index reading nothing again, find by tag, date, type, text, colour and directory, set / tag / unset with the journal,
# deferred writes and commit, a file changed before the write-back (a conflict, not overwritten), a duplicate and a
# retried operation id, two offline edits against one revision (tags merge, set/set conflicts), touch after an external
# change, a move and a duplicate as relations, an interrupted and a killed scan (nothing gone, resumed), thumbnails through
# flxp-thumb; then liblhcat's own test (normalization, picture headers, sha256, a 2,000,000-line listing under 30 s and a
# tag query on it under 50 ms; LHCAT_PERF_LINES=N for another size). Needs setfattr/getfattr (on FreeBSD extattr's tools
# through tests/headless/xattrs.sh); everything is under a temporary directory removed at the end.
cd "$(dirname "$0")/.." || exit 1
B=$PWD/build/bin/lhde-cat
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export XDG_STATE_HOME="$T/state" XDG_CACHE_HOME="$T/cache" XDG_CONFIG_HOME="$T/config"
fails=0 checks=0
ok() { checks=$((checks + 1)); }
no() { checks=$((checks + 1)); fails=$((fails + 1)); echo "FAIL $*"; }
eq() {   # WHAT EXPECTED GOT
    if [ "$2" = "$3" ]; then ok; else no "$1: expected [$2] got [$3]"; fi
}
has() {   # WHAT NEEDLE TEXT
    case "$3" in *"$2"*) ok ;; *) no "$1: [$2] not in [$3]" ;; esac
}
. tests/headless/xattrs.sh   # getfattr / setfattr over extattr where attr is not installed (FreeBSD)
command -v setfattr > /dev/null && command -v getfattr > /dev/null || { echo "skip: no setfattr/getfattr"; exit 0; }
R=$T/root
mkdir -p "$R/docs/sub" "$R/pics/2019" "$R/pics/2021"
touch "$T/x" && setfattr -n user.t -v 1 "$T/x" 2> /dev/null || { echo "skip: no user attributes on $T"; exit 0; }
# SQL on a database: through python3's sqlite3 module, else the sqlite3 shell (FreeBSD's python3 has the module only with
# py312-sqlite3); rows one per line, columns joined by |, NULL as nothing; sql_hold DB: another program that has read
# from DB and keeps its connection open for 30 s, printing "open" once it has (run with &: it execs)
if python3 -c 'import sqlite3' 2> /dev/null; then
    sql() { python3 -c 'import sqlite3, sys; c = sqlite3.connect(sys.argv[1]); print("\n".join("|".join("" if v is None else str(v) for v in r) for r in c.execute(sys.argv[2])))' "$1" "$2"; }
    sql_script() { python3 -c 'import sqlite3, sys; sqlite3.connect(sys.argv[1]).executescript(sys.argv[2])' "$1" "$2"; }
    sql_hold() { exec python3 -c 'import sqlite3, sys, time; c = sqlite3.connect(sys.argv[1]); c.execute("SELECT count(*) FROM locations").fetchone(); print("open", flush=True); time.sleep(30)' "$1"; }
elif command -v sqlite3 > /dev/null; then
    sql() { sqlite3 -batch -noheader -separator '|' "$1" "$2"; }
    sql_script() { sqlite3 -batch "$1" "$2"; }
    sql_hold() { exec sqlite3 -batch -cmd 'SELECT count(*) FROM locations' -cmd '.print open' -cmd '.shell exec sleep 30' "$1" < /dev/null; }
else echo "skip: needs python3's sqlite3 or the sqlite3 shell"; exit 0; fi
db_of() { sql "$T/state/lhde/catalogue/$1.db" "$2"; }
db() { db_of t "$1"; }

# ---- the scratch root ------------------------------------------------------------------------------------------------
i=0
while [ $i -lt 100 ]; do
    f="$R/docs/note$i.txt"; [ $i -ge 80 ] && f="$R/docs/sub/note$i.txt"
    echo "note $i" > "$f"
    case $((i % 4)) in
        0) setfattr -n user.xdg.tags -v "work,red" "$f" ;;
        1) setfattr -n user.xdg.tags -v "home" "$f"; setfattr -n user.lux.desc -v "a shopping list $i" "$f" ;;
        2) setfattr -n user.lux.color -v blue "$f"; setfattr -n user.xdg.comment -v "first line
second line about sunsets" "$f" ;;
        3) setfattr -n user.lux.desc -v "minutes of meeting $i" "$f" ;;
    esac
    i=$((i + 1))
done
pics=0
if python3 -c 'import PIL' 2> /dev/null; then
    python3 - "$R/pics" <<'EOF'
import sys
from PIL import Image
base = sys.argv[1]
for i in range(20):
    year = 2019 if i < 10 else 2021
    im = Image.new('RGB', (64 + i, 48), (i * 10, 100, 200))
    ex = Image.Exif()
    ex[0x010F] = 'Canon'
    ex[0x0110] = 'Canon EOS 5D'
    ex[0x0112] = 6 if i == 3 else 1
    ex[0x8769] = {0x9003: '%d:%02d:15 10:%02d:00' % (year, i % 10 + 1, i)}
    im.save('%s/%d/img%02d.jpg' % (base, year, i), exif=ex.tobytes())
EOF
    pics=1
else echo "note: no Pillow: the picture checks are skipped"; fi
setfattr -n user.xdg.tags -v "holiday,family" "$R/pics/2019/img01.jpg" 2> /dev/null

# ---- import-du, then index ------------------------------------------------------------------------------------------
$B add-root t "$R" > /dev/null || no "add-root"
(cd "$R" && env -u BLOCKSIZE -u BLOCK_SIZE -u DU_BLOCK_SIZE du -a .) > "$T/listing.txt"
out=$($B import-du t "$T/listing.txt" --quiet) || no "import-du"
has "import-du" "rows" "$out"
eq "listed only (all but the . line)" "$(($(grep -c . "$T/listing.txt") - 1))" "$(db "SELECT count(*) FROM locations WHERE coverage & 1 AND parent != 0")"
eq "the root's total from the . line" "$(tail -n 1 "$T/listing.txt" | cut -f1)" "$(db "SELECT usage_kib FROM locations WHERE parent = 0")"
eq "nothing statted yet" "0" "$(db "SELECT count(*) FROM locations WHERE coverage & 2 AND parent != 0")"
eq "find on listed rows" "docs/note5.txt" "$($B find t --text note5.txt --quiet)"
out=$($B index t --quiet) || no "index"
has "index" "0 gone" "$out"
eq "every file statted" "$(db "SELECT count(*) FROM locations WHERE parent != 0")" "$(db "SELECT count(*) FROM locations WHERE coverage & 2 AND parent != 0")"
eq "the listing's objects kept" "$(db "SELECT count(*) FROM locations")" "$(db "SELECT count(*) FROM objects")"
out=$($B index t --quiet)
has "a second index reads nothing again" "0 read again" "$out"

# ---- find -------------------------------------------------------------------------------------------------------------
eq "--tag work" "25" "$($B find t --tag work --quiet | wc -l | tr -d ' ')"
eq "--tag is case-blind" "25" "$($B find t --tag WORK --quiet | wc -l | tr -d ' ')"
eq "--desc" "docs/note13.txt" "$($B find t --desc 'shopping list 13' --quiet)"
eq "--text over the comment" "25" "$($B find t --text sunsets --quiet | wc -l | tr -d ' ')"
eq "--color" "25" "$($B find t --color Blue --quiet | wc -l | tr -d ' ')"
eq "--in" "20" "$($B find t --in docs/sub --files --quiet | wc -l | tr -d ' ')"
eq "--type text/plain" "100" "$($B find t --type 'text/*' --quiet | wc -l | tr -d ' ')"
eq "--no-tag" "75" "$($B find t --no-tag work --in docs --files --quiet | wc -l | tr -d ' ')"
# a part of the name: from the name index (3 characters and more), row by row (fewer), the rows not in the index yet too
eq "the name index is up to date after index" "$(db "SELECT max(id) FROM locations")" "$(db "SELECT upto FROM names_upto")"
eq "--name a part" "docs/note13.txt" "$($B find t --name TE13 --quiet)"
eq "--name short" "$($B find t --text 13 --files --quiet | sort)" "$($B find t --name 13 --files --quiet | sort)"
eq "--name none" "0" "$($B find t --name zzzq --quiet | wc -l | tr -d ' ')"
sql_script "$T/state/lhde/catalogue/t.db" "UPDATE names_upto SET upto = (SELECT min(id) FROM locations WHERE name = 'note13.txt') - 1"
eq "--name past the index" "docs/note13.txt" "$($B find t --name note13 --limit 1 --quiet)"
eq "find --name brings the index up" "$(db "SELECT max(id) FROM locations")" "$(db "SELECT upto FROM names_upto")"
eq "--tag a,b: both" "$($B find t --tag work --quiet | sort)" "$($B find t --tag 'work, WORK' --quiet | sort)"
eq "--type a,b: either" "$(($($B find t --type 'text/*' --quiet | wc -l) + $($B find t --type inode/directory --quiet | wc -l)))" "$($B find t --type 'text/*,inode/directory' --quiet | wc -l | tr -d ' ')"
err=$($B find t --tag work 2>&1 > /dev/null)
has "coverage said" "have their attributes read" "$err"
if [ $pics = 1 ]; then
    eq "pictures" "20" "$(db "SELECT count(*) FROM picture WHERE taken_at IS NOT NULL")"
    eq "EXIF date" "2019-02-15 10:01:00|Canon EOS 5D|65|48" "$(db "SELECT taken_at, camera, width, height FROM picture p JOIN locations l ON l.object_id = p.object_id WHERE l.name = 'img01.jpg'")"
    eq "orientation" "6" "$(db "SELECT orientation FROM picture p JOIN locations l ON l.object_id = p.object_id WHERE l.name = 'img03.jpg'")"
    eq "--since/--until by the date taken" "10" "$($B find t --type 'image/*' --since 2019 --until 2019 --quiet | wc -l | tr -d ' ')"
    eq "--since a month" "5" "$($B find t --type image/jpeg --since 2021-06 --quiet | wc -l | tr -d ' ')"
    eq "--until a day" "1" "$($B find t --type image/jpeg --since 2021-01-15 --until 2021-01-15 --quiet | wc -l | tr -d ' ')"
    eq "--tag on a picture" "pics/2019/img01.jpg" "$($B find t --tag family --quiet)"
    json=$($B find t --tag holiday --json --quiet)
    has "--json" '"taken_at":"2019-02-15 10:01:00"' "$json"
fi

# ---- set, tag, unset, the journal -------------------------------------------------------------------------------------
F=$R/docs/note0.txt
$B set "$F" desc "a new
description" > /dev/null || no "set"
eq "set writes" "a new description" "$(getfattr --only-values -n user.lux.desc "$F" 2> /dev/null)"
$B tag "$F" +urgent -red > /dev/null || no "tag"
eq "tag ops" "work,urgent" "$(getfattr --only-values -n user.xdg.tags "$F" 2> /dev/null)"
$B unset "$F" desc > /dev/null
eq "unset" "" "$(getfattr --only-values -n user.lux.desc "$F" 2> /dev/null)"
eq "journal" "set|applied tag_add|applied tag_remove|applied unset|applied" "$(db "SELECT op || '|' || status FROM journal WHERE path = 'docs/note0.txt' ORDER BY seq" | tr '\n' ' ' | sed 's/ $//')"
has "journal host and program" "/lhde-cat" "$($B journal t --path docs/note0.txt)"

# deferred, committed
$B set "$F" comment "later" --defer > /dev/null
eq "deferred: the file untouched" "" "$(getfattr --only-values -n user.xdg.comment "$F" 2> /dev/null)"
has "diff" "catalogue: later" "$($B diff t 2> /dev/null)"
eq "find sees the desired value" "docs/note0.txt" "$($B find t --text later --quiet)"
$B commit t > /dev/null
eq "commit writes" "later" "$(getfattr --only-values -n user.xdg.comment "$F" 2> /dev/null)"
eq "pending cleared" "none" "$(db "SELECT a.pending FROM attrs a JOIN locations l ON l.object_id = a.object_id WHERE l.name = 'note0.txt' AND a.name = 'user.xdg.comment'")"

# a file changed before the write-back: a conflict, the external value kept
$B set "$F" comment "mine" --defer > /dev/null
setfattr -n user.xdg.comment -v "theirs" "$F"
$B commit t > /dev/null; st=$?
eq "commit says conflict" "3" "$st"
eq "not overwritten" "theirs" "$(getfattr --only-values -n user.xdg.comment "$F" 2> /dev/null)"
has "conflicts" "file: theirs" "$($B conflicts t 2> /dev/null)"
$B index t --quiet > /dev/null
eq "a scan keeps the conflict" "conflict|mine|theirs" "$(db "SELECT a.pending, a.desired, a.observed FROM attrs a JOIN locations l ON l.object_id = a.object_id WHERE l.name = 'note0.txt' AND a.name = 'user.xdg.comment'")"
$B resolve "$F" comment catalogue && $B commit t > /dev/null
eq "resolved for the catalogue" "mine" "$(getfattr --only-values -n user.xdg.comment "$F" 2> /dev/null)"

# the same operation id twice: one operation
G=$R/docs/note3.txt
$B set "$G" desc "once" --op-id test-op-1 > /dev/null
out=$($B set "$G" desc "twice" --op-id test-op-1)
has "duplicate op id" "already applied" "$out"
eq "the first value stays" "once" "$(getfattr --only-values -n user.lux.desc "$G" 2> /dev/null)"
eq "one journal row" "1" "$(db "SELECT count(*) FROM journal WHERE op_id = 'test-op-1'")"
# a lost acknowledgement: the operation is in the journal, its write not done; the retry writes it
$B set "$G" desc "acked late" --defer --op-id test-op-2 > /dev/null
$B set "$G" desc "acked late" --write --op-id test-op-2 > /dev/null
eq "the retry wrote it" "acked late" "$(getfattr --only-values -n user.lux.desc "$G" 2> /dev/null)"
eq "applied once" "1|applied" "$(db "SELECT count(*) || '|' || max(status) FROM journal WHERE op_id = 'test-op-2'")"

# two offline edits against one revision: tag additions merge, set against set conflicts
H=$R/docs/note7.txt
rev=$(db "SELECT a.rev FROM attrs a JOIN locations l ON l.object_id = a.object_id WHERE l.name = 'note7.txt' AND a.name = 'user.lux.desc'")
trev=0
$B tag "$H" +left --base-rev $trev --op-id off-a > /dev/null
$B tag "$H" +right --base-rev $trev --op-id off-b > /dev/null
eq "tag_add merges" "left,right" "$(getfattr --only-values -n user.xdg.tags "$H" 2> /dev/null)"
$B set "$H" desc "side A" --base-rev "$rev" --op-id off-c > /dev/null
$B set "$H" desc "side B" --base-rev "$rev" --op-id off-d > /dev/null; st=$?
eq "set/set conflicts" "3" "$st"
eq "the first set stands" "side A" "$(getfattr --only-values -n user.lux.desc "$H" 2> /dev/null)"
eq "the second in the journal as a conflict" "conflict" "$(db "SELECT status FROM journal WHERE op_id = 'off-d'")"

# touch after an external change; filesystem authority: the file's value wins, noted in the journal
K=$R/docs/note12.txt
setfattr -n user.xdg.tags -v "external" "$K"
$B touch "$K"
eq "touch observes" "docs/note12.txt" "$($B find t --tag external --quiet)"
eq "observed noted" "observed" "$(db "SELECT op FROM journal WHERE path = 'docs/note12.txt' ORDER BY seq DESC LIMIT 1")"

# catalogue authority: an external change is a difference to commit, not the new truth
$B add-root t "$R" --authority catalogue > /dev/null
setfattr -n user.xdg.tags -v "drifted" "$K"
$B touch "$K"
eq "catalogue authority keeps desired" "external|drifted" "$(db "SELECT a.desired || '|' || a.observed FROM attrs a JOIN locations l ON l.object_id = a.object_id WHERE l.name = 'note12.txt' AND a.name = 'user.xdg.tags'")"
$B set "$K" color red > /dev/null
eq "deferred by default" "" "$(getfattr --only-values -n user.lux.color "$K" 2> /dev/null)"
$B commit t > /dev/null
eq "commit writes the difference" "external|red" "$(getfattr --only-values -n user.xdg.tags "$K" 2> /dev/null)|$(getfattr --only-values -n user.lux.color "$K" 2> /dev/null)"
$B add-root t "$R" --authority filesystem > /dev/null

# a move and a duplicate are relations, not the same object
mv "$R/docs/note20.txt" "$R/docs/sub/moved20.txt"
cp -a "$R/docs/note21.txt" "$R/docs/copy21.txt"
$B index t --hash --quiet > /dev/null
eq "the old path gone" "gone" "$(db "SELECT state FROM locations WHERE name = 'note20.txt'")"
eq "a new object for the new path" "1" "$(db "SELECT count(*) FROM relations r JOIN locations a ON a.object_id = r.object_a JOIN locations b ON b.object_id = r.object_b WHERE r.kind = 'moved' AND a.name = 'note20.txt' AND b.name = 'moved20.txt'")"
has "a duplicate by hash" "duplicate" "$($B relations t)"
eq "the moved file's attributes observed" "docs/sub/moved20.txt" "$($B find t --tag work --in docs/sub --quiet | grep moved)"

# an interrupted and a killed scan mark nothing gone; the next index resumes and finishes
rm "$R/docs/note30.txt" "$R/docs/sub/note90.txt"
LHCAT_TEST_STOP=10 $B index t --quiet > "$T/i1" ; st=$?
eq "interrupted" "130" "$st"
eq "nothing gone after an interruption" "present|present" "$(db "SELECT group_concat(state, '|') FROM locations WHERE name IN ('note30.txt', 'note90.txt')")"
LHCAT_COMMIT_MS=0 LHCAT_TEST_STOP=kill:40 $B index t --quiet > /dev/null 2>&1
eq "nothing gone after a kill" "present|present" "$(db "SELECT group_concat(state, '|') FROM locations WHERE name IN ('note30.txt', 'note90.txt')")"
out=$($B index t --quiet)
has "resumed" "(resumed)" "$out"
eq "gone after the whole scan" "gone|gone" "$(db "SELECT group_concat(state, '|') FROM locations WHERE name IN ('note30.txt', 'note90.txt')")"
eq "scans" "complete" "$(db "SELECT status FROM scans ORDER BY id DESC LIMIT 1")"

# an unreadable directory: what is under it is never taken as gone
mkdir -p "$R/locked" && echo x > "$R/locked/inner.txt"
$B index t --quiet > /dev/null
chmod 000 "$R/locked"
if ! ls "$R/locked" > /dev/null 2>&1; then
    $B index t --quiet > /dev/null
    eq "under an unreadable directory: present" "present" "$(db "SELECT state FROM locations WHERE name = 'inner.txt'")"
fi
chmod 755 "$R/locked"

# thumbnails (flxp-thumb), copied beside the catalogue with --self-contained
if [ $pics = 1 ] && [ -x build/bin/flxp-thumb ]; then
    out=$($B index t pics --thumbs 128 --self-contained --quiet)
    has "thumbnails" "20 thumbnails" "$out"
    eq "thumb rows" "20" "$(db "SELECT count(*) FROM thumbs WHERE cache_path LIKE '%t.thumbs/%'")"
    eq "in the freedesktop cache too" "20" "$(ls "$XDG_CACHE_HOME/thumbnails/normal" | wc -l | tr -d ' ')"
    has "the uri" "file://" "$(db "SELECT uri FROM thumbs LIMIT 1")"
    out=$($B index t pics --thumbs 128 --quiet)
    has "thumbnails not made again" "0 read again" "$out"
fi
has "stats" "attributes read" "$($B stats t)"

# an offline root: a listing of a tree not reachable here
printf '3\t./a/b.jpg\n7\t./a\n10\t.\n' > "$T/off.txt"
$B add-root off /nonexistent/tree --offline > /dev/null || no "offline add-root"
$B import-du off "$T/off.txt" --quiet > /dev/null || no "offline import-du"
eq "offline find" "a/b.jpg" "$($B find off --type image/jpeg --quiet)"
$B index off --quiet > /dev/null 2>&1 && no "an offline root is not indexed" || ok

# ---- a home directory: excludes, mount points, the dry run, the time limit -------------------------------------------
H2=$T/home
mkdir -p "$H2/.cache/fonts" "$H2/.local/share/Trash/files" "$H2/n/indra" "$H2/n/stash" "$H2/src/app/node_modules/left-pad" \
         "$H2/src/app/build" "$H2/src/lib/build" "$H2/docs" "$H2/mnt"
for f in .cache/fonts/a .local/share/Trash/files/old src/app/node_modules/left-pad/index.js src/app/build/app.o \
         src/lib/build/lib.o src/app/main.c src/lib/lib.c docs/one.txt docs/two.txt n/readme; do echo x > "$H2/$f"; done
out=$(HOME=$H2 $B add-root home "$H2")
has "a home root gets the default excludes" ".cache/  .local/share/Trash/  .thumbnails/  **/node_modules/" "$out"
eq "excludes NAME +GLOB" "$(printf '.cache/\n.local/share/Trash/\n.thumbnails/\n**/node_modules/\nn/')" "$($B excludes home +n/)"
eq "excludes NAME -GLOB" "$(printf '.cache/\n.local/share/Trash/\n**/node_modules/\nn/')" "$($B excludes home -.thumbnails/)"
$B add-root plain "$R/docs" > /dev/null
eq "another root: no excludes" "" "$($B excludes plain)"
plan=$($B index home --dry-run --exclude '**/build/' | tr -s ' ')
has "dry run: an excluded top-level directory" " .cache excluded (.cache/)" "$plan"
has "dry run: counts under a walked one" " src 5 entries 3 dirs, 3 excluded" "$plan"
has "dry run: the total" "11 entries would be walked (7 directories), 6 excluded" "$plan"
eq "dry run writes nothing" "1" "$(db_of home "SELECT count(*) FROM locations")"
out=$($B index home --exclude '**/build/' --quiet)
has "index: what was not walked" "not walked: 6 excluded, 0 mount points" "$out"
eq "excluded directories recorded, not walked" ".cache|d Trash|d build|d build|d n|d node_modules|d" "$(db_of home "SELECT name || '|' || kind FROM locations WHERE state = 'excluded' ORDER BY name" | tr '\n' ' ' | sed 's/ $//')"
eq "nothing under an excluded directory" "0" "$(db_of home "SELECT count(*) FROM locations WHERE name IN ('a', 'old', 'index.js', 'app.o', 'lib.o', 'readme')")"
eq "find leaves them out" "" "$($B find home --in src --quiet | grep -e build -e node_modules)"
has "stats: not walked" " src/app/build excluded" "$($B stats home | tr -s ' ')"
$B index home --quiet > /dev/null
eq "a glob dropped: walked again" "present|present" "$(db_of home "SELECT group_concat(state, '|') FROM locations WHERE name = 'app.o' OR (name = 'build' AND parent = (SELECT id FROM locations WHERE name = 'app'))")"
eq "the object kept" "1" "$(db_of home "SELECT count(*) FROM locations WHERE name = 'app.o' AND state = 'present'")"
$B index home --exclude 'src/' --quiet > /dev/null
eq "newly excluded: what was under it too" "excluded|excluded" "$(db_of home "SELECT group_concat(DISTINCT state) FROM locations WHERE name IN ('src', 'main.c')")|$(db_of home "SELECT state FROM locations WHERE name = 'lib.c'")"
$B index home --quiet > /dev/null
eq "and back" "present" "$(db_of home "SELECT state FROM locations WHERE name = 'lib.c'")"
eq "no gone ones on the way" "0" "$(db_of home "SELECT count(*) FROM locations WHERE state = 'gone'")"
# the time limit: stops after the first entry, the next index resumes and finishes
mkdir "$H2/docs/many" && (cd "$H2/docs/many" && touch $(seq -f f%g.txt 2000))
$B index home --time 0.00001 --quiet > "$T/tl"; st=$?
eq "--time: stopped (exit 124)" "124" "$st"
has "--time: said" "stopped at the time limit" "$(cat "$T/tl")"
out=$($B index home --quiet); st=$?
has "--time: resumed" "(resumed)" "$out"
eq "--time: finished" "0|complete" "$st|$(db_of home "SELECT status FROM scans ORDER BY id DESC LIMIT 1")"
# mount points (in a user namespace with a tmpfs over n/indra): recorded, not entered; --cross-fs enters
$B excludes home -n/ > /dev/null
if unshare -rm true 2> /dev/null; then
    cat > "$T/ns.sh" <<'EOF'
q() { python3 -c 'import sqlite3, sys; c = sqlite3.connect(sys.argv[1]); print(c.execute(sys.argv[2]).fetchone()[0])' "$DB" "$1"; }
mount -t tmpfs none "$H2/n/indra" || exit 3
echo remote > "$H2/n/indra/remote.txt"
echo "plan: $($B index home --dry-run | grep '^  n ' | tr -s ' ')"
echo "index: $($B index home --quiet | grep 'not walked')"
echo "kind=$(q "SELECT kind || state || coverage FROM locations WHERE name = 'indra'")"
echo "under=$(q "SELECT count(*) FROM locations WHERE name = 'remote.txt'")"
echo "stats: $($B stats home | grep '^  n/indra ' | tr -s ' ')"
echo "by device: $(LHCAT_TEST_NO_MOUNTTAB=1 $B index home --quiet | grep 'not walked')"
$B index home --cross-fs --quiet > /dev/null
echo "cross=$(q "SELECT count(*) FROM locations WHERE name = 'remote.txt' AND state = 'present'")"
EOF
    ns=$(B=$B H2=$H2 DB=$T/state/lhde/catalogue/home.db unshare -rm sh "$T/ns.sh" 2>&1)
    has "a mount point in the dry run" " n 3 entries 2 dirs, 1 mounts not entered" "$ns"
    has "a mount point not entered" "index: not walked: 3 excluded, 1 mount points not entered" "$ns"
    has "recorded as kind m, coverage 0" "kind=mpresent0" "$ns"
    has "nothing under it" "under=0" "$ns"
    has "stats: the mount table" " n/indra tmpfs not entered" "$ns"
    has "by its device when the table does not say" "by device: not walked: 3 excluded, 1 mount points not entered" "$ns"
    has "--cross-fs enters it" "cross=1" "$ns"
else echo "note: no user namespaces: the mount point checks are skipped"; fi

# ---- directory paths (dirs): subtree queries, kept through import, index, kind changes, a migration; rename-root ----
# every directory location's dirs path is the chain of its parents' names, and nothing else is in dirs
dirs_bad() {
    db_of "$1" "WITH RECURSIVE p(id, path) AS (SELECT id, '' FROM locations WHERE parent = 0
        UNION ALL SELECT l.id, CASE WHEN p.path = '' THEN l.name ELSE p.path || '/' || l.name END FROM locations l JOIN p ON l.parent = p.id),
      want(id, path) AS (SELECT p.id, p.path FROM p JOIN locations l ON l.id = p.id WHERE l.kind = 'd')
      SELECT count(*) FROM (SELECT id FROM dirs UNION SELECT id FROM want) u
      WHERE (SELECT path FROM dirs WHERE id = u.id) IS NOT (SELECT path FROM want WHERE id = u.id)"
}
eq "dirs after import-du and index" "0" "$(dirs_bad t)"
eq "dirs after the excludes and mounts" "0" "$(dirs_bad home)"
eq "--in: directory by directory, names sorted" "docs/sub/moved20.txt docs/sub/note80.txt docs/sub/note81.txt" "$($B find t --in docs/sub --limit 3 --quiet | tr '\n' ' ' | sed 's/ $//')"
eq "--in with a tag: the same order" "docs/sub/moved20.txt docs/sub/note80.txt" "$($B find t --in docs/sub --tag work --limit 2 --quiet | tr '\n' ' ' | sed 's/ $//')"
eq "--in a subdirectory's subtree" "$($B find t --quiet | grep -c '^pics/')" "$($B find t --in pics --quiet | wc -l | tr -d ' ')"
eq "--in a file" "1" "$($B find t --in docs/note1.txt --quiet > /dev/null 2>&1; echo $?)"
eq "--in a name that is a prefix of another" "0" "$($B find t --in doc --quiet 2> /dev/null | wc -l | tr -d ' ')"
mkdir -p "$R/docs-old" && echo x > "$R/docs-old/x.txt" && $B index t --quiet > /dev/null
eq "--in docs leaves docs-old out" "0" "$($B find t --in docs --quiet | grep -c docs-old)"
# a file that becomes a directory, a directory that becomes a file
rm "$R/docs/note2.txt" && mkdir "$R/docs/note2.txt" && echo y > "$R/docs/note2.txt/inner.txt"
rm -r "$R/docs-old" && echo z > "$R/docs-old"
$B index t --quiet > /dev/null
eq "a file became a directory: --in it" "docs/note2.txt/inner.txt" "$($B find t --in docs/note2.txt --quiet)"
eq "dirs after kind changes" "0" "$(dirs_bad t)"
eq "the group index in catalogue order" "$($B find t --type 'image/*' --quiet | head -n 3)" "$($B find t --type 'image/*' --limit 3 --quiet)"
# the migration fills dirs from the locations of an older database
sql_script "$T/state/lhde/catalogue/t.db" "DROP TABLE dirs; DROP INDEX locations_group; ALTER TABLE roots DROP COLUMN machine; DROP TABLE names; DROP TABLE names_upto; DELETE FROM migrations WHERE version >= 3"
$B stats t > /dev/null
eq "migrated: dirs filled" "0" "$(dirs_bad t)"
eq "migrated: version" "5" "$(db "SELECT max(version) FROM migrations")"
eq "migrated: the group index" "1" "$(db "SELECT count(*) FROM sqlite_schema WHERE name = 'locations_group'")"
eq "migrated: the name index empty" "0|1" "$(db "SELECT upto FROM names_upto")|$(db "SELECT count(*) FROM sqlite_schema WHERE name = 'names'")"
# rename-root: the row, the file, the thumbnails' directory and their recorded paths
work=$($B find t --tag work --quiet | wc -l | tr -d ' ')
$B rename-root t t2 > /dev/null || no "rename-root"
eq "renamed: the file" "no|yes" "$([ -e "$T/state/lhde/catalogue/t.db" ] && echo yes || echo no)|$([ -e "$T/state/lhde/catalogue/t2.db" ] && echo yes || echo no)"
eq "renamed: no log left" "no" "$([ -e "$T/state/lhde/catalogue/t.db-wal" ] || [ -e "$T/state/lhde/catalogue/t2.db-wal" ] && echo yes || echo no)"
eq "renamed: the row" "t2" "$(db_of t2 "SELECT name FROM roots")"
eq "renamed: find" "$work" "$($B find t2 --tag work --quiet | wc -l | tr -d ' ')"
has "renamed: roots" "t2	$R" "$($B roots)"
if [ $pics = 1 ] && [ -x build/bin/flxp-thumb ]; then
    eq "renamed: the thumbnails' directory" "yes" "$([ -d "$T/state/lhde/catalogue/t2.thumbs" ] && echo yes || echo no)"
    eq "renamed: their paths" "20" "$(db_of t2 "SELECT count(*) FROM thumbs WHERE cache_path LIKE '%/t2.thumbs/%'")"
fi
$B rename-root t2 plain > /dev/null 2>&1 && no "rename-root onto an existing name" || ok
# another program with the database open: it has read from it and keeps its connection for 30 s (sql_hold)
sql_hold "$T/state/lhde/catalogue/t2.db" > "$T/reader" 2> /dev/null &
reader=$!
i=0; while [ $i -lt 50 ] && ! grep -q open "$T/reader"; do sleep 0.1; i=$((i + 1)); done
$B rename-root t2 t3 > /dev/null 2> "$T/busy" && no "rename-root while another program has it open" || ok
pkill -P $reader 2> /dev/null; kill $reader 2> /dev/null; wait $reader 2> /dev/null
has "refused while open" "in use" "$(cat "$T/busy")"
eq "and left as it was" "t2" "$(db_of t2 "SELECT name FROM roots")"
$B rename-root t2 t > /dev/null || no "rename-root back"

# ---- machines: machines.ini, roots bound to them, the probe (a stand-in ssh: up answers, slow never does) ----------
mkdir -p "$T/config/lhde" "$T/bin"
cat > "$T/config/lhde/machines.ini" <<'EOF'
[here]
os = linux
note = this machine

[up]
host = up.example    ; reached
os = freebsd
note = FreeBSD 13.1, ZFS

[down]
host = down.example
os = freebsd

[slow]
host = slow.example
os = linux
EOF
cat > "$T/bin/ssh" <<'EOF'
#!/bin/sh
for a; do host=$a; [ "$a" = true ] && break; prev=$a; done
case "$prev" in up.example) exit 0 ;; slow.example) exec sleep 23.5 ;; *) exit 255 ;; esac
EOF
chmod +x "$T/bin/ssh"
$B add-root plain "$R/docs" --machine nowhere > /dev/null 2> "$T/err" && no "an unknown machine refused" || ok
has "an unknown machine named" "no machine 'nowhere'" "$(cat "$T/err")"
has "add-root --machine" "machine here" "$($B add-root plain "$R/docs" --machine here)"
$B add-root home "$H2" --machine up > /dev/null
$B add-root t "$R" --machine up > /dev/null || no "add-root t --machine"
has "roots: the machine" "plain	$R/docs	here" "$($B roots)"
eq "machines: their roots" "here	(this machine)	linux	plain	this machine|up	up.example	freebsd	home, t	FreeBSD 13.1, ZFS|down	down.example	freebsd	-|slow	slow.example	linux	-" \
   "$($B machines | tr '\n' '|' | sed 's/|$//')"
start=$(date +%s)
out=$(PATH="$T/bin:$PATH" $B machines --probe)
took=$(($(date +%s) - start))
has "probe: here" "plain	here	this machine" "$out"
has "probe: reachable" "home, t	reachable" "$out"
has "probe: not reachable" "down.example	freebsd	-	not reachable" "$out"
has "probe: no answer" "slow.example	linux	-	no answer in 3 s" "$out"
[ $took -le 5 ] && ok || no "probe: 3 s at most ($took s)"
eq "probe: nothing left running" "" "$(pgrep -f '[s]leep 23.5' | head -n 1)"
$B add-root plain "$R/docs" --machine - > /dev/null
eq "unbound" "plain	$R/docs" "$($B roots | grep '^plain')"
XDG_CONFIG_HOME=$T/none $B machines | grep -q "no machines" && ok || no "machines without the file"

# ---- liblhcat's own -------------------------------------------------------------------------------------------------
mkdir -p "$T/perf"
XDG_STATE_HOME="$T/perf" build/liblhcat/tests/cat ${LHCAT_PERF_LINES:-} > "$T/cat.log" 2>&1 || no "liblhcat tests: $(cat "$T/cat.log")"
cat "$T/cat.log" | grep '^perf'
checks=$((checks + $(tail -n 1 "$T/cat.log" | cut -d' ' -f1)))
echo "$checks checks, $fails failed"
[ $fails = 0 ]
