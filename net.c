#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* The client socket descriptor for the connection to the server */
int client_socket_descriptor = -1;

/* Function to read 'len' bytes from file descriptor 'fd' into buffer 'buf' */
bool read_bytes(int fd, int len, uint8_t *buf) {
    int bytes_read = 0;
    int remaining_bytes = len;

    while (remaining_bytes > 0) {
        int temp_read = read(fd, buf + bytes_read, remaining_bytes);

        if (temp_read == -1) {
            // Handle error properly and provide meaningful error messages
            return false;
        } else if (temp_read == 0) {
            // If the read returns 0, it means EOF, handle it based on your use case
            break;
        }

        bytes_read += temp_read;
        remaining_bytes -= temp_read;
    }

    // Return true only if the requested number of bytes is read
    return (remaining_bytes == 0);
}

/* Function to write 'len' bytes from buffer 'buf' to file descriptor 'fd' */
static bool write_bytes(int fd, int len, uint8_t *buf) {
    int bytes_written = 0;

    // Keep track of how many bytes you've sent
    while (bytes_written < len) {
        int temp_written = write(fd, ((char *)buf + bytes_written), (len - bytes_written)); //write returns how many bytes were sent
        if (temp_written == -1) {
            return false;
        }
        bytes_written += temp_written;
    }
    return true;
}

/* Function to receive a packet from the server */
static bool receive_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {

    // Packet buffer
    uint8_t packet[264];

    // Receive packet from server
    if (read_bytes(sd, HEADER_LEN, packet) == false) {
        return false;
    }

    // Extract packet length from header buffer, convert to host byte order
    uint16_t packet_length = ntohs(*((uint16_t *)packet));

    // Copy opcode and return code from packet
    *op = ntohl(*((uint32_t *)(packet + 2)));
    *ret = ntohs(*((uint16_t *)(packet + 6)));

    // If there is a block, JBOD_READ_BLOCK was called
    if (packet_length > HEADER_LEN) {
        // Fill block from packet
        if (read_bytes(sd, packet_length - HEADER_LEN, block) == false) {
            return false;
        }
    }

    return true;
}

/* Function to send a jbod request packet to the server */
static bool send_packet(int sd, uint32_t op, uint8_t *block) {

    // Create buffer for packet
    uint8_t packet[264];
    uint16_t length = HEADER_LEN;

    // Increase the length by block length if op is JBOD_WRITE
    if (block != NULL) {
        length += JBOD_BLOCK_SIZE;
    }

    // Convert header fields to network byte order
    uint16_t server_length = htons(length);
    uint32_t send_op = htonl(op);

    // Copy length and opcode into the packet
    memcpy(packet, &server_length, sizeof(uint16_t));
    memcpy(packet + 2, &send_op, sizeof(uint32_t));

    // Copy block if it exists
    if (block != NULL) {
        memcpy(packet + HEADER_LEN, block, JBOD_BLOCK_SIZE);
    }

    // Send the packet to the server
    if (write_bytes(sd, length, packet) == false) {
        return false;
    }

    return true;
}

/* Function to connect to the server */
bool jbod_connect(const char *ip, uint16_t port) {
    struct sockaddr_in server_address;

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_aton(ip, &server_address.sin_addr) == 0) {
        return false;
    }

    client_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket_descriptor == -1) {
        return false;
    }

    if (connect(client_socket_descriptor, (const struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        return false;
    }

    return true;
}

/* Function to disconnect from the server and reset cli_sd */
void jbod_disconnect(void) {
    close(client_socket_descriptor);
    client_socket_descriptor = -1;
}

/* Function to send the JBOD operation to the server and receive and process the response */
int jbod_client_operation(uint32_t op, uint8_t *block) {
    uint16_t return_code = 0;

    if (send_packet(client_socket_descriptor, op, block) == false) {
        return -1;
    }

    if (receive_packet(client_socket_descriptor, &op, &return_code, block) == false) {
        return -1;
    }

    if (return_code == -1) {
        return -1;
    }

    return 0;
}
