__attribute__((section(".text.mtapi"), visibility("default"), used))
void*
__tls_get_addr(
    void* TlsIndex
)
{
    return TlsIndex;
}
