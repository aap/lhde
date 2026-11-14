# getfattr and setfattr for the headless scripts (sourced) where attr's tools are absent and extattr's are there (FreeBSD:
# user.NAME is NAME in the user namespace), in the forms the scripts use:
#   setfattr -n NAME -v VALUE FILE
#   getfattr --only-values -n NAME FILE    the value; false when it is not set
#   getfattr -n NAME FILE                  NAME="VALUE"; false when it is not set
#   getfattr -d FILE                       NAME="VALUE" per user attribute
if ! command -v setfattr >/dev/null 2>&1 && command -v setextattr >/dev/null 2>&1; then
    _xname() { case $1 in user.?*) echo "${1#user.}" ;; *) return 1 ;; esac; }
    setfattr() {
        [ "$1" = -n ] && [ "$3" = -v ] && [ $# = 5 ] || { echo "setfattr (extattr): -n NAME -v VALUE FILE only" >&2; return 2; }
        _x=$(_xname "$2") || return 1
        setextattr user "$_x" "$4" "$5"
    }
    getfattr() {
        _only=; _x=; _full=; _dump=
        while [ $# -gt 1 ]; do
            case $1 in
            --only-values) _only=1 ;;
            -n) shift; _full=$1; _x=$(_xname "$1") || return 1 ;;
            -d) _dump=1 ;;
            esac
            shift
        done
        if [ -n "$_dump" ]; then
            for _a in $(lsextattr -q user "$1" 2>/dev/null); do echo "user.$_a=\"$(getextattr -q user "$_a" "$1")\""; done
            return 0
        fi
        _v=$(getextattr -q user "$_x" "$1" 2>/dev/null) || return 1
        if [ -n "$_only" ]; then printf %s "$_v"; else echo "$_full=\"$_v\""; fi
    }
fi
