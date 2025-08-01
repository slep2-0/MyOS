/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Variadic functions compiler helpers.
 */
#ifndef __STDARG_H
#define __STDARG_H
#ifndef _MSC_VER
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#else
#define va_arg
#endif

#endif
#pragma once
