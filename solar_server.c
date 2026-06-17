#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/i2c-dev.h>

#define I2C_BUS             "/dev/i2c-3"
#define INA226_ADDRESS      0x40
#define WEB_PORT            8081

#define CONFIG_REG          0x00
#define BUSVOLTAGE_REG      0x02
#define POWER_REG           0x03
#define CURRENT_REG         0x04
#define CALIBRATION_REG     0x05

#define RELAY1_GPIO         70
#define RELAY2_GPIO         69
#define MAX_LOGS            10
#define SETTINGS_PATH       "/var/lib/solar_relay.conf"

float v_low_1 = 12.1, v_high_1 = 13.2, c_high_1 = 150.0;
float v_low_2 = 12.1, v_high_2 = 13.2;

int relay1_state = 0;
int relay2_state = 0;

float v_now = 0.0, c_now = 0.0, p_now = 0.0;
float peak_v = 0.0, peak_c = 0.0, peak_p = 0.0;
char peak_v_time[16] = "N/A";
char peak_c_time[16] = "N/A";
float total_Wh = 0.0;

uint64_t relay1_total_on_ms = 0;
uint64_t relay1_last_activation_ms = 0;
uint64_t relay2_total_on_ms = 0;
uint64_t relay2_last_activation_ms = 0;

uint64_t debounce1_delay_ms = 60000;
uint64_t debounce1_timer_start = 0;
uint64_t debounce2_delay_ms = 10000;
uint64_t debounce2_timer_start = 0;

float v_filtered = -1.0;
float c_filtered = -1.0;
int daily_reset_done = 0;

char eventLogs[MAX_LOGS][64];
int logCount = 0;

uint64_t get_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void get_time_string_short(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t->tm_year < 120) { snprintf(buf, len, "N/A"); return; }    strftime(buf, len, "%H:%M:%S", t);
}

void get_time_string_full(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t->tm_year < 120) { snprintf(buf, len, "Time Not Synced"); return; }
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", t);
}

void add_log(const char *msg) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32] = "[No Time] ";
    if (t->tm_year > 120) {
        char buff[20];
        strftime(buff, sizeof(buff), "%d/%m %H:%M:%S", t);
        snprintf(timestamp, sizeof(timestamp), "[%s] ", buff);
    }
    char entry[64];
    snprintf(entry, sizeof(entry), "%s%s", timestamp, msg);
    if (logCount < MAX_LOGS) {
        strncpy(eventLogs[logCount], entry, sizeof(eventLogs[logCount]) - 1);
        logCount++;
    } else {
        for (int i = 0; i < MAX_LOGS - 1; i++) strcpy(eventLogs[i], eventLogs[i + 1]);
        strncpy(eventLogs[MAX_LOGS - 1], entry, sizeof(eventLogs[MAX_LOGS - 1]) - 1);
    }
}

void gpio_export(int gpio) {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) { char buf[16]; snprintf(buf, sizeof(buf), "%d", gpio); write(fd, buf, strlen(buf)); close(fd); }
}

void gpio_set_direction(int gpio, const char *dir) {
    char path[64]; snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY); if (fd >= 0) { write(fd, dir, strlen(dir)); close(fd); }
}

void gpio_write(int gpio, int value) {
    char path[64]; snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_WRONLY); if (fd >= 0) { char c = value ? '1' : '0'; write(fd, &c, 1); close(fd); }
}

void load_settings(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (f) {
        if (fscanf(f, "%f %f %f %d %f %f %d", &v_low_1, &v_high_1, &c_high_1, &relay1_state, &v_low_2, &v_high_2, &relay2_state) != 7) {
            v_low_1 = 12.1; v_high_1 = 13.2; c_high_1 = 150.0; relay1_state = 0; v_low_2 = 12.1; v_high_2 = 13.2; relay2_state = 0;
        }
        fclose(f);
    }
}

void save_settings(void) {
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (f) {
        fprintf(f, "%.2f %.2f %.2f %d %.2f %.2f %d", v_low_1, v_high_1, c_high_1, relay1_state, v_low_2, v_high_2, relay2_state);
        fclose(f);
    }
}

