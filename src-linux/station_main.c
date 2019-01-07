// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#if defined(CFG_linux)

extern int sys_main (int argc, char** argv);

int main (int argc, char** argv) {
    return sys_main(argc, argv);
}

#endif // defined(CFG_linux)
