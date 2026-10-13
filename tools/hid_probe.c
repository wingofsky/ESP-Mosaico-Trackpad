// macOS read-only probe for this USB mouse. Never seizes the input device.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>

static int received;
static void report(void *ctx,IOReturn result,void *sender,IOHIDReportType type,
                   uint32_t id,uint8_t *bytes,CFIndex length)
{
    printf("REPORT result=%x id=%u len=%ld data=",result,id,(long)length);
    for(CFIndex i=0;i<length;i++) printf("%02x",bytes[i]);
    putchar('\n');fflush(stdout);received++;
}
int main(int argc,char **argv)
{
    int vid=0x303a,pid=0x4005;
    double seconds=argc>1?atof(argv[1]):15;
    IOHIDManagerRef m=IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);
    CFMutableDictionaryRef match=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFNumberRef v=CFNumberCreate(NULL,kCFNumberIntType,&vid),p=CFNumberCreate(NULL,kCFNumberIntType,&pid);
    CFDictionarySetValue(match,CFSTR(kIOHIDVendorIDKey),v);
    CFDictionarySetValue(match,CFSTR(kIOHIDProductIDKey),p);
    IOHIDManagerSetDeviceMatching(m,match);
    IOReturn r=IOHIDManagerOpen(m,kIOHIDOptionsTypeNone);
    printf("OPEN=%x\n",r);
    CFSetRef devices=IOHIDManagerCopyDevices(m);
    CFIndex count=devices?CFSetGetCount(devices):0;
    printf("DEVICES=%ld\n",(long)count);fflush(stdout);
    if(!count) return 2;
    IOHIDManagerRegisterInputReportCallback(m,report,NULL);
    IOHIDManagerScheduleWithRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
    CGEventRef before=CGEventCreate(NULL);
    CGPoint start=CGEventGetLocation(before);CFRelease(before);
    printf("CURSOR_START=%.1f,%.1f\n",start.x,start.y);fflush(stdout);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);
    printf("REPORTS=%d\n",received);
    IOHIDManagerClose(m,kIOHIDOptionsTypeNone);
    CFRelease(devices);CFRelease(match);CFRelease(v);CFRelease(p);CFRelease(m);
    return received?0:3;
}
