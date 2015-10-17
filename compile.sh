upcc main.c htable/htable_lp.c nodecache.c localstore.c varchain.c bdd.c cache.c wstealer/wstealer.c -opt -O -nopthreads -network=$1 -o distbdd -I ./htable -I ./wstealer -I ./ -Wl,-Wl,-lpthread
