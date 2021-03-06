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


/*
 *  Implementation of public API functions.
 */


#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netdb.h> // gethostbyname
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>  // close

#include <arpa/inet.h>  // sockets etc.
#include <sys/types.h>  // sockets etc.
#include <sys/socket.h> // sockets etc.

#include "pcep_socket_comm.h"
#include "pcep_socket_comm_internals.h"
#include "pcep_utils_logging.h"
#include "pcep_utils_ordered_list.h"
#include "pcep_utils_queue.h"


pcep_socket_comm_handle *socket_comm_handle_ = NULL;


/* simple compare method callback used by pcep_utils_ordered_list
 * for ordered list insertion. */
int socket_fd_node_compare(void *list_entry, void *new_entry)
{
    return ((pcep_socket_comm_session *) new_entry)->socket_fd - ((pcep_socket_comm_session *) list_entry)->socket_fd;
}


bool initialize_socket_comm_loop()
{
    if (socket_comm_handle_ != NULL)
    {
        /* already initialized */
        return true;
    }

    socket_comm_handle_ = malloc(sizeof(pcep_socket_comm_handle));
    bzero(socket_comm_handle_, sizeof(pcep_socket_comm_handle));

    socket_comm_handle_->active = true;
    socket_comm_handle_->num_active_sessions = 0;
    socket_comm_handle_->read_list = ordered_list_initialize(socket_fd_node_compare);
    socket_comm_handle_->write_list = ordered_list_initialize(socket_fd_node_compare);
    socket_comm_handle_->session_list = ordered_list_initialize(pointer_compare_function);

    if (pthread_mutex_init(&(socket_comm_handle_->socket_comm_mutex), NULL) != 0)
    {
        pcep_log(LOG_ERR, "Cannot initialize socket_comm mutex.");
        return false;
    }

    if(pthread_create(&(socket_comm_handle_->socket_comm_thread), NULL, socket_comm_loop, socket_comm_handle_))
    {
        pcep_log(LOG_ERR, "Cannot initialize socket_comm thread.");
        return false;
    }

    return true;
}


bool destroy_socket_comm_loop()
{
    socket_comm_handle_->active = false;

    pthread_join(socket_comm_handle_->socket_comm_thread, NULL);
    ordered_list_destroy(socket_comm_handle_->read_list);
    ordered_list_destroy(socket_comm_handle_->write_list);
    ordered_list_destroy(socket_comm_handle_->session_list);
    pthread_mutex_destroy(&(socket_comm_handle_->socket_comm_mutex));

    free(socket_comm_handle_);
    socket_comm_handle_ = NULL;

    return true;
}

/* Internal common init function */
static pcep_socket_comm_session *
socket_comm_session_initialize_pre(message_received_handler message_handler,
                            message_ready_to_read_handler message_ready_handler,
                            message_sent_notifier msg_sent_notifier,
                            connection_except_notifier notifier,
                            uint32_t connect_timeout_millis,
                            void *session_data)
{
    /* check that not both message handlers were set */
    if (message_handler != NULL && message_ready_handler != NULL)
    {
        pcep_log(LOG_WARNING, "Only one of <message_received_handler | message_ready_to_read_handler> can be set.");
        return NULL;
    }

    /* check that at least one message handler was set */
    if (message_handler == NULL && message_ready_handler == NULL)
    {
        pcep_log(LOG_WARNING, "At least one of <message_received_handler | message_ready_to_read_handler> must be set.");
        return NULL;
    }

    if (!initialize_socket_comm_loop())
    {
        pcep_log(LOG_WARNING, "ERROR: cannot initialize socket_comm_loop.");

        return NULL;
    }

    /* initialize everything for a pcep_session socket_comm */

    pcep_socket_comm_session *socket_comm_session = malloc(sizeof(pcep_socket_comm_session));
    bzero(socket_comm_session, sizeof(pcep_socket_comm_session));

    socket_comm_session->socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_comm_session->socket_fd == -1) {
        pcep_log(LOG_WARNING, "Cannot create socket errno [%d %s].", errno, strerror(errno));
        socket_comm_session_teardown(socket_comm_session);//socket_comm_session freed inside fn so NOLINT next.

        return NULL;//NOLINT(clang-analyzer-unix.Malloc)
    }

    socket_comm_handle_->num_active_sessions++;
    socket_comm_session->close_after_write = false;
    socket_comm_session->session_data = session_data;
    socket_comm_session->message_handler = message_handler;
    socket_comm_session->message_ready_to_read_handler = message_ready_handler;
    socket_comm_session->message_sent_handler = msg_sent_notifier;
    socket_comm_session->conn_except_notifier = notifier;
    socket_comm_session->message_queue = queue_initialize();
    socket_comm_session->connect_timeout_millis = connect_timeout_millis;

    return socket_comm_session;
}

