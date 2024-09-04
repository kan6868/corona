#!/bin/sh

path=`dirname $0`

CORONA_PROJECT_DIR="$path"/../../test/assets2
OUTPUT_HTML=build/corona_test.html

"$path"/build_app.sh "$CORONA_PROJECT_DIR" "$OUTPUT_HTML" $1

# # -----------------------------------------------------------------------------

# # 
# # Canonicalize relative paths to absolute paths
# # 
# pushd $path > /dev/null
# dir=`pwd`
# path=$dir
# popd > /dev/null

# # -----------------------------------------------------------------------------

# #
# # Prints usage
# # 
# usage() {
# 	echo ""
# 	echo "USAGE: $0 "
# 	exit -1
# }

# #
# # Checks exit value for error
# # 
# checkError() {
# 	if [ $? -ne 0 ]
# 	then
# 		echo "Exiting due to errors (above)"
# 		exit -1
# 	fi
# }

# # -----------------------------------------------------------------------------

# echo "Using following settings:"

# # Detect for defaults
# if [ -e ~/.emscripten ]
# then
# 	export $(grep EMSCRIPTEN_ROOT ~/.emscripten | tr -d \')
# 	if [ -d "$EMSCRIPTEN_ROOT" ]
# 	then
# 		EMSDK=$EMSCRIPTEN_ROOT
# 	fi
# fi

# if [ -z "$EMSDK" ]
# then
# 	# Fallback to symlink
# 	EMSDK=$path/emsdk

# 	if [ ! -d "$EMSDK" ]
# 	then
# 		echo "\t ERROR: The symlink ($path/emsdk) does not point to a directory. You should point it to the version of Emscripten you are using, e.g. it should contain 'emcc' among others."
# 		exit -1
# 	fi
# fi
# echo "\t Emscripten path = '$EMSDK'"

# if [ -z "$CONFIG" ]
# then
# 	CONFIG=Debug
# fi
# echo "\t Configuration = '$CONFIG'"

# if [ -z "$OUTPUT_HTML" ]
# then
# 	OUTPUT_HTML=corona_test.html
# fi
# echo "\t Output = '$OUTPUT_HTML'"

# # -----------------------------------------------------------------------------

# pushd $path > /dev/null

# 	echo " "
# 	echo "Building Corona libraries:"

# 	echo '\t' make CC="$EMSDK"/emcc CXX="$EMSDK"/em++ verbose=1 config="$CONFIG"
# 	make CC="$EMSDK"/emcc CXX="$EMSDK"/em++ verbose=1 config="$CONFIG"
# 	checkError

# 	echo " "
# 	echo "Building HTML:"
# 	echo '\t' "$EMSDK"/emcc obj/"$CONFIG"/libcorona.o --use-preload-plugins --preload-file "$path"/../../test/assets2@/ -o "$OUTPUT_HTML"
# 	"$EMSDK"/emcc obj/"$CONFIG"/libcorona.o --use-preload-plugins --preload-file "$path"/../../test/assets2@/ -o "$OUTPUT_HTML"
# 	checkError

# 	echo "SUCCESS! Run with command:"
# 	echo '\t' $EMSDK/emrun $OUTPUT_HTML

# popd $path > /dev/null
