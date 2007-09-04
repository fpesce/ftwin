#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <apr.h>		/* define apr_uint32_t */

typedef unsigned char ub1;

#define HASHSTATE 8

/*
--------------------------------------------------------------------
hash() -- hash a variable-length key into a 256-bit value
  k     : the key (the unaligned variable-length array of bytes)
  len   : the length of the key, counting by bytes
  state : an array of HASHSTATE 4-byte values (256 bits)
The state is the checksum.  Every bit of the key affects every bit of
the state.  There are no funnels.  About 112+6.875len instructions.

If you are hashing n strings (ub1 **)k, do it like this:
  for (i=0; i<8; ++i) state[i] = 0x9e3779b9;
  for (i=0, h=0; i<n; ++i) hash( k[i], len[i], state);

(c) Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial, as long
as this whole comment accompanies it.

See http://burtleburtle.net/bob/hash/evahash.html
Use to detect changes between revisions of documents, assuming nobody
is trying to cause collisions.  Do NOT use for cryptography.
--------------------------------------------------------------------
*/
void hash(register ub1 * k, register apr_uint32_t len, register apr_uint32_t *state);

/*
--------------------------------------------------------------------
 This works on all machines, is identical to hash() on little-endian 
 machines, and it is much faster than hash(), but it requires
 -- that buffers be aligned, that is, ASSERT(((apr_uint32_t)k)&3 == 0), and
 -- that all your machines have the same endianness.
--------------------------------------------------------------------
*/
void hash2(register ub1 * k, register apr_uint32_t len, register apr_uint32_t *state);

#endif /* CHECKSUM_H */
