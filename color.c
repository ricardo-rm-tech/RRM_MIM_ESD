#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#define I2C_BUS "/dev/i2c-1"
#define TCS34725_ADDRESS 0x29
#define COMMAND_BIT 0x80
#define ENABLE_REGISTER 0x00
#define ENABLE_POWER_ON 0x01
#define ENABLE_RGBC 0x02
#define ATIME_REGISTER 0x01
#define CONTROL_REGISTER 0x0F
#define CDATA 0x14 // Clear data low byte

int scale_color(int value, int clear) {
    if (clear == 0) return 0;
    int scaled = (value * 255) / clear;
    if (scaled > 255) return 255;
    return scaled;
}

int main() {
    int file;
    if ((file = open(I2C_BUS, O_RDWR)) < 0) {
        perror("Failed to open I2C bus");
        exit(1);
    }

    if (ioctl(file, I2C_SLAVE, TCS34725_ADDRESS) < 0) {
        perror("Failed to acquire bus access");
        exit(1);
    }

    // Habilitar sensor (encender + activar RGBC)
    char enable_cmd[] = {COMMAND_BIT | ENABLE_REGISTER, ENABLE_POWER_ON | ENABLE_RGBC};
    write(file, enable_cmd, 2);
    usleep(3000); // espera mínima

    // Tiempo de integración: 700ms (mayor sensibilidad)
    char atime_cmd[] = {COMMAND_BIT | ATIME_REGISTER, 0x00};
    write(file, atime_cmd, 2);

    // Ganancia 1x
    char control_cmd[] = {COMMAND_BIT | CONTROL_REGISTER, 0x00};
    write(file, control_cmd, 2);
    while(1){
    sleep(1); // esperar lectura

    // Leer 8 bytes desde 0x14 (Clear, R, G, B)
    char reg = COMMAND_BIT | CDATA;
    write(file, &reg, 1);
    char data[8];
    read(file, data, 8);

    int c = data[1] << 8 | data[0];
    int r = data[3] << 8 | data[2];
    int g = data[5] << 8 | data[4];
    int b = data[7] << 8 | data[6];

    int red = scale_color(r, c);
    int green = scale_color(g, c);
    int blue = scale_color(b, c);

    printf("Raw: C=%d R=%d G=%d B=%d\n", c, r, g, b);
    printf("Scaled RGB: R=%d G=%d B=%d\n", red, green, blue);
    }
    close(file);
    return 0;
}
