

export PATH=$PATH:/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/bin
export OBJECK_LIB_PATH=/Users/randyhollines/Documents/Code/objeck-lang/core/release/deploy/lib

obc -src ../../core/compiler/lib_src/net.obs,../../core/compiler/lib_src/net_common.obs,../../core/compiler/lib_src/net_secure.obs -tar lib -lib json -dest ../../core/lib/net.obl

if [ ! -z "$1" ]; then
	obc -src $1 -lib net,json
	obr $1 $2 $3 $4 $5
fi