/* Internal common init function */
bool socket_comm_session_initialize_post(pcep_socket_comm_session *socket_comm_session)
{
    /* If we dont use SO_REUSEADDR, the socket will take 2 TIME_WAIT
     * periods before being closed in the kernel if bind() was called */
    int reuse_addr = 1;
    if (setsockopt(socket_comm_session->socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(int)) < 0)
    {
        pcep_log(LOG_WARNING, "Error in setsockopt() SO_REUSEADDR errno [%d %s].",
                errno, strerror(errno));
        socket_comm_session_teardown(socket_comm_session);

        return false;
    }

    struct sockaddr *src_sock_addr = (socket_comm_session->is_ipv6 ?
            (struct sockaddr *) &(socket_comm_session->src_sock_addr.src_sock_addr_ipv6) :
            (struct sockaddr *) &(socket_comm_session->src_sock_addr.src_sock_addr_ipv4));
    int addr_len = (socket_comm_session->is_ipv6 ?
         sizeof(socket_comm_session->src_sock_addr.src_sock_addr_ipv6) :
         sizeof(socket_comm_session->src_sock_addr.src_sock_addr_ipv4));
    if (bind(socket_comm_session->socket_fd, src_sock_addr, addr_len) == -1)
    {
        pcep_log(LOG_WARNING, "Cannot bind address to socket errno [%d %s].",
                errno, strerror(errno));
        socket_comm_session_teardown(socket_comm_session);

        return false;
    }

    /* Register the session as active with the Socket Comm Loop */
    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    ordered_list_add_node(socket_comm_handle_->session_list, socket_comm_session);
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    /* dont connect to the destination yet, since the PCE will have a timer
     * for max time between TCP connect and PCEP open. we'll connect later
     * when we send the PCEP open. */

    return true;
}


pcep_socket_comm_session *
socket_comm_session_initialize(message_received_handler message_handler,
                            message_ready_to_read_handler message_ready_handler,
                            message_sent_notifier msg_sent_notifier,
                            connection_except_notifier notifier,
                            struct in_addr *dest_ip,
                            short dest_port,
                            uint32_t connect_timeout_millis,
                            void *session_data)
{
    return socket_comm_session_initialize_with_src(
            message_handler, message_ready_handler, msg_sent_notifier, notifier,
            NULL, 0, dest_ip, dest_port, connect_timeout_millis, session_data);
}

pcep_socket_comm_session *
socket_comm_session_initialize_ipv6(message_received_handler message_handler,
                            message_ready_to_read_handler message_ready_handler,
                            message_sent_notifier msg_sent_notifier,
                            connection_except_notifier notifier,
                            struct in6_addr *dest_ip,
                            short dest_port,
                            uint32_t connect_timeout_millis,
                            void *session_data)
{
    return socket_comm_session_initialize_with_src_ipv6(
            message_handler, message_ready_handler, msg_sent_notifier, notifier,
            NULL, 0, dest_ip, dest_port, connect_timeout_millis, session_data);
}


