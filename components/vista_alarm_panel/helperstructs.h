#pragma once
struct ReceivedPacket  
{
    int type; //0 = yellow wire, 1 = green wire
    int source{0};
    char payload[48];
    int size; 
};

struct SendPacket  
{
    int type{-1}; //0 = hex, 1 = text, 2 = no_ack_expected
    char payload[24];
    int keypadaddress;
    int size;
    char sequence;
};

struct gpioTaskArgs  
{
    TaskHandle_t task_handle;
    int pin;
};