author: Marcos Indiano y Ricardo Ruiz
/* client.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <errno.h>


#define SAMPLES    10
#define BUF_SIZE   4096
#define I2C_BUS    "/dev/i2c-1"

// MPU6050
#define MPU_ADDR     0x68
#define PWR_MGMT_1   0x6B
#define ACCEL_XOUT_H 0x3B

// TCS34725
#define TCS_ADDR      0x29
#define CMD_BIT       0x80
#define REG_ENABLE    0x00
#define ENABLE_PON_RGBC (0x01|0x02)
#define REG_ATIME     0x01
#define REG_CONTROL   0x0F
#define CDATA         0x14

static int scale_color(int val, int clear) {
    if (!clear) return 0;
    int scaled = (val * 255) / clear;
    return (scaled > 255) ? 255 : scaled;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <IP_servidor> <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *srv_ip = argv[1];
    int srv_port = atoi(argv[2]);

    // 1. Crear socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return EXIT_FAILURE; }
    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port   = htons(srv_port)
    };
    if (inet_pton(AF_INET, srv_ip, &srv.sin_addr) != 1) {
        perror("inet_pton"); close(sockfd); return EXIT_FAILURE;
    }
    socklen_t srv_len = sizeof(srv);

    // 2. Inicializar I2C para MPU6050
    int fd_mpu = open(I2C_BUS, O_RDWR);
    if (fd_mpu < 0) { perror("open mpu"); close(sockfd); return EXIT_FAILURE; }
    if (ioctl(fd_mpu, I2C_SLAVE, MPU_ADDR) < 0) {
        perror("ioctl mpu"); close(fd_mpu); close(sockfd); return EXIT_FAILURE;
    }
    uint8_t wake[2] = { PWR_MGMT_1, 0x00 };
    if (write(fd_mpu, wake, 2) != 2) perror("wake mpu");

    // 3. Inicializar I2C para TCS34725
    int fd_tcs = open(I2C_BUS, O_RDWR);
    if (fd_tcs < 0) { perror("open tcs"); close(fd_mpu); close(sockfd); return EXIT_FAILURE; }
    if (ioctl(fd_tcs, I2C_SLAVE, TCS_ADDR) < 0) {
        perror("ioctl tcs"); close(fd_tcs); close(fd_mpu); close(sockfd); return EXIT_FAILURE;
    }
    // Power ON + RGBC
    {
        uint8_t buf[2];
        buf[0] = CMD_BIT | REG_ENABLE;
        buf[1] = ENABLE_PON_RGBC;
        if (write(fd_tcs, buf, 2) != 2) {
            perror("write ENABLE");
            // manejar error si quieres
        }
    }
    usleep(3000);

    // Integración 700 ms
    {
        uint8_t buf[2];
        buf[0] = CMD_BIT | REG_ATIME;
        buf[1] = 0x00;
        if (write(fd_tcs, buf, 2) != 2) {
            perror("write ATIME");
        }
    }

    // Ganancia 1×
    {
        uint8_t buf[2];
        buf[0] = CMD_BIT | REG_CONTROL;
        buf[1] = 0x00;
        if (write(fd_tcs, buf, 2) != 2) {
            perror("write CONTROL");
        }
    }
    // 4. Bucle de muestreo y envío cada 10 s
    float ax[SAMPLES], ay[SAMPLES], az[SAMPLES];
    int   r[SAMPLES], g[SAMPLES], b[SAMPLES];
    char buf[BUF_SIZE];
    char mensaje[1024];
    char token[] = "vLbODfxFkU0BHXvjLsap";
    while (1) {
        for (int i = 0; i < SAMPLES; i++) {
            sleep(1);
            // MPU6050
            uint8_t reg = ACCEL_XOUT_H;
            write(fd_mpu, &reg, 1);
            uint8_t mpu_dat[6];
            if (read(fd_mpu, mpu_dat, 6) != 6) perror("mpu read");
            int16_t vx = (mpu_dat[0]<<8)|mpu_dat[1],
                    vy = (mpu_dat[2]<<8)|mpu_dat[3],
                    vz = (mpu_dat[4]<<8)|mpu_dat[5];
            ax[i] = vx/16384.0f; ay[i] = vy/16384.0f; az[i] = vz/16384.0f;

            // TCS34725
            reg = CMD_BIT | CDATA;
            write(fd_tcs, &reg, 1);
            uint8_t tcs_dat[8];
            if (read(fd_tcs, tcs_dat, 8) != 8) perror("tcs read");
            int clear = (tcs_dat[1]<<8)|tcs_dat[0];
            r[i] = scale_color((tcs_dat[3]<<8)|tcs_dat[2], clear);
            g[i] = scale_color((tcs_dat[5]<<8)|tcs_dat[4], clear);
            b[i] = scale_color((tcs_dat[7]<<8)|tcs_dat[6], clear);
        }

        // Serializar CSV
        int len = snprintf(buf, BUF_SIZE, "AX,AY,AZ,R,G,B\n");
        for (int i = 0; i < SAMPLES; i++) {
            len += snprintf(buf+len, BUF_SIZE-len,
                            "%.2f,%.2f,%.2f,%d,%d,%d\n",
                            ax[i], ay[i], az[i], r[i], g[i], b[i]);
        }

        // Enviar
        if (sendto(sockfd, buf, len, 0,
                   (struct sockaddr*)&srv, srv_len) < 0) {
            perror("sendto");
        }
        snprintf(mensaje, sizeof(mensaje),"mosquitto_pub -d -q 1 -h %s -p 1883 -t v1/devices/me/telemetry -u \"%s\" ""-m \"{\\\"ax\\\": %.2f, \\\"ay\\\": %.2f, \\\"az\\\": %.2f, \\\"R\\\": %d, \\\"G\\\": %d, \\\"B\\\": %d}\"",srv_ip,token,ax[5],ay[5],az[5],r[5],g[5],b[5]);
        system(mensaje);
    }


    close(fd_mpu);
    close(fd_tcs);
    close(sockfd);
    return 0;
}
