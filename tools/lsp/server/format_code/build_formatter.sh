rm *.obe
obc -src test.obs,formatter.obs,scanner.obs -lib gen_collect -dest code_formatter
# deploy
if [ ! -z "$1" ]; then
	obr code_formatter "$1"
fi;
