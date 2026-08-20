#ifndef MATANELOS_INTELLISENSE_STDATOMIC_H
#define MATANELOS_INTELLISENSE_STDATOMIC_H

typedef enum memory_order {
    memory_order_relaxed = 0,
    memory_order_consume = 1,
    memory_order_acquire = 2,
    memory_order_release = 3,
    memory_order_acq_rel = 4,
    memory_order_seq_cst = 5
} memory_order;

#define ATOMIC_VAR_INIT(value) (value)
#define atomic_init(object, value) (*(object) = (value))
#define atomic_thread_fence(order) ((void)(order))
#define atomic_signal_fence(order) ((void)(order))

#endif
