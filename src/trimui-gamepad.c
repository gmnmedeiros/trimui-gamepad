#define _GNU_SOURCE
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef HID_DT_HID
#define HID_DT_HID 0x21
#endif
#ifndef HID_DT_REPORT
#define HID_DT_REPORT 0x22
#endif

#define REPORT_SIZE 11
#define READY_FILE "/tmp/trimui-gamepad-ffs-ready"
#define STOP_FILE "/tmp/trimui-gamepad-stop"
#define STATUS_FILE "/tmp/trimui-gamepad-status"
#define STATUS_TEMP_FILE "/tmp/trimui-gamepad-status.tmp"
#define EP0_PATH "/dev/ffs-gamepad/ep0"
#define EP1_PATH "/dev/ffs-gamepad/ep81"

struct hid_class_descriptor_local {
    uint8_t bDescriptorType;
    uint16_t wDescriptorLength;
} __attribute__((packed));

struct hid_descriptor_local {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    struct hid_class_descriptor_local desc[1];
} __attribute__((packed));

struct descriptor_set {
    struct usb_interface_descriptor interface;
    struct hid_descriptor_local hid;
    struct usb_endpoint_descriptor_no_audio endpoint;
} __attribute__((packed));

struct descriptors_blob {
    struct usb_functionfs_descs_head_v2 header;
    uint32_t fs_count;
    uint32_t hs_count;
    struct descriptor_set fs;
    struct descriptor_set hs;
} __attribute__((packed));

/* Google Stadia USB controller report descriptor (VID:PID 18d1:9400).
 * Steam has a dedicated enabled HIDAPI driver for this standard HID protocol.
 */
static const uint8_t report_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x03,
    0x05, 0x01, 0x75, 0x04, 0x95, 0x01, 0x25, 0x07,
    0x46, 0x3B, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81,
    0x42, 0x45, 0x00, 0x65, 0x00, 0x75, 0x01, 0x95,
    0x04, 0x81, 0x01, 0x05, 0x09, 0x15, 0x00, 0x25,
    0x01, 0x75, 0x01, 0x95, 0x0F, 0x09, 0x12, 0x09,
    0x11, 0x09, 0x14, 0x09, 0x13, 0x09, 0x0D, 0x09,
    0x0C, 0x09, 0x0B, 0x09, 0x0F, 0x09, 0x0E, 0x09,
    0x08, 0x09, 0x07, 0x09, 0x05, 0x09, 0x04, 0x09,
    0x02, 0x09, 0x01, 0x81, 0x02, 0x75, 0x01, 0x95,
    0x01, 0x81, 0x01, 0x05, 0x01, 0x15, 0x01, 0x26,
    0xFF, 0x00, 0x09, 0x01, 0xA1, 0x00, 0x09, 0x30,
    0x09, 0x31, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0, 0x09, 0x01, 0xA1, 0x00, 0x09, 0x32, 0x09,
    0x35, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0xC0,
    0x05, 0x02, 0x75, 0x08, 0x95, 0x02, 0x15, 0x00,
    0x26, 0xFF, 0x00, 0x09, 0xC5, 0x09, 0xC4, 0x81,
    0x02, 0x05, 0x0C, 0x15, 0x00, 0x25, 0x01, 0x09,
    0xE9, 0x09, 0xEA, 0x75, 0x01, 0x95, 0x02, 0x81,
    0x02, 0x09, 0xCD, 0x95, 0x01, 0x81, 0x02, 0x95,
    0x05, 0x81, 0x01, 0x85, 0x05, 0x06, 0x0F, 0x00,
    0x09, 0x97, 0x75, 0x10, 0x95, 0x02, 0x27, 0xFF,
    0xFF, 0x00, 0x00, 0x91, 0x02, 0xC0,
};

struct gamepad_report {
    uint16_t buttons;
    uint8_t hat;
    int16_t x;
    int16_t y;
    int16_t rx;
    int16_t ry;
} __attribute__((packed));

struct stadia_report {
    uint8_t report_id;
    uint8_t hat;
    uint8_t system_buttons;
    uint8_t game_buttons;
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t left_trigger;
    uint8_t right_trigger;
    uint8_t consumer_buttons;
} __attribute__((packed));

_Static_assert(sizeof(struct stadia_report) == REPORT_SIZE, "unexpected Stadia report size");

static uint8_t stadia_axis(int16_t value) {
    int scaled = 128 + ((int)value * 127) / 32767;
    if (scaled < 1) return 1;
    if (scaled > 255) return 255;
    return (uint8_t)scaled;
}

