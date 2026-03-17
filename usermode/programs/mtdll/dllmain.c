// MTDLL Should not have a main execution point, it is a helper DLL only. (no attaching).
// It supplies the system calls and initialization interfaces for processes and threads.
// So, address of entry point is a nullptr.