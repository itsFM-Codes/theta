#ifndef THREAD_POOL_H
#define THREAD_POOL_H

int thread_count();
int thread_pool_init(int num_threads);
void thread_pool_destroy();

#endif // THREAD_POOL_H