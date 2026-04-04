#pragma once

 //EN
    //Looks for the <space>*<space> found in the "Hit * to view messages".
    extern const char * HITSTAR;      
    
    //messages to display to home assistant

    extern const char * STATUS_ARMED;
    extern const char * STATUS_STAY;
    extern const char * STATUS_NIGHT ;
    extern const char * STATUS_OFF ;
    extern const char * STATUS_ONLINE;
    extern const char * STATUS_OFFLINE;
    extern const char * STATUS_TRIGGERED;
    extern const char * STATUS_READY;
    extern const char * STATUS_ARMING;
    extern const char * STATUS_PENDING;
      
    //the default ha alarm panel card likes to see "unavailable" instead of not_ready when the system can't be armed
    extern const char * STATUS_NOT_READY;
    extern const char * MSG_ZONE_BYPASS;
    extern const char * MSG_ARMED_BYPASS;
    extern const char * MSG_NO_ENTRY_DELAY;
    extern const char * MSG_NONE;