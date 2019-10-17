/*
 * pcep_session_logic.h
 *
 *  Created on: sep 20, 2019
 *      Author: brady
 */

#ifndef INCLUDE_PCEPSESSIONLOGIC_H_
#define INCLUDE_PCEPSESSIONLOGIC_H_

#include <stdbool.h>

#include "pcep_socket_comm.h"
#include "pcep-objects.h"


typedef struct pcep_configuration_
{
    int keep_alive_seconds;
    int dead_timer_seconds;
    int request_time_seconds;
    int max_unknown_requests;
    int max_unknown_messages;

} pcep_configuration;

/* The format of a PCReq message is as follows:
       <PCReq message>::= <Common header>
                          [<svec-list>]
                          <request-list>

   where:
      <svec-list>::=<SVEC>[<svec-list>]
      <request-list>::=<request>[<request-list>]

      <request>::= <RP>
                   <END-POINTS>
                   [<LSPA>] label switched path attrs
                   [<BANDWIDTH>]
                   [<metric-list>]
                   [<RRO>[<BANDWIDTH>]]
                   [<IRO>]
                   [<LOAD-BALANCING>]

   where:
      <metric-list>::=<METRIC>[<metric-list>]
 */
/* path computation request */
typedef struct pcep_pce_request_
{
    /* RP flags - mandatory field
     * RP request_id is created internally */
    bool rp_flag_reoptimization;
    bool rp_flag_bidirectional;
    bool rp_flag_loose_path;
    char rp_flag_priority; /* 3 bits, values from 0 - 7 */

    /* endpoints - mandatory field
     * ip_version must be either IPPROTO_IP (for IPv4) or IPPROTO_IPV6,
     * defined in netinet/in.h */
    int endpoint_ipVersion;
    union src_endpoint_ip_ {
        struct in_addr srcV4Endpoint_ip;
        struct in6_addr srcV6Endpoint_ip;
    } src_endpoint_ip;
    union dst_endpoint_ip_ {
        struct in_addr dstV4Endpoint_ip;
        struct in6_addr dstV6Endpoint_ip;
    } dst_endpoint_ip;

    /*
     * The rest of these fields are optional
     */

    /* Populate with pcep_obj_create_bandwidth() */
    struct pcep_object_bandwidth *bandwidth;

    /* Label Switch Path Attributes
     * populate with pcep_obj_create_lspa() */
    struct pcep_object_lspa *lspa;

    /* Contiguous group of metrics
     * populate with pcep_obj_create_metric() */
    struct pcep_object_metric *metrics;

    /* Reported Route Object
     * Populate with pcep_obj_create_rro()
     * TODO not supported yet, need to implement */
    struct pcep_object_eros_list *rro_list;

    /* Include Route Object
     * Populate with pcep_obj_create_iro()
     * TODO not supported yet, need to implement */
    struct pcep_object_eros_list *iro_list;

    /* Populate with pcep_obj_create_load_balancing() */
    struct pcep_object_load_balancing *load_balancing;

    /* if path_count > 1, use svec: synchronization vector
    int path_count; */

} pcep_pce_request;


typedef enum pcep_session_state_
{
    SESSION_STATE_UNKNOWN = 0,
    SESSION_STATE_INITIALIZED = 1,
    SESSION_STATE_TCP_CONNECTED = 2,
    SESSION_STATE_OPENED = 3,
    SESSION_STATE_WAIT_PCREQ = 4,
    SESSION_STATE_IDLE = 5

} pcep_session_state;


typedef struct pcep_session_
{
    int session_id;
    pcep_session_state session_state;
    int timer_idOpen_keep_wait;
    int timer_idPc_req_wait;
    int timer_idDead_timer;
    int timer_idKeep_alive;
    bool pcep_open_received;
    int num_erroneous_messages;
    pcep_socket_comm_session *socket_comm_session;
    /* Configuration sent from the PCC to the PCE */
    pcep_configuration pcc_config;
    /* Configuration received from the PCE, to be used in the PCC */
    pcep_configuration pce_config;

} pcep_session;


typedef enum pcep_message_response_status_
{
    RESPONSE_STATE_UNKNOWN = 0,
    RESPONSE_STATE_WAITING = 1,
    RESPONSE_STATE_READY = 2,
    RESPONSE_STATE_TIMED_OUT = 3,
    RESPONSE_STATE_ERROR = 4

} pcep_message_response_status;

/* currently used when pcReq messages are sent to wait for pcRep responses */
typedef struct pcep_message_response_
{
    int request_id;
    pcep_message_response_status prev_response_status;
    pcep_message_response_status response_status;
    struct timespec time_request_registered;
    struct timespec time_response_received;
    int max_wait_time_milli_seconds;
    pcep_session *session;
    struct pcep_messages_list *response_msg_list;
    bool response_condition;
    pthread_mutex_t response_mutex;
    pthread_cond_t response_cond_var;

} pcep_message_response;


bool run_session_logic();

bool run_session_logic_wait_for_completion();

bool stop_session_logic();

pcep_session *create_pcep_session(pcep_configuration *config, struct in_addr *pce_ip, short port);

void destroy_pcep_session(pcep_session *session);

/* Register a Request Message request_id as having been sent, and internally
 * store the details. when and if a Reply is received with the request_id,
 * then the pcep_message_response object will be updated. intended to be used in
 * conjunction with either query_response_message() or wait_for_response_message()
 * returns a pointer to the registered pcep_message_response object.  */
pcep_message_response *register_response_message(
        pcep_session *session, int request_id, unsigned int max_wait_time_milli_seconds);

/* Destroy a previously registered pcep_message_response object */
void destroy_response_message(pcep_message_response *response);

pcep_message_response *get_registered_response_message(int request_id);

/* Query if a message Response is available
 * if one is available, the supplied pcep_message_response will be updated.
 * modification and querying of the msg_response is thread safe.
 * returns true if the message response is available or if there is a
 * change in the pcep_message_response status, false otherwise */
bool query_response_message(pcep_message_response *msg_response);

/* Wait for a message response until the response is available, or
 * until the pcep_message_response->max_wait_time_milli_seconds is reached.
 * returns true if a response was received, false otherwise. */
bool wait_for_response_message(pcep_message_response *msg_response);

#endif /* INCLUDE_PCEPSESSIONLOGIC_H_ */