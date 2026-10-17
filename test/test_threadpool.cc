#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "event.h"
#include "threadpool.h"

TEST(threadpool, create) {
    void *pool = threadpool_create(1, 1);
    ASSERT_NE(pool, nullptr);
    EXPECT_TRUE(threadpool_delete(&pool));
    ASSERT_EQ(pool, nullptr);
}

static void threadpool_run_one_worker(void *arg) {
    bool *job_was_run = (bool *)arg;
    *job_was_run = true;
}

TEST(threadpool, run_one) {
    bool job_was_run = false;
    void *pool = threadpool_create(1, 4);
    ASSERT_NE(pool, nullptr);
    EXPECT_TRUE(threadpool_enqueue(pool, &job_was_run, threadpool_run_one_worker));
    threadpool_wait(pool);
    EXPECT_TRUE(job_was_run);
    EXPECT_TRUE(threadpool_delete(&pool));
    ASSERT_EQ(pool, nullptr);
}

static void threadpool_run_many_worker(void *arg) {
    bool *job_was_run = (bool *)arg;
    *job_was_run = true;
}

TEST(threadpool, run_many) {
    bool job_was_run[100] = {false};
    void *pool = threadpool_create(1, 4);
    ASSERT_NE(pool, nullptr);
    for (int32_t i = 0; i < sizeof(job_was_run); i++)
        EXPECT_TRUE(threadpool_enqueue(pool, &job_was_run[i], threadpool_run_many_worker));
    threadpool_wait(pool);
    for (int32_t i = 0; i < sizeof(job_was_run); i++)
        EXPECT_TRUE(job_was_run[i]);
    EXPECT_TRUE(threadpool_delete(&pool));
    ASSERT_EQ(pool, nullptr);
}

typedef struct threadpool_delete_job_s {
    void *started;
    bool finished;
} threadpool_delete_job_s;

static void threadpool_delete_worker(void *arg) {
    threadpool_delete_job_s *job = (threadpool_delete_job_s *)arg;
    event_set(job->started);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    job->finished = true;
}

TEST(threadpool, delete_waits_for_running_job) {
    threadpool_delete_job_s job = {event_create(), false};
    ASSERT_NE(job.started, nullptr);
    void *pool = threadpool_create(1, 1);
    ASSERT_NE(pool, nullptr);
    EXPECT_TRUE(threadpool_enqueue(pool, &job, threadpool_delete_worker));
    // Delete while the job is still running
    EXPECT_TRUE(event_wait(job.started, 5000));
    EXPECT_TRUE(threadpool_delete(&pool));
    EXPECT_TRUE(job.finished);
    event_delete(&job.started);
}
