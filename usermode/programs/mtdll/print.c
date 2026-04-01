
#ifndef _MSC_VER
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#else
#define va_arg(ap, type) ((type)0)
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "includes/mtdll.h"

// Helper: Safely place a character into the buffer
static inline void put_char(char** buf, size_t* left, char c) {
    if (*left > 1) { // Leave room for null terminator
        **buf = c;
        (*buf)++;
        (*left)--;
    }
}

// Helper: Safely place a string into the buffer
static void put_string(char** buf, size_t* left, const char* str) {
    if (!str) str = "(null)";
    while (*str) {
        put_char(buf, left, *str++);
    }
}

// Helper: Convert number to string and place in buffer
static void put_number(char** buf, size_t* left, uint64_t val, int base, int is_signed) {
    char temp[65]; // Large enough for 64-bit binary/hex/dec
    char* t = temp + sizeof(temp) - 1;
    const char* digits = "0123456789abcdef";
    *t = '\0';

    if (is_signed && (int64_t)val < 0) {
        put_char(buf, left, '-');
        val = (uint64_t)(-(int64_t)val);
    }

    if (val == 0) {
        *--t = '0';
    }
    else {
        while (val) {
            *--t = digits[val % base];
            val /= base;
        }
    }
    put_string(buf, left, t);
}

// The core formatting engine
int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    if (!str || size == 0 || !format) return 0;

    char* buf = str;
    size_t left = size;

    while (*format && left > 1) {
        if (*format == '%') {
            format++;

            // Handle length modifiers (e.g., %lld, %lx)
            int is_long = 0;
            while (*format == 'l') {
                is_long++;
                format++;
            }

            switch (*format) {
            case 'd':
                if (is_long >= 2) put_number(&buf, &left, va_arg(ap, long long), 10, 1);
                else put_number(&buf, &left, (int64_t)va_arg(ap, int), 10, 1);
                break;
            case 'u':
                if (is_long >= 2) put_number(&buf, &left, va_arg(ap, unsigned long long), 10, 0);
                else put_number(&buf, &left, (uint64_t)va_arg(ap, unsigned int), 10, 0);
                break;
            case 'x':
                if (is_long >= 2) put_number(&buf, &left, va_arg(ap, unsigned long long), 16, 0);
                else put_number(&buf, &left, (uint64_t)va_arg(ap, unsigned int), 16, 0);
                break;
            case 'p':
                put_string(&buf, &left, "0x");
                put_number(&buf, &left, (uint64_t)(uintptr_t)va_arg(ap, void*), 16, 0);
                break;
            case 'c':
                put_char(&buf, &left, (char)va_arg(ap, int));
                break;
            case 's':
                put_string(&buf, &left, va_arg(ap, const char*));
                break;
            case '%':
                put_char(&buf, &left, '%');
                break;
            default:
                // If we don't recognize it, just print it raw
                put_char(&buf, &left, '%');
                put_char(&buf, &left, *format);
                break;
            }
        }
        else {
            put_char(&buf, &left, *format);
        }
        format++;
    }

    *buf = '\0'; // Guarantee null termination

    // Note: A truly POSIX-compliant snprintf returns the number of characters 
    // that *would* have been written if the buffer was infinitely large. 
    // For early OS dev, returning the actual written amount is usually fine.
    return (int)(buf - str);
}

// The variadic wrapper you actually call
int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

void printf(uint32_t Color, const char* fmt, ...) {
    char buffer[256];

    va_list ap;
    va_start(ap, fmt);
    // Format the string locally in user space
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    
    MtPrintConsole(
        Color,
        (const char*)buffer
    );
}