static struct stadia_report make_stadia_report(const struct gamepad_report *source) {
    struct stadia_report report;
    memset(&report, 0, sizeof(report));
    report.report_id = 0x03;
    report.hat = source->hat;

    if (source->buttons & (1u << 8)) report.system_buttons |= 0x40;  /* Back */
    if (source->buttons & (1u << 9)) report.system_buttons |= 0x20;  /* Start */
    if (source->buttons & (1u << 10)) report.system_buttons |= 0x10; /* Guide */

    if (source->buttons & (1u << 0)) report.game_buttons |= 0x40; /* A / South */
    if (source->buttons & (1u << 1)) report.game_buttons |= 0x20; /* B / East */
    if (source->buttons & (1u << 2)) report.game_buttons |= 0x10; /* X / West */
    if (source->buttons & (1u << 3)) report.game_buttons |= 0x08; /* Y / North */
    if (source->buttons & (1u << 4)) report.game_buttons |= 0x04; /* L1 */
    if (source->buttons & (1u << 5)) report.game_buttons |= 0x02; /* R1 */

    report.left_x = stadia_axis(source->x);
    report.left_y = stadia_axis(source->y);
    report.right_x = stadia_axis(source->rx);
    report.right_y = stadia_axis(source->ry);
    report.left_trigger = (source->buttons & (1u << 6)) ? 0xFF : 0x00;
    report.right_trigger = (source->buttons & (1u << 7)) ? 0xFF : 0x00;
    return report;
}

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t stop_signal = 0;

static void stop_handler(int signal_number) {
    stop_signal = signal_number;
    running = 0;
}

static int write_all(int fd, const void *buffer, size_t length) {
    const uint8_t *cursor = buffer;
    while (length > 0) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        cursor += written;
        length -= (size_t)written;
    }
    return 0;
}

static struct descriptor_set make_descriptor_set(uint8_t interval) {
    struct descriptor_set set;
    memset(&set, 0, sizeof(set));

    set.interface.bLength = sizeof(set.interface);
    set.interface.bDescriptorType = USB_DT_INTERFACE;
    set.interface.bInterfaceNumber = 0;
    set.interface.bAlternateSetting = 0;
    set.interface.bNumEndpoints = 1;
    set.interface.bInterfaceClass = USB_CLASS_HID;
    set.interface.bInterfaceSubClass = 0;
    set.interface.bInterfaceProtocol = 0;
    set.interface.iInterface = 1;

    set.hid.bLength = sizeof(set.hid);
    set.hid.bDescriptorType = HID_DT_HID;
    set.hid.bcdHID = htole16(0x0111);
    set.hid.bCountryCode = 0;
    set.hid.bNumDescriptors = 1;
    set.hid.desc[0].bDescriptorType = HID_DT_REPORT;
    set.hid.desc[0].wDescriptorLength = htole16(sizeof(report_descriptor));

    set.endpoint.bLength = sizeof(set.endpoint);
    set.endpoint.bDescriptorType = USB_DT_ENDPOINT;
    set.endpoint.bEndpointAddress = USB_DIR_IN | 1;
    set.endpoint.bmAttributes = USB_ENDPOINT_XFER_INT;
    set.endpoint.wMaxPacketSize = htole16(64);
    set.endpoint.bInterval = interval;

    return set;
}

static int initialize_functionfs(int ep0) {
    struct descriptors_blob descriptors;
    memset(&descriptors, 0, sizeof(descriptors));
    descriptors.header.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    descriptors.header.length = htole32(sizeof(descriptors));
    descriptors.header.flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
                                         FUNCTIONFS_VIRTUAL_ADDR);
    descriptors.fs_count = htole32(3);
    descriptors.hs_count = htole32(3);
    descriptors.fs = make_descriptor_set(1);
    descriptors.hs = make_descriptor_set(4);

    if (write_all(ep0, &descriptors, sizeof(descriptors)) < 0) {
        perror("write FunctionFS descriptors");
        return -1;
    }

    static const struct {
        struct usb_functionfs_strings_head header;
        uint16_t language;
        char interface_name[15];
    } __attribute__((packed)) strings = {
        .header = {
            .magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
            .length = htole32(sizeof(strings)),
            .str_count = htole32(1),
            .lang_count = htole32(1),
        },
        .language = htole16(0x0409),
        .interface_name = "TRIMUI Gamepad",
    };

    if (write_all(ep0, &strings, sizeof(strings)) < 0) {
        perror("write FunctionFS strings");
        return -1;
    }

    int ready = open(READY_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ready < 0) {
        perror("create ready file");
        return -1;
    }
    dprintf(ready, "%ld\n", (long)getpid());
    close(ready);
    return 0;
}

