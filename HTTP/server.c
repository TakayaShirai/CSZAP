#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

typedef struct {
    char method[8];
    char path[1024];
    char query[1024];
} Request;

typedef struct {
    char header[1024];
    char body[8096];
    size_t header_len;
    size_t body_len;
} Response;

ssize_t recv_request(int socket, char *buffer, size_t length, int flags) {
    if (socket < 0 || buffer == NULL) {
        printf("invalid socket");
        return -1;
    };

    ssize_t total_received_bytes = 0;

    while (length > (size_t)total_received_bytes + 1) {
        ssize_t received_bytes = recv(socket, buffer + total_received_bytes, length - total_received_bytes - 1, flags);

        if (received_bytes <= 0) {
            return received_bytes;
        };

        total_received_bytes += received_bytes;
        buffer[total_received_bytes] = '\0';

        if (strstr(buffer, "\r\n\r\n") != NULL) {
            return total_received_bytes;
        }
    };

    errno = EMSGSIZE; 
    return -1;
};

int parse_request(char * buffer, Request * request) {
    // GET /calc?q=1%2B2 HTTP/1.1\r\n
    // Host: 127.0.0.1:9096\r\n
    // User-Agent: curl/8.7.1\r\n
    // Accept: */*\r\n
    // \r\n

    if (buffer == NULL) {
        return -1;
    }

    char path_query[1024];
    char version[16];

    if (sscanf(buffer, "%7s %1023s %15s", request->method, path_query, version) != 3) {
        return -1;
    }

    if (sscanf(path_query, "%1023[^?]?%1023s", request->path, request->query) < 1) {
        return -1;
    }

    return 0;
};

int parse_query(const char *query, double *left_num, double *right_num, char *operator) {
    char code;

    if (sscanf(query, "q=%lf%%2%c%lf", left_num, &code, right_num) == 3) {
        switch (code) {
            case 'B': 
                *operator = '+'; 
                return 0;
            case 'D': 
                *operator = '-';
                return 0;
            case 'A':
                *operator = '*';
                return 0;
            case 'F':
                *operator = '/';
                return 0;
            default: 
                return -1;
        }
    }

    if (sscanf(query, "q=%lf%c%lf", left_num, operator, right_num) == 3) {
        switch (*operator) {
            case '+':
                return 0; 
            case '-':
                return 0; 
            case '*': 
                return 0;
            case '/':
                return 0;
            default:
                return -1;
        }
    }

    return -1;
};

double calc(double left_num, double right_num, char operator) {
    double result = 0;

    switch (operator) {
        case '+':
            result = left_num + right_num;
            break;
        case '-':
            result = left_num - right_num;
            break;
        case '*':
            result = left_num * right_num;
            break;
        case '/':
            if (right_num == 0) {
                return -1; // TODO: Proper Error Handling.
            }
            result = left_num / right_num;
            break;
        default:
            break;
    }

    return result;
};

void build_header(Response * response, int status, const char * reason, int body_len) {
    int header_len = snprintf(response->header, sizeof(response->header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, reason, (size_t)body_len);

    response->body_len = (size_t)body_len;
    response->header_len = (size_t)header_len;
};

int error_response(Response * response, int status, const char * reason) {
    int body_len = snprintf(response->body, sizeof(response->body),
        "<!DOCTYPE html>\n"
        "<html lang=\"ja\">\n"
        "<head>\n"
        "    <meta charset=\"utf-8\">\n"
        "    <title>%d %s</title>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>%d %s</h1>\n"
        "</body>\n"
        "</html>\n",
        status, reason, status, reason);

    build_header(response, status, reason, body_len);
    return status;
};

int calc_response(Response * response, double left_num, char operator, double right_num, double result) {
    int body_len = snprintf(response->body, sizeof(response->body),
        "<!DOCTYPE html>\n"
        "<html lang=\"ja\">\n"
        "<head>\n"
        "    <meta charset=\"utf-8\">\n"
        "    <title>Calc Result</title>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>Calc Result</h1>\n"
        "    <p>%g %c %g = %g</p>\n"
        "</body>\n"
        "</html>\n",
        left_num, operator, right_num, result);

    build_header(response, 200, "OK", body_len);
    return 200;
};

int build_response(Request * request, Response * response) {
    if (strcmp(request->method, "GET") != 0) {
        return error_response(response, 405, "Method Not Allowed");
    }

    if (strcmp(request->path, "/calc") != 0) {
        return error_response(response, 404, "Not Found");
    }

    // 1. parse query
    double left_num;
    double right_num;
    char operator;

    if (parse_query(request->query, &left_num, &right_num, &operator) == -1) {
        return error_response(response, 400, "Bad Request");
    }

    // 2. calculation
    double result = calc(left_num, right_num, operator);
    return calc_response(response, left_num, operator, right_num, result);
};

int main(void)
{
    // 1. Create a socket.
    // TODO: Create separate sockets for IPv4 and IPv6 for portability and security. Reference: man 4 inet6 "Interaction between IPv4/v6 sockets".
    int server_fd = socket(AF_INET6, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return 0;
    };

    int v6only = 0;
    if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return 0;
    };

    // 2. Assign an address to the socket.
    struct sockaddr_in6 addr = {0};
    addr.sin6_port = htons(80);
	addr.sin6_family = AF_INET6;
	addr.sin6_addr = in6addr_any;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 0;
    };

    // 3. Make the socker ready for requests.
    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(server_fd);
        return 0;
    };

    while (1) {
        // 4. Check if there is a request in a queue.
        struct sockaddr_in6 client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);

        if (client_fd == -1) {
            perror("accept");
            continue;
        };

        // 5. Handle requests.
        char buffer[8192];
        int received_bytes = recv_request(client_fd, buffer, sizeof(buffer), 0);

        if (received_bytes <= 0) {
            perror("recv");
            close(client_fd);
            continue;
        };

        Request request = {0};
        if (parse_request(buffer, &request) == -1) {
            close(client_fd);
            continue;
        }

        Response response = {0};
        if (build_response(&request, &response) == -1) {
            close(client_fd);
            continue;
        }

        // 6. Push to the sending queue.
        send(client_fd, response.header, response.header_len ,0);
        send(client_fd, response.body, response.body_len, 0);

        // 7. close the client connection.
        close(client_fd);
        printf("client_fd: %d closed.\n", client_fd);
    };
    
    // 8. Close the main connection.
    close(server_fd);
    return 0;
};