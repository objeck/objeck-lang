#!/bin/bash

OBJECK_ROOT="../../../objeck-lang"

cd "$(dirname "$0")"

export PATH="$PATH:$OBJECK_ROOT/core/release/deploy-x64/bin"
export OBJECK_LIB_PATH="$OBJECK_ROOT/core/release/deploy-x64/lib"

rm -f formatter_regression.obe

echo "Building formatter regression test..."
obc -src regression_test.obs,formatter.obs,scanner.obs -lib gen_collect -dest formatter_regression
if [ $? -ne 0 ]; then
	echo "Build failed"
	exit 1
fi

echo "Running formatter regression test..."
echo "---"
obr formatter_regression