static int find_gamepad(void) {
    char path[64];
    char name[256];
    for (int index = 0; index < 32; index++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && strcmp(name, "TRIMUI Player1") == 0) {
            fprintf(stderr, "Using input device %s (%s)\n", path, name);
            return fd;
        }
        close(fd);
    }
    fprintf(stderr, "TRIMUI Player1 input device was not found\n");
    return -1;
}

static int16_t read_axis(int fd, unsigned code) {
    struct input_absinfo info;
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, EVIOCGABS(code), &info) < 0) return 0;
    if (info.value < -32767) return -32767;
    if (info.value > 32767) return 32767;
    return (int16_t)info.value;
}

static int test_bit(const unsigned long *bits, unsigned bit) {
    const unsigned bits_per_word = sizeof(unsigned long) * 8;
    return !!(bits[bit / bits_per_word] & (1UL << (bit % bits_per_word)));
}

static uint16_t button_mask(unsigned code) {
    switch (code) {
        case BTN_EAST:   return 1u << 0;  /* Physical A */
        case BTN_SOUTH:  return 1u << 1;  /* Physical B */
        case BTN_WEST:   return 1u << 2;  /* Physical X */
        case BTN_NORTH:  return 1u << 3;  /* Physical Y */
        case BTN_TL:     return 1u << 4;
        case BTN_TR:     return 1u << 5;
        case BTN_TL2:    return 1u << 6;
        case BTN_TR2:    return 1u << 7;
        case BTN_SELECT: return 1u << 8;
        case BTN_START:  return 1u << 9;
        case BTN_MODE:   return 1u << 10;
        default:         return 0;
    }
}

static uint8_t make_hat(int x, int y) {
    if (y < 0 && x == 0) return 0;
    if (y < 0 && x > 0)  return 1;
    if (y == 0 && x > 0) return 2;
    if (y > 0 && x > 0)  return 3;
    if (y > 0 && x == 0) return 4;
    if (y > 0 && x < 0)  return 5;
    if (y == 0 && x < 0) return 6;
    if (y < 0 && x < 0)  return 7;
    return 8;
}

static void write_status(const struct gamepad_report *report) {
    static char previous[512] = "";
    char status[512];
    snprintf(status, sizeof(status), "buttons=%04x hat=%u x=%d y=%d rx=%d ry=%d",
             report->buttons, report->hat, report->x, report->y, report->rx, report->ry);
    if (strcmp(status, previous) == 0) return;

    int fd = open(STATUS_TEMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    if (write_all(fd, status, strlen(status)) == 0) {
        close(fd);
        if (rename(STATUS_TEMP_FILE, STATUS_FILE) == 0) {
            snprintf(previous, sizeof(previous), "%s", status);
            return;
        }
    } else {
        close(fd);
    }
    unlink(STATUS_TEMP_FILE);
}

static void initialize_report(int input_fd, struct gamepad_report *report, int *hat_x, int *hat_y) {
    memset(report, 0, sizeof(*report));
    report->hat = 8;
    report->x = read_axis(input_fd, ABS_X);
    report->y = read_axis(input_fd, ABS_Y);
    report->rx = read_axis(input_fd, ABS_RX);
    report->ry = read_axis(input_fd, ABS_RY);
    if (read_axis(input_fd, ABS_Z) > 127) report->buttons |= 1u << 6;
    if (read_axis(input_fd, ABS_RZ) > 127) report->buttons |= 1u << 7;
    *hat_x = read_axis(input_fd, ABS_HAT0X);
    *hat_y = read_axis(input_fd, ABS_HAT0Y);
    report->hat = make_hat(*hat_x, *hat_y);

    unsigned long keys[(KEY_MAX + sizeof(unsigned long) * 8) / (sizeof(unsigned long) * 8)];
    memset(keys, 0, sizeof(keys));
    if (ioctl(input_fd, EVIOCGKEY(sizeof(keys)), keys) >= 0) {
        const unsigned codes[] = {BTN_EAST, BTN_SOUTH, BTN_WEST, BTN_NORTH, BTN_TL, BTN_TR,
                                  BTN_TL2, BTN_TR2, BTN_SELECT, BTN_START, BTN_MODE};
        for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
            if (test_bit(keys, codes[i])) report->buttons |= button_mask(codes[i]);
        }
    }
}

static int min_int(int left, int right) {
    return left < right ? left : right;
}

static void stall_ep0(int ep0) {
    char byte;
    (void)read(ep0, &byte, 0);
}

static void ack_ep0(int ep0) {
    (void)write(ep0, "", 0);
}

