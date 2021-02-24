#pragma once

#include <obs-module.h>
#include "caffeine-queue.h"

#define USIGNED_MAX 18446744073709551615ull

void caffeine_queue_init(caffeine_queue *queue)
{
	memset(queue, 0, sizeof(caffeine_queue));
	queue->start_index = 0;
	queue->end_index = -1;
	queue->size = 0;
}

void caffeine_enqueue(caffeine_queue *queue, uint64_t element)
{
	if (caffeine_queue_is_full(queue)) {
		// Queue is full so return
		return;
	} else {
		if (queue->end_index == BUFFER_SIZE - 1) {
			queue->end_index = -1;
		}
		queue->end_index++;
		queue->data[queue->end_index] = element;
		queue->size++;
	}
}

bool caffeine_queue_is_full(caffeine_queue *queue)
{
	return queue->size == BUFFER_SIZE;
}

bool caffeine_queue_is_empty(caffeine_queue *queue)
{
	return queue->size == 0;
}

int caffeine_queue_size(caffeine_queue *queue)
{
	return queue->size;
}

// Removes the frame from beginining
void caffeine_dequeue(caffeine_queue *queue)
{
	if (queue->start_index == BUFFER_SIZE) {
		queue->start_index = 0;
	} else {
		queue->start_index++;
	}
	queue->size--;
}

uint64_t caffeine_queue_get_first_element(caffeine_queue *queue)
{
	return queue->data[queue->start_index];
}
