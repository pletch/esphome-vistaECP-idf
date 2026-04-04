#pragma once

#include "ecp_protocol.h"
#include "vista_bus.h"

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

    int monitor_task_sync(uint8_t * buf, uint32_t &val)
    {
        return static_cast<ProtocolType*>(this)->monitor_task_sync_impl(buf, val);
    }

    void quick_decodeFA(const char * cbuf)
    {
        static_cast<ProtocolType*>(this)->quick_decodeFA_impl(cbuf);
    }

    void dispatchFA()
    {
        static_cast<ProtocolType*>(this)->dispatchFA_impl();
    }

private:


};



