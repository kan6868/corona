-- plugin.s3d (Lua half). Embedded in the DLL / .so and run with the table of C functions as its argument.
local C = ...

local M = { version = C.version }

local display, graphics, system, io = display, graphics, system, io
local sqrt, sin, cos, pi, abs = math.sqrt, math.sin, math.cos, math.pi, math.abs

-- ---------------------------------------------------------------------------
-- Effect
-- Fog: v_S3D_Depth = camera depth (gl_Position.w; the shell transform passes it so fog is computed per pixel)
local FOG = [[
uniform P_DEFAULT vec4 u_S3D_Fog;      // rgb: color, a: 1 = enabled
uniform P_DEFAULT vec4 u_S3D_FogRange; // x: start, y: end

P_DEFAULT float S3D_FogFactor()
{
    P_DEFAULT float range = max( u_S3D_FogRange.y - u_S3D_FogRange.x, 0.0001 );
    return u_S3D_Fog.a * clamp( ( v_S3D_Depth - u_S3D_FogRange.x ) / range, 0.0, 1.0 );
}

// color is premultiplied by alpha
P_COLOR vec4 S3D_ApplyFog( P_COLOR vec4 c )
{
    return vec4( mix( c.rgb, u_S3D_Fog.rgb * c.a, S3D_FogFactor() ), c.a );
}
]]

-- Shadow map: v_S3D_ShadowPos (light clip coordinates) is declared and computed by the shell transform
local SHADOW = [[
uniform P_DEFAULT vec4 u_S3D_ShadowParams; // x: 0 off, 1/2/3 quality (1, 4, 9 taps); y: bias; z: 1/size; w: darkness
uniform sampler2D u_S3D_ShadowMap;

P_DEFAULT float S3D_Unpack( P_DEFAULT vec3 c )
{
    return dot( c, vec3( 1.0, 1.0 / 255.0, 1.0 / 65025.0 ) );
}

P_DEFAULT float S3D_ShadowTap( P_DEFAULT vec2 uv, P_DEFAULT float z )
{
    return ( z > S3D_Unpack( texture2D( u_S3D_ShadowMap, uv ).rgb ) ) ? 0.0 : 1.0;
}

// lit fraction: 1 = lit, 0 = in shadow. PCF filter: 1 tap, 2x2 or 3x3 depending on quality
P_DEFAULT float S3D_ShadowLit( P_DEFAULT float ndl )
{
    if ( u_S3D_ShadowParams.x < 0.5 ) { return 1.0; }
    P_DEFAULT vec3 p = v_S3D_ShadowPos.xyz / v_S3D_ShadowPos.w * 0.5 + 0.5;
    if ( p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0 ) { return 1.0; }
    P_DEFAULT float bias = u_S3D_ShadowParams.y * ( 1.0 + 3.0 * ( 1.0 - ndl ) ); // grazing surfaces need more bias
    P_DEFAULT float z = p.z - bias;
    P_DEFAULT float t = u_S3D_ShadowParams.z;
    if ( u_S3D_ShadowParams.x < 1.5 ) { return S3D_ShadowTap( p.xy, z ); }
    if ( u_S3D_ShadowParams.x < 2.5 )
    {
        P_DEFAULT float h = 0.5 * t; // 2x2 texels around the point
        return 0.25 * ( S3D_ShadowTap( p.xy + vec2( -h, -h ), z ) + S3D_ShadowTap( p.xy + vec2( h, -h ), z )
                      + S3D_ShadowTap( p.xy + vec2( -h, h ), z ) + S3D_ShadowTap( p.xy + vec2( h, h ), z ) );
    }
    P_DEFAULT float lit = 0.0;
    for ( int i = -1; i <= 1; i++ )
    {
        for ( int j = -1; j <= 1; j++ )
        {
            lit += S3D_ShadowTap( p.xy + vec2( float( i ), float( j ) ) * t, z );
        }
    }
    return lit / 9.0;
}

P_DEFAULT float S3D_ShadowVisibility( P_DEFAULT float ndl )
{
    return mix( 1.0 - u_S3D_ShadowParams.w, 1.0, S3D_ShadowLit( ndl ) );
}
]]

local FRAG_LIT = [[
uniform P_DEFAULT vec4 u_S3D_LightDir;
uniform P_DEFAULT vec4 u_S3D_LightColor;
]] .. FOG .. SHADOW .. [[

P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord )
{
    P_COLOR vec4 c = texture2D( CoronaSampler0, texCoord );
    P_DEFAULT vec3 n = normalize( CoronaVertexUserData.xyz );
    P_DEFAULT float ndl = dot( n, u_S3D_LightDir.xyz );
    P_DEFAULT float d = 0.0;
    if ( ndl > 0.0 ) { d = ndl * S3D_ShadowVisibility( ndl ); } // facing away from the light: skip the shadow map read
    P_DEFAULT vec3 light = vec3( u_S3D_LightDir.w ) + u_S3D_LightColor.rgb * d;
    return S3D_ApplyFog( CoronaColorScale( vec4( c.rgb * light, c.a ) ) );
}
]]

-- Shadow pass: writes light-space depth (0..1) packed into 24 bits of RGB, alpha = 1 (normal blending keeps the value)
local FRAG_DEPTH = [[
P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord )
{
    P_DEFAULT float z = clamp( v_S3D_ShadowPos.z / v_S3D_ShadowPos.w * 0.5 + 0.5, 0.0, 1.0 );
    P_DEFAULT vec3 enc = fract( z * vec3( 1.0, 255.0, 65025.0 ) );
    enc -= enc.yzz * vec3( 1.0 / 255.0, 1.0 / 255.0, 0.0 );
    return vec4( enc, 1.0 );
}
]]

local FRAG_UNLIT = FOG .. [[

P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord )
{
    return S3D_ApplyFog( CoronaColorScale( texture2D( CoronaSampler0, texCoord ) ) );
}
]]

local FRAG_UNLIT_NOFOG = [[
P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord )
{
    return CoronaColorScale( texture2D( CoronaSampler0, texCoord ) );
}
]]

-- Planar shadow: color comes from setFillColor; fades out in fog instead of being tinted by it
local FRAG_SHADOW = FOG .. [[

P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord )
{
    return CoronaColorScale( vec4( 1.0 ) ) * ( 1.0 - S3D_FogFactor() );
}
]]

local FRAG_NORMALS = [[
P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord )
{
    P_DEFAULT vec3 n = normalize( CoronaVertexUserData.xyz );
    return CoronaColorScale( vec4( n * 0.5 + 0.5, 1.0 ) );
}
]]

