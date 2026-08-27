// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "vista_binary_sensor.h"

namespace esphome::alarm_panel {
void VistaBinarySensor::process(bool triggered) { this->publish_state(triggered); }
}  // namespace esphome::alarm_panel
