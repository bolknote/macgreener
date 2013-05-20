#!/bin/bash
#Evgeny Stepanischev May 2013 /*
IFS=: GCC=$(for dir in $PATH; do
    ls -1 $dir 2>&- | grep -Eo 'gcc-\d+.\d+'
done | sort -rn | head -1)
[ -z "$GCC" ] && GCC=gcc

sed '1,11d' "$0" | $GCC -framework CoreFoundation -framework IOKit -o macgreener -xc -
strip macgreener
exit 0
*/
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/param.h>

#define EXITONFAIL(code) do { if (errcode != KERN_SUCCESS) {\
    fprintf(stderr, "Error code: 0x%X\n", GETCODE(errcode));\
    return code;\
} } while (0)

#define GETCODE(err) ((err)&0x3fff)
#define SLEEP true
#define AWAKE false

#define THRESHOLDUP 0.2
#define THRESHOLDDOWN 0.2
#define DOWNANGLE 20

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} ostruct;

void macSleepAwake(bool sleep) {
    io_registry_entry_t r = IORegistryEntryFromPath(
        kIOMasterPortDefault,
        "IOService:/IOResources/IODisplayWrangler"
    );

    if (r) {
        IORegistryEntrySetCFProperty(r, CFSTR("IORequestIdle"), sleep ? kCFBooleanTrue : kCFBooleanFalse);
        IOObjectRelease(r);
    }
}

int main() {
    mach_port_t masterPort;
    kern_return_t errcode;
    io_iterator_t iterator;
    io_object_t service;
    io_connect_t connect;

    void *in;
    ostruct *out;
    size_t osize = 40, isize = 40;

    errcode = IOMasterPort(MACH_PORT_NULL, &masterPort);
    EXITONFAIL(-1);

    CFMutableDictionaryRef matchingDictionary = IOServiceMatching("SMCMotionSensor");

    errcode = IOServiceGetMatchingServices(masterPort, matchingDictionary, &iterator);
    EXITONFAIL(-2);

    service = IOIteratorNext(iterator);
    IOObjectRelease(iterator);

    if (service == kIOReturnNoDevice) {
        return -3;
    }

    errcode = IOServiceOpen(service, mach_task_self(), 0, &connect);
    IOObjectRelease(service);
    EXITONFAIL(-4);

    in = malloc(isize);
    out = malloc(osize);

    if (in == NULL || out == NULL) {
        return -6;
    }

    memset(out, 0, osize);
    memset(in, 1, isize);

    int16_t prevz;
    float dev;
    bool inited = false;

    for(;;) {
        errcode = IOConnectCallStructMethod(connect, 5, in, osize, out, &isize);
        EXITONFAIL(-5);

        int16_t currz = out->z;

        if (inited) {
            float min = MIN(prevz, currz);
            float max = MAX(prevz, currz);

            if (max) {
                float dev = 1 - min / max;

                if (prevz > currz) {
                    if (dev > THRESHOLDUP || abs(out->x) > DOWNANGLE || abs(out->y) > DOWNANGLE) {
                        macSleepAwake(SLEEP);
                    }
                } else {
                    if (dev > THRESHOLDDOWN || (abs(out->x) <= DOWNANGLE || abs(out->y) <= DOWNANGLE)) {
                        macSleepAwake(AWAKE);
                    }
                }
            }
        } else {
            inited = true;
        }

        prevz = currz;
        usleep(10000);
    }

    return 0;
}