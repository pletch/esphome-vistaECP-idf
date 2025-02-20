#include "vista_text_sensor.h"

namespace esphome 
{
    namespace alarm_panel 
    {

        void VistaTextSensor::process(std::string text) 
        {
            this->publish_state(text);  
        }

    }  // namespace alarm_panel
}  // namespace esphome
