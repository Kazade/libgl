#pragma once

typedef struct ParameterEquation {
    float a;
    float b;
    float c;
} ParameterEquation;

struct EdgeEquation;

void ParameterEquationInit(
    ParameterEquation* equation,
    float p0, float p1, float p2,
    const EdgeEquation* e0, const EdgeEquation* e1, const EdgeEquation* e2, float area);

float ParameterEquationEvaluate(const ParameterEquation* equation, float x, float y);