void update_relay_timing(int relay, int newState, int oldState) {
    uint64_t now = get_millis();
    if (relay == 1) {
        if (newState == 1 && oldState == 0) relay1_last_activation_ms = now;
        else if (newState == 0 && oldState == 1) {
            if (relay1_last_activation_ms > 0) relay1_total_on_ms += (now - relay1_last_activation_ms);
            relay1_last_activation_ms = 0;
        }
    } else if (relay == 2) {
        if (newState == 1 && oldState == 0) relay2_last_activation_ms = now;
        else if (newState == 0 && oldState == 1) {
            if (relay2_last_activation_ms > 0) relay2_total_on_ms += (now - relay2_last_activation_ms);
            relay2_last_activation_ms = 0;
        }
    }
}

void get_relay_on_time_string(int relay, int current_hardware_state, char *buf, size_t len) {
    uint64_t total_ms = 0, now = get_millis();
    if (relay == 1) {
        uint64_t current_session = (current_hardware_state == 1 && relay1_last_activation_ms > 0) ? now - relay1_last_activation_ms : 0;
        total_ms = relay1_total_on_ms + current_session;
    } else if (relay == 2) {
        uint64_t current_session = (current_hardware_state == 1 && relay2_last_activation_ms > 0) ? now - relay2_last_activation_ms : 0;
        total_ms = relay2_total_on_ms + current_session;
    }
    uint64_t total_secs = total_ms / 1000;
    snprintf(buf, len, "%dh %dm", (int)(total_secs / 3600), (int)((total_secs % 3600) / 60));
}

void reset_stats(void) {
    peak_v = 0.0; peak_c = 0.0; peak_p = 0.0; total_Wh = 0.0;
    strcpy(peak_v_time, "N/A"); strcpy(peak_c_time, "N/A");
    relay1_total_on_ms = 0; relay1_last_activation_ms = relay1_state ? get_millis() : 0;
    relay2_total_on_ms = 0; relay2_last_activation_ms = relay2_state ? get_millis() : 0;
}

void check_daily_reset(void) {
    time_t now = time(NULL); struct tm *t = localtime(&now);
    if (t->tm_year > 120) {
        if (t->tm_hour == 6 && t->tm_min == 30 && !daily_reset_done) {
            reset_stats(); add_log("Daily 6:30AM Reset"); daily_reset_done = 1;
        } else if (t->tm_hour != 6 || t->tm_min != 30) daily_reset_done = 0;
    }
}

float get_battery_percentage(float v, float c_mA) {
    float low = v_low_1;
    float high = (c_mA > 50.0) ? 14.7 : 12.85;
    if (v <= low) return 0.0;
    if (v >= high) return 100.0;
    return ((v - low) / (high - low)) * 100.0;
}

int write_i2c_reg(int file, uint8_t reg, uint16_t value) {
    uint8_t buf[3]; buf[0] = reg; buf[1] = (value >> 8) & 0xFF; buf[2] = value & 0xFF;
    return (write(file, buf, 3) == 3) ? 0 : -1;
}

int16_t read_i2c_reg(int file, uint8_t reg, int *success) {
    uint8_t buf[2]; buf[0] = reg;
    if (write(file, buf, 1) != 1) { *success = 0; return 0; }
    if (read(file, buf, 2) != 2) { *success = 0; return 0; }
    *success = 1;
    return (int16_t)((buf[0] << 8) | buf[1]);
}

