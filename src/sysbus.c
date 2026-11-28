/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * system bus - Physical-memory access for Verilated DMA simulation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include "sysbus.h"

// Maximum number of memory banks supported
#define MAX_MEMORY_BANKS 8

// Memory bank structure
typedef struct {
    uint8_t* data;
    size_t size;
    int initialized;
    int owns_data;
} memory_bank_t_struct;

// Array of memory banks
static memory_bank_t_struct g_banks[MAX_MEMORY_BANKS] = {0};
static int g_bank_count = 0;
static atomic_uint_fast64_t g_dma_completion_count = 0;
static atomic_int g_dma_irq_pending = 0;
static pthread_mutex_t g_dma_irq_event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_dma_irq_event_cond = PTHREAD_COND_INITIALIZER;
static int g_dma_irq_event_ready = 0;
static int g_dma_irq_event_stopping = 0;

// Initialize a memory bank with given size in bytes
memory_bank_t memory_bank_init(size_t size_bytes) {
    // Find first free slot
    int free_slot = -1;
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (!g_banks[i].initialized) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        printf("Maximum number of memory banks (%d) reached\n", MAX_MEMORY_BANKS);
        return MEMORY_BANK_INVALID;
    }

    if (size_bytes == 0) {
        printf("Invalid bank size: 0\n");
        return MEMORY_BANK_INVALID;
    }

    uint8_t* data = (uint8_t*)malloc(size_bytes);
    if (!data) {
        printf("Failed to allocate memory bank of %zu bytes\n", size_bytes);
        return MEMORY_BANK_INVALID;
    }

    memset(data, 0, size_bytes);
    g_banks[free_slot].data = data;
    g_banks[free_slot].size = size_bytes;
    g_banks[free_slot].initialized = 1;
    g_banks[free_slot].owns_data = 1;
    g_bank_count++;

    printf("Memory bank [%d] initialized: %zu bytes at address %p\n", free_slot, size_bytes, (void*)data);
    return free_slot;
}

int memory_bank_bind(void *memory, size_t size_bytes) {
    if (memory == NULL || size_bytes == 0)
        return -1;

    if (g_banks[0].initialized && g_banks[0].owns_data)
        free(g_banks[0].data);
    if (!g_banks[0].initialized)
        g_bank_count++;

    g_banks[0].data = (uint8_t *)memory;
    g_banks[0].size = size_bytes;
    g_banks[0].initialized = 1;
    g_banks[0].owns_data = 0;
    return 0;
}

// Cleanup memory bank
void memory_bank_cleanup(void) {
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (g_banks[i].initialized) {
            if (g_banks[i].data && g_banks[i].owns_data) {
                free(g_banks[i].data);
            }
            g_banks[i].data = NULL;
            g_banks[i].size = 0;
            g_banks[i].initialized = 0;
            g_banks[i].owns_data = 0;
            g_bank_count--;
            printf("Memory bank [%d] cleaned up\n", i);
        }
    }
    atomic_store(&g_dma_irq_pending, 0);
    atomic_store(&g_dma_completion_count, 0);
}

int memory_bank_is_available(void) {
    // Check if at least one memory bank is initialized
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (g_banks[i].initialized) {
            return 1;
        }
    }
    return 0;
}

// Get pointer to locate data at offset
void* memory_bank_get_ptr(uint64_t offset) {
    if (!g_banks[0].initialized || offset >= g_banks[0].size) {
        return NULL;
    }

    return g_banks[0].data + offset;
}

int memory_bank_reserve(size_t size_bytes) {
    uint8_t* resized;
    size_t old_size;

    if (size_bytes == 0) {
        return -1;
    }
    if (!g_banks[0].initialized) {
        return memory_bank_init(size_bytes) == MEMORY_BANK_INVALID ? -1 : 0;
    }
    if (size_bytes <= g_banks[0].size) {
        return 0;
    }
    if (!g_banks[0].owns_data)
        return -1;

    old_size = g_banks[0].size;
    resized = (uint8_t*)realloc(g_banks[0].data, size_bytes);
    if (resized == NULL) {
        return -1;
    }
    memset(resized + old_size, 0, size_bytes - old_size);
    g_banks[0].data = resized;
    g_banks[0].size = size_bytes;
    return 0;
}

// Get bank size in bytes
size_t memory_bank_get_size() {
    return g_banks[0].initialized ? g_banks[0].size : 0;
}


#ifdef __cplusplus
extern "C" {
#endif

/**
 * DPI Callback: Called from RTL when DMA read access
 */
int dpi_bank_read32_legacy(long long offset) {
    uint32_t data;
    const uint8_t* bytes;
    void* src;

    if (offset < 0 || (uint64_t)offset > memory_bank_get_size() ||
        memory_bank_get_size() - (size_t)offset < sizeof(data))
        return 0;

    src = memory_bank_get_ptr((uint64_t)offset);
    if (src == NULL)
        return 0;
    /* Keep byte-lane order independent of the host's native endianness. */
    bytes = (const uint8_t*)src;
    data = (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
    return (int)data;
}

/**
 * DPI Callback: Called from RTL when DMA write access
 */
int dpi_bank_write32_legacy(long long offset, int value) {
    uint32_t data = (uint32_t)value;
    uint8_t* bytes;
    void* dst;

    if (offset < 0 || (uint64_t)offset > memory_bank_get_size() ||
        memory_bank_get_size() - (size_t)offset < sizeof(value))
        return -1;
    dst = memory_bank_get_ptr((uint64_t)offset);
    if (dst == NULL)
        return -1;
    bytes = (uint8_t*)dst;
    bytes[0] = (uint8_t)data;
    bytes[1] = (uint8_t)(data >> 8);
    bytes[2] = (uint8_t)(data >> 16);
    bytes[3] = (uint8_t)(data >> 24);
    return 0;
}


/**
 * DPI Callback: Called from RTL when DMA transfer completes
 */
void dma_done_callback(void)
{
    atomic_fetch_add(&g_dma_completion_count, 1);
    atomic_store(&g_dma_irq_pending, 1);
}

uint64_t dma_completion_count(void)
{
    return atomic_load(&g_dma_completion_count);
}

int dma_irq_is_pending(void)
{
    return atomic_load(&g_dma_irq_pending);
}

void dma_irq_ack(void)
{
    atomic_store(&g_dma_irq_pending, 0);
}

void dma_irq_notify(void)
{
    pthread_mutex_lock(&g_dma_irq_event_lock);
    g_dma_irq_event_ready = 1;
    pthread_cond_signal(&g_dma_irq_event_cond);
    pthread_mutex_unlock(&g_dma_irq_event_lock);
}

int dma_irq_wait(void)
{
    int should_continue;

    pthread_mutex_lock(&g_dma_irq_event_lock);
    while (!g_dma_irq_event_ready && !g_dma_irq_event_stopping)
        pthread_cond_wait(&g_dma_irq_event_cond, &g_dma_irq_event_lock);

    should_continue = g_dma_irq_event_ready;
    g_dma_irq_event_ready = 0;
    pthread_mutex_unlock(&g_dma_irq_event_lock);
    return should_continue;
}

void dma_irq_shutdown(void)
{
    pthread_mutex_lock(&g_dma_irq_event_lock);
    g_dma_irq_event_stopping = 1;
    pthread_cond_broadcast(&g_dma_irq_event_cond);
    pthread_mutex_unlock(&g_dma_irq_event_lock);
}

#ifdef __cplusplus
}
#endif

