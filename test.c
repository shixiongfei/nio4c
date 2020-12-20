/*
 *  test.c
 *
 *  copyright (c) 2019 Xiongfei Shi
 *
 *  author: Xiongfei Shi <xiongfei.shi(a)icloud.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#include "nio4c.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
  niohwaddr_t hwaddrs[8];
  niosockaddr_t ipaddr;
  nioipstr_t ipstr;
  niosocket_t server, client, session;
  niosockaddr_t bind_addr, conn_addr, sess_addr;
  nioselector_t *selector;
  niomonitor_t *monserv, *moncli, *monsess;
  niomonitor_t *monitors[4];
  int n, i, j, k;

  ((void)argc);
  ((void)argv);

  nio_initialize(NULL);

  printf("nio4c version: %s\n", NIO_VERSION);

  n = nio_gethwaddr(hwaddrs, sizeof(hwaddrs) / sizeof(hwaddrs[0]));

  printf("hostname: %s\n", nio_gethostname());

  for (i = 0; i < n; ++i) {
    printf("hwaddr: ");
    for (j = 0; j < NIO_HWADDRLEN; ++j)
      printf("%02x ", hwaddrs[i].hwaddr[j]);
    printf("\n");
  }

  nio_hostaddr(&ipaddr, "shixiongfei.com", 12345);
  nio_ipstr(&ipstr, &ipaddr);
  printf("%s:%d\n", ipstr.addr, ipstr.port);

  nio_resolvehost(&bind_addr, 1, AF_INET, NULL, 13579);

  nio_createtcp4(&server);
  nio_reuseaddr(&server, 1);
  nio_bind(&server, &bind_addr);
  nio_listen(&server, SOMAXCONN);

  printf("server socket: %d\n", server.sockfd);

  nio_resolvehost(&conn_addr, 1, AF_INET, "localhost", 13579);

  nio_createtcp4(&client);
  nio_socketnonblock(&client, 1);
  nio_tcpnodelay(&client, 1);
  nio_connect(&client, &conn_addr);

  printf("client socket: %d\n", client.sockfd);

  selector = nio_selector();
  printf("selector backend: %s\n", selector_backend(selector));

  monserv = selector_register(selector, &server, NIO_READ, NULL);
  moncli = selector_register(selector, &client, NIO_READWRITE, NULL);

  printf("monitor server %p\n", monserv);
  printf("monitor client %p\n", moncli);

  while (!selector_empty(selector)) {
    n = selector_select(selector, monitors, 4, 1234567);

    for (i = 0; i < n; ++i) {
      if (monitor_exception(monitors[i])) {
        nio_sockipstr(monitor_io(monitors[i]), &ipstr);
        printf("%s:%d is error.\n", ipstr.addr, ipstr.port);

        nio_shutdown(monitor_io(monitors[i]), SHUT_RDWR);
      }

      if (monitor_readable(monitors[i])) {
        if (monitor_io(monitors[i]) == &server) {
          nio_accept(&server, &session, &sess_addr);
          monsess = selector_register(selector, &session, NIO_READ, NULL);

          nio_ipstr(&ipstr, &sess_addr);
          printf("%s:%d is connected.\n", ipstr.addr, ipstr.port);

          monitor_close(monitors[i], 1);
        } else {
          char buffer[16] = {0};

          k = nio_recv(monitor_io(monitors[i]), buffer, sizeof(buffer));
          nio_sockipstr(monitor_io(monitors[i]), &ipstr);

          if (k <= 0) {
            printf("%s:%d is disconnected.\n", ipstr.addr, ipstr.port);
            monitor_close(monitors[i], 1);
          } else {
            printf("%s:%d receive %s\n", ipstr.addr, ipstr.port, buffer);
            nio_send(monitor_io(monitors[i]), "ByeBye!", 7);

            nio_shutdown(monitor_io(monitors[i]), SHUT_RDWR);
          }
        }
      }

      if (monitor_writable(monitors[i])) {
        if (monitor_io(monitors[i]) == &client) {
          printf("Client is connected.\n");

          nio_sendall(&client, "HelloWorld.", 11);
          monitor_removeinterest(monitors[i], NIO_WRITE);
        }
      }

      if (monitor_closed(monitors[i])) {
        niosocket_t *io = monitor_io(monitors[i]);
        monitor_destroy(monitors[i]);
        nio_destroysocket(io);
      }
    }
  }

  selector_destroy(selector);
  nio_finalize();

  return 0;
}
