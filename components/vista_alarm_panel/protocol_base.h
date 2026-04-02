#pragma once

#include "ecp_protocol.h"
#include "vistabus.h"

template <typename ProtocolType>
class ProtocolBase : public VistaECP
{
public:
    using VistaECP::VistaECP;
    void check_send_Q(SendPacket &pkt) 
    {
        static_cast<ProtocolType*>(this)->check_send_Q_impl(pkt);
    }

    int handle_UART_events(const SendPacket &pkt_to_send, uint8_t * buf)
    {
        return static_cast<ProtocolType*>(this)->handle_UART_events_impl(pkt_to_send, buf);
    }

    int monitor_task_sync(uint8_t * buf, uint32_t &val, ReceivedPacket &rcvd_extPkt)
    {
        return static_cast<ProtocolType*>(this)->monitor_task_sync_impl(buf, val, rcvd_extPkt);
    }

    void processFA(const char * cbuf)
    {
        static_cast<ProtocolType*>(this)->processFA_impl(cbuf);
    }

private:

};



