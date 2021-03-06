/* mbed Microcontroller Library
 * Copyright (c) 2013-2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "mbed.h"
#include "greentea-client/test_env.h"
#include "utest/utest.h"
#include "unity/unity.h"


using utest::v1::Case;

#define ONE_MILLI_SEC 1000
#define TICKER_COUNT 16
#define MULTI_TICKER_TIME_MS 100
volatile uint32_t callback_trigger_count = 0;
static const int test_timeout = 240;
static const int total_ticks = 10;


/* Tolerance is quite arbitrary due to large number of boards with varying level of accuracy */
#define TOLERANCE_US 1000

volatile uint32_t ticker_callback_flag;
volatile uint32_t multi_counter;

DigitalOut led1(LED1);
DigitalOut led2(LED2);

Ticker *volatile ticker1;
Ticker *volatile ticker2;
Timer gtimer;

volatile int ticker_count = 0;
volatile bool print_tick = false;

void ticker_callback_1_switch_to_2(void);
void ticker_callback_2_switch_to_1(void);

void increment_ticker_counter(void)
{
    ++callback_trigger_count;
}

void switch_led1_state(void)
{
    led1 = !led1;
}

void switch_led2_state(void)
{
    led2 = !led2;
}

void ticker_callback_1_switch_to_2(void)
{
    ++callback_trigger_count;
    // If ticker is NULL then it is being or has been deleted
    if (ticker1) {
        ticker1->detach();
        ticker1->attach_us(ticker_callback_2_switch_to_1, ONE_MILLI_SEC);
    }
    switch_led1_state();
}

void ticker_callback_2_switch_to_1(void)
{
    ++callback_trigger_count;
    // If ticker is NULL then it is being or has been deleted
    if (ticker2) {
        ticker2->detach();
        ticker2->attach_us(ticker_callback_1_switch_to_2, ONE_MILLI_SEC);
    }
    switch_led2_state();
}


void sem_release(Semaphore *sem)
{
    sem->release();
}


void stop_gtimer_set_flag(void)
{
    gtimer.stop();
    core_util_atomic_incr_u32((uint32_t*)&ticker_callback_flag, 1);
}

void increment_multi_counter(void)
{
    core_util_atomic_incr_u32((uint32_t*)&multi_counter, 1);
}


/* Tests is to measure the accuracy of Ticker over a period of time
 *
 * 1) DUT would start to update callback_trigger_count every milli sec, in 2x callback we use 2 tickers
 *    to update the count alternatively.
 * 2) Host would query what is current count base_time, Device responds by the callback_trigger_count
 * 3) Host after waiting for measurement stretch. It will query for device time again final_time.
 * 4) Host computes the drift considering base_time, final_time, transport delay and measurement stretch
 * 5) Finally host send the results back to device pass/fail based on tolerance.
 * 6) More details on tests can be found in timing_drift_auto.py
 */
void test_case_1x_ticker()
{
    char _key[11] = { };
    char _value[128] = { };
    int expected_key = 1;

    greentea_send_kv("timing_drift_check_start", 0);
    ticker1->attach_us(&increment_ticker_counter, ONE_MILLI_SEC);

    // wait for 1st signal from host
    do {
        greentea_parse_kv(_key, _value, sizeof(_key), sizeof(_value));
        expected_key = strcmp(_key, "base_time");
    } while (expected_key);
    greentea_send_kv(_key, callback_trigger_count * ONE_MILLI_SEC);

    // wait for 2nd signal from host
    greentea_parse_kv(_key, _value, sizeof(_key), sizeof(_value));
    greentea_send_kv(_key, callback_trigger_count * ONE_MILLI_SEC);

    //get the results from host
    greentea_parse_kv(_key, _value, sizeof(_key), sizeof(_value));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("pass", _key,"Host side script reported a fail...");
}

