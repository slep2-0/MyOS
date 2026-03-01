int main(void) { return 1; }

// MTDLL Should not have a main execution point, it is a helper DLL only. (no attaching).
// It supplies the system calls and initialization interfaces for processes and threads.
// In windows, NTDLL.DLL EntryPoint is set to 0 (nullptr)
// I should incorporate this in the linker script, oh well!