pcep_socket_comm_session *
socket_comm_session_initialize_with_src(message_received_handler message_handler,
                            message_ready_to_read_handler message_ready_handler,
                            message_sent_notifier msg_sent_notifier,
                            connection_except_notifier notifier,
                            struct in_addr *src_ip,
                            short src_port,
                            struct in_addr *dest_ip,
                            short dest_port,
                            uint32_t connect_timeout_millis,
                            void *session_data)
{
    if (dest_ip == NULL)
    {
        pcep_log(LOG_WARNING, "dest_ipv4 is NULL");
        return NULL;
    }

    pcep_socket_comm_session *socket_comm_session =
            socket_comm_session_initialize_pre(message_handler,
                    message_ready_handler,
                    msg_sent_notifier,
                    notifier,
                    connect_timeout_millis,
                    session_data);
    if (socket_comm_session == NULL)
    {
        return NULL;
    }

    socket_comm_session->socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_comm_session->socket_fd == -1) {
        pcep_log(LOG_WARNING, "Cannot create ipv4 socket errno [%d %s].", errno, strerror(errno));
        socket_comm_session_teardown(socket_comm_session);//socket_comm_session freed inside fn so NOLINT next.

        return NULL;//NOLINT(clang-analyzer-unix.Malloc)
    }

    socket_comm_session->is_ipv6 = false;
    socket_comm_session->dest_sock_addr.dest_sock_addr_ipv4.sin_family = AF_INET;
    socket_comm_session->src_sock_addr.src_sock_addr_ipv4.sin_family = AF_INET;
    socket_comm_session->dest_sock_addr.dest_sock_addr_ipv4.sin_port = htons(dest_port);
    socket_comm_session->src_sock_addr.src_sock_addr_ipv4.sin_port = htons(src_port);
    socket_comm_session->dest_sock_addr.dest_sock_addr_ipv4.sin_addr.s_addr = dest_ip->s_addr;
    if (src_ip != NULL)
    {
        socket_comm_session->src_sock_addr.src_sock_addr_ipv4.sin_addr.s_addr = src_ip->s_addr;
    }
    else
    {
        socket_comm_session->src_sock_addr.src_sock_addr_ipv4.sin_addr.s_addr = INADDR_ANY;
    }

    if (socket_comm_session_initialize_post(socket_comm_session) == false)
    {
        return NULL;
    }

    return socket_comm_session;
}

pcep_socket_comm_session *
socket_comm_session_initialize_with_src_ipv6(message_received_handler message_handler,
                            message_ready_to_read_handler message_ready_handler,
                            message_sent_notifier msg_sent_notifier,
                            connection_except_notifier notifier,
                            struct in6_addr *src_ip,
                            short src_port,
                            struct in6_addr *dest_ip,
                            short dest_port,
                            uint32_t connect_timeout_millis,
                            void *session_data)
{
    if (dest_ip == NULL)
    {
        pcep_log(LOG_WARNING, "dest_ipv6 is NULL");
        return NULL;
    }

    pcep_socket_comm_session *socket_comm_session =
            socket_comm_session_initialize_pre(message_handler,
                    message_ready_handler,
                    msg_sent_notifier,
                    notifier,
                    connect_timeout_millis,
                    session_data);
    if (socket_comm_session == NULL)
    {
        return NULL;
    }

    socket_comm_session->socket_fd = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (socket_comm_session->socket_fd == -1) {
        pcep_log(LOG_WARNING, "Cannot create ipv6 socket errno [%d %s].", errno, strerror(errno));
        socket_comm_session_teardown(socket_comm_session);//socket_comm_session freed inside fn so NOLINT next.

        return NULL;//NOLINT(clang-analyzer-unix.Malloc)
    }

    socket_comm_session->is_ipv6 = true;
    socket_comm_session->dest_sock_addr.dest_sock_addr_ipv6.sin6_family = AF_INET6;
    socket_comm_session->src_sock_addr.src_sock_addr_ipv6.sin6_family = AF_INET6;
    socket_comm_session->dest_sock_addr.dest_sock_addr_ipv6.sin6_port = htons(dest_port);
    socket_comm_session->src_sock_addr.src_sock_addr_ipv6.sin6_port = htons(src_port);
    memcpy(&socket_comm_session->dest_sock_addr.dest_sock_addr_ipv6.sin6_addr, dest_ip, sizeof(struct in6_addr));
    if (src_ip != NULL)
    {
        memcpy(&socket_comm_session->src_sock_addr.src_sock_addr_ipv6.sin6_addr, src_ip, sizeof(struct in6_addr));
    }
    else
    {
        socket_comm_session->src_sock_addr.src_sock_addr_ipv6.sin6_addr = in6addr_any;
    }

    if (socket_comm_session_initialize_post(socket_comm_session) == false)
    {
        return NULL;
    }

    return socket_comm_session;
}


