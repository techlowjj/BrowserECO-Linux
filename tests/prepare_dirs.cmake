# Recree les dossiers de travail des tests avant chaque execution de la suite.
#
# Chaque test doit demarrer SANS settings.txt / cache.db / history.db : sinon il
# herite des reglages ecrits par le test precedent (ou par une execution
# precedente) et ses assertions deviennent fausses.
#
# Usage: cmake -DTESTDIRS="dir1;dir2" -P prepare_dirs.cmake

foreach(dir IN LISTS TESTDIRS)
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    file(MAKE_DIRECTORY "${dir}/shots")
endforeach()