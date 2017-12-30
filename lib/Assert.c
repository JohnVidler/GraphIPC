//
// Created by john on 14/12/17.
//

#include "Assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

bool bailOnAssertFail = false;
bool reportAssertCalls = false;
unsigned int testsPassed = 0;
unsigned int testsFailed = 0;

bool _assert( bool state, char * errorMessage, char * file, int line ) {
    if( reportAssertCalls ) {
        printf("%s:%u\tASSERT? '%s'\n", file, line, (state ? "True" : "False"));
        fflush( stdout );
    }

    if( !state ) {
        fprintf( stderr, "%s:%u\tASSERT: %s\n", file, line, errorMessage );
        fflush( stderr );
        testsFailed++;

        if( bailOnAssertFail )
            exit( EXIT_FAILURE );
        return false;
    }
    testsPassed++;
    return true;
}

bool _assertEqual( uint64_t a, uint64_t b, char * file, int line ) {
    if( reportAssertCalls ) {
        printf("%s:%u\tASSERT-EQUAL? '%llu' == '%llu' -> %s\n", file, line, a, b, (a == b ? "True" : "False"));
        fflush( stdout );
    }

    if( a != b ) {
        fprintf( stderr, "%s:%u\tASSERT-EQUAL: '%llu' was not equal to '%llu'\n", file, line, a, b );
        fflush( stderr );

        if( bailOnAssertFail )
            exit( EXIT_FAILURE );

        return false;
    }
    return true;
}

void setReportAssert( bool state ) {
    reportAssertCalls = state;
}

void setExitOnAssert( bool state ) {
    bailOnAssertFail = state;
}