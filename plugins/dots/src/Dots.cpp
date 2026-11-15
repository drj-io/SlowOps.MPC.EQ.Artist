// EQ Artist Dots: the EQ Artist plugin with its curves drawn as dots.
// All of the plugin is in plugin-common/EqArtistPlugin.hpp; this file only picks the variant.
#include "../../../plugin-common/EqArtistPlugin.hpp"

struct Dots
{
    static constexpr bool kLines = false;
    static constexpr const char* kName = "EQ Artist Dots";   // == plugin.json "name". One name is the browser entry, the FX slot (shortened by the MPC, under "Slow Ops") and the page title; a longer name gets cut badly in the slot
    static constexpr int kUid = MPC_FOURCC('S', 'o', 'E', 'D');
    static constexpr int kVersion = 1000;  // 1.0.0
};

using EqArtistDots = eqartist::EqArtistPlugin<Dots>;
MPC_PLUGIN_ENTRY(EqArtistDots)