void handle_http_client(int client_fd) {
    char buffer[2048] = {0};
    read(client_fd, buffer, sizeof(buffer) - 1);

    if (strstr(buffer, "OPTIONS /") != NULL) {
        const char *cors = "HTTP/1.1 204 No Content\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                           "Access-Control-Allow-Headers: Content-Type\r\n"
                           "Content-Length: 0\r\n\r\n";
        write(client_fd, cors, strlen(cors));
        close(client_fd);
        return;
    }

    if (strstr(buffer, "GET /api/data") != NULL) {
        char time_str[32], uptime1[16], uptime2[16], json[2048];        get_time_string_full(time_str, sizeof(time_str));
        get_relay_on_time_string(1, relay1_state, uptime1, sizeof(uptime1));
        get_relay_on_time_string(2, relay2_state, uptime2, sizeof(uptime2));

        int pos = snprintf(json, sizeof(json),
            "{\n"
            "  \"voltage\": %.2f, \"current_ma\": %.1f, \"power_mw\": %.1f,\n"
            "  \"peak_v\": %.2f, \"peak_v_time\": \"%s\", \"peak_c_ma\": %.1f, \"peak_c_time\": \"%s\", \"peak_p_mw\": %.1f,\n"
            "  \"energy_wh\": %.3f, \"battery_pct\": %.1f, \"is_charging\": %d,\n"
            "  \"relay\": %d, \"uptime_relay\": \"%s\", \"relay2\": %d, \"uptime_relay2\": \"%s\",\n"
            "  \"timestamp\": \"%s\",\n"
            "  \"v_low_1\": %.1f, \"v_high_1\": %.1f, \"c_high_1\": %.1f, \"v_low_2\": %.1f, \"v_high_2\": %.1f,\n"
            "  \"logs\": [",
            v_now, c_now, p_now, peak_v, peak_v_time, peak_c, peak_c_time, peak_p, total_Wh,
            get_battery_percentage(v_now, c_now), (c_now > 50.0) ? 1 : 0,
            relay1_state, uptime1, relay2_state, uptime2, time_str,
            v_low_1, v_high_1, c_high_1, v_low_2, v_high_2);

        for (int i = 0; i < logCount; i++) {
            pos += snprintf(json + pos, sizeof(json) - pos, "\"%s\"%s", eventLogs[i], (i < logCount - 1) ? "," : "");
        }
        snprintf(json + pos, sizeof(json) - pos, "]\n}");

        char response[2560];
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(json), json);
        write(client_fd, response, strlen(response));
    }
    else if (strstr(buffer, "GET /toggle") != NULL) {
        int r = 0, s = 0;
        char *p = strstr(buffer, "relay="); if(p) r = atoi(p + 6);
        p = strstr(buffer, "state="); if(p) s = atoi(p + 6);

        if (r == 1) {
            update_relay_timing(1, s, relay1_state); relay1_state = s; gpio_write(RELAY1_GPIO, s);
            char msg[32]; snprintf(msg, sizeof(msg), "Manual R1 -> %s", s ? "ON" : "OFF"); add_log(msg);
        } else if (r == 2) {
            update_relay_timing(2, s, relay2_state); relay2_state = s; gpio_write(RELAY2_GPIO, s);
            char msg[32]; snprintf(msg, sizeof(msg), "Manual R2 -> %s", s ? "ON" : "OFF"); add_log(msg);
        }
        save_settings();

        const char *json_res = "{\"status\":\"success\"}";
        char response[256];
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(json_res), json_res);
        write(client_fd, response, strlen(response));
    }
    else if (strstr(buffer, "POST /save") != NULL) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) {
            body += 4;
            float vl1, vh1, ch1, vl2, vh2;
            if (sscanf(body, "v_low=%f&v_high=%f&c_high=%f&v_low_2=%f&v_high_2=%f", &vl1, &vh1, &ch1, &vl2, &vh2) == 5) {
                v_low_1 = vl1; v_high_1 = vh1; c_high_1 = ch1; v_low_2 = vl2; v_high_2 = vh2;
                save_settings(); add_log("Settings updated");
            }
        }
        const char *json_res = "{\"status\":\"success\"}";
        char response[256];
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(json_res), json_res);
        write(client_fd, response, strlen(response));
    }
    else {
        const char *json_err = "{\"error\":\"not_found\"}";
        char response[256];
        snprintf(response, sizeof(response),
            "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(json_err), json_err);
        write(client_fd, response, strlen(response));
    }
    close(client_fd);
}

