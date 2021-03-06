#!/bin/bash
#Evgeny Stepanischev May 2013 /*
IFS=: GCC=$(for dir in $PATH; do
    ls -1 $dir 2>&- | grep -Eo 'gcc-\d+.\d+'
done | sort -rn | head -1)
[ -z "$GCC" ] && GCC=gcc

sed '1,12d' "$0" | $GCC -framework CoreFoundation -framework IOKit -o macgreener -O3 -xc -std=c99 -
error=$?
[ $error -eq 0 ] && strip macgreener && /bin/cp -f macgreener /usr/local/bin
exit $error
*/
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define EXITONFAIL(code) do { if (errcode != KERN_SUCCESS) {\
    fprintf(stderr, "Error code: 0x%X\n", GETCODE(errcode));\
    return code;\
} } while (0)

#define GETCODE(err) ((err)&0x3fff)
#define SLEEP true
#define AWAKE false

#define THRESHOLD 10
#define ANGLE 10

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

    int16_t prevz, currz, diffz;
    bool inited = false, sleeping = false;

    for(;;) {
        errcode = IOConnectCallStructMethod(connect, 5, in, osize, out, &isize);
        EXITONFAIL(-5);

        currz = out->z;

        if (inited) {
            if (abs(currz - prevz) > THRESHOLD) {
                if (prevz > currz && !sleeping && abs(out->x) >= ANGLE) {
                    macSleepAwake(SLEEP);
                    sleeping = true;
                } else if (sleeping) {
                    if (abs(out->x) < ANGLE && abs(out->y) < ANGLE) {
                        macSleepAwake(AWAKE);
                        sleeping = false;
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