/*
 * This file is part of the PCEPlib, a PCEP protocol library.
 *
 * Copyright (C) 2020 Volta Networks https://voltanet.io/
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author : Brady Johnson <brady@voltanet.io>
 *
 */

#include <stddef.h>
#include <string.h>

#include "pcep_utils_double_linked_list.h"
#include "pcep_utils_logging.h"
#include "pcep_utils_memory.h"

double_linked_list *dll_initialize()
{
    double_linked_list *handle = pceplib_malloc(PCEPLIB_INFRA, sizeof(double_linked_list));
    if (handle != NULL)
    {
        memset(handle, 0, sizeof(double_linked_list));
        handle->num_entries = 0;
        handle->head = NULL;
        handle->tail = NULL;
    }
    else
    {
        pcep_log(LOG_WARNING, "dll_initialize cannot allocate memory for handle");
        return NULL;
    }

    return handle;
}


void dll_destroy(double_linked_list *handle)
{
    if (handle == NULL)
    {
        pcep_log(LOG_WARNING, "dll_destroy cannot destroy NULL handle");
        return;
    }

    double_linked_list_node *node = handle->head;
    while(node != NULL)
    {
        double_linked_list_node *node_to_delete = node;
        node = node->next_node;
        pceplib_free(PCEPLIB_INFRA, node_to_delete);
    }

    pceplib_free(PCEPLIB_INFRA, handle);
}


void dll_destroy_with_data_memtype(double_linked_list *handle, void *data_memory_type)
{
    if (handle == NULL)
    {
        pcep_log(LOG_WARNING, "dll_destroy_with_data cannot destroy NULL handle");
        return;
    }

    double_linked_list_node *node = handle->head;
    while(node != NULL)
    {
        double_linked_list_node *node_to_delete = node;
        pceplib_free(data_memory_type, node->data);
        node = node->next_node;
        pceplib_free(PCEPLIB_INFRA, node_to_delete);
    }

    pceplib_free(PCEPLIB_INFRA, handle);
}


void dll_destroy_with_data(double_linked_list *handle)
{
    /* Default to destroying the data with the INFRA mem type */
    dll_destroy_with_data_memtype(handle, PCEPLIB_INFRA);
}


/* Creates a node and adds it as the first item in the list */
double_linked_list_node *dll_prepend(double_linked_list *handle, void *data)
{
    if (handle == NULL)
    {
        pcep_log(LOG_WARNING, "dll_prepend_data NULL handle");
        return NULL;
    }

    /* Create the new node */
    double_linked_list_node *new_node = pceplib_malloc(PCEPLIB_INFRA, sizeof(double_linked_list_node));
    memset(new_node, 0, sizeof(double_linked_list_node));
    new_node->data = data;

    if (handle->head == NULL)
    {
        handle->head = new_node;
        handle->tail = new_node;
    }
    else
    {
        new_node->next_node = handle->head;
        handle->head->prev_node = new_node;
        handle->head = new_node;
    }

    (handle->num_entries)++;

    return new_node;
}


/* Creates a node and adds it as the last item in the list */
double_linked_list_node *dll_append(double_linked_list *handle, void *data)
{
    if (handle == NULL)
    {
        pcep_log(LOG_WARNING, "dll_append_data NULL handle");
        return NULL;
    }

    /* Create the new node */
    double_linked_list_node *new_node = pceplib_malloc(PCEPLIB_INFRA, sizeof(double_linked_list_node));
    memset(new_node, 0, sizeof(double_linked_list_node));
    new_node->data = data;

    if (handle->head == NULL)
    {
        handle->head = new_node;
        handle->tail = new_node;
    }
    else
    {
        new_node->prev_node = handle->tail;
        handle->tail->next_node = new_node;
        handle->tail = new_node;
    }

    (handle->num_entries)++;

    return new_node;
}


/* Delete the first node in the list, and return the data */
void *dll_delete_first_node(double_linked_list *handle)
{
    if (handle == NULL)
    {
        pcep_log(LOG_WARNING, "dll_delete_first_node NULL handle");
        return NULL;
    }

    if (handle->head == NULL)
    {
        return NULL;
    }

    double_linked_list_node *delete_node = handle->head;
    void *data = delete_node->data;

    if (delete_node->next_node == NULL)
    {
        /* Its the last node in the list */
        handle->head = NULL;
        handle->tail = NULL;
    }
    else
    {
        handle->head = delete_node->next_node;
        handle->head->prev_node = NULL;
    }

    pceplib_free(PCEPLIB_INFRA, delete_node);
    (handle->num_entries)--;

    return data;
}


/* Delete the last node in the list, and return the data */
void *dll_delete_last_node(double_linked_list *handle)
{
    if (handle == NULL)
    {
        pcep_log(LOG_WARNING, "dll_delete_last_node NULL handle");
        return NULL;
    }

    if (handle->head == NULL)
    {
        return NULL;
    }

    double_linked_list_node *delete_node = handle->tail;
    void *data = delete_node->data;

    if (delete_node->prev_node == NULL)
    {
        /* Its the last node in the list */
        handle->head = NULL;
        handle->tail = NULL;
    }
    else
    {
        handle->tail = delete_node->prev_node;
        handle->tail->next_node = NULL;
    }

    pceplib_free(PCEPLIB_INFRA, delete_node);
    (handle->num_entries)--;

    return data;
}


/* Delete the designated node in the list, and return the data */
void *dll_delete_node(double_linked_list *handle, double_linked_list_node *node)
{
    if (handle == NULL)
    {
        pcep_log(LOG_WARNING, "dll_delete_node NULL handle");
        return NULL;
    }

    if (node == NULL)
    {
        return NULL;
    }

    if (handle->head == NULL)
    {
        return NULL;
    }

    void *data = node->data;

    if (handle->head == handle->tail)
    {
        /* Its the last node in the list */
        handle->head = NULL;
        handle->tail = NULL;
    }
    else if (handle->head == node)
    {
        handle->head = node->next_node;
        handle->head->prev_node = NULL;
    }
    else if (handle->tail == node)
    {
        handle->tail = node->prev_node;
        handle->tail->next_node = NULL;
    }
    else
    {
        /* Its somewhere in the middle of the list */
        node->next_node->prev_node = node->prev_node;
        node->prev_node->next_node = node->next_node;
    }

    pceplib_free(PCEPLIB_INFRA, node);
    (handle->num_entries)--;

    return data;
}
