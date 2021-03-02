varying highp    vec4 var_position;
varying mediump  vec3 var_normal;
varying mediump  vec2 var_texcoord;
varying lowp     vec4 var_color;
varying mediump  vec4 var_light;

uniform lowp sampler2D tex0;
uniform lowp vec4 tint;

void main()
{
    // Pre-multiply alpha since all runtime textures already are
    vec4 tint_pm = vec4(1);//vec4(tint.xyz * tint.w, tint.w);
    vec4 color = texture2D(tex0, var_texcoord.xy) * tint_pm;

    // Diffuse light calculations
    vec3 ambient_light = vec3(0.2);
    vec3 diff_light = vec3(normalize(var_light.xyz - var_position.xyz));
    float intensity = max(dot(var_normal,diff_light), 0.0);
    intensity = clamp(intensity, 0.0, 1.0);
    //intensity = vec3(1.0);

    //gl_FragColor = vec4(color.rgb*intensity,1.0) + vec4(ambient_light, 1);
    //gl_FragColor = vec4(color.rgb*var_color.xyz,1.0);
    gl_FragColor = vec4(var_normal.rgb*0.5 + 0.5,1.0);
    //gl_FragColor = vec4(var_normal.rgb,1.0);
    //gl_FragColor = vec4(-1.0, 1.0, 1.0, 1.0);
}

