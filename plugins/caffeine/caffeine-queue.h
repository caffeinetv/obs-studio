#pragma once

#include <obs-module.h>

#define BUFFER_SIZE 600

/**
 * @file
 * @brief header for implementing queue.
 *
 */

typedef struct caffeine_queue_ {
	uint64_t data[BUFFER_SIZE];
	int size;
	int start_index;
	int end_index;
} caffeine_queue;

/** Function to initialize queue */
void caffeine_queue_init(caffeine_queue *queue);

/** Function to insert element to front of queue */
void caffeine_enqueue(caffeine_queue *queue, uint64_t element);

/** Function to check if queue is full */
bool caffeine_queue_is_full(caffeine_queue *queue);

/** Function to check if queue is empty */
bool caffeine_queue_is_empty(caffeine_queue *queue);

/** Function to get size of queue */
int caffeine_queue_size(caffeine_queue *queue);

/** Function to delete first element of the queue */
void caffeine_dequeue(caffeine_queue *queue);

/** Function to get first element of the queue */
uint64_t caffeine_queue_get_first_element(caffeine_queue *queue);
