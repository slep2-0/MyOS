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

MTDLL_API
char* strchr(const char* s, int c)

/*++

    Routine description:

        Searches a string for its first occurrence of a character.

    Arguments:

        [IN] s - The null-terminated string to search.
        [IN] c - The character to find.

    Return Values:

        A pointer to the matching character, or NULL when no match exists.

--*/
{
    while (*s) {
        if (*s == (char)c) {
            return (char*)s;
        }
        s++;
    }
    return NULL;
}

MTDLL_API
char* strncat(char* dest, const char* src, size_t max_len)

/*++

    Routine description:

        Appends source characters to a destination string within the supplied
        destination length limit.

    Arguments:

        [IN OUT] dest - The destination string.
        [IN] src - The null-terminated source string.
        [IN] max_len - The destination bound including its terminator.

    Return Values:

        dest. If an input is invalid or max_len is zero, dest is returned
        without modification.

    Notes:

        The destination buffer must have room for the resulting string.

--*/
{
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

MTDLL_API
size_t strlen(const char* str)

/*++

    Routine description:

        Counts the characters in a string before its null terminator.

    Arguments:

        [IN] str - The string to measure. NULL is treated as an empty string.

    Return Values:

        The number of characters before the terminator.

--*/
{
    size_t len = 0;
    while (str && str[len] != '\0') {
        len++;
    }
    return len;
}

MTDLL_API
char* strcpy(char* dst, const char* src)

/*++

    Routine description:

        Copies a null-terminated string to a destination buffer.

    Arguments:

        [OUT] dst - The destination buffer.
        [IN] src - The source string.

    Return Values:

        dst.

    Notes:

        The destination buffer must be large enough for the source string and
        its terminator.

--*/
{
    char* ret = dst;
    while ((*dst++ = *src++)) {
        // copy until null terminator
    }
    return ret;
}

MTDLL_API
char* strncpy(char* dst, const char* src, size_t n)

/*++

    Routine description:

        Copies at most n - 1 source characters and always writes a terminator
        when n is nonzero.

    Arguments:

        [OUT] dst - The destination buffer.
        [IN] src - The source string.
        [IN] n - The destination length limit.

    Return Values:

        dst.

    Notes:

        The destination buffer must contain at least n bytes when n is
        nonzero.

--*/
{
    if (n == 0) return dst;
    size_t i = 0;
    while (i + 1 < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return dst;
}

MTDLL_API
int strcmp(const char* s1, const char* s2)

/*++

    Routine description:

        Compares two null-terminated strings lexicographically.

    Arguments:

        [IN] s1 - The first string.
        [IN] s2 - The second string.

    Return Values:

        A negative value when s1 precedes s2, zero when they match, or a
        positive value when s1 follows s2.

--*/
{
    while (*s1 && *s2) {
        if (*s1 != *s2) return (int)((unsigned char)*s1 - (unsigned char)*s2);
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

MTDLL_API
int strncmp(const char* s1, const char* s2, size_t length)

/*++

    Routine description:

        Compares at most length characters from two strings.

    Arguments:

        [IN] s1 - The first string.
        [IN] s2 - The second string.
        [IN] length - The maximum number of characters to compare.

    Return Values:

        A negative value when s1 precedes s2, zero when the compared strings
        match, or a positive value when s1 follows s2.

--*/
{
    if (!length) return length;
    for (size_t i = 0; i < length; i++, s1++, s2++) {
        if (*s1 != *s2) return (int)((unsigned char)*s1 - (unsigned char)*s2);
        if (*s1 == '\0') return 0;
    }
    return 0;
}

MTDLL_API
int isspace(int c)

/*++

    Routine description:

        Reports whether a character is ASCII whitespace.

    Arguments:

        [IN] c - Character to test, convert, or append.

    Return Values:

        A nonzero value for an ASCII whitespace character, or zero otherwise.

--*/

{
    return c == ' ' ||
        c == '\t' ||
        c == '\n' ||
        c == '\r' ||
        c == '\f' ||
        c == '\v';
}