-- Post-processing applied to the snapshot that holds the scene: FXAA + color grading + vignette
local FRAG_POST = [[
uniform P_DEFAULT vec4 u_UserData0; // exposure, contrast, saturation, gamma
uniform P_DEFAULT vec4 u_UserData1; // tint r, g, b, vignette
uniform P_DEFAULT vec4 u_UserData2; // fxaa (0/1), vignette softness, brightness, -

P_DEFAULT float S3D_Luma( P_DEFAULT vec3 c ) { return dot( c, vec3( 0.299, 0.587, 0.114 ) ); }

// FXAA "lite" (based on FXAA 3.x by Timothy Lottes)
P_DEFAULT vec3 S3D_Fxaa( P_UV vec2 uv, P_DEFAULT vec2 px )
{
    P_DEFAULT vec3 rgbNW = texture2D( CoronaSampler0, uv + vec2( -1.0, -1.0 ) * px ).rgb;
    P_DEFAULT vec3 rgbNE = texture2D( CoronaSampler0, uv + vec2( 1.0, -1.0 ) * px ).rgb;
    P_DEFAULT vec3 rgbSW = texture2D( CoronaSampler0, uv + vec2( -1.0, 1.0 ) * px ).rgb;
    P_DEFAULT vec3 rgbSE = texture2D( CoronaSampler0, uv + vec2( 1.0, 1.0 ) * px ).rgb;
    P_DEFAULT vec3 rgbM = texture2D( CoronaSampler0, uv ).rgb;
    P_DEFAULT float lNW = S3D_Luma( rgbNW ), lNE = S3D_Luma( rgbNE ), lSW = S3D_Luma( rgbSW ), lSE = S3D_Luma( rgbSE );
    P_DEFAULT float lM = S3D_Luma( rgbM );
    P_DEFAULT float lMin = min( lM, min( min( lNW, lNE ), min( lSW, lSE ) ) );
    P_DEFAULT float lMax = max( lM, max( max( lNW, lNE ), max( lSW, lSE ) ) );

    P_DEFAULT vec2 dir = vec2( -( ( lNW + lNE ) - ( lSW + lSE ) ), ( lNW + lSW ) - ( lNE + lSE ) );
    P_DEFAULT float reduce = max( ( lNW + lNE + lSW + lSE ) * 0.03125, 0.0078125 );
    P_DEFAULT float rcpMin = 1.0 / ( min( abs( dir.x ), abs( dir.y ) ) + reduce );
    dir = clamp( dir * rcpMin, vec2( -8.0 ), vec2( 8.0 ) ) * px;

    P_DEFAULT vec3 a = 0.5 * ( texture2D( CoronaSampler0, uv + dir * ( 1.0 / 3.0 - 0.5 ) ).rgb
                             + texture2D( CoronaSampler0, uv + dir * ( 2.0 / 3.0 - 0.5 ) ).rgb );
    P_DEFAULT vec3 b = a * 0.5 + 0.25 * ( texture2D( CoronaSampler0, uv + dir * -0.5 ).rgb
                                        + texture2D( CoronaSampler0, uv + dir * 0.5 ).rgb );
    P_DEFAULT float lB = S3D_Luma( b );
    return ( lB < lMin || lB > lMax ) ? a : b;
}

P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord )
{
    P_COLOR vec4 src = texture2D( CoronaSampler0, texCoord );
    P_DEFAULT vec3 c = ( u_UserData2.x > 0.5 ) ? S3D_Fxaa( texCoord, CoronaTexelSize.xy ) : src.rgb;

    c = c * u_UserData0.x + vec3( u_UserData2.z );                      // exposure, brightness
    c = ( c - 0.5 ) * u_UserData0.y + 0.5;                              // contrast
    c = mix( vec3( S3D_Luma( c ) ), c, u_UserData0.z );                 // saturation
    c *= u_UserData1.rgb;                                               // tint
    c = pow( max( c, vec3( 0.0 ) ), vec3( 1.0 / max( u_UserData0.w, 0.01 ) ) ); // gamma

    P_DEFAULT float d = distance( texCoord, vec2( 0.5 ) ) * 1.41421;    // 0 at the center, 1 in the corners
    c *= 1.0 - u_UserData1.a * smoothstep( 1.0 - u_UserData2.y, 1.0, d );

    return CoronaColorScale( vec4( clamp( c, 0.0, 1.0 ) * src.a, src.a ) );
}
]]

M.EFFECT_LIT = "filter.s3d.lit"
M.EFFECT_UNLIT = "filter.s3d.unlit"
M.EFFECT_NORMALS = "filter.s3d.normals"
M.EFFECT_SHADOW = "filter.s3d.shadow"
M.EFFECT_DEPTH = "filter.s3d.depth"
M.EFFECT_POST = "filter.s3d.post"

local camera = { position = { 0, 2, 6 }, target = { 0, 0, 0 }, up = { 0, 1, 0 }, fov = 60, near = 0.1, far = 100 }
local light = { direction = { -0.4, -1, -0.6 }, ambient = 0.3, color = { 1, 1, 1 } }

local inited = false

-- Solar2D's Vulkan backend has no custom commands, named uniforms or shell transforms yet,
-- so the plugin only works on the OpenGL / OpenGL ES backend.
local backend = system.getInfo( "graphicsBackend" ) or "glBackend"
M.backend = backend
M.supported = ( backend ~= "vulkanBackend" )

function M.isSupported() return M.supported end

local function define( name, fragment )
    graphics.defineEffect{
        language = "glsl", category = "filter", group = "s3d", name = name,
        shellTransform = "s3d", dataType = "s3d",
        fragment = fragment,
    }
end

