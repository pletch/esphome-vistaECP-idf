#include "vista_binary_sensor.h"

namespace esphome 
{
    namespace alarm_panel 
    {
        void VistaBinarySensor::process(bool triggered) 
        {
            this->publish_state(triggered);  
        }
    }  // namespace alarm_panel
}  // namespace esphome
