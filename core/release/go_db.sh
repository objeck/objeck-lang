cd deploy/bin
./obc -src ../../../compiler/lib_src/db.obs -lib collect.obl -tar lib -dest ../lib/db.obl
./obc -src ../../../../programs/db/0_test.obs -lib db.obl,collect.obl -dest ../../test_db.obe
./obr ../../test_db.obe
cd ../..