#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "../vista_alarm.h"

namespace esphome 
{
    namespace alarm_panel {

        class VistaTextSensor : public text_sensor::TextSensor, public vistaECPTextSensor, public Parented<VistaESPHome>
        {
            public:
                void process(std::string text) override;
            protected:
        };

    }  // namespace alarm_panel
}   // namespace esphome