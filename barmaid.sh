#!/usr/bin/env bash
# Barmaid is a command line tool to manipulate BTW files.
#
# Written and placed into the public domain by
# Elias Oenal <barmaid@eliasoenal.com>
#
# GNU sed and grep required.
# Execute "./barmaid.sh -e file_in.btw" to extract container and separate prefix.
# Execute "./barmaid.sh -c file_out.btw" to construct file_out.btw from fragments.

set -e
OS_TYPE=$(uname)


case "$OS_TYPE" in
    "Linux" )
    GREP=grep
    SED=sed
    ;;
    "Darwin" )
    GREP=grep
    SED=gsed
    ;;
    * )
    echo "OS entry needed at line ${LINENO} of this script."
    exit
esac


if [ "$1" == "-e" ] && [ -n "$2" ]; then
    echo "Extracting $2 into prefix.bin and container.bin"
    OFFSET=$(LANG=C $GREP -obUaP "\x49\x45\x4E\x44\xAE\x42\x60\x82\x00\x01" "$2" | LANG=C $SED -e "s/:.*//g")
    let "OFFSET+=10"
    echo "Found container at offset: $OFFSET"

    dd if="$2" bs=1 count=$OFFSET > prefix.bin 2> /dev/null
    dd if="$2" skip=$OFFSET bs=1 2> /dev/null | zlib-flate -uncompress > container.bin

    echo "You may now edit container.bin and use -c to reconstruct the BTW file once you're done."
    exit
fi

if [ "$1" == "-c" ] && [ -n "$2" ]; then
    echo "Constructing $2 from prefix.bin and container.bin"
    cp prefix.bin "$2"
    zlib-flate -compress < container.bin >> "$2"
    exit
fi

echo "Barmaid v0.1"
echo ""
echo "Usage: barmaid.sh [OPTIONS] [FILE]"
echo "Options:"
echo "      -e [FILE]       Extract prefix and container (fragments) from [FILE]"
echo "      -c [FILE]       Construct [FILE] from fragments"
echo ""

exit
