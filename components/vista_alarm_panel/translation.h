// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

// translate extended hex values to correct extended ascii values
// Swedish user provided examples of characters not displaying correctly

// using known values provided in
// https://github.com/pletch/esphome-vistaECP-idf/issues/3#issuecomment-2780955362
extern inline char shift_extended_char(char extended_char) {
  switch (extended_char) {
    case 0xE1:
      return 0xE4;
    case 0xEF:
      return 0xF6;
    default:
      return extended_char;
  }
}
