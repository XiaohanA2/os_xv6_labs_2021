#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  // 锁定互斥锁，确保对barrier状态的操作是原子性的，避免竞态条件
  pthread_mutex_lock(&bstate.barrier_mutex);

  // 增加已经到达屏障的线程数
  bstate.nthread++;

  // 如果到达屏障的线程数还未达到目标线程数
  if (bstate.nthread < nthread) {
    // 当前线程在条件变量上等待，释放锁并暂停执行，直到其他线程唤醒它
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  } else {
    // 当最后一个线程到达时，进入此分支
    // 增加轮次计数，用于追踪屏障的使用轮次
    bstate.round++;

    // 重置计数器，为下次使用屏障做好准备
    bstate.nthread = 0;

    // 唤醒所有在条件变量上等待的线程，让它们继续执行
    pthread_cond_broadcast(&bstate.barrier_cond);
  }

  // 解锁互斥锁，允许其他线程访问和修改barrier状态
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
