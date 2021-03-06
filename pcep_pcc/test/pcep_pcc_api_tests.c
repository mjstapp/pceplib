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


#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
#include <CUnit/TestDB.h>


extern void pcep_pcc_api_test_setup();
extern void pcep_pcc_api_test_teardown();
extern void test_initialize_pcc();
extern void test_connect_pce();
extern void test_connect_pce_ipv6();
extern void test_connect_pce_with_src_ip();
extern void test_disconnect_pce();
extern void test_send_message();
extern void test_event_queue();
extern void test_get_event_type_str();

int main(int argc, char **argv)
{
    CU_initialize_registry();

    /*
     * Tests defined in pcep_socket_comm_test.c
     */
    CU_pSuite test_pcc_api_suite = CU_add_suite_with_setup_and_teardown(
            "PCEP PCC API Test Suite",
            NULL, NULL, // suite setup and cleanup function pointers
            pcep_pcc_api_test_setup,      // test case setup function pointer
            pcep_pcc_api_test_teardown);  // test case teardown function pointer

    CU_add_test(test_pcc_api_suite, "test_initialize_pcc", test_initialize_pcc);
    CU_add_test(test_pcc_api_suite, "test_connect_pce", test_connect_pce);
    CU_add_test(test_pcc_api_suite, "test_connect_pce_ipv6", test_connect_pce_ipv6);
    CU_add_test(test_pcc_api_suite, "test_connect_pce_with_src_ip", test_connect_pce_with_src_ip);
    CU_add_test(test_pcc_api_suite, "test_disconnect_pce", test_disconnect_pce);
    CU_add_test(test_pcc_api_suite, "test_send_message", test_send_message);
    CU_add_test(test_pcc_api_suite, "test_event_queue", test_event_queue);
    CU_add_test(test_pcc_api_suite, "test_get_event_type_str", test_get_event_type_str);

    /*
     * Run the tests and cleanup.
     */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_pRunSummary run_summary = CU_get_run_summary();
    int result = run_summary->nTestsFailed;
    CU_cleanup_registry();

    return result;
}
