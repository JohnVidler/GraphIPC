void sink_spawn( sink_context_t * context ) {

    // 2x MTU, so we always have room for one entire transmission
    /*uint8_t outputBuffer[ config.network_mtu * 2 ];
    memset( outputBuffer, 0, config.network_mtu * 2 );
    uint8_t * outputTail = outputBuffer;

    log_debug( "Sink started!" );

    // Become two actual processes, launch the child.
    pid_t childPID = fork();
    if( childPID == 0 ) {
        processRunner(PIPE_READ(context->wrap_stdin), PIPE_WRITE(context->wrap_stdout), context->binary, context->binary_arguments);
        return NULL; // Should never happen...
    }

    log_debug( "CHILD PID:\t%d", childPID );

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 2 );

    watch_fd[0].fd = PIPE_READ(context->wrap_stdout);
    watch_fd[0].events = POLLIN;

    int rfd = getRouterFD(); // Note: we don't need to close this, the host binary will do this for us!
    context->stream_address = acquireNodeAddress( 0 );

    int status = 0;
    while( status == 0 ) {
        int rv = poll( watch_fd, 1, config.read_timeout * 1000 );

        if( rv == -1 ) {        // Error state while poll'ing
            log_error( "Error during poll cycle for input. Shutting down." );
            break;
        } else if( rv == 0 ) {  // Timed out on poll wait

            // Has the wrapped process exited/died?
            int status = 0;
            int ret = waitpid( childPID, &status, WNOHANG );
            if( ret == -1 && WIFEXITED(status) ) {
                log_error( "Process terminated (exit code = %d)", WEXITSTATUS(status));
                status = 1;
                break;
            }
        } else {  // Data ready from the process itself.

            // Handle any events on the process output
            if( watch_fd[0].revents != 0 ) {
                if ((watch_fd[0].revents & POLLIN) == POLLIN) {
                    uint8_t buffer[config.network_mtu];

                    ssize_t bytesRead = read(watch_fd[0].fd, buffer, config.network_mtu);

                    if( config.arg_verbosity > 0 )
                        fprintf( stderr, ">>>\t%s\n", buffer );

                    if( bytesRead > 0 ) {
                        // Do we have a delim at any point in this new block?
                        for( size_t offset=0; offset<bytesRead; offset++ ) {
                            outputTail = packet_write_u8( outputTail, buffer[offset] );

                            if( buffer[offset] == config.arg_delimiter ) {
                                uint8_t txBuffer[ outputTail-outputBuffer ];
                                size_t txLength = outputTail-outputBuffer;
                                outputTail = packet_shift( outputBuffer, config.network_mtu * 2, txBuffer, txLength );
                                gnw_emitDataPacket( rfd, context->stream_address, txBuffer, txLength ); // Always emit from the node source address, not the stream source address
                            }
                        }
                    }
                }
            }

            watch_fd[0].revents = 0;
        }
    }*/

    log_warn( "Sink died - no way to handle this right now!" );
}

/**
 * Tries to find a sink context based on the source address.
 *
 * This is slightly magic in one particular case - if the application is in 'immediate mode' it doesn't know what the
 * source address of the first stream is before it <strong>must</strong> run the inner binary, so it creates a context
 * where the stream address is zero - ie. the invalid stream, then when the first message from <strong>any</strong>
 * stream comes in, this function grabs the context with a zero stream address, fixes the address, then passes it back
 * to the caller, now as a completely valid context.
 *
 * This might be useful later for stream re-use, if there is a sensible way to handle disconnects, which at the time
 * of writing (version 1.0.0, as it were) disconnections are silent for the sink endpoints.
 *
 * Messy, but it seems to be the most sane way to handle this odd edge case for now. -John.
 *
 * @param needle The address to find a context for
 * @return The matching sink_context_t pointer, or NULL if no context matches
 */
