#pragma once
#include "helperstructs.h"
#include "helperfuncs.h"

template <typename VistaProtocolDeriv>
class VistaProtocol 
{
public:
    bool check_send_Q(QueueHandle_t sendQueue, SendPacket &pkt, bool &req_to_send) 
    {
        return static_cast<VistaProtocolDeriv*>(this)->check_send_Q_impl(sendQueue, pkt, req_to_send);
    }

    int handle_UART_events(const QueueHandle_t uartevtQueue, const TaskHandle_t rx_tx_task_Handle, const TaskHandle_t monitor_rx_task_Handle,
                int uartNum, int rxPin, bool &req_to_send, bool &pulse_marked, int64_t &pulse_mark_time, bool &is_2400,
                const SendPacket &pkt_to_send, uint8_t * buf)
    {
        return static_cast<VistaProtocolDeriv*>(this)->handle_UART_events_impl(uartevtQueue, rx_tx_task_Handle, monitor_rx_task_Handle,
                uartNum, rxPin, req_to_send, pulse_marked, pulse_mark_time, is_2400, pkt_to_send, buf);
    }

    int monitor_task_sync(int extuartNum, uint8_t * buf, uint32_t &val, QueueHandle_t receiveQueue, ReceivedPacket &rcvd_extPkt)
    {
        return static_cast<VistaProtocolDeriv*>(this)->monitor_task_sync_impl(extuartNum, buf, val, receiveQueue, rcvd_extPkt);
    }
    bool legacy_protocol()
    {   
        return static_cast<VistaProtocolDeriv*>(this)->legacy_protocol_impl();
    }
};



