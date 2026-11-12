/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* math.h for OPNCPU.PRG (opncpu.c): src/opnfast.c's tables use these at
   its start only, and opncpu.c replaces their results by opntab.h's */
#ifndef OPNCPU_MATH_H
#define OPNCPU_MATH_H
#define M_PI 3.14159265358979323846
double sin(double x);
double pow(double x, double y);
double floor(double x);
#endif
