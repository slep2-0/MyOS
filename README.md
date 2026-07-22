![DEVELOPMENT](https://img.shields.io/badge/Status-DEVELOPMENT,_UNSTABLE,_NOT_USABLE-darkred?style=for-the-badge)

**64 BIT LONG MODE - UEFI**

**Kernel might (should, i only commit bootable changes) boot here, might not, changes are made here, then are merged to master**

## Windows setup

Run `initial_setup.bat` once after cloning. It configures the complete Windows
build and QEMU environment. Then open `KernelDevelopment.sln` and build the x64
Debug or Release configuration.

Building the solution before setup fails with a message directing you to
`initial_setup.bat`.
