#pragma once

typedef struct _SINGLE_LINKED_LIST {
    struct _SINGLE_LINKED_LIST* Next;
} SINGLE_LINKED_LIST, * PSINGLE_LINKED_LIST;

typedef struct _DOUBLY_LINKED_LIST {
    struct _DOUBLY_LINKED_LIST* Blink;
    struct _DOUBLY_LINKED_LIST* Flink;
} DOUBLY_LINKED_LIST, * PDOUBLY_LINKED_LIST;

struct _ITHREAD;
typedef struct _ITHREAD ITHREAD;
typedef ITHREAD* PITHREAD;

struct _IPROCESS;
typedef struct _IPROCESS IPROCESS;
typedef IPROCESS* PIPROCESS;

struct _ETHREAD;
typedef struct _ETHREAD ETHREAD;
typedef ETHREAD* PETHREAD;

struct _PROCESSOR;
typedef struct _PROCESSOR PROCESSOR;
typedef PROCESSOR* PPROCESSOR;

struct _EPROCESS;
typedef struct _EPROCESS EPROCESS;
typedef EPROCESS* PEPROCESS;

struct _TRAP_FRAME;
typedef struct _TRAP_FRAME TRAP_FRAME;
typedef TRAP_FRAME* PTRAP_FRAME;