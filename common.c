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

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <sys/ioctl.h>
#include <net/if.h>

int socket_connect(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *client_info;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Hard coded loopback, for now... -John
    if (getaddrinfo(host, port, &hints, &client_info) != 0)
        return -1;

    // loop through all the results and connect to the first we can
    int sockfd = -1;
    struct addrinfo *p;
    for(p = client_info; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL)
        return -1;

    freeaddrinfo(client_info); // all done with this structure

    return sockfd;
}

/**
 * This <strong>should</strong> get the MTU of the supplied interface.
 *
 * @param interface The interface to query
 * @return The MTU of the interface supplied
 */
int getIFaceMTU( const char * interface ) {
    // Build the query
    struct ifreq query;
    memcpy( query.ifr_name, interface, strlen(interface) );
    query.ifr_name[strlen(interface)] = '\0';

    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if( sock < 0 )
        return -1;

    if( ioctl( sock, SIOCGIFMTU, &query ) == 0 ) {
        close( sock );
        return query.ifr_mtu;
    }

    close( sock );
    return -1;
}

/**
 * Attempts to find a particular file from the available PATH environment variable along with
 * the current working directory.
 *
 * If no PATH-prefixed path exists, the target will be filled with the original binary path.
 *
 * <b>Usage</b>
 * <pre>
 * char path[128] = { 0 };
 * findRealPath( path, "binary_file" );
 * </pre>
 *
 * @param target Character pointer to be filled with the path. Must be large enough to handle the result!
 * @param binary The file to search for.
 */
void findRealPath( char * target, const char * binary ) {
    char pathBuffer[2048] = { 0 };
    size_t env_path_length = strlen( getenv( "PATH" ) );
    char * path = alloca( env_path_length + 1 );
    memcpy( path, getenv( "PATH" ), env_path_length );

    char * parent = strtok( path, ":" );
    while( parent != NULL ) {
        memset( pathBuffer, 0, 2048 );
        sprintf( pathBuffer, "%s/%s", parent, binary );

        if( access(pathBuffer, F_OK) != -1 ) {
            sprintf( target, "%s/%s", parent, binary );
            free( path );
            return;
        }

        parent = strtok( NULL, ":" );
    }

    // Guess we must have an actual path already...
    sprintf( target, "%s", binary );
}