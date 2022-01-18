/*
 * --- Revised 3-Clause BSD License ---
 * Copyright Semtech Corporation 2022. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the documentation
 *       and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the names of its
 *       contributors may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "selftests.h"
#include "xq.h"
#include "uj.h"

static int in_queue(txq_t* txq, txidx_t q) {
    txjob_t* j = txq_idx2job(txq, q);
    int m = (j!=NULL);
    while( (j = txq_nextJob(txq,j)) != NULL )
        m++;

    txidx_t* pidx = &q;
    int n = (q!=TXIDX_END);
    while( *(pidx = txq_nextIdx(txq,pidx)) != TXIDX_END )
        n++;

    TCHECK(n==m);
    return n;
}

#define txq (*_txq)
void selftest_txq () {
    txidx_t heads[1];
    txq_t * _txq = rt_malloc(txq_t);
    int n;

    heads[0] = TXIDX_END;
    txq_ini(&txq);

    TCHECK(NULL           == txq_idx2job(&txq, TXIDX_NIL));
    TCHECK(NULL           == txq_idx2job(&txq, TXIDX_END));
    TCHECK(&txq.txjobs[0] == txq_idx2job(&txq, 0));
    TCHECK(&txq.txjobs[1] == txq_idx2job(&txq, 1));
    TCHECK(&txq.txjobs[2] == txq_idx2job(&txq, 2));

    TCHECK(TXIDX_NIL == txq_job2idx(&txq, NULL));
    TCHECK(0         == txq_job2idx(&txq, &(txq.txjobs[0])));
    TCHECK(1         == txq_job2idx(&txq, &(txq.txjobs[1])));

    char* outbuf = rt_mallocN(char, 512);
    ujbuf_t B = {.buf=outbuf, .bufsize=512, .pos=0 };

    xprintf(&B, "%J", &txq.txjobs[0]);
    TCHECK(strcmp("::0 diid=0 [ant#0]", B.buf) == 0);
    rt_free(B.buf);
    B.buf = NULL;

    txjob_t* j;

    for( int k=0; k<40000; k++ ) {
        int action, phase = k / (MAX_TXJOBS+3);
        switch( phase % 4 ) {
        case 0: action = 0; break;
        case 2: action = 1; break;
        default: action = rand()&1; break;
        }
        switch( action ) {
        case 0: {
            // Add txjob
            if( (j = txq_reserveJob(&txq)) == NULL )
                continue;
            // Attach some data
            u1_t data[255];
            memset(data, k, sizeof(data));
            int len = k<100 ? (rand() % 4) * 16 : 255;
            u1_t* txd = txq_reserveData(&txq, 255);
            if( txd == NULL )
                continue;
            memcpy(txd, data, len);
            j->len = len;
            txq_commitJob(&txq, j);
            TCHECK((txd && j->off != TXOFF_NIL) || (!txd && j->off == TXOFF_NIL));
            // Insert somewhere along the Q
            int l = rand()%3;
            txidx_t* p = &heads[0];
            while( --l > 0 )
                p = txq_nextIdx(&txq, p);
            txq_insJob(&txq, p, j);
            break;
        }
        case 1: {
            // Remove txjob
            j = txq_idx2job(&txq, heads[0]);
            if( j == NULL )
                break;  // queue empty
            if( j->off != TXOFF_NIL ) {
                // Data corrupted?
                u1_t* d = &txq.txdata[j->off];
                u1_t c = d[0];
                for( int i=j->len-1; i>0; i-- )
                    TCHECK(d[i] == c);
            }
            if( rand() & 1 ) {
                txq_unqJob(&txq, &heads[0]);
                txq_freeJob(&txq, j);
                TCHECK(j->off == TXOFF_NIL);
            }
            else {
                txq_freeData(&txq, j);
                TCHECK(j->off == TXOFF_NIL);
            }
            break;
        }
        }
        txidx_t* p = &txq.freeJobs;
        while( *p != TXIDX_END ) {
            txjob_t* j = txq_idx2job(&txq, *p);
            p = txq_nextIdx(&txq, p);
            TCHECK(j->off == TXOFF_NIL && j->len == 0);
        }
        n = in_queue(&txq, txq.freeJobs) + in_queue(&txq, heads[0]);
        TCHECK(n==MAX_TXJOBS);
    }
    while( heads[0] != TXIDX_END ) {
        txq_freeJob(&txq, txq_unqJob(&txq, &heads[0]));
    }
    n = in_queue(&txq, txq.freeJobs) + in_queue(&txq, heads[0]);
    TCHECK(n==MAX_TXJOBS);
    TCHECK(txq.txdataInUse==0);

    do {
        if( (j = txq_reserveJob(&txq)) == NULL )
            TFAIL("Fail");    // LCOV_EXCL_LINE
        if( txq_reserveData(&txq, 255) == NULL )
            break;
        j->len = 255;
        txq_commitJob(&txq, j);
    } while(1);

    heads[0] = TXIDX_END;
    TCHECK(NULL == txq_unqJob(&txq, &heads[0]));
    rt_free(_txq);
}

#define rxq (*_rxq)
void selftest_rxq () {
    int r;
    rxq_t * _rxq = rt_malloc(rxq_t);
    rxjob_t *j;

    rxq_ini(&rxq);
    for( int k=0; k<400; k++ ) {
        r = rand() % 5;
        switch( r ) {
        case 0:
        case 1:
        case 2: {
            j = rxq_nextJob(&rxq);
            if( j != NULL ) {
                j->len = k < 300 ? 196 : 16;
                rxq_commitJob(&rxq, j);
            }
            break;
        }
        case 3: {
            if( rxq.first < rxq.next )
                rxq.first += 1;
            break;
        }
        case 4: {
            if( rxq.first+2 < rxq.next )
                rxq_dropJob(&rxq, &rxq.rxjobs[rxq.first+1]);
            break;
        }
        }
        TCHECK(rxq.first <= MAX_RXJOBS);
        TCHECK(rxq.next <= MAX_RXJOBS);
        TCHECK(rxq.first <= rxq.next);
        TCHECK(rxq.first == rxq.next || rxq.rxjobs[rxq.first].len > 0);
        for( int i=rxq.first+1; i < rxq.next; i++ ) {
            TCHECK(rxq.rxjobs[i-1].len > 0);
            TCHECK(rxq.rxjobs[i-1].off + rxq.rxjobs[i-1].len == rxq.rxjobs[i].off);
        }
    }
    rt_free(_rxq);
}