bool socket_comm_session_connect_tcp(pcep_socket_comm_session *socket_comm_session)
{
    if (socket_comm_session == NULL)
    {
        pcep_log(LOG_WARNING, "socket_comm_session_connect_tcp NULL socket_comm_session.");
        return NULL;
    }

    /* Set the socket to non-blocking, so connect() does not block */
    int fcntl_arg;
    if ((fcntl_arg = fcntl(socket_comm_session->socket_fd, F_GETFL, NULL)) < 0 )
    {
        pcep_log(LOG_WARNING, "Error fcntl(..., F_GETFL) [%d %s]", errno, strerror(errno));
        return false;
    }

    fcntl_arg |= O_NONBLOCK;
    if (fcntl(socket_comm_session->socket_fd, F_SETFL, fcntl_arg) < 0)
    {
        pcep_log(LOG_WARNING, "Error fcntl(..., F_SETFL) [%d %s]", errno, strerror(errno));
        return false;
    }

    int connect_result = 0;
    if (socket_comm_session->is_ipv6)
    {
        connect_result = connect(socket_comm_session->socket_fd,
                (struct sockaddr *) &(socket_comm_session->dest_sock_addr.dest_sock_addr_ipv6),
                sizeof(socket_comm_session->dest_sock_addr.dest_sock_addr_ipv6));
    }
    else
    {
        connect_result = connect(socket_comm_session->socket_fd,
                (struct sockaddr *) &(socket_comm_session->dest_sock_addr.dest_sock_addr_ipv4),
                sizeof(socket_comm_session->dest_sock_addr.dest_sock_addr_ipv4));
    }

    if (connect_result < 0)
    {
        if (errno == EINPROGRESS)
        {
            /* Calculate the configured timeout in seconds and microseconds */
            struct timeval tv;
            if (socket_comm_session->connect_timeout_millis > 1000)
            {
                tv.tv_sec = socket_comm_session->connect_timeout_millis / 1000;
                tv.tv_usec = (socket_comm_session->connect_timeout_millis - (tv.tv_sec * 1000)) * 1000;
            }
            else
            {
                tv.tv_sec = 0;
                tv.tv_usec = socket_comm_session->connect_timeout_millis * 1000;
            }

            /* Use select to wait a max timeout for connect
             * https://stackoverflow.com/questions/2597608/c-socket-connection-timeout */
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(socket_comm_session->socket_fd, &fdset);
            if (select(socket_comm_session->socket_fd + 1, NULL, &fdset, NULL, &tv) > 0)
            {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(socket_comm_session->socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error)
                {
                    pcep_log(LOG_WARNING, "TCP connect failed on socket_fd [%d].",
                            socket_comm_session->socket_fd);
                    return false;
                }
            }
            else
            {
                pcep_log(LOG_WARNING, "TCP connect timed-out on socket_fd [%d].",
                        socket_comm_session->socket_fd);
                return false;
            }
        }
        else
        {
            pcep_log(LOG_WARNING, "TCP connect, error connecting on socket_fd [%d] errno [%d %s]",
                    socket_comm_session->socket_fd, errno, strerror(errno));
            return false;
        }
    }

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    /* once the TCP connection is open, we should be ready to read at any time */
    ordered_list_add_node(socket_comm_handle_->read_list, socket_comm_session);
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    return true;
}


