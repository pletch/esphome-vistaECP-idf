#pragma once

#include <string>

namespace esphome
{
    namespace alarm_panel
    {
        class vistaECPBinarySensor
        {
        public:
            virtual void process(bool triggered) = 0;
            virtual ~vistaECPBinarySensor() = default;
        };

        class vistaECPTextSensor
        {
        public:
            virtual void process(std::string text) = 0;
            virtual ~vistaECPTextSensor() = default;
        };

    } // namespace alarm_panel
} // namespace esphome
