//
// Created by john on 14/12/17.
//

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool _assert( bool state, char * errorMessage, char * file, int line );
#define assert(state, message) _assert( state, message, __FILE__, __LINE__ )

bool _assertEqual( uint64_t a, uint64_t b, char * file, int line );
#define assertEqual(a, b) _assertEqual( a, b, __FILE__, __LINE__ )

bool _assertj( bool state, char * errorMessage, void (* jumpTarget)(char *, int), char * file, int line );
#define assertj( state, message, target ) _assertj( state, message, target, __FILE__, __LINE__ )

void setReportAssert( bool state );
void setExitOnAssert( bool state );