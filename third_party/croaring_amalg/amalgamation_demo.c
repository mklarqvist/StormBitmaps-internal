// !!! DO NOT EDIT - THIS IS AN AUTO-GENERATED FILE !!!
// Created by amalgamation.sh on 2026-08-04T12:03:02Z


#include <stdio.h>
#include <stdlib.h>
#include "roaring.c"


static inline void or_many(void) {
    roaring_bitmap_t *r1 = roaring_bitmap_from(500, 1000);
    roaring_bitmap_t *r2 = roaring_bitmap_from(1000, 2000);

    const roaring_bitmap_t *bitmap_arr[2] = {r1, r2};
    fprintf(stderr, "Going to or many\n");
    for (int i = 0; i < 10000; i++) {
        roaring_bitmap_t *r = roaring_bitmap_or_many(2, bitmap_arr);
        roaring_bitmap_free(r);
    }

    fprintf(stderr, "Got done\n");

    roaring_bitmap_free(r2);
    roaring_bitmap_free(r1);
}

int main() {
  roaring_bitmap_t *r1 = roaring_bitmap_create();
  for (uint32_t i = 100; i < 1000; i++) roaring_bitmap_add(r1, i);
  printf("cardinality = %d\n", (int) roaring_bitmap_get_cardinality(r1));
  roaring_bitmap_free(r1);

  roaring64_bitmap_t *r2 = roaring64_bitmap_create();
  for (uint64_t i = 100; i < 1000; i++) roaring64_bitmap_add(r2, i);
  printf("cardinality (64-bit) = %d\n", (int) roaring64_bitmap_get_cardinality(r2));
  roaring64_bitmap_free(r2);

  bitset_t *b = bitset_create();
  for (int k = 0; k < 1000; ++k) {
        bitset_set(b, 3 * k);
  }
  printf("%zu \n", bitset_count(b));
  bitset_free(b);
  or_many();
  return EXIT_SUCCESS;
}