int main(void) {
    load_settings();

    gpio_export(RELAY1_GPIO); gpio_export(RELAY2_GPIO);

    usleep(100000);

    gpio_set_direction(RELAY1_GPIO, "out");
    gpio_set_direction(RELAY2_GPIO, "out");

    gpio_write(RELAY1_GPIO, relay1_state);
    gpio_write(RELAY2_GPIO, relay2_state);

    int i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0 || ioctl(i2c_fd, I2C_SLAVE, INA226_ADDRESS) < 0) {
        add_log("INA226 connection failed"); return 1;
    }
    write_i2c_reg(i2c_fd, CONFIG_REG, 0x4127);
    write_i2c_reg(i2c_fd, CALIBRATION_REG, 0x0200);
    add_log("System Initialized");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(WEB_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind Port 8081 gagal"); return 1;
    }
    listen(server_fd, 10);
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    uint64_t last_read = get_millis();
    uint64_t current_interval = 2000;

    while (1) {
        uint64_t now_ms = get_millis();

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd >= 0) {
            handle_http_client(client_fd);
        }

        if (now_ms - last_read >= current_interval) {
            float time_diff_hours = (now_ms - last_read) / 3600000.0;
            last_read = now_ms;

            int s1, s2, s3;
            float v = read_i2c_reg(i2c_fd, BUSVOLTAGE_REG, &s1) * 0.00125;
            int16_t raw_curr = read_i2c_reg(i2c_fd, CURRENT_REG, &s2);
            float p = (float)read_i2c_reg(i2c_fd, POWER_REG, &s3) * 25.0;

            if (!s1 || !s2 || !s3) {
                add_log("I2C read error");
            } else if (v >= 1.0) {
                v_now = v; c_now = (float)raw_curr; p_now = p;
                if (v > peak_v) { peak_v = v; get_time_string_short(peak_v_time, sizeof(peak_v_time)); }
                if (c_now > peak_c) { peak_c = c_now; get_time_string_short(peak_c_time, sizeof(peak_c_time)); }
                if (p > peak_p) peak_p = p;

                total_Wh += (p / 1000.0) * time_diff_hours;

                if (v_filtered < 0.0) { v_filtered = v; c_filtered = c_now; }
                else { v_filtered = (v_filtered * 0.8) + (v * 0.2); c_filtered = (c_filtered * 0.8) + (c_now * 0.2); }

                int in_critical = (fabs(v_filtered - v_high_1) < 0.2) || (fabs(v_filtered - v_low_1) < 0.2) ||
                                  (fabs(v_filtered - v_high_2) < 0.2) || (fabs(v_filtered - v_low_2) < 0.2);
                current_interval = in_critical ? 1000 : 2000;

                int desired_1 = relay1_state;
                if (v_filtered <= v_low_1) desired_1 = 1;
                else if (v_filtered >= v_high_1 && c_filtered >= c_high_1) desired_1 = 0;

                if (desired_1 != relay1_state) {
                    if (debounce1_timer_start == 0) debounce1_timer_start = get_millis();
                    if (get_millis() - debounce1_timer_start >= debounce1_delay_ms) {
                        update_relay_timing(1, desired_1, relay1_state);
                        relay1_state = desired_1; gpio_write(RELAY1_GPIO, desired_1);
                        debounce1_timer_start = 0; char msg[32];                        snprintf(msg, sizeof(msg), "Relay1 -> %s", desired_1 ? "ON" : "OFF"); add_log(msg); save_settings();
                    }
                } else debounce1_timer_start = 0;

                int desired_2 = relay2_state;
                if (v_filtered <= v_low_2) desired_2 = 0;
                else if (v_filtered >= v_high_2) desired_2 = 1;

                if (desired_2 != relay2_state) {
                    if (debounce2_timer_start == 0) debounce2_timer_start = get_millis();
                    if (get_millis() - debounce2_timer_start >= debounce2_delay_ms) {
                        update_relay_timing(2, desired_2, relay2_state);
                        relay2_state = desired_2; gpio_write(RELAY2_GPIO, desired_2);
                        debounce2_timer_start = 0; char msg[32];                        snprintf(msg, sizeof(msg), "Relay2 -> %s", desired_2 ? "ON" : "OFF"); add_log(msg); save_settings();
                    }
                } else debounce2_timer_start = 0;
            }
            time_t n = time(NULL); struct tm *tm_n = localtime(&n);
            if (tm_n->tm_year > 120) check_daily_reset();
        }
        usleep(10000);
    }
    close(i2c_fd); close(server_fd); return 0;
}
