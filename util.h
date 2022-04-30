/* See LICENSE file for copyright and license details. */

#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))
#define CONCAT1(A, B)            A ## B
#define CONCAT(A, B) CONCAT1(A, B)
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define CALC_SIZE(X) const size_t CONCAT(X, _size) = LENGTH(X)

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);

#endif /* UTIL_H */

