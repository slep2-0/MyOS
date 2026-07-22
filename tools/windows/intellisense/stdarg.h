#ifndef __STDARG_H
#define __STDARG_H

/* Parsing stubs only. The LLVM build supplies the real ABI implementation. */
typedef char* va_list;

#define va_start(arguments, last_named) \
    ((void)(last_named), (arguments) = (char*)0)
#define va_end(arguments) ((void)(arguments))
#define va_arg(ap, type) ((type)0)
#define va_copy(destination, source) ((destination) = (source))

#endif
