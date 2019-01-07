#!/bin/bash

set -e
cd $(dirname $0)

LGW_VERSION=v5.0.1

if [[ ! -d git-repo ]]; then
    git clone -b $LGW_VERSION --single-branch --depth 1 https://github.com/Lora-net/lora_gateway.git git-repo
fi

if [[ -z "$platform" ]] || [[ -z "$variant" ]]; then
    echo "Expecting env vars platform/variant to be set - comes naturally if called from a makefile"
    echo "If calling manually try: variant=std platform=linux $0"
    exit 1
fi

if [[ ! -d platform-$platform ]]; then
    git clone git-repo platform-$platform

    cd platform-$platform
    if [ -f ../$LGW_VERSION-$platform.patch ]; then
        echo "Applying $LGW_VERSION-$platform.patch ..."
        git apply ../$LGW_VERSION-$platform.patch
    fi
fi
