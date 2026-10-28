// Liberty Edge Resolution Unlock - downsample pixel shader (ps_3_0), compiled by CI with fxc into downsample_ps.h
//
// The game renders at a resolution above the monitor and the presented backbuffer is scaled down to the
// window. DXVK's present blit (downscale) reads, for output pixel x, the two texels around
// c = (x + 0.5) * ratio: floor(c) with weight 1 - fract(c) and floor(c) + 1 with weight fract(c). For integer
// ratios that is a single texel, so 3 of every 4 samples are thrown away. This pass writes, into every
// backbuffer texel, the linear-light box average of the footprint of the output pixel that reads it with the
// highest weight (exact for integer ratios), optionally followed by contrast adaptive sharpening.

sampler2D SrcS : register(s0);
float4 gSrc    : register(c0); // backbuffer width, height, 1/width, 1/height
float4 gRatio  : register(c1); // xy: backbuffer / output per axis, zw: output width, height
float4 gParams : register(c2); // x: sharpening [0; 1], y: taps per axis (1..3)

float PickOutput(float q, float r)
{
    float x0 = floor((q + 0.5) / r);
    float best = x0;
    float bw = 0.0;
    [unroll] for (int k = -1; k <= 1; k++)
    {
        float x = x0 + k;
        float c = (x + 0.5) * r;
        float fl = floor(c);
        float f = c - fl;
        float w = (fl == q) ? (1.0 - f) : ((fl + 1.0 == q) ? f : 0.0);
        if (w > bw) { bw = w; best = x; }
    }
    return best;
}

float3 Footprint(float2 x)
{
    x = clamp(x, 0.0, gRatio.zw - 1.0);
    float n = gParams.y;
    float2 st = gRatio.xy / n;
    float2 o = x * gRatio.xy + 0.5 * st;
    float3 s = 0.0;
    [loop] for (int j = 0; j < 3; j++)
    {
        if (j < n)
        {
            [loop] for (int i = 0; i < 3; i++)
            {
                if (i < n)
                {
                    float2 p = o + float2(i, j) * st;
                    s += pow(tex2Dlod(SrcS, float4(p * gSrc.zw, 0.0, 0.0)).rgb, 2.2);
                }
            }
        }
    }
    return s / (n * n);
}

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float2 q = floor(uv * gSrc.xy);
    float2 x = float2(PickOutput(q.x, gRatio.x), PickOutput(q.y, gRatio.y));
    float3 c = Footprint(x);
    if (gParams.x > 0.0)
    {
        float3 n = Footprint(x + float2(0.0, -1.0));
        float3 w = Footprint(x + float2(-1.0, 0.0));
        float3 e = Footprint(x + float2(1.0, 0.0));
        float3 s = Footprint(x + float2(0.0, 1.0));
        float3 mn = min(c, min(min(n, w), min(e, s)));
        float3 mx = max(c, max(max(n, w), max(e, s)));
        float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-5)));
        float3 k = -amp / lerp(8.0, 5.0, gParams.x);
        c = saturate((c + k * (n + w + e + s)) / (1.0 + 4.0 * k));
    }
    return float4(pow(c, 1.0 / 2.2), 1.0);
}