function M.init()
    if inited then return end
    inited = true
    if not M.supported then
        print( "WARNING: plugin.s3d: backend '" .. backend .. "' is not supported. Solar2D's Vulkan backend has no custom commands, "
            .. "named uniforms or shell transforms yet, so 3D objects will be hidden. Use the OpenGL backend: remove backend = \"wantVulkan\" "
            .. "from config.lua." )
    end
    C.init()
    display.setDefault( "enableDepth", true ) -- clear the depth buffer every frame
    -- on Vulkan, do not declare custom uniforms in the kernel (the Vulkan shader translator rejects them)
    define( "lit", M.supported and FRAG_LIT or FRAG_UNLIT_NOFOG )
    define( "unlit", M.supported and FRAG_UNLIT or FRAG_UNLIT_NOFOG )
    define( "normals", FRAG_NORMALS )
    define( "shadow", M.supported and FRAG_SHADOW or FRAG_UNLIT_NOFOG )
    define( "depth", M.supported and FRAG_DEPTH or FRAG_UNLIT_NOFOG )
    graphics.defineEffect{
        language = "glsl", category = "filter", group = "s3d", name = "post",
        uniformData = {
            { name = "grade", type = "vec4", index = 0, default = { 1, 1, 1, 1 } },     -- exposure, contrast, saturation, gamma
            { name = "tint", type = "vec4", index = 1, default = { 1, 1, 1, 0 } },      -- r, g, b, vignette
            { name = "options", type = "vec4", index = 2, default = { 1, 0.5, 0, 0 } }, -- fxaa, vignette softness, brightness
        },
        fragment = FRAG_POST,
    }
    Runtime:addEventListener( "enterFrame", function() C.beginFrame() end )
    M.setCamera( camera )
    M.setLight( light )
end

-- ---------------------------------------------------------------------------
-- Camera / light
local function v3( t, d ) t = t or d; return { t[1] or t.x or 0, t[2] or t.y or 0, t[3] or t.z or 0 } end

function M.setCamera( opts )
    opts = opts or {}
    camera.position = v3( opts.position, camera.position )
    camera.target = v3( opts.target, camera.target )
    camera.up = v3( opts.up, camera.up )
    camera.fov = opts.fov or camera.fov
    camera.near = opts.near or camera.near
    camera.far = opts.far or camera.far
    local aspect = opts.aspect or ( display.actualContentWidth / display.actualContentHeight )
    local p, t, u = camera.position, camera.target, camera.up
    C.setCamera( p[1], p[2], p[3], t[1], t[2], t[3], u[1], u[2], u[3], camera.fov, aspect, camera.near, camera.far )
end

function M.getCamera()
    return {
        position = { unpack( camera.position ) }, target = { unpack( camera.target ) }, up = { unpack( camera.up ) },
        fov = camera.fov, near = camera.near, far = camera.far,
    }
end

-- Camera orbiting a target: yaw/pitch in degrees
function M.orbit( yaw, pitch, distance, target )
    target = v3( target, camera.target )
    local y, p = math.rad( yaw ), math.rad( pitch )
    local cp = cos( p )
    M.setCamera{
        target = target,
        position = { target[1] + distance * cp * sin( y ), target[2] + distance * sin( p ), target[3] + distance * cp * cos( y ) },
    }
end

function M.setLight( opts )
    opts = opts or {}
    light.direction = v3( opts.direction, light.direction )
    light.ambient = opts.ambient or light.ambient
    light.color = opts.color or light.color
    local d, c = light.direction, light.color
    C.setLight( d[1], d[2], d[3], light.ambient, c[1] or 1, c[2] or 1, c[3] or 1 )
end

-- ---------------------------------------------------------------------------
-- Fog
local fog = { color = { 0.7, 0.8, 0.9 }, start = 10, ["end"] = 40, enabled = false }

-- s3d.setFog{ color = { r, g, b }, start = 10, ["end"] = 40 } ; s3d.setFog( false ) turns it off
function M.setFog( opts )
    if opts == false then
        fog.enabled = false
    else
        opts = opts or {}
        fog.color = opts.color or fog.color
        fog.start = opts.start or fog.start
        fog["end"] = opts["end"] or opts.finish or fog["end"]
        fog.enabled = opts.enabled ~= false
    end
    local c = fog.color
    C.setFog( c[1], c[2], c[3], fog.start, fog["end"], fog.enabled )
end

-- ---------------------------------------------------------------------------
-- Planar shadows: s3d.setShadow{ planeY = 0, color = { 0, 0, 0, 0.35 } }; obj:castShadow( true )
local shadowColor = { 0, 0, 0, 0.35 }
function M.setShadow( opts )
    opts = opts or {}
    if opts.color then shadowColor = opts.color end
    C.setShadowPlane( opts.planeY or 0 )
end

-- Shadow map (shadows fall on every object, including the caster itself), one directional light:
--   s3d.setShadowMap{ enabled = true, size = 1024, area = 10, center = { 0, 0, 0 }, distance = 30, bias = 0.0015, darkness = 0.6 }
-- area: half the side of the shadowed square (world units); center: center of that square; distance: light to center.
-- Once enabled, obj:castShadow( true ) uses the shadow map instead of planar shadows.
local IS_MOBILE = ( function()
    local p = system.getInfo( "platform" )
    return p == "android" or p == "ios" or p == "tvos"
end )()
local SHADOW_QUALITY = { low = 1, medium = 2, high = 3 }
-- Default: 2x2 PCF on mobile, 3x3 on desktop
local shadowMap = { enabled = false, size = 1024, area = 10, center = { 0, 0, 0 }, distance = 30, bias = 0.0015, darkness = 0.6,
    quality = IS_MOBILE and "medium" or "high" }

local shadowMode = "planar" -- or "map" after s3d.setShadowMap{ enabled = true }

function M.setShadowMap( opts )
    opts = opts or {}
    for k, v in pairs( opts ) do shadowMap[k] = v end
    -- "enabled" omitted: the first call turns shadows on, later calls keep the current state (so changing center/quality never re-enables shadows that were turned off)
    if opts.enabled == nil and not shadowMap.configured then shadowMap.enabled = true end
    shadowMap.configured = true
    shadowMode = shadowMap.enabled and "map" or "planar"
    local c = v3( shadowMap.center, { 0, 0, 0 } )
    local q = SHADOW_QUALITY[shadowMap.quality] or tonumber( shadowMap.quality ) or 3
    C.setShadowMap( shadowMap.enabled, shadowMap.size, shadowMap.area, c[1], c[2], c[3], shadowMap.distance, shadowMap.bias, shadowMap.darkness, q )
end

function M.getShadowMode() return shadowMode end

M.isMobile = IS_MOBILE

-- ---------------------------------------------------------------------------
-- Post-processing: draw the scene into a snapshot, then apply filter.s3d.post
--   local post = s3d.newPostProcess{ clearColor = { 0.5, 0.7, 0.9 }, fxaa = true, exposure = 1.1, ... }
--   s3d.newCube( post.scene, 1 )
--   post:set{ saturation = 1.2, vignette = 0.4 }
local POST_DEFAULTS = {
    fxaa = true, exposure = 1, contrast = 1, saturation = 1, gamma = 1, brightness = 0,
    tint = { 1, 1, 1 }, vignette = 0, vignetteSoftness = 0.5,
}

