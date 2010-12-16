/*
 * Copyright (c) 2007, 2009 Joseph Gaeddert
 * Copyright (c) 2007, 2009 Virginia Polytechnic Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// convolutional code (macros)
//

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "liquid.internal.h"

#define VERBOSE_FEC_CONV    0

#if HAVE_FEC_H == 1 // (config.h)
#include "fec.h"

fec fec_conv_create(fec_scheme _fs)
{
    fec q = (fec) malloc(sizeof(struct fec_s));

    q->scheme = _fs;
    q->rate = fec_get_rate(q->scheme);

    q->encode_func = &fec_conv_encode;
    q->decode_func = &fec_conv_decode;

    switch (q->scheme) {
    case FEC_CONV_V27:  fec_conv_init_v27(q);   break;
    case FEC_CONV_V29:  fec_conv_init_v29(q);   break;
    case FEC_CONV_V39:  fec_conv_init_v39(q);   break;
    case FEC_CONV_V615: fec_conv_init_v615(q);  break;
    default:
        fprintf(stderr,"error: fec_conv_create(), invalid type\n");
        exit(1);
    }

    // convolutional-specific decoding
    q->num_dec_bytes = 0;
    q->enc_bits = NULL;
    q->vp = NULL;

    return q;
}

void fec_conv_destroy(fec _q)
{
    // delete viterbi decoder
    if (_q->vp != NULL)
        _q->delete_viterbi(_q->vp);

    free(_q);
}

void fec_conv_encode(fec _q,
                     unsigned int _dec_msg_len,
                     unsigned char *_msg_dec,
                     unsigned char *_msg_enc)
{
    unsigned int i,j,r; // bookkeeping
    unsigned int sr=0;  // convolutional shift register
    unsigned int n=0;   // output bit counter

    unsigned char bit;
    unsigned char byte_in;
    unsigned char byte_out=0;

    for (i=0; i<_dec_msg_len; i++) {
        byte_in = _msg_dec[i];

        // break byte into individual bits
        for (j=0; j<8; j++) {
            // shift bit starting with most significant
            bit = (byte_in >> (7-j)) & 0x01;
            sr = (sr << 1) | bit;

            // compute parity bits for each polynomial
            for (r=0; r<_q->R; r++) {
                byte_out = (byte_out<<1) | parity(sr & _q->poly[r]);
                _msg_enc[n/8] = byte_out;
                n++;
            }
        }
    }

    // tail bits
    for (i=0; i<(_q->K)-1; i++) {
        // shift register: push zeros
        sr = (sr << 1);

        // compute parity bits for each polynomial
        for (r=0; r<_q->R; r++) {
            byte_out = (byte_out<<1) | parity(sr & _q->poly[r]);
            _msg_enc[n/8] = byte_out;
            n++;
        }
    }

    // ensure even number of bytes
    while (n%8) {
        // shift zeros
        byte_out <<= 1;
        _msg_enc[n/8] = byte_out;
        n++;
    }

    assert(n == 8*fec_get_enc_msg_length(_q->scheme,_dec_msg_len));
}

//unsigned int
void fec_conv_decode(fec _q,
                     unsigned int _dec_msg_len,
                     unsigned char *_msg_enc,
                     unsigned char *_msg_dec)
{
    // re-allocate resources if necessary
    fec_conv_setlength(_q, _dec_msg_len);

    // unpack bytes
    unsigned int num_written;
    liquid_unpack_bytes(_msg_enc,               // encoded message (bytes)
                        _q->num_enc_bytes,      // encoded message length (#bytes)
                        _q->enc_bits,           // encoded messsage (bits)
                        _q->num_enc_bytes*8,    // encoded message length (#bits)
                        &num_written);

#if VERBOSE_FEC_CONV
    unsigned int i;
    printf("msg encoded (bits):\n");
    for (i=0; i<8*_q->num_enc_bytes; i++) {
        printf("%1u", _q->enc_bits[i]);
        if (((i+1)%8)==0)
            printf(" ");
    }
    printf("\n");
#endif

    // invoke hard-decision scaling
    unsigned int k;
    for (k=0; k<8*_q->num_enc_bytes; k++)
        _q->enc_bits[k] = _q->enc_bits[k] ? FEC_SOFTBIT_1 : FEC_SOFTBIT_0;

    // run decoder
    _q->init_viterbi(_q->vp,0);
    _q->update_viterbi_blk(_q->vp, _q->enc_bits, 8*_q->num_dec_bytes+_q->K-1);
    _q->chainback_viterbi(_q->vp, _msg_dec, 8*_q->num_dec_bytes, 0);

#if VERBOSE_FEC_CONV
    for (i=0; i<_dec_msg_len; i++)
        printf("%.2x ", _msg_dec[i]);
    printf("\n");
#endif
}

void fec_conv_setlength(fec _q, unsigned int _dec_msg_len)
{
    // re-allocate resources as necessary
    unsigned int num_dec_bytes = _dec_msg_len;

    // return if length has not changed
    if (num_dec_bytes == _q->num_dec_bytes)
        return;

#if VERBOSE_FEC_CONV
    printf("(re)creating viterbi decoder, %u frame bytes\n", num_dec_bytes);
#endif

    // reset number of framebits
    _q->num_dec_bytes = num_dec_bytes;
    _q->num_enc_bytes = fec_get_enc_msg_length(_q->scheme,
                                               _dec_msg_len);

    // delete old decoder if necessary
    if (_q->vp != NULL)
        _q->delete_viterbi(_q->vp);

    // re-create / re-allocate memory buffers
    _q->vp = _q->create_viterbi(8*_q->num_dec_bytes);
    _q->enc_bits = (unsigned char*) realloc(_q->enc_bits,
                                            _q->num_enc_bytes*8*sizeof(unsigned char));
}

// 
// internal
//

void fec_conv_init_v27(fec _q)
{
    _q->R=2;
    _q->K=7;
    _q->poly = fec_conv27_poly;
    _q->create_viterbi = create_viterbi27;
    _q->init_viterbi = init_viterbi27;
    _q->update_viterbi_blk = update_viterbi27_blk;
    _q->chainback_viterbi = chainback_viterbi27;
    _q->delete_viterbi = delete_viterbi27;
}

void fec_conv_init_v29(fec _q)
{
    _q->R=2;
    _q->K=9;
    _q->poly = fec_conv29_poly;
    _q->create_viterbi = create_viterbi29;
    _q->init_viterbi = init_viterbi29;
    _q->update_viterbi_blk = update_viterbi29_blk;
    _q->chainback_viterbi = chainback_viterbi29;
    _q->delete_viterbi = delete_viterbi29;
}

void fec_conv_init_v39(fec _q)
{
    _q->R=3;
    _q->K=9;
    _q->poly = fec_conv39_poly;
    _q->create_viterbi = create_viterbi39;
    _q->init_viterbi = init_viterbi39;
    _q->update_viterbi_blk = update_viterbi39_blk;
    _q->chainback_viterbi = chainback_viterbi39;
    _q->delete_viterbi = delete_viterbi39;
}

void fec_conv_init_v615(fec _q)
{
    _q->R=6;
    _q->K=15;
    _q->poly = fec_conv615_poly;
    _q->create_viterbi = create_viterbi615;
    _q->init_viterbi = init_viterbi615;
    _q->update_viterbi_blk = update_viterbi615_blk;
    _q->chainback_viterbi = chainback_viterbi615;
    _q->delete_viterbi = delete_viterbi615;
}



#else   // HAVE_FEC_H (config.h)

fec fec_conv_create(fec_scheme _fs)
{
    return NULL;
}

void fec_conv_destroy(fec _q)
{
}

void fec_conv_encode(fec _q,
                     unsigned int _dec_msg_len,
                     unsigned char *_msg_dec,
                     unsigned char *_msg_enc)
{
}

//unsigned int
void fec_conv_decode(fec _q,
                     unsigned int _dec_msg_len,
                     unsigned char *_msg_enc,
                     unsigned char *_msg_dec)
{
}

#endif  // HAVE_FEC_H (config.h)

