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
 * This <strong>should</strong> get the MTU of the supplied interface, but it doesn't work.
 * Can't find a good source on how ioctl calls like this are supposed to work, which makes
 * this pretty much impossible.
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