/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.] */

#include <openssl/bn.h>

#include <limits.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/mem.h>

#include "internal.h"
#include "../delocate.h"


BIGNUM *BN_new(void) {
  BIGNUM *bn = OPENSSL_malloc(sizeof(BIGNUM));

  if (bn == NULL) {
    OPENSSL_PUT_ERROR(BN, ERR_R_MALLOC_FAILURE);
    return NULL;
  }

  OPENSSL_memset(bn, 0, sizeof(BIGNUM));
  bn->flags = BN_FLG_MALLOCED;

  return bn;
}

void BN_init(BIGNUM *bn) {
  OPENSSL_memset(bn, 0, sizeof(BIGNUM));
}

void BN_free(BIGNUM *bn) {
  if (bn == NULL) {
    return;
  }

  if ((bn->flags & BN_FLG_STATIC_DATA) == 0) {
    OPENSSL_free(bn->d);
  }

  if (bn->flags & BN_FLG_MALLOCED) {
    OPENSSL_free(bn);
  } else {
    bn->d = NULL;
  }
}

void BN_clear_free(BIGNUM *bn) {
  char should_free;

  if (bn == NULL) {
    return;
  }

  if (bn->d != NULL) {
    if ((bn->flags & BN_FLG_STATIC_DATA) == 0) {
      OPENSSL_free(bn->d);
    } else {
      OPENSSL_cleanse(bn->d, bn->dmax * sizeof(bn->d[0]));
    }
  }

  should_free = (bn->flags & BN_FLG_MALLOCED) != 0;
  if (should_free) {
    OPENSSL_free(bn);
  } else {
    OPENSSL_cleanse(bn, sizeof(BIGNUM));
  }
}

BIGNUM *BN_dup(const BIGNUM *src) {
  BIGNUM *copy;

  if (src == NULL) {
    return NULL;
  }

  copy = BN_new();
  if (copy == NULL) {
    return NULL;
  }

  if (!BN_copy(copy, src)) {
    BN_free(copy);
    return NULL;
  }

  return copy;
}

BIGNUM *BN_copy(BIGNUM *dest, const BIGNUM *src) {
  if (src == dest) {
    return dest;
  }

  if (!bn_wexpand(dest, src->top)) {
    return NULL;
  }

  OPENSSL_memcpy(dest->d, src->d, sizeof(src->d[0]) * src->top);

  dest->top = src->top;
  dest->neg = src->neg;
  return dest;
}

void BN_clear(BIGNUM *bn) {
  if (bn->d != NULL) {
    OPENSSL_memset(bn->d, 0, bn->dmax * sizeof(bn->d[0]));
  }

  bn->top = 0;
  bn->neg = 0;
}

DEFINE_METHOD_FUNCTION(BIGNUM, BN_value_one) {
  static const BN_ULONG kOneLimbs[1] = { 1 };
  out->d = (BN_ULONG*) kOneLimbs;
  out->top = 1;
  out->dmax = 1;
  out->neg = 0;
  out->flags = BN_FLG_STATIC_DATA;
}

// BN_num_bits_word returns the minimum number of bits needed to represent the
// value in |l|.
unsigned BN_num_bits_word(BN_ULONG l) {
  // |BN_num_bits| is often called on RSA prime factors. These have public bit
  // lengths, but all bits beyond the high bit are secret, so count bits in
  // constant time.
  BN_ULONG x, mask;
  int bits = (l != 0);

#if BN_BITS2 > 32
  x = l >> 32;
  mask = 0u - x;
  mask = (0u - (mask >> (BN_BITS2 - 1)));
  bits += 32 & mask;
  l ^= (x ^ l) & mask;
#endif

  x = l >> 16;
  mask = 0u - x;
  mask = (0u - (mask >> (BN_BITS2 - 1)));
  bits += 16 & mask;
  l ^= (x ^ l) & mask;

  x = l >> 8;
  mask = 0u - x;
  mask = (0u - (mask >> (BN_BITS2 - 1)));
  bits += 8 & mask;
  l ^= (x ^ l) & mask;

  x = l >> 4;
  mask = 0u - x;
  mask = (0u - (mask >> (BN_BITS2 - 1)));
  bits += 4 & mask;
  l ^= (x ^ l) & mask;

  x = l >> 2;
  mask = 0u - x;
  mask = (0u - (mask >> (BN_BITS2 - 1)));
  bits += 2 & mask;
  l ^= (x ^ l) & mask;

  x = l >> 1;
  mask = 0u - x;
  mask = (0u - (mask >> (BN_BITS2 - 1)));
  bits += 1 & mask;

  return bits;
}

unsigned BN_num_bits(const BIGNUM *bn) {
  if(bn ==NULL)
    return 0;
  const int width = bn_minimal_width(bn);
  if (width == 0) {
    return 0;
  }

  return (width - 1) * BN_BITS2 + BN_num_bits_word(bn->d[width - 1]);
}

unsigned BN_num_bytes(const BIGNUM *bn) {
  return (BN_num_bits(bn) + 7) / 8;
}

void BN_zero(BIGNUM *bn) {
  bn->top = bn->neg = 0;
}

