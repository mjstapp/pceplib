/*
 * This file is part of the libpcep, a PCEP library.
 *
 * Copyright (C) 2011 Acreo AB http://www.acreo.se
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
 * Author : Viktor Nordell <viktor.nordell@acreo.se>
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "pcep-tools.h"

static struct pcep_obj_list*
pcep_obj_parse(struct pcep_object_header* hdr)
{
    struct pcep_obj_list *item = NULL;
    struct pcep_object_header *obj = NULL; 
    
    item = malloc(sizeof(struct pcep_obj_list));
    
    bzero(item, sizeof(struct pcep_obj_list));        
    
    switch(hdr->object_class) {
        case PCEP_OBJ_CLASS_OPEN:
            pcep_unpack_obj_open((struct pcep_object_open*) hdr);
            break;
        case PCEP_OBJ_CLASS_RP:
            pcep_unpack_obj_rp((struct pcep_object_rp*) hdr);
            break;
        case PCEP_OBJ_CLASS_NOPATH:
            pcep_unpack_obj_nopath((struct pcep_object_nopath*) hdr);
            break;
        case PCEP_OBJ_CLASS_ENDPOINTS:
            if(hdr->object_type == PCEP_OBJ_TYPE_ENDPOINT_IPV4) {
                pcep_unpack_obj_ep_ipv4((struct pcep_object_endpoints_ipv4*) hdr);
            } else if(hdr->object_type == PCEP_OBJ_TYPE_ENDPOINT_IPV6) {
                pcep_unpack_obj_ep_ipv6((struct pcep_object_endpoints_ipv6*) hdr);
            }
            break;
        case PCEP_OBJ_CLASS_BANDWIDTH:
            pcep_unpack_obj_bandwidth((struct pcep_object_bandwidth*) hdr);
            break;
        case PCEP_OBJ_CLASS_METRIC:
            pcep_unpack_obj_metic((struct pcep_object_metric*) hdr);
            break;
        case PCEP_OBJ_CLASS_ERO:
            pcep_unpack_obj_ero((struct pcep_object_ero*) hdr);
            break;        
        case PCEP_OBJ_CLASS_LSPA:
            pcep_unpack_obj_lspa((struct pcep_object_lspa*) hdr);
            break;        
        case PCEP_OBJ_CLASS_SVEC:
            pcep_unpack_obj_svec((struct pcep_object_svec*) hdr);
            break;        
        case PCEP_OBJ_CLASS_ERROR:
            pcep_unpack_obj_error((struct pcep_object_error*) hdr);
            break;
        case PCEP_OBJ_CLASS_CLOSE:
            pcep_unpack_obj_close((struct pcep_object_close*) hdr);
            break;
        case PCEP_OBJ_CLASS_RRO:
        case PCEP_OBJ_CLASS_IRO:
        case PCEP_OBJ_CLASS_NOTF:
        default:
            fprintf(stderr, "WARNING pcep_obj_parse: Unknown object class\n");
            return NULL;
    }
        
    obj = malloc(hdr->object_length);
    
    bzero(obj, hdr->object_length);          
    memcpy(obj, hdr, hdr->object_length);
    
    item->header = obj;
    
    return item;
}

struct pcep_messages_list*     
pcep_msg_read(int sock_fd)
{
    int ret;
    int err_count = 0;
    uint8_t buffer[PCEP_MAX_SIZE];
    uint16_t buffer_read = 0;
    struct pcep_header *msg_hdr;
    struct pcep_messages_list *head = NULL, *item = NULL;
    
    bzero(&buffer, PCEP_MAX_SIZE);
    
    ret = read(sock_fd, &buffer, PCEP_MAX_SIZE);
    
    if(ret < 0) {
        perror("WARNING pcep_msg_read");
        fprintf(stderr, "WARNING pcep_msg_read: Failed to read from socket\n");
    } else if(ret == 0) {
        fprintf(stderr, "WARNING pcep_msg_read: Remote shutdown\n");
    }
        
    while((ret - buffer_read) >= sizeof(struct pcep_header)) {
        
        uint16_t obj_read = sizeof(struct pcep_header);
        
        msg_hdr = (struct pcep_header*) &buffer[buffer_read];
        
        pcep_unpack_msg_header(msg_hdr);
        
        if((ret - buffer_read) < msg_hdr->length) {
            int read_len = (msg_hdr->length - (ret - buffer_read));
            int read_ret = 0;
            fprintf(stderr, "WARNING pcep_msg_read: Message not fully read! Trying to read %d bytes more\n", read_len);
            
            read_ret = read(sock_fd, &buffer[ret], read_len);
            
            if(read_ret != read_len) {
                fprintf(stderr, "WARNING pcep_msg_read: Did not manage to read enough data (%d != %d)\n", read_ret, read_len);
                return head;
            }
        }
        
        buffer_read += msg_hdr->length;
        
        item = (struct pcep_messages_list*) malloc(sizeof(struct pcep_messages_list));
        
        bzero(item, sizeof(struct pcep_messages_list));
        
        memcpy(&item->header, msg_hdr, sizeof(struct pcep_header));                
        
        while((msg_hdr->length - obj_read) > sizeof(struct pcep_object_header)) {    
            struct pcep_obj_list* obj_item;
            struct pcep_object_header* obj_hdr = (struct pcep_object_header*) (((uint8_t*)msg_hdr) + obj_read);
            
            pcep_unpack_obj_header(obj_hdr);                
            
            obj_item = pcep_obj_parse(obj_hdr);
            
            if(obj_item != NULL) {            
                DL_APPEND(item->list, obj_item);                
            } else {
                err_count++;
            }
            
            obj_read += obj_hdr->object_length;
            
            if(err_count > 5) break;
        };

        DL_APPEND(head, item);
    }
    
    return head;
}

struct pcep_messages_list* 
pcep_msg_get(struct pcep_messages_list* list, uint8_t type)
{
    struct pcep_messages_list *item;
    
    DL_FOREACH(list, item) {  
        if(item->header.type == type) {
            return item;
        }
    }
    
    return NULL;
}

struct pcep_messages_list* 
pcep_msg_get_next(struct pcep_messages_list* current, uint8_t type)
{
    struct pcep_messages_list *item;
    
    DL_FOREACH(current, item) {  
        if(item == current) continue;
        if(item->header.type == type) {
            return item;
        }
    }
    
    return NULL;
}

struct pcep_obj_list* 
pcep_obj_get(struct pcep_messages_list* item, uint8_t type)
{
    struct pcep_obj_list *obj_item;    
    
    DL_FOREACH(item->list, obj_item) {         
        if(obj_item->header->object_class == type) {
            return obj_item;
        }
    }
    
    return NULL;
}

struct pcep_obj_list* 
pcep_obj_get_next(struct pcep_obj_list* current, uint8_t type)
{
    struct pcep_obj_list *obj_item;    
    
    DL_FOREACH(current->next, obj_item) {         
        if(obj_item->header->object_class == type) {
            return obj_item;
        }
    }
    
    return NULL;
}

void
pcep_msg_free(struct pcep_messages_list* list)
{
    struct pcep_messages_list *item, *item_tmp;
    struct pcep_obj_list *obj_item, *obj_tmp;
        
    DL_FOREACH_SAFE(list, item, item_tmp) {           
        DL_FOREACH_SAFE(item->list, obj_item, obj_tmp) {               
            DL_DELETE(item->list, obj_item);
            free(obj_item->header);
            free(obj_item);
        }
        
        DL_DELETE(list, item);
        free(item);
    }      
}

void
pcep_msg_print(struct pcep_messages_list* list)
{
    struct pcep_messages_list *item;
    struct pcep_obj_list *obj_item;
    
    DL_FOREACH(list, item) {   
        switch(item->header.type) {
            case PCEP_TYPE_OPEN:
                printf("PCEP_TYPE_OPEN\n");
                break;
            case PCEP_TYPE_KEEPALIVE:
                printf("PCEP_TYPE_KEEPALIVE\n");
                break;
            case PCEP_TYPE_PCREQ:
                printf("PCEP_TYPE_PCREQ\n");
                break;
            case PCEP_TYPE_PCREP:
                printf("PCEP_TYPE_PCREP\n");
                break;
            case PCEP_TYPE_PCNOTF:
                printf("PCEP_TYPE_PCNOTF\n");
                break;
            case PCEP_TYPE_ERROR:
                printf("PCEP_TYPE_ERROR\n");
                break;
            case PCEP_TYPE_CLOSE:  
                printf("PCEP_TYPE_CLOSE\n");
                break;
            default:
                printf("UNKOWN\n");
                continue;
        } 
        
        DL_FOREACH(item->list, obj_item) {   
            printf("\t");
            switch(obj_item->header->object_class) {
                case PCEP_OBJ_CLASS_OPEN:
                    printf("PCEP_OBJ_CLASS_OPEN\n");
                    break;
                case PCEP_OBJ_CLASS_RP:
                    printf("PCEP_OBJ_CLASS_RP\n");               
                    break;
                case PCEP_OBJ_CLASS_NOPATH:
                    printf("PCEP_OBJ_CLASS_NOPATH\n");
                    break;
                case PCEP_OBJ_CLASS_ENDPOINTS:
                    if(obj_item->header->object_type == PCEP_OBJ_TYPE_ENDPOINT_IPV4) {
                        printf("PCEP_OBJ_CLASS_ENDPOINTS IPv4\n");
                    } else if(obj_item->header->object_type == PCEP_OBJ_TYPE_ENDPOINT_IPV6) {
                        printf("PCEP_OBJ_CLASS_ENDPOINTS IPv6\n");
                    }
                    break;
                case PCEP_OBJ_CLASS_BANDWIDTH:
                    printf("PCEP_OBJ_CLASS_BANDWIDTH\n");
                    break;
                case PCEP_OBJ_CLASS_METRIC:
                    printf("PCEP_OBJ_CLASS_METRIC\n");
                    break;
                case PCEP_OBJ_CLASS_ERO:
                    printf("PCEP_OBJ_CLASS_ERO\n");
                    break;        
                case PCEP_OBJ_CLASS_LSPA:
                    printf("PCEP_OBJ_CLASS_LSPA\n");
                    break;        
                case PCEP_OBJ_CLASS_SVEC:
                    printf("PCEP_OBJ_CLASS_SVEC\n");
                    break;        
                case PCEP_OBJ_CLASS_ERROR:
                    printf("PCEP_OBJ_CLASS_ERROR\n");
                    break;
                case PCEP_OBJ_CLASS_CLOSE:
                    printf("PCEP_OBJ_CLASS_CLOSE\n");
                    break;
                case PCEP_OBJ_CLASS_RRO:
                case PCEP_OBJ_CLASS_IRO:
                case PCEP_OBJ_CLASS_NOTF:
                default:
                    printf("UNSUPPORTED CLASS\n");
                    break;
            }        
        }
    }   
}

int
pcep_msg_send(int sock_fd, struct pcep_header* hdr)
{
    if(hdr == NULL) return 0;
    
    return write(sock_fd, hdr, ntohs(hdr->length));
}