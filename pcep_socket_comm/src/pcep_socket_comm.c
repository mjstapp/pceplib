/*
 * pcep_socket_comm.c
 *
 *  Created on: sep 17, 2019
 *      Author: brady
 *
 *  Implementation of public API functions.
 */


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
#include "pcep_utils_ordered_list.h"
#include "pcep_utils_queue.h"
#include "pcep_socket_comm_internals.h"


pcep_socket_comm_handle *socket_comm_handle_ = NULL;


/* simple compare method callback used by pcep_utils_ordered_list
 * for ordered list insertion. */
int socket_fdNode_compare(void *list_entry, void *new_entry)
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
    socket_comm_handle_->read_list = ordered_list_initialize(socket_fdNode_compare);
    socket_comm_handle_->write_list = ordered_list_initialize(socket_fdNode_compare);

    if (pthread_mutex_init(&(socket_comm_handle_->socket_comm_mutex), NULL) != 0)
    {
        fprintf(stderr, "ERROR: cannot initialize socket_comm mutex.\n");
        return false;
    }

    if(pthread_create(&(socket_comm_handle_->socket_comm_thread), NULL, socket_comm_loop, socket_comm_handle_))
    {
        fprintf(stderr, "ERROR: cannot initialize socket_comm thread.\n");
        return false;
    }

    return true;
}


pcep_socket_comm_session *
socket_comm_session_initialize(message_received_handler message_handler,
                            message_ready_toRead_handler message_ready_handler,
                            connection_except_notifier notifier,
                            struct in_addr *host_ip,
                            short port,
                            void *session_data)
{
    /* check that not both message handlers were set */
    if (message_handler != NULL && message_ready_handler != NULL)
    {
        fprintf(stderr, "Only one of <message_received_handler | message_ready_toRead_handler> can be set.\n");
    }

    /* check that at least one message handler was set */
    if (message_handler == NULL && message_ready_handler == NULL)
    {
        fprintf(stderr, "At least one of <message_received_handler | message_ready_toRead_handler> must be set.\n");
    }

    if (!initialize_socket_comm_loop())
    {
        fprintf(stderr, "ERROR: cannot initialize socket_comm_loop.\n");

        return NULL;
    }

    /* initialize everything for a pcep_session socket_comm */

    pcep_socket_comm_session *socket_comm_session = malloc(sizeof(pcep_socket_comm_session));
    bzero(socket_comm_session, sizeof(pcep_socket_comm_session));

    socket_comm_session->socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_comm_session->socket_fd == -1) {
        fprintf(stderr, "ERROR: cannot create socket.\n");
        socket_comm_session_teardown(socket_comm_session);

        return NULL;
    }

    socket_comm_session->close_after_write = false;
    socket_comm_session->session_data = session_data;
    socket_comm_session->message_handler = message_handler;
    socket_comm_session->message_ready_toRead_handler = message_ready_handler;
    socket_comm_session->conn_except_notifier = notifier;
    socket_comm_session->message_queue = queue_initialize();
    socket_comm_session->dest_sock_addr.sin_family = AF_INET;
    socket_comm_session->dest_sock_addr.sin_port = htons(port);
    memcpy(&(socket_comm_session->dest_sock_addr.sin_addr), host_ip, sizeof(struct in_addr));

    /* dont connect to the destination yet, since the PCE will have a timer
     * for max time between TCP connect and PCEP open. we'll connect later
     * when we send the PCEP open. */

    return socket_comm_session;
}


bool socket_comm_session_connect_tcp(pcep_socket_comm_session *socket_comm_session)
{
    int retval = connect(socket_comm_session->socket_fd,
                         (struct sockaddr *) &(socket_comm_session->dest_sock_addr),
                         sizeof(struct sockaddr));

    if (retval == -1) {
        fprintf(stderr, "ERROR: TCP connect failed on socket_fd [%d].\n",
                socket_comm_session->socket_fd);
        socket_comm_session_teardown(socket_comm_session);

        return false;
    }

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    /* once the TCP connection is open, we should be ready to read at any time */
    ordered_list_add_node(socket_comm_handle_->read_list, socket_comm_session);
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    return true;
}


bool socket_comm_session_close_tcp(pcep_socket_comm_session *socket_comm_session)
{
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
    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    socket_comm_session->close_after_write = true;
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    return true;
}

bool socket_comm_session_teardown(pcep_socket_comm_session *socket_comm_session)
{
    /* TODO when should we teardown the socket_comm_handle_ ??
     *      should we keep a pcep_socket_comm_session ref counter and free it when
     *      the ref count reaches 0? */

    if (socket_comm_session->socket_fd > 0)
    {
        shutdown(socket_comm_session->socket_fd, SHUT_RDWR);
        close(socket_comm_session->socket_fd);
    }

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    ordered_list_remove_first_node_equals(socket_comm_handle_->read_list, socket_comm_session);
    ordered_list_remove_first_node_equals(socket_comm_handle_->write_list, socket_comm_session);
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));

    free(socket_comm_session);

    return false;
}


void socket_comm_session_send_message(pcep_socket_comm_session *socket_comm_session, const char *message, unsigned int msg_length)
{
    pcep_socket_comm_queued_message *queued_message = malloc(sizeof(pcep_socket_comm_queued_message));
    queued_message->unmarshalled_message = message;
    queued_message->msg_length = msg_length;

    pthread_mutex_lock(&(socket_comm_handle_->socket_comm_mutex));
    queue_enqueue(socket_comm_session->message_queue, queued_message);
    ordered_list_add_node(socket_comm_handle_->write_list, socket_comm_session);
    pthread_mutex_unlock(&(socket_comm_handle_->socket_comm_mutex));
}