int BN_one(BIGNUM *bn) {
  return BN_set_word(bn, 1);
}

int BN_set_word(BIGNUM *bn, BN_ULONG value) {
  if (value == 0) {
    BN_zero(bn);
    return 1;
  }

  if (!bn_wexpand(bn, 1)) {
    return 0;
  }

  bn->neg = 0;
  bn->d[0] = value;
  bn->top = 1;
  return 1;
}

int BN_set_u64(BIGNUM *bn, uint64_t value) {
#if BN_BITS2 == 64
  return BN_set_word(bn, value);
#elif BN_BITS2 == 32
  if (value <= BN_MASK2) {
    return BN_set_word(bn, (BN_ULONG)value);
  }

  if (!bn_wexpand(bn, 2)) {
    return 0;
  }

  bn->neg = 0;
  bn->d[0] = (BN_ULONG)value;
  bn->d[1] = (BN_ULONG)(value >> 32);
  bn->top = 2;
  return 1;
#else
#error "BN_BITS2 must be 32 or 64."
#endif
}

int bn_set_words(BIGNUM *bn, const BN_ULONG *words, size_t num) {
  if (!bn_wexpand(bn, num)) {
    return 0;
  }
  OPENSSL_memmove(bn->d, words, num * sizeof(BN_ULONG));
  // |bn_wexpand| verified that |num| isn't too large.
  bn->top = (int)num;
  bn_correct_top(bn);
  bn->neg = 0;
  return 1;
}

int bn_fits_in_words(const BIGNUM *bn, size_t num) {
  // All words beyond |num| must be zero.
  BN_ULONG mask = 0;
  for (size_t i = num; i < (size_t)bn->top; i++) {
    mask |= bn->d[i];
  }
  return mask == 0;
}

int bn_copy_words(BN_ULONG *out, size_t num, const BIGNUM *bn) {
  if (bn->neg) {
    OPENSSL_PUT_ERROR(BN, BN_R_NEGATIVE_NUMBER);
    return 0;
  }

  size_t width = (size_t)bn->top;
  if (width > num) {
    if (!bn_fits_in_words(bn, num)) {
      OPENSSL_PUT_ERROR(BN, BN_R_BIGNUM_TOO_LONG);
      return 0;
    }
    width = num;
  }

  OPENSSL_memset(out, 0, sizeof(BN_ULONG) * num);
  OPENSSL_memcpy(out, bn->d, sizeof(BN_ULONG) * width);
  return 1;
}

int BN_is_negative(const BIGNUM *bn) {
  return bn->neg != 0;
}

void BN_set_negative(BIGNUM *bn, int sign) {
  if (sign && !BN_is_zero(bn)) {
    bn->neg = 1;
  } else {
    bn->neg = 0;
  }
}

int bn_wexpand(BIGNUM *bn, size_t words) {
  BN_ULONG *a;

  if (words <= (size_t)bn->dmax) {
    return 1;
  }

  if (words > (INT_MAX / (4 * BN_BITS2))) {
    OPENSSL_PUT_ERROR(BN, BN_R_BIGNUM_TOO_LONG);
    return 0;
  }

  if (bn->flags & BN_FLG_STATIC_DATA) {
    OPENSSL_PUT_ERROR(BN, BN_R_EXPAND_ON_STATIC_BIGNUM_DATA);
    return 0;
  }

  a = OPENSSL_malloc(sizeof(BN_ULONG) * words);
  if (a == NULL) {
    OPENSSL_PUT_ERROR(BN, ERR_R_MALLOC_FAILURE);
    return 0;
  }

  OPENSSL_memcpy(a, bn->d, sizeof(BN_ULONG) * bn->top);

  OPENSSL_free(bn->d);
  bn->d = a;
  bn->dmax = (int)words;

  return 1;
}

int bn_expand(BIGNUM *bn, size_t bits) {
  if (bits + BN_BITS2 - 1 < bits) {
    OPENSSL_PUT_ERROR(BN, BN_R_BIGNUM_TOO_LONG);
    return 0;
  }
  return bn_wexpand(bn, (bits+BN_BITS2-1)/BN_BITS2);
}

int bn_resize_words(BIGNUM *bn, size_t words) {
  if ((size_t)bn->top <= words) {
    if (!bn_wexpand(bn, words)) {
      return 0;
    }
    OPENSSL_memset(bn->d + bn->top, 0, (words - bn->top) * sizeof(BN_ULONG));
    bn->top = words;
    return 1;
  }

  // All words beyond the new width must be zero.
  if (!bn_fits_in_words(bn, words)) {
    OPENSSL_PUT_ERROR(BN, BN_R_BIGNUM_TOO_LONG);
    return 0;
  }
  bn->top = words;
  return 1;
}

int bn_minimal_width(const BIGNUM *bn) {
  int ret = bn->top;
  while (ret > 0 && bn->d[ret - 1] == 0) {
    ret--;
  }
  return ret;
}

void bn_correct_top(BIGNUM *bn) {
  bn->top = bn_minimal_width(bn);
  if (bn->top == 0) {
    bn->neg = 0;
  }
}
