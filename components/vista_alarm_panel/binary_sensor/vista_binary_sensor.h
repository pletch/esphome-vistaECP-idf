#ifndef __VISTA_BINARY_SENSOR_H__
#define __VISTA_BINARY_SENSOR_H__

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "../vistaalarm.h"

namespace esphome {
    namespace alarm_panel {

        class VistaBinarySensor : public binary_sensor::BinarySensor, public vistaECPBinarySensor, public Parented<vistaECPHome>
        {
            public:
                void process(bool triggered) override;
            protected:
        };

    }  // namespace alarm_panel
}  // namespace esphome

#endif //guard
