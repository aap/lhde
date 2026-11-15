#!/bin/sh
# make check-liblhcat: lhde-cat over a scratch root (docs/CATALOGUE.md). 100 files with attributes and 20 JPEGs with EXIF
# dates (Pillow; the picture checks skip without it): import-du of a listing, index (rows, EXIF, attributes), a second
# index reading nothing again, find by tag, date, type, text, colour and directory, set / tag / unset with the journal,
# deferred writes and commit, a file changed before the write-back (a conflict, not overwritten), a duplicate and a
# retried operation id, two offline edits against one revision (tags merge, set/set conflicts), touch after an external
# change, a move and a duplicate as relations, an interrupted and a killed scan (nothing gone, resumed), thumbnails through
# flxp-thumb; then liblhcat's own test (normalization, picture headers, sha256, a 2,000,000-line listing under 30 s and a
# tag query on it under 50 ms; LHCAT_PERF_LINES=N for another size). Needs setfattr/getfattr; everything is under a
# temporary directory removed at the end.
cd "$(dirname "$0")/.." || exit 1
B=$PWD/build/bin/lhde-cat
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export XDG_STATE_HOME="$T/state" XDG_CACHE_HOME="$T/cache"
fails=0 checks=0
ok() { checks=$((checks + 1)); }
no() { checks=$((checks + 1)); fails=$((fails + 1)); echo "FAIL $*"; }
eq() {   # WHAT EXPECTED GOT
    if [ "$2" = "$3" ]; then ok; else no "$1: expected [$2] got [$3]"; fi
}
has() {   # WHAT NEEDLE TEXT
    case "$3" in *"$2"*) ok ;; *) no "$1: [$2] not in [$3]" ;; esac
}
command -v setfattr > /dev/null && command -v getfattr > /dev/null || { echo "skip: no setfattr/getfattr"; exit 0; }
R=$T/root
mkdir -p "$R/docs/sub" "$R/pics/2019" "$R/pics/2021"
touch "$T/x" && setfattr -n user.t -v 1 "$T/x" 2> /dev/null || { echo "skip: no user attributes on $T"; exit 0; }
db() { python3 -c 'import sqlite3, sys; c = sqlite3.connect(sys.argv[1]); print("\n".join("|".join("" if v is None else str(v) for v in r) for r in c.execute(sys.argv[2])))' "$T/state/lhde/catalogue/t.db" "$1"; }

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

# ---- liblhcat's own -------------------------------------------------------------------------------------------------
mkdir -p "$T/perf"
XDG_STATE_HOME="$T/perf" build/liblhcat/tests/cat ${LHCAT_PERF_LINES:-} > "$T/cat.log" 2>&1 || no "liblhcat tests: $(cat "$T/cat.log")"
cat "$T/cat.log" | grep '^perf'
checks=$((checks + $(tail -n 1 "$T/cat.log" | cut -d' ' -f1)))
echo "$checks checks, $fails failed"
[ $fails = 0 ]
