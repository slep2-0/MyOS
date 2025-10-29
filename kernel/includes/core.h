#ifndef MATANEL_CORE_H
#define MATANEL_CORE_H

/* minimal shared types to avoid circular includes */

typedef enum _IRQL {
	PASSIVE_LEVEL = 0,
	DISPATCH_LEVEL = 2,
	PROFILE_LEVEL = 27,
	CLOCK_LEVEL = 28,
	IPI_LEVEL = 29,
	POWER_LEVEL = 30,
	HIGH_LEVEL = 31
} IRQL, * PIRQL;

typedef struct _SINGLE_LINKED_LIST {
	struct _SINGLE_LINKED_LIST* Next;
} SINGLE_LINKED_LIST, * PSINGLE_LINKED_LIST;

typedef struct _DOUBLY_LINKED_LIST {
	struct _DOUBLY_LINKED_LIST* Blink;
	struct _DOUBLY_LINKED_LIST* Flink;
} DOUBLY_LINKED_LIST, * PDOUBLY_LINKED_LIST;

/* forward-declare thread struct and public typedef */
struct _ITHREAD;
typedef struct _ITHREAD ITHREAD;
typedef ITHREAD* PITHREAD;

/* forward-declare processor if needed */
struct _PROCESSOR;
typedef struct _PROCESSOR PROCESSOR;
typedef PROCESSOR* PPROCESSOR;

#endif // MATANEL_CORE_H
