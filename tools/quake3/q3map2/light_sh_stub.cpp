/* -------------------------------------------------------------------------------

   The -shbake stage's stand-in for a q3map2 built without SH_BAKER=1: it links in
   place of light_sh.cpp, so LightMain can tell the user to rebuild before loading
   anything (design decision 13).

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#include "q3map2.h"

const bool shBakeAvailable = false;

void ShBakeWorld(){
	/* LightMain stops on -shbake before this can be reached */
	Error( "-shbake: this q3map2 was built without SH_BAKER=1; rebuild it with `make binaries-q3map2 SH_BAKER=1`" );
}

void ShWriteSideFile(){
	Error( "-shbake: this q3map2 was built without SH_BAKER=1; rebuild it with `make binaries-q3map2 SH_BAKER=1`" );
}
