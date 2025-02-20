#ifndef __ALARM_TEXT_SENSOR_H__
#define __ALARM_TEXT_SENSOR_H__

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

#endif //guard