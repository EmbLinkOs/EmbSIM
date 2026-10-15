#ifndef EMBSIM_KERNEL_H
#define EMBSIM_KERNEL_H

#include <stdint.h>

/* ABI 1. All calls on one kernel must be serialized by its owner.
 * Logic: 0=low, 1=high, 2=Z, 3=X. Strength: 0=weak, 1=strong.
 * IDs remain valid until destruction. Failed operations do not change state.
 * Capacity: 256 nets, 1024 drivers, 8192 pending events per kernel.
 */
typedef struct es_kernel es_kernel;
#if defined(_WIN32) && defined(ES_BUILD)
#define ES_API __declspec(dllexport)
#elif defined(_WIN32)
#define ES_API __declspec(dllimport)
#else
#define ES_API
#endif
ES_API es_kernel *es_create(void);
ES_API void es_destroy(es_kernel *k);
ES_API int es_add_net(es_kernel *k);
ES_API int es_add_driver(es_kernel *k, int net, int strength);
ES_API int es_schedule(es_kernel *k, uint64_t at, int driver, int value);
ES_API int es_advance(es_kernel *k, uint64_t until);
ES_API int es_read(const es_kernel *k, int net);
ES_API uint64_t es_now(const es_kernel *k);
ES_API const char *es_error(int status);

#endif
