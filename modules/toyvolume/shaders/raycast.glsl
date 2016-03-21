/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2016                                                                    *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/


uniform vec4 color#{id};
uniform float time#{id};
uniform float maxStepSize#{id} = 0.02;

vec4 sample#{id}(vec3 samplePos, vec3 dir, vec4 foregroundColor, inout float maxStepSize) {
    maxStepSize = maxStepSize#{id};

    // Generate an arbitrary procedural volume.
    // In real situations, the sample function would sample a
    // 3D texture to retrieve the color contribution of a given point.
    
    vec3 fromCenter = vec3(0.5, 0.5, 0.5) - samplePos;
    
    vec4 c = color#{id};
    float r = length(fromCenter);
    c.a *= (1.0 - smoothstep(0.4, 0.45, r));
    c.a *= (1.0 - smoothstep(0.35, 0.3, r));
    c.a *= (1.0 - smoothstep(0.1, 0.2, abs(fromCenter.y)));

    float theta = atan(fromCenter.x, fromCenter.z);
    float angularRatio = (theta + 3.1415) / 6.283;

    angularRatio = mod(angularRatio + time#{id}*0.01, 1.0);
    
    c.a *= smoothstep(0.0, 0.2, clamp(angularRatio, 0.0, 1.0));
    c.a *= smoothstep(1.0, 0.8, clamp(angularRatio, 0.0, 1.0));

    return c;
}

float stepSize#{id}(vec3 samplePos, vec3 dir) {
    return maxStepSize#{id};
}
