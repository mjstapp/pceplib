/*
 * pcep_utils_queue.h
 *
 *  Created on: Sep 19, 2019
 *      Author: brady
 */

#ifndef INCLUDE_PCEPUTILSQUEUE_H_
#define INCLUDE_PCEPUTILSQUEUE_H_

typedef struct queue_node_
{
    struct queue_node_ *next_node;
    void *data;

} queue_node;

typedef struct queue_handle_
{
    queue_node *head;
    queue_node *tail;
    unsigned int num_entries;
    /* Set to 0 to disable */
    unsigned int max_entries;

} queue_handle;

queue_handle *queue_initialize();
queue_handle *queue_initialize_with_size(unsigned int max_entries);
void queue_destroy(queue_handle *handle);
queue_node *queue_enqueue(queue_handle *handle, void *data);
void *queue_dequeue(queue_handle *handle);

#endif /* INCLUDE_PCEPUTILSQUEUE_H_ */
