-- Landscape: Solar2D defines width/height in portrait terms; the landscape orientation in build.settings
-- turns this into a 1280 x 720 content area. Screens with another aspect ratio get extra margins; use display.screenOriginX/Y
-- and display.actualContentWidth/Height (main.lua already does).
application =
{
    content =
    {
        width = 720,
        height = 1280,
        scale = "letterbox",
        fps = 60,
    },
}
