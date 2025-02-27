#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "../vistaalarm.h"

namespace esphome 
{
    namespace alarm_panel {

        class VistaTextSensor : public text_sensor::TextSensor, public vistaECPTextSensor, public Parented<vistaECPHome>
        {
            public:
                void process(std::string text) override;
            protected:
        };

    }  // namespace alarm_panel
}   // namespace esphome