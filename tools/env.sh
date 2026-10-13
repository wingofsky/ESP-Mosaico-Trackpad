# Source from bash or zsh after installing the pinned SDK (see README).
_trackpad_idf="${TRACKPAD_IDF_PATH:-${IDF_PATH:-$HOME/.cache/mosaico-trackpad/esp-idf}}"
_trackpad_pin=7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe
if [ ! -f "$_trackpad_idf/export.sh" ] || \
   [ "$(git -C "$_trackpad_idf" rev-parse HEAD 2>/dev/null)" != "$_trackpad_pin" ]; then
    echo "Install the pinned ESP-IDF revision from README or set TRACKPAD_IDF_PATH." >&2
    return 1
fi
if ! git -C "$_trackpad_idf" diff --quiet HEAD --; then
    echo "Pinned ESP-IDF has tracked modifications; use a clean SDK checkout." >&2
    return 1
fi
export IDF_PATH="$_trackpad_idf"
. "$IDF_PATH/export.sh" || return 1
unset _trackpad_idf _trackpad_pin
