/*++

Module Name:

    string.c

Purpose:

    This translation unit contains the standard library functions involving string operations.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/exports.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char*)s;
        }
        s++;
    }
    return NULL;
}

char* strncat(char* dest, const char* src, size_t max_len) {
    if (!dest || !src || max_len == 0) return dest;

    // Move dest_ptr to the end of the current string
    size_t dest_len = 0;
    while (dest_len < max_len && dest[dest_len] != '\0') {
        dest_len++;
    }

    if (dest_len == max_len) {
        // dest is already full, cannot append
        return dest;
    }

    size_t i = 0;
    while (dest_len + i < max_len - 1 && src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i++;
    }

    // Null-terminate
    dest[dest_len + i] = '\0';
    return dest;
}

//-----------------------------------------------------------------------------
// strlen: Return length of string (excluding null terminator).
//-----------------------------------------------------------------------------
size_t strlen(const char* str) {
    size_t len = 0;
    while (str && str[len] != '\0') {
        len++;
    }
    return len;
}

//-----------------------------------------------------------------------------
// strcpy: Copy string from src to dst. Assumes dst is large enough.
//-----------------------------------------------------------------------------
char* strcpy(char* dst, const char* src) {
    char* ret = dst;
    while ((*dst++ = *src++)) {
        // copy until null terminator
    }
    return ret;
}

//-----------------------------------------------------------------------------
// strncpy: Copy up to n characters from src to dst.
//           Assumes dst is large enough.
//-----------------------------------------------------------------------------
char* strncpy(char* dst, const char* src, size_t n) {
    if (n == 0) return dst;
    size_t i = 0;
    while (i + 1 < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return dst;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2) return (int)((unsigned char)*s1 - (unsigned char)*s2);
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

int strncmp(const char* s1, const char* s2, size_t length) {
    if (!length) return length;
    for (size_t i = 0; i < length; i++, s1++, s2++) {
        if (*s1 != *s2) return (int)((unsigned char)*s1 - (unsigned char)*s2);
        if (*s1 == '\0') return 0;
    }
    return 0;
}