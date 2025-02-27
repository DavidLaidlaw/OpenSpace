/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2019                                                               *
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

#include "fragment.glsl"
#include "floatoperations.glsl"

// keep in sync with renderablestars.h:ColorOption enum
const int COLOROPTION_COLOR     = 0;
const int COLOROPTION_VELOCITY  = 1; 
const int COLOROPTION_SPEED     = 2;
const int COLOROPTION_OTHERDATA = 3;

uniform sampler1D colorTexture;
uniform sampler2D psfTexture;
uniform float alphaValue;

uniform int colorOption;

uniform sampler1D otherDataTexture;
uniform vec2 otherDataRange;
uniform bool filterOutOfRange;

in vec4 vs_position;
in vec2 psfCoords;
flat in vec4 ge_bvLumAbsMagAppMag;
flat in vec3 ge_velocity;
flat in float ge_speed;
flat in float ge_observationDistance;
flat in float gs_screenSpaceDepth;

vec4 bv2rgb(float bv) {
    // BV is [-0.4,2.0]
    float t = (bv + 0.4) / (2.0 + 0.4);
    return texture(colorTexture, t);
}

bool isOtherDataValueInRange() {
    float t = (ge_bvLumAbsMagAppMag.x - otherDataRange.x) / (otherDataRange.y - otherDataRange.x);
    return t >= 0.0 && t <= 1.0;
}
vec4 otherDataValue() {
    float t = (ge_bvLumAbsMagAppMag.x - otherDataRange.x) / (otherDataRange.y - otherDataRange.x);
    t = clamp(t, 0.0, 1.0);
    return texture(otherDataTexture, t);
}

Fragment getFragment() {
    // Something in the color calculations need to be changed because before it was dependent
    // on the gl blend functions since the abuffer was not involved

    vec4 color = vec4(0.0);
    switch (colorOption) {
        case COLOROPTION_COLOR: 
            color = bv2rgb(ge_bvLumAbsMagAppMag.x);
            break;
        case COLOROPTION_VELOCITY:
            color = vec4(abs(ge_velocity), 0.5);
            break;
        case COLOROPTION_SPEED:
            // @TODO Include a transfer function here ---abock
            color = vec4(vec3(ge_speed), 0.5);
            break;
        case COLOROPTION_OTHERDATA:
            if (filterOutOfRange && !isOtherDataValueInRange()) {
                discard;
            }
            else {
                color = otherDataValue();
            }
            break;
    }

    vec4 textureColor = texture(psfTexture, 0.5*psfCoords + 0.5);
    vec4 fullColor = vec4(color.rgb, textureColor.a);
    fullColor.a *= alphaValue;
    
    if (fullColor.a == 0) {
        discard;
    }

    Fragment frag;
    frag.color     = fullColor;
    frag.depth     = gs_screenSpaceDepth;
    frag.gPosition = vs_position;
    frag.gNormal   = vec4(0.0, 0.0, 0.0, 1.0);
    
    return frag;
}