function M.newPostProcess( opts )
    M.init()
    opts = opts or {}
    local w = opts.width or display.actualContentWidth
    local h = opts.height or display.actualContentHeight
    local snap = display.newSnapshot( w, h )
    if opts.parent then opts.parent:insert( snap ) end
    snap.x = opts.x or display.contentCenterX
    snap.y = opts.y or display.contentCenterY
    local cc = opts.clearColor or { 0, 0, 0, 1 }
    snap.clearColor = { cc[1], cc[2], cc[3], cc[4] or 1 }
    snap.fill.effect = M.EFFECT_POST

    local post = { snapshot = snap, scene = M.newScene(), settings = {} }
    snap.group:insert( post.scene )
    for k, v in pairs( POST_DEFAULTS ) do post.settings[k] = v end

    function post:set( o )
        local st = self.settings
        for k, v in pairs( o or {} ) do st[k] = v end
        local fx = self.snapshot.fill.effect
        fx.grade = { st.exposure, st.contrast, st.saturation, st.gamma }
        fx.tint = { st.tint[1], st.tint[2], st.tint[3], st.vignette }
        fx.options = { st.fxaa and 1 or 0, st.vignetteSoftness, st.brightness, 0 }
    end

    function post:setEnabled( on )
        self.snapshot.fill.effect = on and M.EFFECT_POST or nil
        if on then self:set() end
    end

    local function onFrame() if snap.invalidate then snap:invalidate() end end
    Runtime:addEventListener( "enterFrame", onFrame )
    function post:removeSelf()
        Runtime:removeEventListener( "enterFrame", onFrame )
        display.remove( self.snapshot )
    end

    post:set( opts )
    return post
end

-- ---------------------------------------------------------------------------
-- Projection / rays (Solar2D content coordinates)
function M.worldToScreen( x, y, z )
    local nx, ny, nz = C.project( x, y, z )
    if not nx then return nil end
    local sx = display.screenOriginX + ( nx * 0.5 + 0.5 ) * display.actualContentWidth
    local sy = display.screenOriginY + ( 0.5 - ny * 0.5 ) * display.actualContentHeight
    return sx, sy, nz
end

function M.screenToRay( sx, sy )
    local nx = ( sx - display.screenOriginX ) / display.actualContentWidth * 2 - 1
    local ny = 1 - ( sy - display.screenOriginY ) / display.actualContentHeight * 2
    local ox, oy, oz, dx, dy, dz = C.unproject( nx, ny )
    return { ox, oy, oz }, { dx, dy, dz }
end

-- intersection of a ray with the plane y = h; returns x, y, z or nil
function M.rayPlaneY( origin, dir, h )
    h = h or 0
    if abs( dir[2] ) < 1e-6 then return nil end
    local t = ( h - origin[2] ) / dir[2]
    if t < 0 then return nil end
    return origin[1] + dir[1] * t, h, origin[3] + dir[3] * t
end

-- ---------------------------------------------------------------------------
-- Mesh building
local Builder = {}
Builder.__index = Builder

local function newBuilder()
    return setmetatable( { P = {}, U = {}, N = {} }, Builder )
end

