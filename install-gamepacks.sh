#!/bin/sh

: ${ECHO:=echo}
: ${SH:=sh}
: ${CP:=cp}
: ${CP_R:=cp -r}

dest=$1

case "$DOWNLOAD_GAMEPACKS" in
	yes)
		LICENSEFILTER=GPL BATCH=1 $SH download-gamepacks.sh
		;;
	allinone)
		LICENSEFILTER=allinone BATCH=1 $SH download-gamepacks.sh
		;;
	all)
		BATCH=1 $SH download-gamepacks.sh
		;;
	*)
		;;
esac

set -e
found=no
for GAME in games/*Pack; do
	if [ "$GAME" = "games/*Pack" ]; then
		$ECHO "Game packs not found, please run"
		$ECHO "  ./download-gamepacks.sh"
		$ECHO "and then try again!"
	else
		$SH install-gamepack.sh "$GAME" "$dest"
		found=yes
	fi
done

# the repository's own pbr pack goes last: it overlays the downloaded Quake III game file (games/Q3.game with
# entities="pbr") and adds Q3.game/baseq3/_pbr_lights.ent, so it must not be overwritten by Quake3Pack/NRCPack
# without them there is no Q3.game to overlay, and installing the overlay alone would leave a Quake III game
# that knows only the three light classes
if [ "$found" = yes ]; then
	$SH install-gamepack.sh setup/data/gamepacks/pbr "$dest"
fi