static void handle_setup(int ep0, const struct usb_ctrlrequest *setup, const struct gamepad_report *report,
                         uint8_t *idle_rate, uint8_t *protocol) {
    uint16_t value = le16toh(setup->wValue);
    uint16_t length = le16toh(setup->wLength);
    uint8_t type = setup->bRequestType;

    if ((type & USB_TYPE_MASK) == USB_TYPE_STANDARD &&
        (type & USB_DIR_IN) == USB_DIR_IN && setup->bRequest == USB_REQ_GET_DESCRIPTOR) {
        uint8_t descriptor_type = (uint8_t)(value >> 8);
        if (descriptor_type == HID_DT_REPORT) {
            (void)write(ep0, report_descriptor, min_int(length, sizeof(report_descriptor)));
            return;
        }
        if (descriptor_type == HID_DT_HID) {
            struct hid_descriptor_local hid = {
                .bLength = sizeof(hid),
                .bDescriptorType = HID_DT_HID,
                .bcdHID = htole16(0x0111),
                .bCountryCode = 0,
                .bNumDescriptors = 1,
                .desc = {{.bDescriptorType = HID_DT_REPORT,
                          .wDescriptorLength = htole16(sizeof(report_descriptor))}},
            };
            (void)write(ep0, &hid, min_int(length, sizeof(hid)));
            return;
        }
    }

    if ((type & USB_TYPE_MASK) == USB_TYPE_CLASS && (type & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
        switch (setup->bRequest) {
            case 0x01: /* GET_REPORT */
                if (type & USB_DIR_IN) {
                    struct stadia_report stadia = make_stadia_report(report);
                    (void)write(ep0, &stadia, min_int(length, sizeof(stadia)));
                    return;
                }
                break;
            case 0x09: /* SET_REPORT */
                if (!(type & USB_DIR_IN)) {
                    uint8_t discard[64];
                    size_t remaining = length;
                    while (remaining > 0) {
                        ssize_t got = read(ep0, discard, remaining < sizeof(discard) ? remaining : sizeof(discard));
                        if (got <= 0) break;
                        remaining -= (size_t)got;
                    }
                    ack_ep0(ep0);
                    return;
                }
                break;
            case 0x02: /* GET_IDLE */
                if (type & USB_DIR_IN) {
                    (void)write(ep0, idle_rate, 1);
                    return;
                }
                break;
            case 0x0A: /* SET_IDLE */
                if (!(type & USB_DIR_IN)) {
                    *idle_rate = (uint8_t)(value >> 8);
                    ack_ep0(ep0);
                    return;
                }
                break;
            case 0x03: /* GET_PROTOCOL */
                if (type & USB_DIR_IN) {
                    (void)write(ep0, protocol, 1);
                    return;
                }
                break;
            case 0x0B: /* SET_PROTOCOL */
                if (!(type & USB_DIR_IN)) {
                    *protocol = (uint8_t)value;
                    ack_ep0(ep0);
                    return;
                }
                break;
        }
    }

    stall_ep0(ep0);
}

static int64_t monotonic_milliseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int main(void) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    signal(SIGHUP, stop_handler);
    unlink(READY_FILE);
    unlink(STOP_FILE);
    unlink(STATUS_FILE);
    unlink(STATUS_TEMP_FILE);

    int ep0 = open(EP0_PATH, O_RDWR);
    if (ep0 < 0) {
        perror("open ep0");
        return 1;
    }
    if (initialize_functionfs(ep0) < 0) {
        close(ep0);
        return 1;
    }

    int input_fd = find_gamepad();
    if (input_fd < 0) {
        close(ep0);
        unlink(READY_FILE);
        return 1;
    }

    struct gamepad_report report;
    int hat_x = 0;
    int hat_y = 0;
    initialize_report(input_fd, &report, &hat_x, &hat_y);
    write_status(&report);

    int ep1 = -1;
    uint8_t idle_rate = 0;
    uint8_t protocol = 0;
    int64_t exit_combo_since = 0;
    int exit_armed = 0;
    int64_t last_report_ms = 0;
    int report_dirty = 0;
    fprintf(stderr, "FunctionFS gamepad is ready (buttons=0x%04x hat=%u axes=%d,%d,%d,%d)\n",
            report.buttons, report.hat, report.x, report.y, report.rx, report.ry);

    while (running) {
        struct pollfd descriptors[2] = {
            {.fd = ep0, .events = POLLIN},
            {.fd = input_fd, .events = POLLIN},
        };
        int polled = poll(descriptors, 2, 100);
        if (polled < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        if (descriptors[0].revents & POLLIN) {
            struct usb_functionfs_event events[4];
            ssize_t bytes = read(ep0, events, sizeof(events));
            if (bytes > 0) {
                size_t count = (size_t)bytes / sizeof(events[0]);
                for (size_t i = 0; i < count; i++) {
                    fprintf(stderr, "FunctionFS event %u\n", events[i].type);
                    switch (events[i].type) {
                        case FUNCTIONFS_ENABLE:
                            if (ep1 < 0) {
                                ep1 = open(EP1_PATH, O_WRONLY | O_NONBLOCK);
                                if (ep1 < 0) perror("open ep1");
                                else {
                                    struct stadia_report stadia = make_stadia_report(&report);
                                    if (write(ep1, &stadia, sizeof(stadia)) == sizeof(stadia)) {
                                        last_report_ms = monotonic_milliseconds();
                                        report_dirty = 0;
                                    }
                                }
                            }
                            break;
                        case FUNCTIONFS_DISABLE:
                        case FUNCTIONFS_UNBIND:
                            if (ep1 >= 0) close(ep1);
                            ep1 = -1;
                            break;
                        case FUNCTIONFS_SETUP:
                            handle_setup(ep0, &events[i].u.setup, &report, &idle_rate, &protocol);
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        if (descriptors[1].revents & POLLIN) {
            struct input_event events[32];
            ssize_t bytes = read(input_fd, events, sizeof(events));
            if (bytes > 0) {
                size_t count = (size_t)bytes / sizeof(events[0]);
                for (size_t i = 0; i < count; i++) {
                    const struct input_event *event = &events[i];
                    if (event->type == EV_KEY) {
                        uint16_t mask = button_mask(event->code);
                        if (mask) {
                            if (event->value) report.buttons |= mask;
                            else report.buttons &= (uint16_t)~mask;
                            report_dirty = 1;
                        }
                    } else if (event->type == EV_ABS) {
                        int value = event->value;
                        if (value < -32767) value = -32767;
                        if (value > 32767) value = 32767;
                        switch (event->code) {
                            case ABS_X: report.x = (int16_t)value; report_dirty = 1; break;
                            case ABS_Y: report.y = (int16_t)value; report_dirty = 1; break;
                            case ABS_RX: report.rx = (int16_t)value; report_dirty = 1; break;
                            case ABS_RY: report.ry = (int16_t)value; report_dirty = 1; break;
                            case ABS_Z:
                                if (value > 127) report.buttons |= 1u << 6;
                                else report.buttons &= (uint16_t)~(1u << 6);
                                report_dirty = 1;
                                break;
                            case ABS_RZ:
                                if (value > 127) report.buttons |= 1u << 7;
                                else report.buttons &= (uint16_t)~(1u << 7);
                                report_dirty = 1;
                                break;
                            case ABS_HAT0X: hat_x = value; report.hat = make_hat(hat_x, hat_y); report_dirty = 1; break;
                            case ABS_HAT0Y: hat_y = value; report.hat = make_hat(hat_x, hat_y); report_dirty = 1; break;
                            default: break;
                        }
                    }
                }
            }
        }

        write_status(&report);

        const uint16_t exit_combo = (1u << 0) | (1u << 1);
        if ((report.buttons & exit_combo) == exit_combo) {
            if (!exit_armed && exit_combo_since == 0) exit_combo_since = monotonic_milliseconds();
            if (!exit_armed && exit_combo_since > 0 &&
                monotonic_milliseconds() - exit_combo_since >= 4000) {
                exit_armed = 1;
            }
        } else {
            if (exit_armed) {
                int stop = open(STOP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (stop >= 0) close(stop);
            }
            exit_armed = 0;
            exit_combo_since = 0;
        }

        int64_t now = monotonic_milliseconds();
        if (ep1 >= 0 && report_dirty && now - last_report_ms >= 8) {
            struct stadia_report stadia = make_stadia_report(&report);
            ssize_t result = write(ep1, &stadia, sizeof(stadia));
            if (result == sizeof(stadia)) {
                report_dirty = 0;
                last_report_ms = now;
            } else if (result < 0 && errno != EAGAIN && errno != ESHUTDOWN && errno != ENODEV) {
                perror("write gamepad report");
            }
        }
    }

    fprintf(stderr, "FunctionFS gamepad stopping (signal=%d buttons=0x%04x)\n",
            (int)stop_signal, report.buttons);
    if (ep1 >= 0) close(ep1);
    close(input_fd);
    close(ep0);
    unlink(READY_FILE);
    unlink(STOP_FILE);
    unlink(STATUS_FILE);
    unlink(STATUS_TEMP_FILE);
    return 0;
}
