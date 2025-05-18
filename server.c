//Author Marcos Indiano & Ricardo Ruiz
//made with AI and refined by us
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <math.h>

#define BUF_SIZE   4096
#define SAMPLES    10
#define PACKETS    6   // 6 paquetes → 1 minuto

static void stats(const float *d, int n,
                  float *mean, float *min, float *max, float *std) {
    *mean = *min = *max = d[0];
    float sum = d[0];
    for (int i = 1; i < n; i++) {
        sum += d[i];
        if (d[i] < *min) *min = d[i];
        if (d[i] > *max) *max = d[i];
    }
    *mean = sum / n;
    float var = 0;
    for (int i = 0; i < n; i++) {
        float diff = d[i] - *mean;
        var += diff*diff;
    }
    *std = sqrtf(var / n);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int port = atoi(argv[1]);

    // 1. Crear y bindear socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));

    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    srv.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("bind"); close(sockfd); exit(EXIT_FAILURE);
    }

    // 2. Preparar buffers
    char buf[BUF_SIZE];
    int pkt = 0;
    float ax[SAMPLES*PACKETS], ay[SAMPLES*PACKETS], az[SAMPLES*PACKETS];
    float r[SAMPLES*PACKETS],  g[SAMPLES*PACKETS],  b[SAMPLES*PACKETS];

    // 3. Recibir y procesar
    while (1) {
        ssize_t n = recvfrom(sockfd, buf, BUF_SIZE-1, 0, NULL, NULL);
        if (n < 0) { perror("recvfrom"); continue; }
        buf[n] = '\0';

        // Saltar encabezado CSV
        char *line = strtok(buf, "\n");
        int base = pkt * SAMPLES;
        for (int i = 0; i < SAMPLES; i++) {
            line = strtok(NULL, "\n");
            if (!line) break;
            float axv, ayv, azv;
            int rv, gv, bv;
            if (sscanf(line, "%f,%f,%f,%d,%d,%d",
                       &axv, &ayv, &azv, &rv, &gv, &bv) == 6) {
                ax[base+i] = axv;
                ay[base+i] = ayv;
                az[base+i] = azv;
                r [base+i] = rv;
                g [base+i] = gv;
                b [base+i] = bv;
            }
        }

        pkt++;
        if (pkt == PACKETS) {
            float mean, min, max, std;
            printf("=== Estadísticas último minuto ===\n");
            stats(ax, SAMPLES*PACKETS, &mean,&min,&max,&std);
            printf("AX: mean=%.2f min=%.2f max=%.2f std=%.2f\n",
                   mean,min,max,std);
            stats(ay, SAMPLES*PACKETS, &mean,&min,&max,&std);
            printf("AY: mean=%.2f min=%.2f max=%.2f std=%.2f\n",
                   mean,min,max,std);
            stats(az, SAMPLES*PACKETS, &mean,&min,&max,&std);
            printf("AZ: mean=%.2f min=%.2f max=%.2f std=%.2f\n",
                   mean,min,max,std);
            stats(r,  SAMPLES*PACKETS, &mean,&min,&max,&std);
            printf("R : mean=%.2f min=%.2f max=%.2f std=%.2f\n",
                   mean,min,max,std);
            stats(g,  SAMPLES*PACKETS, &mean,&min,&max,&std);
            printf("G : mean=%.2f min=%.2f max=%.2f std=%.2f\n",
                   mean,min,max,std);
            stats(b,  SAMPLES*PACKETS, &mean,&min,&max,&std);
            printf("B : mean=%.2f min=%.2f max=%.2f std=%.2f\n",
                   mean,min,max,std);
            printf("===============================\n\n");
            pkt = 0;
        }
    }

    close(sockfd);
    return 0;
}