void test_case_2x_callbacks()
{
    char _key[11] = { };
    char _value[128] = { };
    int expected_key =  1;

    led1 = 0;
    led2 = 0;
    callback_trigger_count = 0;

    greentea_send_kv("timing_drift_check_start", 0);
    ticker1->attach_us(ticker_callback_1_switch_to_2, ONE_MILLI_SEC);

    // wait for 1st signal from host
    do {
        greentea_parse_kv(_key, _value, sizeof(_key), sizeof(_value));
        expected_key = strcmp(_key, "base_time");
    } while (expected_key);
    greentea_send_kv(_key, callback_trigger_count * ONE_MILLI_SEC);

    // wait for 2nd signal from host
    greentea_parse_kv(_key, _value, sizeof(_key), sizeof(_value));
    greentea_send_kv(_key, callback_trigger_count * ONE_MILLI_SEC);

    //get the results from host
    greentea_parse_kv(_key, _value, sizeof(_key), sizeof(_value));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("pass", _key,"Host side script reported a fail...");
}

/** Test many tickers run one after the other

    Given many Tickers
    When schedule them one after the other with the same time intervals
    Then tickers properly execute callbacks
    When schedule them one after the other with the different time intervals
    Then tickers properly execute callbacks
 */
void test_multi_ticker(void)
{
    Ticker ticker[TICKER_COUNT];
    const uint32_t extra_wait = 5; // extra 5ms wait time

    multi_counter = 0;
    for (int i = 0; i < TICKER_COUNT; i++) {
        ticker[i].attach_us(callback(increment_multi_counter), MULTI_TICKER_TIME_MS * 1000);
    }

    Thread::wait(MULTI_TICKER_TIME_MS + extra_wait);
    for (int i = 0; i < TICKER_COUNT; i++) {
            ticker[i].detach();
    }
    TEST_ASSERT_EQUAL(TICKER_COUNT, multi_counter);

    multi_counter = 0;
    for (int i = 0; i < TICKER_COUNT; i++) {
        ticker[i].attach_us(callback(increment_multi_counter), (MULTI_TICKER_TIME_MS + i) * 1000);
    }

    Thread::wait(MULTI_TICKER_TIME_MS + TICKER_COUNT + extra_wait);
    for (int i = 0; i < TICKER_COUNT; i++) {
        ticker[i].detach();
    }
    TEST_ASSERT_EQUAL(TICKER_COUNT, multi_counter);
}

/** Test multi callback time

    Given a Ticker
    When the callback is attached multiple times
    Then ticker properly execute callback multiple times
 */
void test_multi_call_time(void)
{
    Ticker ticker;
    int time_diff;
    const int attach_count = 10;

    for (int i = 0; i < attach_count; i++) {
        ticker_callback_flag = 0;
        gtimer.reset();

        gtimer.start();
        ticker.attach_us(callback(stop_gtimer_set_flag), MULTI_TICKER_TIME_MS * 1000);
        while(!ticker_callback_flag);
        time_diff = gtimer.read_us();

        TEST_ASSERT_UINT32_WITHIN(TOLERANCE_US, MULTI_TICKER_TIME_MS * 1000, time_diff);
    }
}

/** Test if detach cancel scheduled callback event

    Given a Ticker with callback attached
    When the callback is detached
    Then the callback is not being called
 */
void test_detach(void)
{
    Ticker ticker;
    int32_t ret;
    const float ticker_time_s = 0.1f;
    const uint32_t wait_time_ms = 500;
    Semaphore sem(0, 1);

    ticker.attach(callback(sem_release, &sem), ticker_time_s);

    ret = sem.wait();
    TEST_ASSERT_TRUE(ret > 0);

    ret = sem.wait();
    ticker.detach(); /* cancel */
    TEST_ASSERT_TRUE(ret > 0);

    ret = sem.wait(wait_time_ms);
    TEST_ASSERT_EQUAL(0, ret);
}

/** Test single callback time via attach

    Given a Ticker
    When callback attached with time interval specified
    Then ticker properly executes callback within a specified time interval
 */