bool socket_comm_session_close_tcp(pcep_socket_comm_session *socket_comm_session)
{
    if (socket_comm_session == NULL)
    {
        pcep_log(LOG_WARNING, "socket_comm_session_close_tcp NULL socket_comm_session.");
        return false;
    }

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    ordered_list_remove_first_node_equals(socket_comm_handle_->read_list, socket_comm_session);
    ordered_list_remove_first_node_equals(socket_comm_handle_->write_list, socket_comm_session);
    // TODO should it be close() or shutdown()??
    close(socket_comm_session->socket_fd);
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    return true;
}


bool socket_comm_session_close_tcp_after_write(pcep_socket_comm_session *socket_comm_session)
{
    if (socket_comm_session == NULL)
    {
        pcep_log(LOG_WARNING, "socket_comm_session_close_tcp_after_write NULL socket_comm_session.");
        return false;
    }

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    socket_comm_session->close_after_write = true;
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    return true;
}


bool socket_comm_session_teardown(pcep_socket_comm_session *socket_comm_session)
{
    if (socket_comm_handle_ == NULL)
    {
        pcep_log(LOG_WARNING, "Cannot teardown NULL socket_comm_handle");
        return false;
    }

    if (socket_comm_session == NULL)
    {
        pcep_log(LOG_WARNING, "Cannot teardown NULL session");
        return false;
    }

    if (socket_comm_session->socket_fd > 0)
    {
        shutdown(socket_comm_session->socket_fd, SHUT_RDWR);
        close(socket_comm_session->socket_fd);
    }

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    queue_destroy(socket_comm_session->message_queue);
    ordered_list_remove_first_node_equals(socket_comm_handle_->session_list, socket_comm_session);
    ordered_list_remove_first_node_equals(socket_comm_handle_->read_list, socket_comm_session);
    ordered_list_remove_first_node_equals(socket_comm_handle_->write_list, socket_comm_session);
    socket_comm_handle_->num_active_sessions--;
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    pcep_log(LOG_INFO, "[%ld-%ld] socket_comm_session [%d] destroyed, [%d] sessions remaining",
            time(NULL), pthread_self(),
            socket_comm_session->socket_fd,
            socket_comm_handle_->num_active_sessions);

    free(socket_comm_session);

    /* It would be nice to call destroy_socket_comm_loop() here if
     * socket_comm_handle_->num_active_sessions == 0, but this function
     * will usually be called from the message_sent_notifier callback,
     * which gets called in the middle of the socket_comm_loop, and that
     * is dangerous, so destroy_socket_comm_loop() must be called upon
     * application exit. */

    return true;
}


void socket_comm_session_send_message(pcep_socket_comm_session *socket_comm_session,
                                      char *message,
                                      unsigned int msg_length,
                                      bool free_after_send)
{
    if (socket_comm_session == NULL)
    {
        pcep_log(LOG_WARNING, "socket_comm_session_send_message NULL socket_comm_session.");
        return;
    }

    pcep_socket_comm_queued_message *queued_message = malloc(sizeof(pcep_socket_comm_queued_message));
    queued_message->unmarshalled_message = message;
    queued_message->msg_length = msg_length;
    queued_message->free_after_send = free_after_send;

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    queue_enqueue(socket_comm_session->message_queue, queued_message);
    ordered_list_add_node(socket_comm_handle_->write_list, socket_comm_session);
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));
}
