#pragma once
#include "helperstructs.h"
#include "helperfuncs.h"
#define PULSE_CYCLE_PERIOD 550 // maximum period of long low pulse cycle on yellow wire. sets maximum delay of rx_tx_task looping.
                             // Vista-20p pulse cycle is 330 ms but older panel such as 4140XMPT2 cycle is 525 ms.

template <typename VistaProtocolDeriv>
class VistaProtocol 
{
public:
    void check_send_Q(const QueueHandle_t sendQueue, SendPacket &pkt) 
    {
        static_cast<VistaProtocolDeriv*>(this)->check_send_Q_impl(sendQueue, pkt);
    }

    int handle_UART_events(const QueueHandle_t uartevtQueue, const TaskHandle_t rx_tx_task_Handle, 
            const TaskHandle_t monitor_rx_task_Handle, int uartNum, int rxPin, const SendPacket &pkt_to_send, uint8_t * buf)
    {
        return static_cast<VistaProtocolDeriv*>(this)->handle_UART_events_impl(uartevtQueue, rx_tx_task_Handle, monitor_rx_task_Handle,
                uartNum, rxPin, pkt_to_send, buf);
    }

    int monitor_task_sync(int extuartNum, uint8_t * buf, uint32_t &val, const QueueHandle_t receiveQueue, ReceivedPacket &rcvd_extPkt)
    {
        return static_cast<VistaProtocolDeriv*>(this)->monitor_task_sync_impl(extuartNum, buf, val, receiveQueue, rcvd_extPkt);
    }
    bool legacy_protocol()
    {   
        return static_cast<VistaProtocolDeriv*>(this)->legacy_protocol_impl();
    }
    
    int64_t pulse_mark_time = 0;
    bool pulse_marked = false;
    bool req_to_send = false;
    bool is_2400 = false;
    bool legacy_programmode = false;
};



