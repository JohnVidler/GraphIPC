/*
 * GraphIPC
 * Copyright (C) 2017  John Vidler (john@johnvidler.co.uk)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <wait.h>
#include <pthread.h>
#include <assert.h>
#include "GraphNetwork.h"

struct RouteSpec {
    UID_t source;
    UID_t target;
};
typedef struct RouteSpec RouteSpec_t;

RouteSpec_t routes[MAX_ROUTES];
UID_t       clients[MAX_CLIENTS];

int getListenSocket( struct addrinfo * hints ) {
    int ret;
    struct addrinfo *server_info;

    if((ret = getaddrinfo(NULL, ROUTER_PORT, hints, &server_info )) != 0 ) {
        fprintf( stderr, "Unable to get local NIC details: %s\n", gai_strerror(ret) );
        return -1;
    }

    if( server_info == NULL ) {
        fprintf( stderr, "Failed to bind to any interface" );
        return -1;
    }

    int yes = 1;
    int sockfd = -1;
    struct addrinfo * p;
    for( p = server_info; p != NULL; p = p->ai_next ) {
        if( (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1 ) {
            perror( "Server: socket" );
            continue;
        }

        if( (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1 ) {
            perror( "setsockopt" );
            sockfd = -1;
            break;
        }

        if( bind(sockfd, p->ai_addr, p->ai_addrlen) == -1 ) {
            close( sockfd );
            perror( "Server: bind" );
            continue;
        }
    }

    freeaddrinfo( server_info );

    return sockfd;
}

void * clientProcess( void * args ) {
    int remote_fd = (int)(args);

    ssize_t readData = 1;
    while( readData > 0 ) {
        printf( "Waiting for data...{%d}\n", readData );
        char buffer[1024] = { 0 };
        readData = read( remote_fd, buffer, 1024 );
        if( readData > 0 )
            write( remote_fd, buffer, readData );
    }
    close( remote_fd );
    printf( "Socket closed.\n" );
}

void dumpStateInfo() {
    printf( "Clients\n" );
}

int main(int argc, char ** argv ) {
    memset( &routes,  0, MAX_ROUTES * sizeof(RouteSpec_t) );
    memset( &clients, 0, MAX_CLIENTS * sizeof(UID_t) );

    struct addrinfo listen_hints;
    int ret;

    memset( &listen_hints, 0, sizeof listen_hints );
    listen_hints.ai_family   = AF_UNSPEC;
    listen_hints.ai_socktype = SOCK_STREAM;
    listen_hints.ai_flags    = AI_PASSIVE;

    int listen_fd = getListenSocket( &listen_hints );

    if( listen( listen_fd, ROUTER_BACKLOG ) == -1 ) {
        perror( "listen" );
        exit( 1 );
    }

    while( 1 ) {
        struct sockaddr_storage remote_socket;
        socklen_t newSock_len;
        int remote_fd = accept( listen_fd, (struct sockaddr *)&remote_socket, &newSock_len );
        if( remote_fd == -1 ) {
            perror( "accept" );
            continue;
        }

        pthread_t thread_spec;
        int result = pthread_create( &thread_spec, NULL, clientProcess, (void *)remote_fd );
        assert( !result );

        dumpStateInfo();
    }

    if( listen_fd != -1 )
        close( listen_fd );

    return EXIT_SUCCESS;
}