/*sink_context_t * getSinkContext( gnw_address_t needle ) {
    pthread_mutex_lock( &sink_context_list_mutex );

    struct list_element * iter = sink_context_list->head;
    while( iter != NULL ) {
        if( ((sink_context_t *)iter->data)->stream_address == needle || ((sink_context_t *)iter->data)->stream_address == 0 ) {

            // If we have a context with no valid stream address, fix that, and use it as this one.
            if( ((sink_context_t *)iter->data)->stream_address == 0 )
                ((sink_context_t *)iter->data)->stream_address = needle;

            pthread_mutex_unlock( &sink_context_list_mutex ); // Note: This might be thread-unsafe... not sure. -John
            return iter->data;
        }
        iter = iter->next;
    }

    pthread_mutex_unlock( &sink_context_list_mutex );
    return NULL;
}*/

sink_context_t * createNewSink( char * binary, char ** arguments, gnw_address_t source ) {
    /*sink_context_t * sink_context = malloc( sizeof(sink_context_t) );
    sink_context->stream_address   = source;
    sink_context->binary           = binary;
    sink_context->binary_arguments = arguments;

    // Build a new set of redirected pipes
    if( pipe( sink_context->wrap_stdin ) == -1 ) {
        log_error( "Could not create wrapper for stdin");
        exit( EXIT_FAILURE );
    }

    if( pipe( sink_context->wrap_stdout ) == -1 ) {
        log_error( "Could not create wrapper for stdout");
        exit( EXIT_FAILURE );
    }

    // Spin up a new instance!
    int result = pthread_create( &(sink_context->thread_context), NULL, sink_thread, sink_context );
    if( result != 0 ) {
        log_error( "Could not start up a new sink thread. Cannot continue.");
        exit( EXIT_FAILURE );
    }
    pthread_detach( sink_context->thread_context );

    // Add it in to the list
    pthread_mutex_lock( &sink_context_list_mutex );
    ll_append( sink_context_list, sink_context );
    pthread_mutex_unlock( &sink_context_list_mutex );

    return sink_context;*/
    return NULL;
}

                    /*
                    else if( readBytes > 0 ) {
                        // Is this the router FD?
                        if( stream_fd[index].fd == rfd ) {
                            //printf( "Read %ldB from Router on index = %d\n", readBytes, index );

                            assert( config.rx_buffer_tail >= config.rx_buffer );
                            assert( config.rx_buffer_tail - config.rx_buffer <= config.network_mtu * 20 ); 

                            packet_write_u8_buffer( config.rx_buffer_tail, iBuffer, readBytes );
                            config.rx_buffer_tail += readBytes;

                            uint8_t packet_buffer[config.network_mtu + 1];
                            memset( packet_buffer, 0, config.network_mtu + 1 );
                            ssize_t readyBytes = 0;
                            if( (readyBytes = gnw_nextPacket( config.rx_buffer, config.rx_buffer_tail - config.rx_buffer )) != 0 ) {

                                if( readyBytes < 0 ) {
                                    log_warn( "Network desync, attempting to resync... (Err = %d)", readyBytes );
                                    while( config.rx_buffer[0] != GNW_MAGIC && config.rx_buffer < config.rx_buffer_tail ) {
                                        printf( "Dumped 1 byte\n" );
                                        packet_shift( config.rx_buffer, config.network_mtu * 20, NULL, 1 );
                                        config.rx_buffer_tail--;
                                    }
                                    continue;
                                }

                                gnw_header_t header = { 0 };

                                uint8_t * ptr = config.rx_buffer;
                                ptr = packet_read_u8( ptr, &header.magic );
                                ptr = packet_read_u8( ptr, &header.version );
                                ptr = packet_read_u8( ptr, &header.type );
                                ptr = packet_read_u32( ptr, &header.source );
                                ptr = packet_read_u32( ptr, &header.length );

                                handlePacket( index, &header, ptr );

                                // Shift the buffer and sync up the tail pointer
                                packet_shift( config.rx_buffer, config.network_mtu * 20, NULL, readyBytes );
                                config.rx_buffer_tail = config.rx_buffer_tail - (size_t)readyBytes;
                            }

                        }

                        // Is this stdin?
                        else if( stream_fd[index].fd == STDIN_FILENO ) {
                            //printf("Read %ldB from STDIN\n", readBytes);

                            gnw_emitDataPacket( rfd, config.arg_address, iBuffer, readBytes );
                        }
                    }*/