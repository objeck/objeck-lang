# This script runs tkdiff to compare the wxstedit bakefiles to another
# component's to make it easy to update others once the wxstedit ones are
# working properly.

# Usage : $tkdiff_bakefiles.sh [path to other build dir] [main bakefile name]
# $tkdiff_bakefiles.sh ../../wxplotctrl/build wxplotctrl.bkl

p=$1

tkdiff Bakefiles.bkgen $p/Bakefiles.bkgen
tkdiff configure.ac    $p/configure.ac
tkdiff config.sub      $p/config.sub
tkdiff config.guess    $p/config.guess
tkdiff install.sh      $p/install.sh
tkdiff acregen.sh      $p/acregen.sh

tkdiff wxstedit.bkl    $p/$2