function Builder:vertex( x, y, z, u, v, nx, ny, nz )
    local P, U, N = self.P, self.U, self.N
    P[#P + 1] = x; P[#P + 1] = y; P[#P + 1] = z
    U[#U + 1] = u; U[#U + 1] = v
    N[#N + 1] = nx; N[#N + 1] = ny; N[#N + 1] = nz
end

-- a, b, c = { x, y, z, u, v, nx, ny, nz }; winding is flipped when needed so the front face (CCW) faces the normal
function Builder:tri( a, b, c )
    local e1x, e1y, e1z = b[1] - a[1], b[2] - a[2], b[3] - a[3]
    local e2x, e2y, e2z = c[1] - a[1], c[2] - a[2], c[3] - a[3]
    local fx, fy, fz = e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x
    local nx, ny, nz = a[6] + b[6] + c[6], a[7] + b[7] + c[7], a[8] + b[8] + c[8]
    if fx * nx + fy * ny + fz * nz < 0 then b, c = c, b end
    self:vertex( unpack( a ) ); self:vertex( unpack( b ) ); self:vertex( unpack( c ) )
end

local function setTransform( obj )
    local p, r, s = obj.position3d, obj.rotation3d, obj.scale3d
    local q = obj.quat3d
    local key, data
    if q then
        -- quaternion rotation (setPose): reuse one table so no garbage is created every frame
        data = obj._s3dPoseBuf
        if not data then data = {}; obj._s3dPoseBuf = data end
        data[1], data[2], data[3] = p[1], p[2], p[3]
        data[4], data[5], data[6], data[7] = q[1], q[2], q[3], q[4]
        data[8], data[9], data[10] = s[1], s[2], s[3]
        key = "s3dPose"
    else
        data = { p[1], p[2], p[3], r[1], r[2], r[3], s[1], s[2], s[3] }
        key = "s3dTRS"
    end
    obj.fill.effect[key] = data
    local sh = obj._s3dShadow
    if sh and sh.fill and sh.fill.effect then sh.fill.effect[key] = data end
    local ca = obj._s3dCaster
    if ca and ca.fill and ca.fill.effect then ca.fill.effect[key] = data end
end

local methods = {}

function methods:moveTo( x, y, z ) local p = self.position3d; p[1], p[2], p[3] = x, y, z; setTransform( self ); return self end
function methods:moveBy( dx, dy, dz ) local p = self.position3d; p[1], p[2], p[3] = p[1] + dx, p[2] + dy, p[3] + dz; setTransform( self ); return self end
function methods:rotateTo( rx, ry, rz ) local r = self.rotation3d; r[1], r[2], r[3] = rx, ry, rz; self.quat3d = nil; setTransform( self ); return self end
function methods:rotateBy( dx, dy, dz ) local r = self.rotation3d; r[1], r[2], r[3] = r[1] + dx, r[2] + dy, r[3] + dz; self.quat3d = nil; setTransform( self ); return self end
-- Set position + quaternion rotation (x, y, z, w) in one call, e.g. from plugin.box3d:
--   obj:setPose( body:getTransform() )
-- After setPose, rotation3d is stale; call rotateTo to go back to Euler angles.
function methods:setPose( x, y, z, qx, qy, qz, qw )
    local p = self.position3d
    p[1], p[2], p[3] = x, y, z
    if qx then
        local q = self.quat3d
        if not q then q = {}; self.quat3d = q end
        q[1], q[2], q[3], q[4] = qx, qy, qz, qw
    end
    setTransform( self )
    return self
end
function methods:setRotationQuat( qx, qy, qz, qw )
    local q = self.quat3d
    if not q then q = {}; self.quat3d = q end
    q[1], q[2], q[3], q[4] = qx, qy, qz, qw
    setTransform( self )
    return self
end
function methods:scaleTo( sx, sy, sz ) sy = sy or sx; sz = sz or sx; local s = self.scale3d; s[1], s[2], s[3] = sx, sy, sz; setTransform( self ); return self end
function methods:setCulling( on ) self:_setFlag( 2, not on ) return self end
function methods:setDepthTest( on ) self:_setFlag( 4, not on ) return self end
function methods:setDepthWrite( on ) self:_setFlag( 8, not on ) return self end
function methods:_setFlag( bit, value )
    local f = self._s3dFlags or 0
    local has = ( f % ( bit * 2 ) ) >= bit
    if value and not has then f = f + bit elseif not value and has then f = f - bit end
    self._s3dFlags = f
    self.fill.effect.s3dFlags = f
end
-- uniforms each built-in effect has (so the plugin never writes a uniform that does not exist)
local EFFECT_USES = {
    ["filter.s3d.lit"] = { light = true, fog = true, shadow = true },
    ["filter.s3d.unlit"] = { light = false, fog = true, shadow = false },
    ["filter.s3d.shadow"] = { light = false, fog = true, shadow = false },
    ["filter.s3d.normals"] = { light = false, fog = false, shadow = false },
    ["filter.s3d.depth"] = { light = false, fog = false, shadow = false },
}

-- uses = { light = bool, fog = bool, shadow = bool } for custom effects (default: light and fog on, shadow off)
function methods:setEffect( name, uses )
    uses = uses or EFFECT_USES[name] or { light = true, fog = true, shadow = false }
    local f = self._s3dFlags or 0
    f = f % 32 -- keep flags 1..16
    if uses.light == false then f = f + 32 end
    if uses.fog == false then f = f + 64 end
    if not uses.shadow then f = f + 1024 end
    self._s3dFlags = f
    self.fill.effect = name
    self.fill.effect.s3dNormals = self._s3dNormals
    self.fill.effect.s3dFlags = f
    setTransform( self )
    return self
end


local function degenerateMesh()
    local m = display.newMesh{ mode = "triangles", vertices = { 0, 0, 0, 1, 1, 0, 1, 1, 0 }, uvs = { 0, 0, 0, 0, 0, 0 }, hasZ = true }
    local ox, oy = m.path:getVertexOffset()
    m.x, m.y = ox, oy
    return m
end

-- Shadow pass layer of a group: [begin marker, copies..., end marker], always at the bottom (drawn first)
local function casterLayer( parent )
    local layer = parent._s3dCasterLayer
    if layer then return layer end
    layer = display.newGroup()
    parent:insert( 1, layer )
    parent._s3dCasterLayer = layer
    local base = 2 + 4 + 32 + 64 + 1024 -- no cull, no depth, no light / fog / shadow uniforms
    local b = degenerateMesh(); layer:insert( b )
    b.fill.effect = M.EFFECT_DEPTH; b.fill.effect.s3dFlags = base + 256
    local e = degenerateMesh(); layer:insert( e )
    e.fill.effect = M.EFFECT_DEPTH; e.fill.effect.s3dFlags = base + 512
    layer._s3dEnd = e
    return layer
end

local function addCaster( obj )
    local parent = obj.parent
    if not parent or obj._s3dCaster then return end
    local layer = casterLayer( parent )
    local b = obj._s3dGeom
    local c = display.newMesh{ mode = "triangles", vertices = b.P, uvs = b.U, hasZ = true }
    layer:insert( layer.numChildren, c ) -- right before the end marker
    local ox, oy = c.path:getVertexOffset()
    c.x, c.y = ox, oy
    c.fill.effect = M.EFFECT_DEPTH
    c.fill.effect.s3dFlags = 2 + 32 + 64 + 1024 + 128 -- no cull, shadow pass copy
    obj._s3dCaster = c
end

local function hookFinalize( obj )
    if obj._s3dFinalizeHooked then return end
    obj._s3dFinalizeHooked = true
    obj:addEventListener( "finalize", function()
        if obj._s3dShadow then display.remove( obj._s3dShadow ); obj._s3dShadow = nil end
        if obj._s3dCaster then display.remove( obj._s3dCaster ); obj._s3dCaster = nil end
    end )
end

function methods:castShadow( on )
    if on == false then
        if self._s3dShadow then display.remove( self._s3dShadow ); self._s3dShadow = nil end
        if self._s3dCaster then display.remove( self._s3dCaster ); self._s3dCaster = nil end
        return self
    end
    if not self._s3dGeom then return self end
    local parent = self.parent
    if not parent then return self end
    if shadowMode == "map" then
        addCaster( self )
        hookFinalize( self )
        setTransform( self )
        return self
    end
    if self._s3dShadow then return self end
    local layer = parent._s3dShadowLayer
    if not layer then
        layer = display.newGroup()
        parent:insert( layer )
        parent._s3dShadowLayer = layer
    end
    local b = self._s3dGeom
    local sh = display.newMesh{ mode = "triangles", vertices = b.P, uvs = b.U, hasZ = true }
    layer:insert( sh )
    layer:toFront()
    local ox, oy = sh.path:getVertexOffset()
    sh.x, sh.y = ox, oy
    sh:setFillColor( unpack( shadowColor ) )
    sh.fill.effect = M.EFFECT_SHADOW
    -- projected shadow + no light uniforms. Culling stays on: only triangles facing the light remain after projection,
    -- so convex objects never get two layers of shadow.
    sh.fill.effect.s3dFlags = 16 + 32
    self._s3dShadow = sh
    hookFinalize( self )
    setTransform( self )
    sh.isVisible = self.isVisible
    return self
end

local function finalize( parent, b, opts )
    opts = opts or {}
    -- degenerate triangle at the origin: keeps the object's 2D bounds around (0, 0) so Solar2D does not cull it by mistake
    b:vertex( 0, 0, 0, 0, 0, 0, 0, 1 ); b:vertex( 1, 1, 0, 0, 0, 0, 0, 1 ); b:vertex( 1, 1, 0, 0, 0, 0, 0, 1 )

    local mesh = display.newMesh{ mode = "triangles", vertices = b.P, uvs = b.U, hasZ = true }
    if not mesh then error( "s3d: display.newMesh failed", 3 ) end
    if parent then
        parent:insert( mesh )
        if parent._s3dShadowLayer then parent._s3dShadowLayer:toFront() end -- planar shadows are always drawn last
    end
    mesh._s3dGeom = b

    -- newMesh recenters the 2D bounds; undo that so vertex coordinates stay as given
    local ox, oy = mesh.path:getVertexOffset()
    mesh.x, mesh.y = ox, oy

    if opts.texture then
        mesh.fill = { type = "image", filename = opts.texture, baseDir = opts.baseDir }
    end
    if opts.color then mesh:setFillColor( unpack( opts.color ) ) end

    for k, f in pairs( methods ) do mesh[k] = f end
    mesh.position3d = { 0, 0, 0 }
    mesh.rotation3d = { 0, 0, 0 }
    mesh.scale3d = { 1, 1, 1 }
    mesh._s3dNormals = C.newBuffer( b.N )
    mesh._s3dFlags = 0
    mesh.vertexCount3d = #b.P / 3 - 3

    mesh:setEffect( opts.effect or ( opts.unlit and M.EFFECT_UNLIT ) or M.EFFECT_LIT )
    if not M.supported then mesh.isVisible = false end
    if opts.cull == false then mesh:setCulling( false ) end
    if opts.castShadow then mesh:castShadow( true ) end
    if opts.x or opts.y or opts.z then mesh:moveTo( opts.x or 0, opts.y or 0, opts.z or 0 ) end
    return mesh
end

-- ---------------------------------------------------------------------------
-- Primitive
local FACES = {
    { n = {  1, 0, 0 }, u = { 0, 0, -1 }, v = { 0, 1, 0 } },
    { n = { -1, 0, 0 }, u = { 0, 0,  1 }, v = { 0, 1, 0 } },
    { n = { 0,  1, 0 }, u = { 1, 0, 0 }, v = { 0, 0, -1 } },
    { n = { 0, -1, 0 }, u = { 1, 0, 0 }, v = { 0, 0,  1 } },
    { n = { 0, 0,  1 }, u = { 1, 0, 0 }, v = { 0, 1, 0 } },
    { n = { 0, 0, -1 }, u = { -1, 0, 0 }, v = { 0, 1, 0 } },
}

function M.newCube( parent, size, opts )
    M.init()
    local h = ( size or 1 ) * 0.5
    local b = newBuilder()
    for _, f in ipairs( FACES ) do
        local n, u, v = f.n, f.u, f.v
        local function corner( a, c )
            local su, sv = a * 2 - 1, c * 2 - 1
            return { h * ( n[1] + su * u[1] + sv * v[1] ), h * ( n[2] + su * u[2] + sv * v[2] ), h * ( n[3] + su * u[3] + sv * v[3] ),
                a, 1 - c, n[1], n[2], n[3] }
        end
        local p00, p10, p11, p01 = corner( 0, 0 ), corner( 1, 0 ), corner( 1, 1 ), corner( 0, 1 )
        b:tri( p00, p10, p11 ); b:tri( p00, p11, p01 )
    end
    return finalize( parent, b, opts )
end

function M.newSphere( parent, radius, segments, rings, opts )
    M.init()
    radius, segments, rings = radius or 0.5, segments or 24, rings or 16
    local b = newBuilder()
    local function pt( i, j )
        local th, ph = pi * i / rings, 2 * pi * j / segments
        local nx, ny, nz = sin( th ) * sin( ph ), cos( th ), sin( th ) * cos( ph )
        return { radius * nx, radius * ny, radius * nz, j / segments, i / rings, nx, ny, nz }
    end
    for i = 0, rings - 1 do
        for j = 0, segments - 1 do
            local a, c, d, e = pt( i, j ), pt( i + 1, j ), pt( i + 1, j + 1 ), pt( i, j + 1 )
            if i > 0 then b:tri( a, c, e ) end
            if i < rings - 1 then b:tri( e, c, d ) end
        end
    end
    return finalize( parent, b, opts )
end

function M.newPlane( parent, width, depth, opts )
    M.init()
    opts = opts or {}
    local w, d = ( width or 1 ) * 0.5, ( depth or width or 1 ) * 0.5
    local rep = opts.uvRepeat or 1
    local b = newBuilder()
    local p00 = { -w, 0,  d, 0, rep, 0, 1, 0 }
    local p10 = {  w, 0,  d, rep, rep, 0, 1, 0 }
    local p11 = {  w, 0, -d, rep, 0, 0, 1, 0 }
    local p01 = { -w, 0, -d, 0, 0, 0, 1, 0 }
    b:tri( p00, p10, p11 ); b:tri( p00, p11, p01 )
    return finalize( parent, b, opts )
end

-- Capsule along the Y axis: a segment of length `height` between the two hemisphere centers (total height = height + 2 * radius)
function M.newCapsule( parent, radius, height, segments, rings, opts )
    M.init()
    radius, height, segments, rings = radius or 0.25, height or 1, segments or 20, rings or 6
    local h = height * 0.5
    local b = newBuilder()
    -- rings from the top pole to the bottom pole; the equator ring is repeated (+h and -h) to form the cylinder
    local rows = {}
    for k = 0, rings do rows[#rows + 1] = { pi * 0.5 * k / rings, h } end
    for k = rings, 2 * rings do rows[#rows + 1] = { pi * 0.5 * k / rings, -h } end
    local total = height + 2 * radius
    local function pt( r, j )
        local th, off = rows[r][1], rows[r][2]
        local ph = 2 * pi * j / segments
        local nx, ny, nz = sin( th ) * sin( ph ), cos( th ), sin( th ) * cos( ph )
        local y = radius * ny + off
        return { radius * nx, y, radius * nz, j / segments, 0.5 - y / total, nx, ny, nz }
    end
    for r = 1, #rows - 1 do
        for j = 0, segments - 1 do
            local a, c, d, e = pt( r, j ), pt( r + 1, j ), pt( r + 1, j + 1 ), pt( r, j + 1 )
            if r > 1 then b:tri( a, c, e ) end
            if r < #rows - 1 then b:tri( e, c, d ) end
        end
    end
    return finalize( parent, b, opts )
end

function M.newCylinder( parent, radius, height, segments, opts )
    M.init()
    radius, height, segments = radius or 0.5, height or 1, segments or 24
    local h = height * 0.5
    local b = newBuilder()
    for j = 0, segments - 1 do
        local a0, a1 = 2 * pi * j / segments, 2 * pi * ( j + 1 ) / segments
        local x0, z0, x1, z1 = sin( a0 ), cos( a0 ), sin( a1 ), cos( a1 )
        local u0, u1 = j / segments, ( j + 1 ) / segments
        local b0 = { radius * x0, -h, radius * z0, u0, 1, x0, 0, z0 }
        local b1 = { radius * x1, -h, radius * z1, u1, 1, x1, 0, z1 }
        local t0 = { radius * x0, h, radius * z0, u0, 0, x0, 0, z0 }
        local t1 = { radius * x1, h, radius * z1, u1, 0, x1, 0, z1 }
        b:tri( b0, b1, t1 ); b:tri( b0, t1, t0 )
        -- caps
        b:tri( { 0, h, 0, 0.5, 0.5, 0, 1, 0 }, { radius * x0, h, radius * z0, 0.5 + x0 * 0.5, 0.5 - z0 * 0.5, 0, 1, 0 },
            { radius * x1, h, radius * z1, 0.5 + x1 * 0.5, 0.5 - z1 * 0.5, 0, 1, 0 } )
        b:tri( { 0, -h, 0, 0.5, 0.5, 0, -1, 0 }, { radius * x0, -h, radius * z0, 0.5 + x0 * 0.5, 0.5 + z0 * 0.5, 0, -1, 0 },
            { radius * x1, -h, radius * z1, 0.5 + x1 * 0.5, 0.5 + z1 * 0.5, 0, -1, 0 } )
    end
    return finalize( parent, b, opts )
end

-- ---------------------------------------------------------------------------
-- OBJ (Wavefront): v, vt, vn, f (polygons -> triangle fans). Materials are ignored.
-- Opens an OBJ file; also tries the name + ".txt" (Solar2D for Android always drops "*.obj" from the APK: the exclusion list
-- is hard-coded in create_build_properties.lua and build.settings cannot remove it). Name models "x.obj.txt".
local function openOBJ( filename, baseDir, level )
    local names = { filename }
    if type( filename ) == "string" and filename:lower():match( "%.obj$" ) then names[2] = filename .. ".txt" end
    for i = 1, #names do
        local path = system.pathForFile( names[i], baseDir )
        local f = path and io.open( path, "r" )
        if f then return f end
    end
    error( "s3d: cannot open " .. tostring( filename )
        .. ( names[2] and ( " or " .. names[2] .. " (Android builds drop *.obj files: rename it to " .. names[2] .. ")" ) or "" ), level + 1 )
end

-- Reads an OBJ into a builder (separate triangles, each vertex with position, uv, normal). Polygons become fans; missing vn gives flat normals.
local function parseOBJ( filename, opts, level )
    local f = openOBJ( filename, opts.baseDir or system.ResourceDirectory, level + 1 )
    local scale = opts.scale or 1
    local V, T, Nn = {}, {}, {}
    local b = newBuilder()
    local tris = 0

    local function idx( s, n )
        local i = tonumber( s )
        if not i then return nil end
        if i < 0 then i = n + i + 1 end
        return i
    end

    for line in f:lines() do
        local tag, rest = line:match( "^%s*(%S+)%s*(.*)$" )
        if tag == "v" then
            local x, y, z = rest:match( "(%S+)%s+(%S+)%s+(%S+)" )
            V[#V + 1] = { tonumber( x ) * scale, tonumber( y ) * scale, tonumber( z ) * scale }
        elseif tag == "vt" then
            local u, v = rest:match( "(%S+)%s+(%S+)" )
            T[#T + 1] = { tonumber( u ), 1 - tonumber( v ) }
        elseif tag == "vn" then
            local x, y, z = rest:match( "(%S+)%s+(%S+)%s+(%S+)" )
            Nn[#Nn + 1] = { tonumber( x ), tonumber( y ), tonumber( z ) }
        elseif tag == "f" then
            local face = {}
            for tok in rest:gmatch( "%S+" ) do
                local vi, ti, ni = tok:match( "^([^/]*)/?([^/]*)/?([^/]*)$" )
                face[#face + 1] = { idx( vi, #V ), idx( ti, #T ), idx( ni, #Nn ) }
            end
            for k = 2, #face - 1 do
                local c = { face[1], face[k], face[k + 1] }
                local p = {}
                for m = 1, 3 do p[m] = V[c[m][1]] end
                local e1 = { p[2][1] - p[1][1], p[2][2] - p[1][2], p[2][3] - p[1][3] }
                local e2 = { p[3][1] - p[1][1], p[3][2] - p[1][2], p[3][3] - p[1][3] }
                local fx, fy, fz = e1[2] * e2[3] - e1[3] * e2[2], e1[3] * e2[1] - e1[1] * e2[3], e1[1] * e2[2] - e1[2] * e2[1]
                local fl = sqrt( fx * fx + fy * fy + fz * fz )
                if fl > 0 then fx, fy, fz = fx / fl, fy / fl, fz / fl end
                for m = 1, 3 do
                    local t = c[m][2] and T[c[m][2]] or { 0, 0 }
                    local n = ( c[m][3] and Nn[c[m][3]] ) or { fx, fy, fz }
                    b:vertex( p[m][1], p[m][2], p[m][3], t[1], t[2], n[1], n[2], n[3] )
                end
                tris = tris + 1
            end
        end
    end
    f:close()
    if tris == 0 then error( "s3d: OBJ file has no faces: " .. tostring( filename ), level + 1 ) end
    return b
end

function M.loadOBJ( parent, filename, opts )
    M.init()
    opts = opts or {}
    return finalize( parent, parseOBJ( filename, opts, 2 ), opts )
end

---------------------------------------------------------------------------------------------
-- Frame animation from a sequence of OBJ files (e.g. Blender: File > Export > Wavefront with "Animation" checked).
--
--   local hero = s3d.loadOBJAnimation( scene, "assets/run/run_%06d.obj", {
--       first = 1, last = 24,           -- frame numbers in the file names (fills %d / %06d); or frames = { "a.obj", "b.obj" }
--       fps = 24, loop = true, interpolate = true, autoPlay = true,
--       clips = { run = { 1, 16 }, jump = { 17, 24, loop = false, fps = 30 } },   -- indices of the loaded frames, from 1
--       texture = "assets/hero.png", castShadow = true, scale = 1,
--       onComplete = function( obj, clip ) end,                                   -- a non-looping clip finished
--   } )
--   hero:play( "jump" ), hero:pause(), hero:resume(), hero:stop(), hero:gotoFrame( 3.5 )
--   hero:setSpeed( 2 ), hero:isPlaying(), hero.frameCount, hero.currentFrame, hero.clip
--
-- All frames must share the same topology (same faces in the same order, as when exported from one mesh). Positions and
-- normals of two neighbouring frames are interpolated in the geometry writer (C), so Lua only changes 5 values per frame.
---------------------------------------------------------------------------------------------
local animMethods = {}

local function animApply( obj )
    local a = obj._s3dAnim
    local n = #a.frames
    local f = a.time * a.fps -- (fractional) frame within the clip, from 0
    local c0, c1 = a.clipFirst, a.clipLast
    local len = c1 - c0 + 1
    local i, t
    if a.loop then
        f = f % len
        i = math.floor( f )
        t = f - i
        local j = ( i + 1 ) % len
        a.buf[1], a.buf[2] = a.frames[c0 + i].p, a.frames[c0 + j].p
        a.buf[4], a.buf[5] = a.frames[c0 + i].n, a.frames[c0 + j].n
    else
        if f >= len - 1 then f = len - 1 end
        i = math.floor( f )
        t = f - i
        local j = math.min( i + 1, len - 1 )
        a.buf[1], a.buf[2] = a.frames[c0 + i].p, a.frames[c0 + j].p
        a.buf[4], a.buf[5] = a.frames[c0 + i].n, a.frames[c0 + j].n
    end
    if not a.interpolate then t = 0 end
    a.buf[3] = t
    obj.currentFrame = c0 + f
    local function put( o ) if o and o.fill and o.fill.effect then o.fill.effect.s3dMorph = a.buf end end
    put( obj ); put( obj._s3dCaster ); put( obj._s3dShadow )
    return n
end

local function animTick( obj, dt )
    local a = obj._s3dAnim
    if not a.playing then
        -- shadow copies may be created later (castShadow): give them the current morph even while paused
        if obj._s3dCaster ~= a.seenCaster or obj._s3dShadow ~= a.seenShadow then
            a.seenCaster, a.seenShadow = obj._s3dCaster, obj._s3dShadow
            animApply( obj )
        end
        return
    end
    a.time = a.time + dt * a.speed
    local len = a.clipLast - a.clipFirst + 1
    if not a.loop and a.time * a.fps >= len - 1 then
        a.time = ( len - 1 ) / a.fps
        a.playing = false
        animApply( obj )
        if a.onComplete then a.onComplete( obj, a.clipName ) end
        obj:dispatchEvent{ name = "animation", phase = "ended", target = obj, clip = a.clipName }
        return
    end
    animApply( obj )
end

function animMethods:play( clip )
    local a = self._s3dAnim
    local c = clip and a.clips[clip]
    if clip and not c then error( "s3d: no clip named " .. tostring( clip ), 2 ) end
    a.clipName = clip
    a.clipFirst = c and c[1] or 1
    a.clipLast = c and c[2] or #a.frames
    a.fps = ( c and c.fps ) or a.baseFps
    if c and c.loop ~= nil then a.loop = c.loop else a.loop = a.baseLoop end
    a.time = 0
    a.playing = true
    self.clip = clip
    animApply( self )
    return self
end
function animMethods:pause() self._s3dAnim.playing = false; return self end
function animMethods:resume() self._s3dAnim.playing = true; return self end
function animMethods:stop() local a = self._s3dAnim; a.playing = false; a.time = 0; animApply( self ); return self end
function animMethods:isPlaying() return self._s3dAnim.playing end
function animMethods:setSpeed( s ) self._s3dAnim.speed = s or 1; return self end
function animMethods:setFPS( fps ) local a = self._s3dAnim; a.time = a.time * a.fps / fps; a.fps = fps; return self end
function animMethods:setInterpolation( on ) self._s3dAnim.interpolate = on and true or false; animApply( self ); return self end
-- frame counted from 1 within the current clip; fractions are allowed (2.5 = halfway between frames 2 and 3)
function animMethods:gotoFrame( frame )
    local a = self._s3dAnim
    a.time = ( ( frame or 1 ) - 1 ) / a.fps
    animApply( self )
    return self
end

function M.loadOBJAnimation( parent, source, opts )
    M.init()
    opts = opts or {}
    local files = {}
    if type( source ) == "table" then
        files = source
    elseif opts.frames then
        files = opts.frames
    else
        local first, last = opts.first or 1, opts.last
        if not last then error( "s3d.loadOBJAnimation: opts.last (last frame) is required with a file name pattern", 2 ) end
        for k = first, last do files[#files + 1] = string.format( source, k ) end
    end
    if #files == 0 then error( "s3d.loadOBJAnimation: no frames", 2 ) end

    local frames, firstBuilder = {}, nil
    for k, name in ipairs( files ) do
        local b = parseOBJ( name, opts, 2 )
        if not firstBuilder then
            firstBuilder = b
        elseif #b.P ~= #firstBuilder.P then
            error( string.format( "s3d.loadOBJAnimation: frame %d (%s) has %d vertices, frame 1 has %d: all frames must share the same topology",
                k, name, #b.P / 3, #firstBuilder.P / 3 ), 2 )
        end
        frames[k] = { p = C.newBuffer( b.P ), n = C.newBuffer( b.N ) }
    end

    local obj = finalize( parent, firstBuilder, opts )
    for k, fn in pairs( animMethods ) do obj[k] = fn end
    obj.frameCount = #frames
    obj._s3dAnim = {
        frames = frames, clips = opts.clips or {}, buf = { false, false, 0, false, false },
        baseFps = opts.fps or 24, baseLoop = opts.loop ~= false, fps = opts.fps or 24, loop = opts.loop ~= false,
        interpolate = opts.interpolate ~= false, speed = 1, time = 0, playing = false,
        clipFirst = 1, clipLast = #frames, onComplete = opts.onComplete,
    }
    -- animApply writes the morph to the object and to its shadow copies (caster / planar shadow) every frame
    local last = system.getTimer()
    local function onFrame( e )
        local now = e.time
        animTick( obj, ( now - last ) / 1000 )
        last = now
    end
    Runtime:addEventListener( "enterFrame", onFrame )
    obj:addEventListener( "finalize", function() Runtime:removeEventListener( "enterFrame", onFrame ) end )
    if opts.autoPlay ~= false then obj:play( opts.clip ) else animApply( obj ) end
    return obj
end

-- 3D group: simply a display group at the origin (0, 0), not rotated or scaled.
-- Do not set x/y/rotation/xScale on 3D objects or their groups: use moveTo/rotateTo/scaleTo.
function M.newScene( parent )
    M.init()
    local g = display.newGroup()
    if parent then parent:insert( g ) end
    return g
end

return M
