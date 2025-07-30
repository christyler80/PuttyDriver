To compile putty.exe for PuttyDriver, download Putty version 0.83 source code from https://www.chiark.greenend.org.uk/~sgtatham/putty/releases/0.83.html.

Overwrite with the following Putty source files, with the 'PuttyDriver' versions in theses folders:

    terminal\puttydriver.c
    terminal\terminal.c

    windows\window.c

    CMakeLists.txt
    cmdline.c
    ldisc.c
    putty.h

Follow the standard Putty build instructions in the README file which accompanies the downloaded Putty source code. 

See 'PuttyDriver - Getting Started.pdf' for PuttyDriver setup and configuration instructions.

Notes:

   - this version of Putty is works on Microsoft Windows 10 or later. Other OS are not currently supported.
   - the *.83 files included are the original Putty files and retained for reference.