template<us_timestamp_t DELAY_US>
void test_attach_time(void)
{
    Ticker ticker;
    ticker_callback_flag = 0;

    gtimer.reset();
    gtimer.start();
    ticker.attach(callback(stop_gtimer_set_flag), ((float)DELAY_US) / 1000000.0f);
    while(!ticker_callback_flag);
    ticker.detach();
    const int time_diff = gtimer.read_us();

    TEST_ASSERT_UINT64_WITHIN(TOLERANCE_US, DELAY_US, time_diff);
}

/** Test single callback time via attach_us

    Given a Ticker
    When callback attached with time interval specified
    Then ticker properly executes callback within a specified time interval
 */
template<us_timestamp_t DELAY_US>
void test_attach_us_time(void)
{
    Ticker ticker;
    ticker_callback_flag = 0;

    gtimer.reset();
    gtimer.start();
    ticker.attach_us(callback(stop_gtimer_set_flag), DELAY_US);
    while(!ticker_callback_flag);
    ticker.detach();
    const int time_diff = gtimer.read_us();

    TEST_ASSERT_UINT64_WITHIN(TOLERANCE_US, DELAY_US, time_diff);
}


utest::v1::status_t one_ticker_case_setup_handler_t(const Case *const source, const size_t index_of_case)
{
    ticker1 = new Ticker();
    return greentea_case_setup_handler(source, index_of_case);
}

utest::v1::status_t two_ticker_case_setup_handler_t(const Case *const source, const size_t index_of_case)
{
    ticker1 = new Ticker();
    ticker2 = new Ticker();
    return utest::v1::greentea_case_setup_handler(source, index_of_case);
}

utest::v1::status_t one_ticker_case_teardown_handler_t(const Case *const source, const size_t passed, const size_t failed, const utest::v1::failure_t reason)
{
    Ticker *temp1 = ticker1;
    ticker1 = NULL;
    delete temp1;
    return utest::v1::greentea_case_teardown_handler(source, passed, failed, reason);
}

utest::v1::status_t two_ticker_case_teardown_handler_t(const Case *const source, const size_t passed, const size_t failed, const utest::v1::failure_t reason)
{
    Ticker *temp1 = ticker1;
    Ticker *temp2 = ticker2;
    ticker1 = NULL;
    ticker2 = NULL;
    delete temp1;
    delete temp2;
    return utest::v1::greentea_case_teardown_handler(source, passed, failed, reason);
}


// Test cases
Case cases[] = {
    Case("Test attach for 0.01s and time measure", test_attach_time<10000>),
    Case("Test attach_us for 10ms and time measure", test_attach_us_time<10000>),
    Case("Test attach for 0.1s and time measure", test_attach_time<100000>),
    Case("Test attach_us for 100ms and time measure", test_attach_us_time<100000>),
    Case("Test attach for 0.5s and time measure", test_attach_time<500000>),
    Case("Test attach_us for 500ms and time measure", test_attach_us_time<500000>),
    Case("Test detach", test_detach),
    Case("Test multi call and time measure", test_multi_call_time),
    Case("Test multi ticker", test_multi_ticker),
    Case("Test timers: 1x ticker", one_ticker_case_setup_handler_t,test_case_1x_ticker, one_ticker_case_teardown_handler_t),
    Case("Test timers: 2x callbacks", two_ticker_case_setup_handler_t,test_case_2x_callbacks, two_ticker_case_teardown_handler_t)
};

utest::v1::status_t greentea_test_setup(const size_t number_of_cases)
{
    GREENTEA_SETUP(test_timeout, "timing_drift_auto");
    return utest::v1::greentea_test_setup_handler(number_of_cases);
}

utest::v1::Specification specification(greentea_test_setup, cases, utest::v1::greentea_test_teardown_handler);

int main()
{
    utest::v1::Harness::run(specification);
}
