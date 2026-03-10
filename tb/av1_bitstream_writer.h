// av1_bitstream_writer.h — C++ AV1 Bitstream Writer
// Produces valid AV1 bitstream (IVF + OBU format) using CDF-based
// range coding matching SVT-AV1's od_ec_enc implementation.
//
// Phase 2: Real coefficient coding from RTL encoder 8x8 blocks
//
// Reference: AV1 Spec Sections 5-7, SVT-AV1 bitstream_unit.c

#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>

// ============================================================
// AV1 Default CDF Tables (ICDF format: value = 32768 - cumprob)
// ============================================================
#define AV1_ICDF(x) ((uint16_t)(32768u - (unsigned)(x)))

// ---- Partition CDFs [20 contexts][max 11 values] ----
static const uint16_t av1_partition_cdf[20][11] = {
    {AV1_ICDF(19132),AV1_ICDF(25510),AV1_ICDF(30392),AV1_ICDF(32768),0,0,0,0,0,0,0},
    {AV1_ICDF(13928),AV1_ICDF(19855),AV1_ICDF(28540),AV1_ICDF(32768),0,0,0,0,0,0,0},
    {AV1_ICDF(12522),AV1_ICDF(23679),AV1_ICDF(28629),AV1_ICDF(32768),0,0,0,0,0,0,0},
    {AV1_ICDF( 9896),AV1_ICDF(18783),AV1_ICDF(25853),AV1_ICDF(32768),0,0,0,0,0,0,0},
    {AV1_ICDF(15597),AV1_ICDF(20929),AV1_ICDF(24571),AV1_ICDF(26706),AV1_ICDF(27664),AV1_ICDF(28821),AV1_ICDF(29601),AV1_ICDF(30571),AV1_ICDF(31902),AV1_ICDF(32768),0},
    {AV1_ICDF( 7925),AV1_ICDF(11043),AV1_ICDF(16785),AV1_ICDF(22470),AV1_ICDF(23971),AV1_ICDF(25043),AV1_ICDF(26651),AV1_ICDF(28701),AV1_ICDF(29834),AV1_ICDF(32768),0},
    {AV1_ICDF( 5414),AV1_ICDF(13269),AV1_ICDF(15111),AV1_ICDF(20488),AV1_ICDF(22360),AV1_ICDF(24500),AV1_ICDF(25537),AV1_ICDF(26336),AV1_ICDF(32117),AV1_ICDF(32768),0},
    {AV1_ICDF( 2662),AV1_ICDF( 6362),AV1_ICDF( 8614),AV1_ICDF(20860),AV1_ICDF(23053),AV1_ICDF(24778),AV1_ICDF(26436),AV1_ICDF(27829),AV1_ICDF(31171),AV1_ICDF(32768),0},
    {AV1_ICDF(18462),AV1_ICDF(20920),AV1_ICDF(23124),AV1_ICDF(27647),AV1_ICDF(28227),AV1_ICDF(29049),AV1_ICDF(29519),AV1_ICDF(30178),AV1_ICDF(31544),AV1_ICDF(32768),0},
    {AV1_ICDF( 7689),AV1_ICDF( 9060),AV1_ICDF(12056),AV1_ICDF(24992),AV1_ICDF(25660),AV1_ICDF(26182),AV1_ICDF(26951),AV1_ICDF(28041),AV1_ICDF(29052),AV1_ICDF(32768),0},
    {AV1_ICDF( 6015),AV1_ICDF( 9009),AV1_ICDF(10062),AV1_ICDF(24544),AV1_ICDF(25409),AV1_ICDF(26545),AV1_ICDF(27071),AV1_ICDF(27526),AV1_ICDF(32047),AV1_ICDF(32768),0},
    {AV1_ICDF( 1394),AV1_ICDF( 2208),AV1_ICDF( 2796),AV1_ICDF(28614),AV1_ICDF(29061),AV1_ICDF(29466),AV1_ICDF(29840),AV1_ICDF(30185),AV1_ICDF(31899),AV1_ICDF(32768),0},
    {AV1_ICDF(20137),AV1_ICDF(21547),AV1_ICDF(23078),AV1_ICDF(29566),AV1_ICDF(29837),AV1_ICDF(30261),AV1_ICDF(30524),AV1_ICDF(30892),AV1_ICDF(31724),AV1_ICDF(32768),0},
    {AV1_ICDF( 6732),AV1_ICDF( 7490),AV1_ICDF( 9497),AV1_ICDF(27944),AV1_ICDF(28250),AV1_ICDF(28515),AV1_ICDF(28969),AV1_ICDF(29630),AV1_ICDF(30104),AV1_ICDF(32768),0},
    {AV1_ICDF( 5945),AV1_ICDF( 7663),AV1_ICDF( 8348),AV1_ICDF(28683),AV1_ICDF(29117),AV1_ICDF(29749),AV1_ICDF(30064),AV1_ICDF(30298),AV1_ICDF(32238),AV1_ICDF(32768),0},
    {AV1_ICDF(  870),AV1_ICDF( 1212),AV1_ICDF( 1487),AV1_ICDF(31198),AV1_ICDF(31394),AV1_ICDF(31574),AV1_ICDF(31743),AV1_ICDF(31881),AV1_ICDF(32332),AV1_ICDF(32768),0},
    {AV1_ICDF(27899),AV1_ICDF(28219),AV1_ICDF(28529),AV1_ICDF(32484),AV1_ICDF(32539),AV1_ICDF(32619),AV1_ICDF(32639),AV1_ICDF(32768),0,0,0},
    {AV1_ICDF( 6607),AV1_ICDF( 6990),AV1_ICDF( 8268),AV1_ICDF(32060),AV1_ICDF(32219),AV1_ICDF(32338),AV1_ICDF(32371),AV1_ICDF(32768),0,0,0},
    {AV1_ICDF( 5429),AV1_ICDF( 6676),AV1_ICDF( 7122),AV1_ICDF(32027),AV1_ICDF(32227),AV1_ICDF(32531),AV1_ICDF(32582),AV1_ICDF(32768),0,0,0},
    {AV1_ICDF(  711),AV1_ICDF(  966),AV1_ICDF( 1172),AV1_ICDF(32448),AV1_ICDF(32538),AV1_ICDF(32617),AV1_ICDF(32664),AV1_ICDF(32768),0,0,0},
};

// ---- Skip CDFs [3 contexts][3 values] ----
static const uint16_t av1_skip_cdf[3][3] = {
    {AV1_ICDF(31671), AV1_ICDF(32768), 0},
    {AV1_ICDF(16515), AV1_ICDF(32768), 0},
    {AV1_ICDF( 4576), AV1_ICDF(32768), 0},
};

// ---- KF Y mode CDFs [5][5][14] ----
static const uint16_t av1_kf_y_mode_cdf[5][5][14] = {
  {{AV1_ICDF(15588),AV1_ICDF(17027),AV1_ICDF(19338),AV1_ICDF(20218),AV1_ICDF(20682),AV1_ICDF(21110),AV1_ICDF(21825),AV1_ICDF(23244),AV1_ICDF(24189),AV1_ICDF(28165),AV1_ICDF(29093),AV1_ICDF(30466),AV1_ICDF(32768),0},
   {AV1_ICDF(12016),AV1_ICDF(18066),AV1_ICDF(19516),AV1_ICDF(20303),AV1_ICDF(20719),AV1_ICDF(21444),AV1_ICDF(21888),AV1_ICDF(23032),AV1_ICDF(24434),AV1_ICDF(28658),AV1_ICDF(30172),AV1_ICDF(31409),AV1_ICDF(32768),0},
   {AV1_ICDF(10052),AV1_ICDF(10771),AV1_ICDF(22296),AV1_ICDF(22788),AV1_ICDF(23055),AV1_ICDF(23239),AV1_ICDF(24133),AV1_ICDF(25620),AV1_ICDF(26160),AV1_ICDF(29336),AV1_ICDF(29929),AV1_ICDF(31567),AV1_ICDF(32768),0},
   {AV1_ICDF(14091),AV1_ICDF(15406),AV1_ICDF(16442),AV1_ICDF(18808),AV1_ICDF(19136),AV1_ICDF(19546),AV1_ICDF(19998),AV1_ICDF(22096),AV1_ICDF(24746),AV1_ICDF(29585),AV1_ICDF(30958),AV1_ICDF(32462),AV1_ICDF(32768),0},
   {AV1_ICDF(12122),AV1_ICDF(13265),AV1_ICDF(15603),AV1_ICDF(16501),AV1_ICDF(18609),AV1_ICDF(20033),AV1_ICDF(22391),AV1_ICDF(25583),AV1_ICDF(26437),AV1_ICDF(30261),AV1_ICDF(31073),AV1_ICDF(32475),AV1_ICDF(32768),0}},
  {{AV1_ICDF(10023),AV1_ICDF(19585),AV1_ICDF(20848),AV1_ICDF(21440),AV1_ICDF(21832),AV1_ICDF(22760),AV1_ICDF(23089),AV1_ICDF(24023),AV1_ICDF(25381),AV1_ICDF(29014),AV1_ICDF(30482),AV1_ICDF(31436),AV1_ICDF(32768),0},
   {AV1_ICDF( 5983),AV1_ICDF(24099),AV1_ICDF(24560),AV1_ICDF(24886),AV1_ICDF(25066),AV1_ICDF(25795),AV1_ICDF(25913),AV1_ICDF(26423),AV1_ICDF(27610),AV1_ICDF(29905),AV1_ICDF(31276),AV1_ICDF(31794),AV1_ICDF(32768),0},
   {AV1_ICDF( 7444),AV1_ICDF(12781),AV1_ICDF(20177),AV1_ICDF(20728),AV1_ICDF(21077),AV1_ICDF(21607),AV1_ICDF(22170),AV1_ICDF(23405),AV1_ICDF(24469),AV1_ICDF(27915),AV1_ICDF(29090),AV1_ICDF(30492),AV1_ICDF(32768),0},
   {AV1_ICDF( 8537),AV1_ICDF(14689),AV1_ICDF(15432),AV1_ICDF(17087),AV1_ICDF(17408),AV1_ICDF(18172),AV1_ICDF(18408),AV1_ICDF(19825),AV1_ICDF(24649),AV1_ICDF(29153),AV1_ICDF(31096),AV1_ICDF(32210),AV1_ICDF(32768),0},
   {AV1_ICDF( 7543),AV1_ICDF(14231),AV1_ICDF(15496),AV1_ICDF(16195),AV1_ICDF(17905),AV1_ICDF(20717),AV1_ICDF(21984),AV1_ICDF(24516),AV1_ICDF(26001),AV1_ICDF(29675),AV1_ICDF(30981),AV1_ICDF(31994),AV1_ICDF(32768),0}},
  {{AV1_ICDF(12613),AV1_ICDF(13591),AV1_ICDF(21383),AV1_ICDF(22004),AV1_ICDF(22312),AV1_ICDF(22577),AV1_ICDF(23401),AV1_ICDF(25055),AV1_ICDF(25729),AV1_ICDF(29538),AV1_ICDF(30305),AV1_ICDF(32077),AV1_ICDF(32768),0},
   {AV1_ICDF( 9687),AV1_ICDF(13470),AV1_ICDF(18506),AV1_ICDF(19230),AV1_ICDF(19604),AV1_ICDF(20147),AV1_ICDF(20695),AV1_ICDF(22062),AV1_ICDF(23219),AV1_ICDF(27743),AV1_ICDF(29211),AV1_ICDF(30907),AV1_ICDF(32768),0},
   {AV1_ICDF( 6183),AV1_ICDF( 6505),AV1_ICDF(26024),AV1_ICDF(26252),AV1_ICDF(26366),AV1_ICDF(26434),AV1_ICDF(27082),AV1_ICDF(28354),AV1_ICDF(28555),AV1_ICDF(30467),AV1_ICDF(30794),AV1_ICDF(32086),AV1_ICDF(32768),0},
   {AV1_ICDF(10718),AV1_ICDF(11734),AV1_ICDF(14954),AV1_ICDF(17224),AV1_ICDF(17565),AV1_ICDF(17924),AV1_ICDF(18561),AV1_ICDF(21523),AV1_ICDF(23878),AV1_ICDF(28975),AV1_ICDF(30287),AV1_ICDF(32252),AV1_ICDF(32768),0},
   {AV1_ICDF( 9194),AV1_ICDF( 9858),AV1_ICDF(16501),AV1_ICDF(17263),AV1_ICDF(18424),AV1_ICDF(19171),AV1_ICDF(21563),AV1_ICDF(25961),AV1_ICDF(26561),AV1_ICDF(30072),AV1_ICDF(30737),AV1_ICDF(32463),AV1_ICDF(32768),0}},
  {{AV1_ICDF(12602),AV1_ICDF(14399),AV1_ICDF(15488),AV1_ICDF(18381),AV1_ICDF(18778),AV1_ICDF(19315),AV1_ICDF(19724),AV1_ICDF(21419),AV1_ICDF(25060),AV1_ICDF(29696),AV1_ICDF(30917),AV1_ICDF(32409),AV1_ICDF(32768),0},
   {AV1_ICDF( 8203),AV1_ICDF(13821),AV1_ICDF(14524),AV1_ICDF(17105),AV1_ICDF(17439),AV1_ICDF(18131),AV1_ICDF(18404),AV1_ICDF(19468),AV1_ICDF(25225),AV1_ICDF(29485),AV1_ICDF(31158),AV1_ICDF(32342),AV1_ICDF(32768),0},
   {AV1_ICDF( 8451),AV1_ICDF( 9731),AV1_ICDF(15004),AV1_ICDF(17643),AV1_ICDF(18012),AV1_ICDF(18425),AV1_ICDF(19070),AV1_ICDF(21538),AV1_ICDF(24605),AV1_ICDF(29118),AV1_ICDF(30078),AV1_ICDF(32018),AV1_ICDF(32768),0},
   {AV1_ICDF( 7714),AV1_ICDF( 9048),AV1_ICDF( 9516),AV1_ICDF(16667),AV1_ICDF(16817),AV1_ICDF(16994),AV1_ICDF(17153),AV1_ICDF(18767),AV1_ICDF(26743),AV1_ICDF(30389),AV1_ICDF(31536),AV1_ICDF(32528),AV1_ICDF(32768),0},
   {AV1_ICDF( 8843),AV1_ICDF(10280),AV1_ICDF(11496),AV1_ICDF(15317),AV1_ICDF(16652),AV1_ICDF(17943),AV1_ICDF(19108),AV1_ICDF(22718),AV1_ICDF(25769),AV1_ICDF(29953),AV1_ICDF(30983),AV1_ICDF(32485),AV1_ICDF(32768),0}},
  {{AV1_ICDF(12578),AV1_ICDF(13671),AV1_ICDF(15979),AV1_ICDF(16834),AV1_ICDF(19075),AV1_ICDF(20913),AV1_ICDF(22989),AV1_ICDF(25449),AV1_ICDF(26219),AV1_ICDF(30214),AV1_ICDF(31150),AV1_ICDF(32477),AV1_ICDF(32768),0},
   {AV1_ICDF( 9563),AV1_ICDF(13626),AV1_ICDF(15080),AV1_ICDF(15892),AV1_ICDF(17756),AV1_ICDF(20863),AV1_ICDF(22207),AV1_ICDF(24236),AV1_ICDF(25380),AV1_ICDF(29653),AV1_ICDF(31143),AV1_ICDF(32277),AV1_ICDF(32768),0},
   {AV1_ICDF( 8356),AV1_ICDF( 8901),AV1_ICDF(17616),AV1_ICDF(18256),AV1_ICDF(19350),AV1_ICDF(20106),AV1_ICDF(22598),AV1_ICDF(25947),AV1_ICDF(26466),AV1_ICDF(29900),AV1_ICDF(30523),AV1_ICDF(32261),AV1_ICDF(32768),0},
   {AV1_ICDF(10835),AV1_ICDF(11815),AV1_ICDF(13124),AV1_ICDF(16042),AV1_ICDF(17018),AV1_ICDF(18039),AV1_ICDF(18947),AV1_ICDF(22753),AV1_ICDF(24615),AV1_ICDF(29489),AV1_ICDF(30883),AV1_ICDF(32482),AV1_ICDF(32768),0},
   {AV1_ICDF( 7618),AV1_ICDF( 8288),AV1_ICDF( 9859),AV1_ICDF(10509),AV1_ICDF(15386),AV1_ICDF(18657),AV1_ICDF(22903),AV1_ICDF(28776),AV1_ICDF(29180),AV1_ICDF(31355),AV1_ICDF(31802),AV1_ICDF(32593),AV1_ICDF(32768),0}},
};

// ---- UV mode CDF — CFL not allowed [13][14] ----
static const uint16_t av1_uv_mode_cdf_no_cfl[13][14] = {
    {AV1_ICDF(22631),AV1_ICDF(24152),AV1_ICDF(25378),AV1_ICDF(25661),AV1_ICDF(25986),AV1_ICDF(26520),AV1_ICDF(27055),AV1_ICDF(27923),AV1_ICDF(28244),AV1_ICDF(30059),AV1_ICDF(30941),AV1_ICDF(31961),AV1_ICDF(32768),0},
    {AV1_ICDF( 9513),AV1_ICDF(26881),AV1_ICDF(26973),AV1_ICDF(27046),AV1_ICDF(27118),AV1_ICDF(27664),AV1_ICDF(27739),AV1_ICDF(27824),AV1_ICDF(28359),AV1_ICDF(29505),AV1_ICDF(29800),AV1_ICDF(31796),AV1_ICDF(32768),0},
    {AV1_ICDF( 9845),AV1_ICDF( 9915),AV1_ICDF(28663),AV1_ICDF(28704),AV1_ICDF(28757),AV1_ICDF(28780),AV1_ICDF(29198),AV1_ICDF(29822),AV1_ICDF(29854),AV1_ICDF(30764),AV1_ICDF(31777),AV1_ICDF(32029),AV1_ICDF(32768),0},
    {AV1_ICDF(13639),AV1_ICDF(13897),AV1_ICDF(14171),AV1_ICDF(25331),AV1_ICDF(25606),AV1_ICDF(25727),AV1_ICDF(25953),AV1_ICDF(27148),AV1_ICDF(28577),AV1_ICDF(30612),AV1_ICDF(31355),AV1_ICDF(32493),AV1_ICDF(32768),0},
    {AV1_ICDF( 9764),AV1_ICDF( 9835),AV1_ICDF( 9930),AV1_ICDF( 9954),AV1_ICDF(25386),AV1_ICDF(27053),AV1_ICDF(27958),AV1_ICDF(28148),AV1_ICDF(28243),AV1_ICDF(31101),AV1_ICDF(31744),AV1_ICDF(32363),AV1_ICDF(32768),0},
    {AV1_ICDF(11825),AV1_ICDF(13589),AV1_ICDF(13677),AV1_ICDF(13720),AV1_ICDF(15048),AV1_ICDF(29213),AV1_ICDF(29301),AV1_ICDF(29458),AV1_ICDF(29711),AV1_ICDF(31161),AV1_ICDF(31441),AV1_ICDF(32550),AV1_ICDF(32768),0},
    {AV1_ICDF(14175),AV1_ICDF(14399),AV1_ICDF(16608),AV1_ICDF(16821),AV1_ICDF(17718),AV1_ICDF(17775),AV1_ICDF(28551),AV1_ICDF(30200),AV1_ICDF(30245),AV1_ICDF(31837),AV1_ICDF(32342),AV1_ICDF(32667),AV1_ICDF(32768),0},
    {AV1_ICDF(12885),AV1_ICDF(13038),AV1_ICDF(14978),AV1_ICDF(15590),AV1_ICDF(15673),AV1_ICDF(15748),AV1_ICDF(16176),AV1_ICDF(29128),AV1_ICDF(29267),AV1_ICDF(30643),AV1_ICDF(31961),AV1_ICDF(32461),AV1_ICDF(32768),0},
    {AV1_ICDF(12026),AV1_ICDF(13661),AV1_ICDF(13874),AV1_ICDF(15305),AV1_ICDF(15490),AV1_ICDF(15726),AV1_ICDF(15995),AV1_ICDF(16273),AV1_ICDF(28443),AV1_ICDF(30388),AV1_ICDF(30767),AV1_ICDF(32416),AV1_ICDF(32768),0},
    {AV1_ICDF(19052),AV1_ICDF(19840),AV1_ICDF(20579),AV1_ICDF(20916),AV1_ICDF(21150),AV1_ICDF(21467),AV1_ICDF(21885),AV1_ICDF(22719),AV1_ICDF(23174),AV1_ICDF(28861),AV1_ICDF(30379),AV1_ICDF(32175),AV1_ICDF(32768),0},
    {AV1_ICDF(18627),AV1_ICDF(19649),AV1_ICDF(20974),AV1_ICDF(21219),AV1_ICDF(21492),AV1_ICDF(21816),AV1_ICDF(22199),AV1_ICDF(23119),AV1_ICDF(23527),AV1_ICDF(27053),AV1_ICDF(31397),AV1_ICDF(32148),AV1_ICDF(32768),0},
    {AV1_ICDF(17026),AV1_ICDF(19004),AV1_ICDF(19997),AV1_ICDF(20339),AV1_ICDF(20586),AV1_ICDF(21103),AV1_ICDF(21349),AV1_ICDF(21907),AV1_ICDF(22482),AV1_ICDF(25896),AV1_ICDF(26541),AV1_ICDF(31819),AV1_ICDF(32768),0},
    {AV1_ICDF(12124),AV1_ICDF(13759),AV1_ICDF(14959),AV1_ICDF(14992),AV1_ICDF(15007),AV1_ICDF(15051),AV1_ICDF(15078),AV1_ICDF(15166),AV1_ICDF(15255),AV1_ICDF(15753),AV1_ICDF(16039),AV1_ICDF(16606),AV1_ICDF(32768),0},
};

// ---- UV mode CDF — CFL allowed [13][15] ----
static const uint16_t av1_uv_mode_cdf_cfl[13][15] = {
    {AV1_ICDF(10407),AV1_ICDF(11208),AV1_ICDF(12900),AV1_ICDF(13181),AV1_ICDF(13823),AV1_ICDF(14175),AV1_ICDF(14899),AV1_ICDF(15656),AV1_ICDF(15986),AV1_ICDF(20086),AV1_ICDF(20995),AV1_ICDF(22455),AV1_ICDF(24212),AV1_ICDF(32768),0},
    {AV1_ICDF( 4532),AV1_ICDF(19780),AV1_ICDF(20057),AV1_ICDF(20215),AV1_ICDF(20428),AV1_ICDF(21071),AV1_ICDF(21199),AV1_ICDF(21451),AV1_ICDF(22099),AV1_ICDF(24228),AV1_ICDF(24693),AV1_ICDF(27032),AV1_ICDF(29472),AV1_ICDF(32768),0},
    {AV1_ICDF( 5273),AV1_ICDF( 5379),AV1_ICDF(20177),AV1_ICDF(20270),AV1_ICDF(20385),AV1_ICDF(20439),AV1_ICDF(20949),AV1_ICDF(21695),AV1_ICDF(21774),AV1_ICDF(23138),AV1_ICDF(24256),AV1_ICDF(24703),AV1_ICDF(26679),AV1_ICDF(32768),0},
    {AV1_ICDF( 6740),AV1_ICDF( 7167),AV1_ICDF( 7662),AV1_ICDF(14152),AV1_ICDF(14536),AV1_ICDF(14785),AV1_ICDF(15034),AV1_ICDF(16741),AV1_ICDF(18371),AV1_ICDF(21520),AV1_ICDF(22206),AV1_ICDF(23389),AV1_ICDF(24182),AV1_ICDF(32768),0},
    {AV1_ICDF( 4987),AV1_ICDF( 5368),AV1_ICDF( 5928),AV1_ICDF( 6068),AV1_ICDF(19114),AV1_ICDF(20315),AV1_ICDF(21857),AV1_ICDF(22253),AV1_ICDF(22411),AV1_ICDF(24911),AV1_ICDF(25380),AV1_ICDF(26027),AV1_ICDF(26376),AV1_ICDF(32768),0},
    {AV1_ICDF( 5370),AV1_ICDF( 6889),AV1_ICDF( 7247),AV1_ICDF( 7393),AV1_ICDF( 9498),AV1_ICDF(21114),AV1_ICDF(21402),AV1_ICDF(21753),AV1_ICDF(21981),AV1_ICDF(24780),AV1_ICDF(25386),AV1_ICDF(26517),AV1_ICDF(27176),AV1_ICDF(32768),0},
    {AV1_ICDF( 4816),AV1_ICDF( 4961),AV1_ICDF( 7204),AV1_ICDF( 7326),AV1_ICDF( 8765),AV1_ICDF( 8930),AV1_ICDF(20169),AV1_ICDF(20682),AV1_ICDF(20803),AV1_ICDF(23188),AV1_ICDF(23763),AV1_ICDF(24455),AV1_ICDF(24940),AV1_ICDF(32768),0},
    {AV1_ICDF( 6608),AV1_ICDF( 6740),AV1_ICDF( 8529),AV1_ICDF( 9049),AV1_ICDF( 9257),AV1_ICDF( 9356),AV1_ICDF( 9735),AV1_ICDF(18827),AV1_ICDF(19059),AV1_ICDF(22336),AV1_ICDF(23204),AV1_ICDF(23964),AV1_ICDF(24793),AV1_ICDF(32768),0},
    {AV1_ICDF( 5998),AV1_ICDF( 7419),AV1_ICDF( 7781),AV1_ICDF( 8933),AV1_ICDF( 9255),AV1_ICDF( 9549),AV1_ICDF( 9753),AV1_ICDF(10417),AV1_ICDF(18898),AV1_ICDF(22494),AV1_ICDF(23139),AV1_ICDF(24764),AV1_ICDF(25989),AV1_ICDF(32768),0},
    {AV1_ICDF(10660),AV1_ICDF(11298),AV1_ICDF(12550),AV1_ICDF(12957),AV1_ICDF(13322),AV1_ICDF(13624),AV1_ICDF(14040),AV1_ICDF(15004),AV1_ICDF(15534),AV1_ICDF(20714),AV1_ICDF(21789),AV1_ICDF(23443),AV1_ICDF(24861),AV1_ICDF(32768),0},
    {AV1_ICDF(10522),AV1_ICDF(11530),AV1_ICDF(12552),AV1_ICDF(12963),AV1_ICDF(13378),AV1_ICDF(13779),AV1_ICDF(14245),AV1_ICDF(15235),AV1_ICDF(15902),AV1_ICDF(20102),AV1_ICDF(22696),AV1_ICDF(23774),AV1_ICDF(25838),AV1_ICDF(32768),0},
    {AV1_ICDF(10099),AV1_ICDF(10691),AV1_ICDF(12639),AV1_ICDF(13049),AV1_ICDF(13386),AV1_ICDF(13665),AV1_ICDF(14125),AV1_ICDF(15163),AV1_ICDF(15636),AV1_ICDF(19676),AV1_ICDF(20474),AV1_ICDF(23519),AV1_ICDF(25208),AV1_ICDF(32768),0},
    {AV1_ICDF( 3144),AV1_ICDF( 5087),AV1_ICDF( 7382),AV1_ICDF( 7504),AV1_ICDF( 7593),AV1_ICDF( 7690),AV1_ICDF( 7801),AV1_ICDF( 8064),AV1_ICDF( 8232),AV1_ICDF( 9248),AV1_ICDF( 9875),AV1_ICDF(10521),AV1_ICDF(29048),AV1_ICDF(32768),0},
};

// ---- Angle delta CDFs for directional intra modes [8][8] ----
static const uint16_t av1_angle_delta_cdf[8][8] = {
    {AV1_ICDF(2180), AV1_ICDF(5032), AV1_ICDF(7567), AV1_ICDF(22776), AV1_ICDF(26989), AV1_ICDF(30217), AV1_ICDF(32768), 0},
    {AV1_ICDF(2301), AV1_ICDF(5608), AV1_ICDF(8801), AV1_ICDF(23487), AV1_ICDF(26974), AV1_ICDF(30330), AV1_ICDF(32768), 0},
    {AV1_ICDF(3780), AV1_ICDF(11018), AV1_ICDF(13699), AV1_ICDF(19354), AV1_ICDF(23083), AV1_ICDF(31286), AV1_ICDF(32768), 0},
    {AV1_ICDF(4581), AV1_ICDF(11226), AV1_ICDF(15147), AV1_ICDF(17138), AV1_ICDF(21834), AV1_ICDF(28397), AV1_ICDF(32768), 0},
    {AV1_ICDF(1737), AV1_ICDF(10927), AV1_ICDF(14509), AV1_ICDF(19588), AV1_ICDF(22745), AV1_ICDF(28823), AV1_ICDF(32768), 0},
    {AV1_ICDF(2664), AV1_ICDF(10176), AV1_ICDF(12485), AV1_ICDF(17650), AV1_ICDF(21600), AV1_ICDF(30495), AV1_ICDF(32768), 0},
    {AV1_ICDF(2240), AV1_ICDF(11096), AV1_ICDF(15453), AV1_ICDF(20341), AV1_ICDF(22561), AV1_ICDF(28917), AV1_ICDF(32768), 0},
    {AV1_ICDF(3605), AV1_ICDF(10428), AV1_ICDF(12459), AV1_ICDF(17676), AV1_ICDF(21244), AV1_ICDF(30655), AV1_ICDF(32768), 0},
};

// ---- Non-key intra/inter and single-ref CDFs ----
static const uint16_t av1_if_y_mode_cdf[4][14] = {
    {AV1_ICDF(22801), AV1_ICDF(23489), AV1_ICDF(24293), AV1_ICDF(24756), AV1_ICDF(25601), AV1_ICDF(26123), AV1_ICDF(26606),
     AV1_ICDF(27418), AV1_ICDF(27945), AV1_ICDF(29228), AV1_ICDF(29685), AV1_ICDF(30349), AV1_ICDF(32768), 0},
    {AV1_ICDF(18673), AV1_ICDF(19845), AV1_ICDF(22631), AV1_ICDF(23318), AV1_ICDF(23950), AV1_ICDF(24649), AV1_ICDF(25527),
     AV1_ICDF(27364), AV1_ICDF(28152), AV1_ICDF(29701), AV1_ICDF(29984), AV1_ICDF(30852), AV1_ICDF(32768), 0},
    {AV1_ICDF(19770), AV1_ICDF(20979), AV1_ICDF(23396), AV1_ICDF(23939), AV1_ICDF(24241), AV1_ICDF(24654), AV1_ICDF(25136),
     AV1_ICDF(27073), AV1_ICDF(27830), AV1_ICDF(29360), AV1_ICDF(29730), AV1_ICDF(30659), AV1_ICDF(32768), 0},
    {AV1_ICDF(20155), AV1_ICDF(21301), AV1_ICDF(22838), AV1_ICDF(23178), AV1_ICDF(23261), AV1_ICDF(23533), AV1_ICDF(23703),
     AV1_ICDF(24804), AV1_ICDF(25352), AV1_ICDF(26575), AV1_ICDF(27016), AV1_ICDF(28049), AV1_ICDF(32768), 0},
};

static const uint16_t av1_newmv_cdf[6][3] = {
    {AV1_ICDF(24035), AV1_ICDF(32768), 0},
    {AV1_ICDF(16630), AV1_ICDF(32768), 0},
    {AV1_ICDF(15339), AV1_ICDF(32768), 0},
    {AV1_ICDF(8386),  AV1_ICDF(32768), 0},
    {AV1_ICDF(12222), AV1_ICDF(32768), 0},
    {AV1_ICDF(4676),  AV1_ICDF(32768), 0},
};

static const uint16_t av1_zeromv_cdf[2][3] = {
    {AV1_ICDF(2175), AV1_ICDF(32768), 0},
    {AV1_ICDF(1054), AV1_ICDF(32768), 0},
};

static const uint16_t av1_refmv_cdf[6][3] = {
    {AV1_ICDF(23974), AV1_ICDF(32768), 0},
    {AV1_ICDF(24188), AV1_ICDF(32768), 0},
    {AV1_ICDF(17848), AV1_ICDF(32768), 0},
    {AV1_ICDF(28622), AV1_ICDF(32768), 0},
    {AV1_ICDF(24312), AV1_ICDF(32768), 0},
    {AV1_ICDF(19923), AV1_ICDF(32768), 0},
};

static const uint16_t av1_drl_cdf[3][3] = {
    {AV1_ICDF(13104), AV1_ICDF(32768), 0},
    {AV1_ICDF(24560), AV1_ICDF(32768), 0},
    {AV1_ICDF(18945), AV1_ICDF(32768), 0},
};

static const uint16_t av1_intra_inter_cdf[4][3] = {
    {AV1_ICDF(806),   AV1_ICDF(32768), 0},
    {AV1_ICDF(16662), AV1_ICDF(32768), 0},
    {AV1_ICDF(20186), AV1_ICDF(32768), 0},
    {AV1_ICDF(26538), AV1_ICDF(32768), 0},
};

static const uint16_t av1_single_ref_cdf[3][6][3] = {
    {
        {AV1_ICDF(4897), AV1_ICDF(32768), 0},
        {AV1_ICDF(1555), AV1_ICDF(32768), 0},
        {AV1_ICDF(4236), AV1_ICDF(32768), 0},
        {AV1_ICDF(8650), AV1_ICDF(32768), 0},
        {AV1_ICDF(904),  AV1_ICDF(32768), 0},
        {AV1_ICDF(1444), AV1_ICDF(32768), 0},
    },
    {
        {AV1_ICDF(16973), AV1_ICDF(32768), 0},
        {AV1_ICDF(16751), AV1_ICDF(32768), 0},
        {AV1_ICDF(19647), AV1_ICDF(32768), 0},
        {AV1_ICDF(24773), AV1_ICDF(32768), 0},
        {AV1_ICDF(11014), AV1_ICDF(32768), 0},
        {AV1_ICDF(15087), AV1_ICDF(32768), 0},
    },
    {
        {AV1_ICDF(29744), AV1_ICDF(32768), 0},
        {AV1_ICDF(30279), AV1_ICDF(32768), 0},
        {AV1_ICDF(31194), AV1_ICDF(32768), 0},
        {AV1_ICDF(31895), AV1_ICDF(32768), 0},
        {AV1_ICDF(26875), AV1_ICDF(32768), 0},
        {AV1_ICDF(30304), AV1_ICDF(32768), 0},
    },
};

// ---- Intra mode context and partition context ----
static const uint8_t av1_intra_mode_context[13] = {0,1,2,3,4,4,4,4,3,0,1,2,0};
static const uint8_t av1_part_ctx_above[4] = {30, 28, 24, 16};
static const uint8_t av1_part_ctx_left[4]  = {30, 28, 24, 16};
static const uint16_t AV1_REF_CAT_LEVEL = 640;
static const int AV1_GLOBALMV_OFFSET = 3;
static const int AV1_REFMV_OFFSET = 4;
static const int AV1_NEWMV_CTX_MASK = (1 << AV1_GLOBALMV_OFFSET) - 1;
static const int AV1_GLOBALMV_CTX_MASK =
    (1 << (AV1_REFMV_OFFSET - AV1_GLOBALMV_OFFSET)) - 1;
static const int AV1_REFMV_CTX_MASK = (1 << (8 - AV1_REFMV_OFFSET)) - 1;

// ---- Default NMV tables from libaom/av1/common/entropymv.c ----
static const uint16_t av1_mv_joint_cdf[5] = {
    AV1_ICDF(4096), AV1_ICDF(11264), AV1_ICDF(19328), AV1_ICDF(32768), 0
};

static const uint16_t av1_mv_class_cdf[12] = {
    AV1_ICDF(28672), AV1_ICDF(30976), AV1_ICDF(31858), AV1_ICDF(32320),
    AV1_ICDF(32551), AV1_ICDF(32656), AV1_ICDF(32740), AV1_ICDF(32757),
    AV1_ICDF(32762), AV1_ICDF(32767), AV1_ICDF(32768), 0
};

static const uint16_t av1_mv_class0_fp_cdf[2][5] = {
    {AV1_ICDF(16384), AV1_ICDF(24576), AV1_ICDF(26624), AV1_ICDF(32768), 0},
    {AV1_ICDF(12288), AV1_ICDF(21248), AV1_ICDF(24128), AV1_ICDF(32768), 0},
};

static const uint16_t av1_mv_fp_cdf[5] = {
    AV1_ICDF(8192), AV1_ICDF(17408), AV1_ICDF(21248), AV1_ICDF(32768), 0
};

static const uint16_t av1_mv_sign_cdf[3] = {
    AV1_ICDF(16384), AV1_ICDF(32768), 0
};

static const uint16_t av1_mv_class0_cdf[3] = {
    AV1_ICDF(27648), AV1_ICDF(32768), 0
};

static const uint16_t av1_mv_bits_cdf[10][3] = {
    {AV1_ICDF(17408), AV1_ICDF(32768), 0},
    {AV1_ICDF(17920), AV1_ICDF(32768), 0},
    {AV1_ICDF(18944), AV1_ICDF(32768), 0},
    {AV1_ICDF(20480), AV1_ICDF(32768), 0},
    {AV1_ICDF(22528), AV1_ICDF(32768), 0},
    {AV1_ICDF(24576), AV1_ICDF(32768), 0},
    {AV1_ICDF(28672), AV1_ICDF(32768), 0},
    {AV1_ICDF(29952), AV1_ICDF(32768), 0},
    {AV1_ICDF(29952), AV1_ICDF(32768), 0},
    {AV1_ICDF(30720), AV1_ICDF(32768), 0},
};

// Inter ext-tx CDF for TX_8X8 under EXT_TX_SET_ALL16 / eset=1.
static const uint16_t av1_inter_tx_type_cdf_8x8_all16[17] = {
    AV1_ICDF(1645),  AV1_ICDF(2573),  AV1_ICDF(4778),  AV1_ICDF(5711),
    AV1_ICDF(7807),  AV1_ICDF(8622),  AV1_ICDF(10522), AV1_ICDF(15357),
    AV1_ICDF(17674), AV1_ICDF(20408), AV1_ICDF(22517), AV1_ICDF(25010),
    AV1_ICDF(27116), AV1_ICDF(28856), AV1_ICDF(30749), AV1_ICDF(32768), 0
};

// ============================================================
// Coefficient Coding CDF Tables (QP group 3: qindex > 120)
// Stored in ICDF format. Only TX_8X8 (txs_ctx=1) and luma (plane=0).
// ============================================================

// txb_skip CDF for TX_4X4 [13 contexts][3 values] — for chroma in 4:2:0
// Source: av1_default_txb_skip_cdfs[3][0] — QP group 3, TX_4X4
static const uint16_t av1_txb_skip_cdf_4x4[13][3] = {
    {AV1_ICDF(26887), AV1_ICDF(32768), 0},
    {AV1_ICDF( 6729), AV1_ICDF(32768), 0},
    {AV1_ICDF(10361), AV1_ICDF(32768), 0},
    {AV1_ICDF(17442), AV1_ICDF(32768), 0},
    {AV1_ICDF(15045), AV1_ICDF(32768), 0},
    {AV1_ICDF(22478), AV1_ICDF(32768), 0},
    {AV1_ICDF(29072), AV1_ICDF(32768), 0},
    {AV1_ICDF( 2713), AV1_ICDF(32768), 0},
    {AV1_ICDF(11861), AV1_ICDF(32768), 0},
    {AV1_ICDF(20773), AV1_ICDF(32768), 0},
    {AV1_ICDF(16384), AV1_ICDF(32768), 0},
    {AV1_ICDF(16384), AV1_ICDF(32768), 0},
    {AV1_ICDF(16384), AV1_ICDF(32768), 0},
};

// txb_skip CDF for TX_8X8 [13 contexts][3 values]
// Source: av1_default_txb_skip_cdfs[3][1] — QP group 3, TX_8X8
static const uint16_t av1_txb_skip_cdf[13][3] = {
    {AV1_ICDF(31903), AV1_ICDF(32768), 0},
    {AV1_ICDF( 2044), AV1_ICDF(32768), 0},
    {AV1_ICDF( 7528), AV1_ICDF(32768), 0},
    {AV1_ICDF(14618), AV1_ICDF(32768), 0},
    {AV1_ICDF(16182), AV1_ICDF(32768), 0},
    {AV1_ICDF(24168), AV1_ICDF(32768), 0},
    {AV1_ICDF(31037), AV1_ICDF(32768), 0},
    {AV1_ICDF( 2786), AV1_ICDF(32768), 0},
    {AV1_ICDF(11194), AV1_ICDF(32768), 0},
    {AV1_ICDF(20155), AV1_ICDF(32768), 0},
    {AV1_ICDF(16384), AV1_ICDF(32768), 0},
    {AV1_ICDF(16384), AV1_ICDF(32768), 0},
    {AV1_ICDF(16384), AV1_ICDF(32768), 0},
};

// EOB multi CDF for 64 coefficients (TX_8X8): [2 planes][8 values]
static const uint16_t av1_eob_multi64_cdf[2][8] = {
    {AV1_ICDF(6307),AV1_ICDF(7541),AV1_ICDF(12060),AV1_ICDF(16358),AV1_ICDF(22553),AV1_ICDF(27865),AV1_ICDF(32768),0},
    {AV1_ICDF(24212),AV1_ICDF(25708),AV1_ICDF(28268),AV1_ICDF(30035),AV1_ICDF(31307),AV1_ICDF(32049),AV1_ICDF(32768),0},
};

// EOB extra CDF for TX_8X8 [2 planes][9 contexts][3 values]
// Source: av1_default_eob_extra_cdfs[3][1] — QP group 3, TX_8X8
static const uint16_t av1_eob_extra_cdf[2][9][3] = {
  {{AV1_ICDF(20238),AV1_ICDF(32768),0},{AV1_ICDF(21057),AV1_ICDF(32768),0},{AV1_ICDF(19159),AV1_ICDF(32768),0},
   {AV1_ICDF(22337),AV1_ICDF(32768),0},{AV1_ICDF(20159),AV1_ICDF(32768),0},{AV1_ICDF(16384),AV1_ICDF(32768),0},
   {AV1_ICDF(16384),AV1_ICDF(32768),0},{AV1_ICDF(16384),AV1_ICDF(32768),0},{AV1_ICDF(16384),AV1_ICDF(32768),0}},
  {{AV1_ICDF(20125),AV1_ICDF(32768),0},{AV1_ICDF(20559),AV1_ICDF(32768),0},{AV1_ICDF(21707),AV1_ICDF(32768),0},
   {AV1_ICDF(22296),AV1_ICDF(32768),0},{AV1_ICDF(17333),AV1_ICDF(32768),0},{AV1_ICDF(16384),AV1_ICDF(32768),0},
   {AV1_ICDF(16384),AV1_ICDF(32768),0},{AV1_ICDF(16384),AV1_ICDF(32768),0},{AV1_ICDF(16384),AV1_ICDF(32768),0}},
};

// coeff_base CDF for TX_8X8, luma plane [42 contexts][5 values]
// Source: av1_default_coeff_base_multi_cdfs[3][1][0] — QP group 3, TX_8X8, luma
static const uint16_t av1_coeff_base_cdf[42][5] = {
    {25014,15820,10626,0,0},
    {7098,438,77,0,0},
    {17105,3543,774,0,0},
    {22890,9480,3610,0,0},
    {26349,15680,8432,0,0},
    {28909,21765,15729,0,0},
    {5206,173,43,0,0},
    {15193,2180,369,0,0},
    {21949,7930,2459,0,0},
    {25644,14082,6852,0,0},
    {28289,20080,13428,0,0},
    {4383,292,95,0,0},
    {17462,3763,830,0,0},
    {23831,11153,4446,0,0},
    {26786,17165,9982,0,0},
    {29148,22501,16632,0,0},
    {5488,304,101,0,0},
    {17161,3608,764,0,0},
    {23677,10633,4028,0,0},
    {26536,16136,8748,0,0},
    {28721,21391,15096,0,0},
    {3548,138,50,0,0},
    {13118,1548,306,0,0},
    {19718,6456,1941,0,0},
    {23540,11898,5300,0,0},
    {26622,17619,10797,0,0},
    {2599,287,145,0,0},
    {15556,3457,1214,0,0},
    {22857,11457,5886,0,0},
    {28281,19454,12396,0,0},
    {30198,24996,19879,0,0},
    {1844,155,60,0,0},
    {13278,2562,661,0,0},
    {21536,8770,3492,0,0},
    {25999,14813,7733,0,0},
    {28370,20145,13554,0,0},
    {2159,141,46,0,0},
    {13398,2186,481,0,0},
    {22311,9149,3359,0,0},
    {26325,15131,7934,0,0},
    {28123,19532,12662,0,0},
    {24576,16384,8192,0,0},
};

// coeff_base_eob CDF for TX_8X8, luma [4 contexts][4 values]
static const uint16_t av1_coeff_base_eob_cdf[4][4] = {
    {AV1_ICDF(21457),AV1_ICDF(31043),AV1_ICDF(32768),0},
    {AV1_ICDF(31951),AV1_ICDF(32483),AV1_ICDF(32768),0},
    {AV1_ICDF(32153),AV1_ICDF(32562),AV1_ICDF(32768),0},
    {AV1_ICDF(31473),AV1_ICDF(32215),AV1_ICDF(32768),0},
};

// coeff_br (LPS) CDF for TX_8X8, luma [21 contexts][5 values]
// Source: av1_default_coeff_lps_multi_cdfs[3][1][0] — QP group 3, TX_8X8, luma
static const uint16_t av1_coeff_br_cdf[21][5] = {
    {AV1_ICDF(18274),AV1_ICDF(24813),AV1_ICDF(27890),AV1_ICDF(32768),0},
    {AV1_ICDF(15537),AV1_ICDF(23149),AV1_ICDF(27003),AV1_ICDF(32768),0},
    {AV1_ICDF(9449),AV1_ICDF(16740),AV1_ICDF(21827),AV1_ICDF(32768),0},
    {AV1_ICDF(6700),AV1_ICDF(12498),AV1_ICDF(17261),AV1_ICDF(32768),0},
    {AV1_ICDF(4988),AV1_ICDF(9866),AV1_ICDF(14198),AV1_ICDF(32768),0},
    {AV1_ICDF(4236),AV1_ICDF(8147),AV1_ICDF(11902),AV1_ICDF(32768),0},
    {AV1_ICDF(2867),AV1_ICDF(5860),AV1_ICDF(8654),AV1_ICDF(32768),0},
    {AV1_ICDF(17124),AV1_ICDF(23171),AV1_ICDF(26101),AV1_ICDF(32768),0},
    {AV1_ICDF(20396),AV1_ICDF(27477),AV1_ICDF(30148),AV1_ICDF(32768),0},
    {AV1_ICDF(16573),AV1_ICDF(24629),AV1_ICDF(28492),AV1_ICDF(32768),0},
    {AV1_ICDF(12749),AV1_ICDF(20846),AV1_ICDF(25674),AV1_ICDF(32768),0},
    {AV1_ICDF(10233),AV1_ICDF(17878),AV1_ICDF(22818),AV1_ICDF(32768),0},
    {AV1_ICDF(8525),AV1_ICDF(15332),AV1_ICDF(20363),AV1_ICDF(32768),0},
    {AV1_ICDF(6283),AV1_ICDF(11632),AV1_ICDF(16255),AV1_ICDF(32768),0},
    {AV1_ICDF(20466),AV1_ICDF(26511),AV1_ICDF(29286),AV1_ICDF(32768),0},
    {AV1_ICDF(23059),AV1_ICDF(29174),AV1_ICDF(31191),AV1_ICDF(32768),0},
    {AV1_ICDF(19481),AV1_ICDF(27263),AV1_ICDF(30241),AV1_ICDF(32768),0},
    {AV1_ICDF(15458),AV1_ICDF(23631),AV1_ICDF(28137),AV1_ICDF(32768),0},
    {AV1_ICDF(12416),AV1_ICDF(20608),AV1_ICDF(25693),AV1_ICDF(32768),0},
    {AV1_ICDF(10261),AV1_ICDF(18011),AV1_ICDF(23261),AV1_ICDF(32768),0},
    {AV1_ICDF(8016),AV1_ICDF(14655),AV1_ICDF(19666),AV1_ICDF(32768),0},
};

// DC sign CDF [2 planes][3 contexts][3 values]
static const uint16_t av1_dc_sign_cdf[2][3][3] = {
  {{AV1_ICDF(16000),AV1_ICDF(32768),0},{AV1_ICDF(13056),AV1_ICDF(32768),0},{AV1_ICDF(18816),AV1_ICDF(32768),0}},
  {{AV1_ICDF(15232),AV1_ICDF(32768),0},{AV1_ICDF(12928),AV1_ICDF(32768),0},{AV1_ICDF(17280),AV1_ICDF(32768),0}},
};

// Intra TX type CDF: eset=1, TX_8X8, DC_PRED — 7 symbols
// DCT_DCT maps to symbol 1 (av1_ext_tx_ind[3][0] = 1)
static const uint16_t av1_intra_tx_type_cdf_8x8[13][8] = {
    {AV1_ICDF(1870), AV1_ICDF(13742), AV1_ICDF(14530), AV1_ICDF(16498), AV1_ICDF(23770), AV1_ICDF(27698), AV1_ICDF(32768), 0},
    {AV1_ICDF(326),  AV1_ICDF(8796),  AV1_ICDF(14632), AV1_ICDF(15079), AV1_ICDF(19272), AV1_ICDF(27486), AV1_ICDF(32768), 0},
    {AV1_ICDF(484),  AV1_ICDF(7576),  AV1_ICDF(7712),  AV1_ICDF(14443), AV1_ICDF(19159), AV1_ICDF(22591), AV1_ICDF(32768), 0},
    {AV1_ICDF(1126), AV1_ICDF(15340), AV1_ICDF(15895), AV1_ICDF(17023), AV1_ICDF(20896), AV1_ICDF(30279), AV1_ICDF(32768), 0},
    {AV1_ICDF(655),  AV1_ICDF(4854),  AV1_ICDF(5249),  AV1_ICDF(5913),  AV1_ICDF(22099), AV1_ICDF(27138), AV1_ICDF(32768), 0},
    {AV1_ICDF(1299), AV1_ICDF(6458),  AV1_ICDF(8885),  AV1_ICDF(9290),  AV1_ICDF(14851), AV1_ICDF(25497), AV1_ICDF(32768), 0},
    {AV1_ICDF(311),  AV1_ICDF(5295),  AV1_ICDF(5552),  AV1_ICDF(6885),  AV1_ICDF(16107), AV1_ICDF(22672), AV1_ICDF(32768), 0},
    {AV1_ICDF(883),  AV1_ICDF(8059),  AV1_ICDF(8270),  AV1_ICDF(11258), AV1_ICDF(17289), AV1_ICDF(21549), AV1_ICDF(32768), 0},
    {AV1_ICDF(741),  AV1_ICDF(7580),  AV1_ICDF(9318),  AV1_ICDF(10345), AV1_ICDF(16688), AV1_ICDF(29046), AV1_ICDF(32768), 0},
    {AV1_ICDF(110),  AV1_ICDF(7406),  AV1_ICDF(7915),  AV1_ICDF(9195),  AV1_ICDF(16041), AV1_ICDF(23329), AV1_ICDF(32768), 0},
    {AV1_ICDF(363),  AV1_ICDF(7974),  AV1_ICDF(9357),  AV1_ICDF(10673), AV1_ICDF(15629), AV1_ICDF(24474), AV1_ICDF(32768), 0},
    {AV1_ICDF(153),  AV1_ICDF(7647),  AV1_ICDF(8112),  AV1_ICDF(9936),  AV1_ICDF(15307), AV1_ICDF(19996), AV1_ICDF(32768), 0},
    {AV1_ICDF(3511), AV1_ICDF(6332),  AV1_ICDF(11165), AV1_ICDF(15335), AV1_ICDF(19323), AV1_ICDF(23594), AV1_ICDF(32768), 0},
};

// ---- Scan order for TX_8X8 (diagonal) ----
static const int16_t default_scan_8x8[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

// nz_map context offset for TX_8X8 (64 entries, raster order)
static const int8_t nz_map_ctx_offset_8x8[64] = {
    0,  1,  6,  6,  21, 21, 21, 21,  1,  6,  6,  21, 21, 21, 21, 21,
    6,  6,  21, 21, 21, 21, 21, 21,  6,  21, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
};

// EOB tables
static const uint8_t eob_to_pos_small[33] = {
    0, 1, 2, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6
};
static const uint8_t eob_to_pos_large[17] = {
    6, 7, 8, 9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11
};
static const int16_t eob_group_start[12] = {0, 1, 2, 3, 5, 9, 17, 33, 65, 129, 257, 513};
static const int16_t eob_offset_bits[12]  = {0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

#undef AV1_ICDF

// ============================================================
// Inline helpers
// ============================================================
static inline int clip_max3(int v) { return v > 3 ? 3 : (v < 0 ? 0 : v); }

static inline int get_padded_idx_8x8(int idx) {
    // bwl=3 for 8x8, TX_PAD_HOR_LOG2=2, TX_PAD_HOR=4
    return idx + ((idx >> 3) << 2);
}

static inline int get_nz_mag_2d(const uint8_t* levels, int bwl) {
    int stride = (1 << bwl) + 4;  // TX_PAD_HOR=4
    int mag = clip_max3(levels[1]);
    mag += clip_max3(levels[stride]);
    mag += clip_max3(levels[stride + 1]);
    mag += clip_max3(levels[2]);
    mag += clip_max3(levels[2 * stride]);
    return mag;
}

static inline int get_nz_map_ctx(const uint8_t* levels, int coeff_idx, int bwl) {
    if (coeff_idx == 0) return 0;
    int pidx = get_padded_idx_8x8(coeff_idx);
    int stats = get_nz_mag_2d(levels + pidx, bwl);
    int ctx = std::min((stats + 1) >> 1, 4);
    return ctx + nz_map_ctx_offset_8x8[coeff_idx];
}

static inline int get_br_ctx_2d(const uint8_t* levels, int c, int bwl) {
    // qcoeff is stored in row-major order in the RTL/testbench, so use the
    // row-major transpose of libaom's context derivation here.
    int row = c >> bwl;
    int col = c - (row << bwl);
    int stride = (1 << bwl) + 4;
    int pos = row * stride + col;
    int mag = levels[pos + 1] + levels[pos + stride] + levels[pos + stride + 1];
    mag = std::min((mag + 1) >> 1, 6);
    if (c == 0) return mag;
    if (row < 2 && col < 2) return mag + 7;
    return mag + 14;
}

static inline int get_eob_pos_token(int eob, int* extra) {
    int t;
    if (eob < 33)
        t = eob_to_pos_small[eob];
    else
        t = eob_to_pos_large[std::min((eob - 1) >> 5, 16)];
    *extra = eob - eob_group_start[t];
    return t;
}

// ============================================================
// AV1 Range Coder (matches SVT-AV1 od_ec_enc)
// ============================================================
class AV1RangeCoder {
public:
    void init() {
        rng_ = 0x8000;
        low_ = 0;
        cnt_ = -9;
        buf_.clear();
        buf_.reserve(1 << 16);
    }

    void encode_bool(int val, unsigned prob_q15) {
        unsigned r = rng_;
        uint64_t l = low_;
        unsigned v = ((r >> 8) * (uint32_t)(prob_q15 >> 6) >> 1) + 4;
        if (val) l += r - v;
        r = val ? v : r - v;
        normalize(l, r);
    }

    void encode_literal(unsigned val, int bits) {
        for (int i = bits - 1; i >= 0; i--)
            encode_bool((val >> i) & 1, 16384);
    }

    void encode_bit(int val) {
        encode_bool(val, 16384);
    }

    void encode_symbol(int symbol, const uint16_t *icdf, int nsyms, bool debug = false) {
        unsigned r = rng_;
        uint64_t l = low_;
        int s = symbol;
        int N = nsyms - 1;
        unsigned fl = (s > 0) ? (unsigned)icdf[s - 1] : 32768u;
        unsigned fh = (unsigned)icdf[s];
        if (debug) fprintf(stderr, "  [RC] sym=%d nsyms=%d fl=%u fh=%u rng=%u low=%llu\n",
                          s, nsyms, fl, fh, r, (unsigned long long)l);
        if (fl < 32768u) {
            unsigned u = ((r >> 8) * (uint32_t)(fl >> 6) >> 1) + 4 * (N - (s - 1));
            unsigned v = ((r >> 8) * (uint32_t)(fh >> 6) >> 1) + 4 * (N - s);
            l += r - u;
            r = u - v;
        } else {
            r -= ((r >> 8) * (uint32_t)(fh >> 6) >> 1) + 4 * (N - s);
        }
        normalize(l, r);
        if (debug) fprintf(stderr, "  [RC] -> rng=%u low=%llu cnt=%d buf_sz=%zu\n",
                          rng_, (unsigned long long)low_, cnt_, buf_.size());
    }

    std::vector<uint8_t> finish() {
        uint64_t l = low_;
        int c = cnt_;
        int s = 10;
        uint64_t m = 0x3FFF;
        uint64_t e = ((l + m) & ~m) | (m + 1);
        s += c;
        if (s > 0) {
            uint64_t n = ((uint64_t)1 << (c + 16)) - 1;
            do {
                uint16_t val = (uint16_t)(e >> (c + 16));
                size_t pos = buf_.size();
                buf_.push_back(val & 0xFF);
                if (val & 0x100) {
                    for (int i = (int)pos - 1; i >= 0; i--) {
                        buf_[i]++;
                        if (buf_[i] != 0) break;
                    }
                }
                e &= n;
                s -= 8;
                c -= 8;
                n >>= 8;
            } while (s > 0);
        }
        return buf_;
    }

    unsigned rng_state() const { return rng_; }
    uint64_t low_state() const { return low_; }
    int cnt_state() const { return cnt_; }
    size_t buf_size() const { return buf_.size(); }

private:
    void normalize(uint64_t low, unsigned rng) {
        int d = 16 - ilog_nz(rng);
        int c = cnt_;
        int s = c + d;
        if (s >= 40) {
            int num_bytes = (s >> 3) + 1;
            c += 24 - (num_bytes << 3);
            uint64_t output = low >> c;
            low &= ((uint64_t)1 << c) - 1;
            uint64_t carry_mask = (uint64_t)1 << (num_bytes * 8);
            bool carry = (output & carry_mask) != 0;
            output &= carry_mask - 1;
            size_t start = buf_.size();
            for (int i = num_bytes - 1; i >= 0; i--)
                buf_.push_back((output >> (i * 8)) & 0xFF);
            if (carry && start > 0) {
                for (int i = (int)start - 1; i >= 0; i--) {
                    buf_[i]++;
                    if (buf_[i] != 0) break;
                }
            }
            s = c + d - 24;
        }
        low_ = low << d;
        rng_ = rng << d;
        cnt_ = s;
    }

    static int ilog_nz(unsigned x) {
        int r = 0;
        while (x > 0) { r++; x >>= 1; }
        return r;
    }

    unsigned rng_;
    uint64_t low_;
    int cnt_;
    std::vector<uint8_t> buf_;
};

// ============================================================
// AV1 Bitstream Writer
// ============================================================
class AV1BitstreamWriter {
public:
    static constexpr uint32_t kDefaultIvfTimebaseNum = 24;
    static constexpr uint32_t kDefaultIvfTimebaseDen = 1;

    struct BlockInfo {
        int16_t qcoeff[64];
        uint8_t pred_mode;
        bool    is_inter;
        int16_t mvx;
        int16_t mvy;
    };

    AV1BitstreamWriter(int width, int height, int qindex)
        : width_(width), height_(height), qindex_(qindex),
          blk_cols_(width / 8), blk_rows_(height / 8),
          mi_cols_(width / 4), mi_rows_(height / 4),
          force_skip0_(false), dc_only_mode_(false), coeff_debug_mode_(false),
          still_picture_mode_(true), include_sequence_header_(true),
          force_video_intra_only_(false), is_keyframe_(true) {}

    void set_force_skip0(bool v) { force_skip0_ = v; }
    void set_dc_only_mode(bool v) { dc_only_mode_ = v; }
    void set_coeff_debug_mode(bool v) { coeff_debug_mode_ = v; }
    void set_still_picture_mode(bool v) { still_picture_mode_ = v; }
    void set_include_sequence_header(bool v) { include_sequence_header_ = v; }
    void set_force_video_intra_only(bool v) { force_video_intra_only_ = v; }
    void set_keyframe(bool v) { is_keyframe_ = v; }

    void add_block(const BlockInfo& blk) {
        blocks_.push_back(blk);
    }

    std::vector<uint8_t> write_temporal_unit() {
        return build_frame();
    }

    std::vector<uint8_t> write_ivf_frame() {
        auto frame_data = write_temporal_unit();
        std::vector<uint8_t> ivf;
        ivf.reserve(32 + 12 + frame_data.size());
        write_bytes(ivf, "DKIF", 4);
        write_le16(ivf, 0);
        write_le16(ivf, 32);
        write_bytes(ivf, "AV01", 4);
        write_le16(ivf, width_);
        write_le16(ivf, height_);
        write_le32(ivf, kDefaultIvfTimebaseNum);
        write_le32(ivf, kDefaultIvfTimebaseDen);
        write_le32(ivf, 1);
        write_le32(ivf, 0);
        write_le32(ivf, (uint32_t)frame_data.size());
        write_le64(ivf, 0);
        ivf.insert(ivf.end(), frame_data.begin(), frame_data.end());
        return ivf;
    }

    static std::vector<uint8_t> write_ivf_sequence(
        int width,
        int height,
        const std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& frames) {
        std::vector<uint8_t> ivf;
        size_t total_size = 32;
        for (const auto& frame : frames) total_size += 12 + frame.second.size();
        ivf.reserve(total_size);
        write_bytes(ivf, "DKIF", 4);
        write_le16(ivf, 0);
        write_le16(ivf, 32);
        write_bytes(ivf, "AV01", 4);
        write_le16(ivf, static_cast<uint16_t>(width));
        write_le16(ivf, static_cast<uint16_t>(height));
        write_le32(ivf, kDefaultIvfTimebaseNum);
        write_le32(ivf, kDefaultIvfTimebaseDen);
        write_le32(ivf, static_cast<uint32_t>(frames.size()));
        write_le32(ivf, 0);
        for (const auto& frame : frames) {
            write_le32(ivf, static_cast<uint32_t>(frame.second.size()));
            write_le64(ivf, frame.first);
            ivf.insert(ivf.end(), frame.second.begin(), frame.second.end());
        }
        return ivf;
    }

private:
    enum : uint8_t {
        REF_LAST = 0,
        REF_LAST2 = 1,
        REF_LAST3 = 2,
        REF_GOLDEN = 3,
        REF_BWDREF = 4,
        REF_ALTREF2 = 5,
        REF_ALTREF = 6,
        REF_NONE = 0xFF,
    };

    enum : uint8_t {
        REDUCED_INTER_NONE = 0,
        REDUCED_INTER_GLOBALMV = 1,
        REDUCED_INTER_NEARESTMV = 2,
        REDUCED_INTER_NEWMV = 3,
    };

    class BitWriter {
    public:
        void write_bit(int b) {
            buf_ = (buf_ << 1) | (b & 1);
            count_++;
            if (count_ == 8) flush_byte();
        }
        void write_bits(unsigned val, int n) {
            for (int i = n - 1; i >= 0; i--)
                write_bit((val >> i) & 1);
        }
        void write_trailing_bits() {
            write_bit(1);
            while (count_ % 8 != 0) write_bit(0);
        }
        std::vector<uint8_t> get_bytes() {
            if (count_ > 0) {
                buf_ <<= (8 - count_);
                bytes_.push_back(buf_ & 0xFF);
                buf_ = 0; count_ = 0;
            }
            return bytes_;
        }
    private:
        void flush_byte() { bytes_.push_back(buf_ & 0xFF); buf_ = 0; count_ = 0; }
        unsigned buf_ = 0;
        int count_ = 0;
        std::vector<uint8_t> bytes_;
    };

    int width_, height_, qindex_;
    int blk_cols_, blk_rows_;
    int mi_cols_, mi_rows_;
    bool force_skip0_;
    bool dc_only_mode_;
    bool coeff_debug_mode_;
    bool still_picture_mode_;
    bool include_sequence_header_;
    bool force_video_intra_only_;
    bool is_keyframe_;
    std::vector<BlockInfo> blocks_;

    // Context arrays
    std::vector<uint8_t> part_ctx_above_, part_ctx_left_;
    std::vector<uint8_t> skip_above_, skip_left_;
    std::vector<uint8_t> mode_above_, mode_left_;
    std::vector<uint8_t> inter_above_, inter_left_;
    std::vector<uint8_t> ref_above_, ref_left_;
    std::vector<uint8_t> dc_sign_above_, dc_sign_left_;
    std::vector<uint8_t> blk_inter_coded_;
    std::vector<uint8_t> blk_ref0_;
    std::vector<uint8_t> blk_inter_mode_;
    std::vector<int16_t> blk_mv_x_;
    std::vector<int16_t> blk_mv_y_;

    struct ReducedMvCandidate {
        int16_t row;
        int16_t col;
        uint16_t weight;
    };

    struct ReducedMvState {
        std::vector<ReducedMvCandidate> stack;
    };

    // ============================================================
    // Context helpers
    // ============================================================
    void init_contexts() {
        part_ctx_above_.assign(mi_cols_, 0);
        part_ctx_left_.assign(mi_rows_, 0);
        skip_above_.assign(mi_cols_, 0);
        skip_left_.assign(mi_rows_, 0);
        mode_above_.assign(mi_cols_, 0);
        mode_left_.assign(mi_rows_, 0);
        inter_above_.assign(mi_cols_, 0);
        inter_left_.assign(mi_rows_, 0);
        ref_above_.assign(mi_cols_, REF_NONE);
        ref_left_.assign(mi_rows_, REF_NONE);
        dc_sign_above_.assign(mi_cols_, 0);
        dc_sign_left_.assign(mi_rows_, 0);
        blk_inter_coded_.assign(blk_cols_ * blk_rows_, 0);
        blk_ref0_.assign(blk_cols_ * blk_rows_, REF_NONE);
        blk_inter_mode_.assign(blk_cols_ * blk_rows_, REDUCED_INTER_NONE);
        blk_mv_x_.assign(blk_cols_ * blk_rows_, 0);
        blk_mv_y_.assign(blk_cols_ * blk_rows_, 0);
    }

    int get_partition_ctx(int org_x, int org_y, int bsl) {
        int mi_col = org_x >> 2, mi_row = org_y >> 2;
        int above = 0, left = 0;
        if (mi_row > 0 && mi_col < mi_cols_) above = (part_ctx_above_[mi_col] >> bsl) & 1;
        if (mi_col > 0 && mi_row < mi_rows_) left = (part_ctx_left_[mi_row] >> bsl) & 1;
        return bsl * 4 + left * 2 + above;
    }

    int get_skip_ctx(int mi_row, int mi_col) {
        int ctx = 0;
        if (mi_row > 0 && mi_col < mi_cols_) ctx += skip_above_[mi_col];
        if (mi_col > 0 && mi_row < mi_rows_) ctx += skip_left_[mi_row];
        return ctx;
    }

    void get_kf_y_mode_ctx(int mi_row, int mi_col, int &above_ctx, int &left_ctx) {
        int above_mode = 0, left_mode = 0;
        if (mi_row > 0 && mi_col < mi_cols_) above_mode = mode_above_[mi_col];
        if (mi_col > 0 && mi_row < mi_rows_) left_mode = mode_left_[mi_row];
        above_ctx = av1_intra_mode_context[above_mode];
        left_ctx = av1_intra_mode_context[left_mode];
    }

    void update_partition_ctx(int org_x, int org_y, int bsize_log2) {
        int mi_col = org_x >> 2, mi_row = org_y >> 2;
        int mi_size = 1 << (bsize_log2 - 2);
        int idx = bsize_log2 - 3;
        uint8_t a_val = av1_part_ctx_above[idx], l_val = av1_part_ctx_left[idx];
        for (int i = 0; i < mi_size && (mi_col + i) < mi_cols_; i++) part_ctx_above_[mi_col + i] = a_val;
        for (int i = 0; i < mi_size && (mi_row + i) < mi_rows_; i++) part_ctx_left_[mi_row + i] = l_val;
    }

    int get_dc_sign_ctx(int mi_row, int mi_col, int mi_size) {
        static const int8_t signs[3] = {0, -1, 1};
        int dc_sign = 0;
        for (int i = 0; i < mi_size; i++) {
            if (mi_row > 0 && (mi_col + i) < mi_cols_) dc_sign += signs[dc_sign_above_[mi_col + i]];
            if (mi_col > 0 && (mi_row + i) < mi_rows_) dc_sign += signs[dc_sign_left_[mi_row + i]];
        }
        if (dc_sign > 0) return 2;
        if (dc_sign < 0) return 1;
        return 0;
    }

    int get_intra_inter_ctx(int mi_row, int mi_col) const {
        const bool has_above = mi_row > 0 && mi_col < mi_cols_;
        const bool has_left = mi_col > 0 && mi_row < mi_rows_;
        const int above_intra = has_above ? !inter_above_[mi_col] : 0;
        const int left_intra = has_left ? !inter_left_[mi_row] : 0;

        if (has_above && has_left) {
            return (left_intra && above_intra) ? 3 : (left_intra || above_intra);
        }
        if (has_above || has_left) {
            return 2 * (has_above ? above_intra : left_intra);
        }
        return 0;
    }

    void collect_neighbor_ref_counts(int mi_row, int mi_col, int counts[7]) const {
        std::fill(counts, counts + 7, 0);
        if (mi_row > 0 && mi_col < mi_cols_ && inter_above_[mi_col] && ref_above_[mi_col] != REF_NONE)
            counts[ref_above_[mi_col]]++;
        if (mi_col > 0 && mi_row < mi_rows_ && inter_left_[mi_row] && ref_left_[mi_row] != REF_NONE)
            counts[ref_left_[mi_row]]++;
    }

    static int compare_ref_counts(int a, int b) {
        if (a == b) return 1;
        return (a < b) ? 0 : 2;
    }

    void encode_last_frame_ref(AV1RangeCoder& rc, int mi_row, int mi_col) {
        int counts[7];
        collect_neighbor_ref_counts(mi_row, mi_col, counts);

        const int fwd_count = counts[REF_LAST] + counts[REF_LAST2] + counts[REF_LAST3] + counts[REF_GOLDEN];
        const int bwd_count = counts[REF_BWDREF] + counts[REF_ALTREF2] + counts[REF_ALTREF];
        if (coeff_debug_mode_) {
            fprintf(stderr,
                    "[REF] mi=(%d,%d) counts L=%d L2=%d L3=%d G=%d B=%d A2=%d A=%d p1ctx=%d\n",
                    mi_row, mi_col, counts[REF_LAST], counts[REF_LAST2], counts[REF_LAST3],
                    counts[REF_GOLDEN], counts[REF_BWDREF], counts[REF_ALTREF2], counts[REF_ALTREF],
                    compare_ref_counts(fwd_count, bwd_count));
        }
        rc.encode_symbol(0, av1_single_ref_cdf[compare_ref_counts(fwd_count, bwd_count)][0], 2);

        const int ll2_count = counts[REF_LAST] + counts[REF_LAST2];
        const int l3g_count = counts[REF_LAST3] + counts[REF_GOLDEN];
        if (coeff_debug_mode_) {
            fprintf(stderr, "[REF] p3ctx=%d p4ctx=%d\n",
                    compare_ref_counts(ll2_count, l3g_count),
                    compare_ref_counts(counts[REF_LAST], counts[REF_LAST2]));
        }
        rc.encode_symbol(0, av1_single_ref_cdf[compare_ref_counts(ll2_count, l3g_count)][2], 2);

        rc.encode_symbol(0, av1_single_ref_cdf[compare_ref_counts(counts[REF_LAST], counts[REF_LAST2])][3], 2);
    }

    bool block_has_matching_ref(int blk_x, int blk_y, uint8_t ref_frame) const {
        if (blk_x < 0 || blk_y < 0 || blk_x >= blk_cols_ || blk_y >= blk_rows_) return false;
        const int idx = blk_y * blk_cols_ + blk_x;
        return blk_inter_coded_[idx] && blk_ref0_[idx] == ref_frame;
    }

    bool block_uses_newmv(int blk_x, int blk_y, uint8_t ref_frame) const {
        if (!block_has_matching_ref(blk_x, blk_y, ref_frame)) return false;
        const int idx = blk_y * blk_cols_ + blk_x;
        return blk_inter_mode_[idx] == REDUCED_INTER_NEWMV;
    }

    bool block_has_top_right(int blk_x, int blk_y) const {
        if (blk_x < 0 || blk_y < 0 || blk_x >= blk_cols_ || blk_y >= blk_rows_) return false;

        // Match the square-block `has_top_right()` behavior for our fixed
        // 8x8-only partition tree inside a 64x64 superblock. Using top-right
        // neighbors unconditionally makes the reduced single-ref contexts drift
        // from the decoder once we get deeper into the recursive split order.
        constexpr int sb_blk_size = 8;   // 64x64 SB / 8x8 blocks
        int bs = 1;                      // 8x8 block in block-grid units
        const int mask_row = blk_y & (sb_blk_size - 1);
        const int mask_col = blk_x & (sb_blk_size - 1);

        bool has_tr = !((mask_row & bs) && (mask_col & bs));
        while (bs < sb_blk_size) {
            if (mask_col & bs) {
                if ((mask_col & (2 * bs)) && (mask_row & (2 * bs))) {
                    has_tr = false;
                    break;
                }
            } else {
                break;
            }
            bs <<= 1;
        }
        return has_tr && (blk_y > 0) && (blk_x + 1 < blk_cols_);
    }

    bool block_has_row_match(int blk_x, int blk_y, uint8_t ref_frame, bool include_top_right) const {
        if (block_has_matching_ref(blk_x, blk_y - 1, ref_frame)) return true;
        if (include_top_right && block_has_matching_ref(blk_x + 1, blk_y - 1, ref_frame)) return true;
        if (block_has_matching_ref(blk_x - 1, blk_y - 1, ref_frame)) return true;
        for (int dy = 2; dy <= 4; ++dy) {
            if (block_has_matching_ref(blk_x, blk_y - dy, ref_frame)) return true;
        }
        return false;
    }

    bool block_has_col_match(int blk_x, int blk_y, uint8_t ref_frame) const {
        if (block_has_matching_ref(blk_x - 1, blk_y, ref_frame)) return true;
        if (block_has_matching_ref(blk_x - 1, blk_y - 1, ref_frame)) return true;
        for (int dx = 2; dx <= 4; ++dx) {
            if (block_has_matching_ref(blk_x - dx, blk_y, ref_frame)) return true;
        }
        return false;
    }

    uint8_t get_reduced_single_ref_mode_ctx(int blk_x, int blk_y, uint8_t ref_frame) const {
        const bool has_tr = block_has_top_right(blk_x, blk_y);
        const bool row_match = block_has_matching_ref(blk_x, blk_y - 1, ref_frame) ||
                               (has_tr && block_has_matching_ref(blk_x + 1, blk_y - 1, ref_frame));
        const bool col_match = block_has_matching_ref(blk_x - 1, blk_y, ref_frame);
        int newmv_count = 0;

        if (block_uses_newmv(blk_x, blk_y - 1, ref_frame)) ++newmv_count;
        if (has_tr && block_uses_newmv(blk_x + 1, blk_y - 1, ref_frame)) ++newmv_count;
        if (block_uses_newmv(blk_x - 1, blk_y, ref_frame)) ++newmv_count;

        const bool row_ref_match = block_has_row_match(blk_x, blk_y, ref_frame, has_tr);
        const bool col_ref_match = block_has_col_match(blk_x, blk_y, ref_frame);

        const int nearest_match = static_cast<int>(row_match) + static_cast<int>(col_match);
        const int ref_match = static_cast<int>(row_ref_match) + static_cast<int>(col_ref_match);
        uint8_t mode_ctx = 0;

        switch (nearest_match) {
        case 0:
            mode_ctx |= ref_match >= 1 ? 1 : 0;
            if (ref_match == 1)
                mode_ctx |= (1 << AV1_REFMV_OFFSET);
            else if (ref_match >= 2)
                mode_ctx |= (2 << AV1_REFMV_OFFSET);
            break;
        case 1:
            mode_ctx |= newmv_count > 0 ? 2 : 3;
            if (ref_match == 1)
                mode_ctx |= (3 << AV1_REFMV_OFFSET);
            else if (ref_match >= 2)
                mode_ctx |= (4 << AV1_REFMV_OFFSET);
            break;
        default:
            mode_ctx |= newmv_count >= 1 ? 4 : 5;
            mode_ctx |= (5 << AV1_REFMV_OFFSET);
            break;
        }

        return mode_ctx;
    }

    void add_reduced_single_ref_mv_candidate(std::vector<ReducedMvCandidate>& stack, int blk_x, int blk_y,
                                             uint8_t ref_frame, uint16_t weight) const {
        if (!block_has_matching_ref(blk_x, blk_y, ref_frame)) return;

        const int blk_idx = blk_y * blk_cols_ + blk_x;
        const int16_t row = static_cast<int16_t>(blk_mv_y_[blk_idx] * 8);
        const int16_t col = static_cast<int16_t>(blk_mv_x_[blk_idx] * 8);

        for (auto& cand : stack) {
            if (cand.row == row && cand.col == col) {
                cand.weight = static_cast<uint16_t>(cand.weight + weight);
                return;
            }
        }
        stack.push_back({row, col, weight});
    }

    ReducedMvState collect_reduced_single_ref_mv_state(int blk_x, int blk_y, uint8_t ref_frame) const {
        ReducedMvState state;
        const bool has_tr = block_has_top_right(blk_x, blk_y);
        add_reduced_single_ref_mv_candidate(state.stack, blk_x,     blk_y - 1, ref_frame, 4);
        if (has_tr)
            add_reduced_single_ref_mv_candidate(state.stack, blk_x + 1, blk_y - 1, ref_frame, 4);
        add_reduced_single_ref_mv_candidate(state.stack, blk_x - 1, blk_y,     ref_frame, 4);

        const size_t nearest_refmv_count = state.stack.size();
        for (size_t i = 0; i < nearest_refmv_count; ++i)
            state.stack[i].weight = static_cast<uint16_t>(state.stack[i].weight + AV1_REF_CAT_LEVEL);

        add_reduced_single_ref_mv_candidate(state.stack, blk_x - 1, blk_y - 1, ref_frame, 4);
        add_reduced_single_ref_mv_candidate(state.stack, blk_x,     blk_y - 2, ref_frame, 4);
        add_reduced_single_ref_mv_candidate(state.stack, blk_x - 2, blk_y,     ref_frame, 4);
        for (int dy = 3; dy <= 4; ++dy) {
            add_reduced_single_ref_mv_candidate(state.stack, blk_x, blk_y - dy, ref_frame, 4);
        }
        for (int dx = 3; dx <= 4; ++dx) {
            add_reduced_single_ref_mv_candidate(state.stack, blk_x - dx, blk_y, ref_frame, 4);
        }

        std::stable_sort(state.stack.begin(), state.stack.end(),
                         [](const ReducedMvCandidate& a, const ReducedMvCandidate& b) {
                             return a.weight > b.weight;
                         });
        return state;
    }

    static ReducedMvCandidate get_reduced_newmv_ref_mv(const ReducedMvState& state) {
        if (!state.stack.empty()) return state.stack[0];
        return {0, 0, 0};
    }

    static int get_drl_ctx_from_weights(const ReducedMvState& state, size_t ref_idx) {
        if (ref_idx + 1 >= state.stack.size()) return 0;
        const uint16_t w0 = state.stack[ref_idx].weight;
        const uint16_t w1 = state.stack[ref_idx + 1].weight;
        if (w0 >= AV1_REF_CAT_LEVEL && w1 >= AV1_REF_CAT_LEVEL) return 0;
        if (w0 >= AV1_REF_CAT_LEVEL && w1 < AV1_REF_CAT_LEVEL) return 1;
        return 2;
    }

    static int get_mv_joint_type(int row, int col) {
        return (col != 0 ? 1 : 0) | (row != 0 ? 2 : 0);
    }

    static int mv_class_base(int mv_class) {
        return mv_class ? (2 << (mv_class + 2)) : 0;
    }

    static int log_in_base_2(unsigned int n) {
        int log = 0;
        while (n > 1) {
            n >>= 1;
            ++log;
        }
        return log;
    }

    static int get_mv_class(int z, int* offset) {
        const int mv_class = (z >= 2 * 4096) ? 10 : log_in_base_2(static_cast<unsigned>(z) >> 3);
        if (offset) *offset = z - mv_class_base(mv_class);
        return mv_class;
    }

    static void encode_mv_component(AV1RangeCoder& rc, int comp, bool use_subpel) {
        const int sign = comp < 0;
        const int mag = sign ? -comp : comp;
        int offset = 0;
        const int mv_class = get_mv_class(mag - 1, &offset);
        const int d = offset >> 3;
        const int fr = (offset >> 1) & 3;

        rc.encode_symbol(sign, av1_mv_sign_cdf, 2);
        rc.encode_symbol(mv_class, av1_mv_class_cdf, 11);
        if (mv_class == 0) {
            rc.encode_symbol(d, av1_mv_class0_cdf, 2);
        } else {
            const int n = mv_class;
            for (int i = 0; i < n; ++i)
                rc.encode_symbol((d >> i) & 1, av1_mv_bits_cdf[i], 2);
        }
        if (use_subpel)
            rc.encode_symbol(fr, mv_class == 0 ? av1_mv_class0_fp_cdf[d] : av1_mv_fp_cdf, 4);
    }

    static void encode_mv(AV1RangeCoder& rc, int mv_row, int mv_col, int ref_row, int ref_col,
                          bool force_integer_mv) {
        const int diff_row = mv_row - ref_row;
        const int diff_col = mv_col - ref_col;
        const int joint = get_mv_joint_type(diff_row, diff_col);
        assert(joint != 0);
        rc.encode_symbol(joint, av1_mv_joint_cdf, 4);
        if (joint == 2 || joint == 3) encode_mv_component(rc, diff_row, !force_integer_mv);
        if (joint == 1 || joint == 3) encode_mv_component(rc, diff_col, !force_integer_mv);
    }

    void update_block_ctx(int mi_row, int mi_col, int mi_size, int skip, int mode, int dc_sign_code, bool is_inter,
                          uint8_t ref_frame, uint8_t inter_mode, int16_t mvx, int16_t mvy) {
        for (int i = 0; i < mi_size && (mi_col + i) < mi_cols_; i++) {
            skip_above_[mi_col + i] = skip;
            mode_above_[mi_col + i] = mode;
            inter_above_[mi_col + i] = is_inter ? 1 : 0;
            ref_above_[mi_col + i] = is_inter ? ref_frame : REF_NONE;
            dc_sign_above_[mi_col + i] = (uint8_t)dc_sign_code;
        }
        for (int i = 0; i < mi_size && (mi_row + i) < mi_rows_; i++) {
            skip_left_[mi_row + i] = skip;
            mode_left_[mi_row + i] = mode;
            inter_left_[mi_row + i] = is_inter ? 1 : 0;
            ref_left_[mi_row + i] = is_inter ? ref_frame : REF_NONE;
            dc_sign_left_[mi_row + i] = (uint8_t)dc_sign_code;
        }

        const int blk_x = mi_col >> 1;
        const int blk_y = mi_row >> 1;
        if (blk_x >= 0 && blk_x < blk_cols_ && blk_y >= 0 && blk_y < blk_rows_) {
            const int blk_idx = blk_y * blk_cols_ + blk_x;
            blk_inter_coded_[blk_idx] = is_inter ? 1 : 0;
            blk_ref0_[blk_idx] = is_inter ? ref_frame : REF_NONE;
            blk_inter_mode_[blk_idx] = is_inter ? inter_mode : REDUCED_INTER_NONE;
            blk_mv_x_[blk_idx] = is_inter ? mvx : 0;
            blk_mv_y_[blk_idx] = is_inter ? mvy : 0;
        }
    }

    // ---- Partition gather (edge SBs) ----
    static int icdf_prob(const uint16_t* icdf, int s) {
        return (int)((s > 0) ? icdf[s-1] : 32768u) - (int)icdf[s];
    }
    static void partition_gather_vert_alike(uint16_t out[3], const uint16_t* in, int nsyms) {
        int p = 32768;
        if (nsyms >= 4) { p -= icdf_prob(in, 2); p -= icdf_prob(in, 3); }
        if (nsyms >= 10) { p -= icdf_prob(in, 4); p -= icdf_prob(in, 6); p -= icdf_prob(in, 7); p -= icdf_prob(in, 9); }
        out[0] = (uint16_t)(32768 - p); out[1] = 0; out[2] = 0;
    }
    static void partition_gather_horz_alike(uint16_t out[3], const uint16_t* in, int nsyms) {
        int p = 32768;
        if (nsyms >= 4) { p -= icdf_prob(in, 1); p -= icdf_prob(in, 3); }
        if (nsyms >= 10) { p -= icdf_prob(in, 4); p -= icdf_prob(in, 5); p -= icdf_prob(in, 6); p -= icdf_prob(in, 8); }
        out[0] = (uint16_t)(32768 - p); out[1] = 0; out[2] = 0;
    }

    // ============================================================
    // Coefficient encoding for one TX block
    // ============================================================
    void init_levels_buf(const int16_t* qcoeff, int w, int h, uint8_t* levels) {
        int stride = w + 4;
        // Clear top padding (2 rows)
        memset(levels - 2 * stride, 0, 2 * stride);
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++)
                levels[i * stride + j] = (uint8_t)std::min(abs((int)qcoeff[i * w + j]), 127);
            for (int j = 0; j < 4; j++)
                levels[i * stride + w + j] = 0;
        }
        // Clear bottom padding
        memset(levels + h * stride, 0, (4 * stride) + 16);
    }

    int compute_eob(const int16_t* qcoeff) {
        int eob = 0;
        for (int c = 0; c < 64; c++) {
            int pos = default_scan_8x8[c];
            if (qcoeff[pos] != 0) eob = c + 1;
        }
        return eob;
    }

    void encode_coeffs_txb(AV1RangeCoder& rc, const int16_t* qcoeff, int plane, int dc_sign_ctx = 0,
                           bool debug = false, bool inter_block = false, int intra_mode = 0) {
        int eob = compute_eob(qcoeff);

        // txb_skip: 0 = has coefficients, 1 = all zero
        // Use context 0 for simplicity (first block default)
        if (debug) fprintf(stderr, "[COEFF] plane=%d eob=%d txb_skip=%d\n", plane, eob, eob==0?1:0);
        rc.encode_symbol(eob == 0 ? 1 : 0, av1_txb_skip_cdf[0], 2, debug);

        if (eob == 0) return;

        // TX type (luma only, qindex > 0)
        if (plane == 0) {
            if (inter_block) {
                // For 8x8 inter blocks with reduced_tx_set=0, libaom uses
                // EXT_TX_SET_ALL16 / eset=1 and DCT_DCT maps to symbol 7.
                if (debug) fprintf(stderr, "[COEFF] inter tx_type symbol=7 nsyms=16\n");
                rc.encode_symbol(7, av1_inter_tx_type_cdf_8x8_all16, 16, debug);
            } else {
                const int intra_tx_mode = (intra_mode >= 0 && intra_mode < 13) ? intra_mode : 0;
                // DCT_DCT maps to symbol 1 in EXT_TX_SET_DTT4_IDTX_1DDCT.
                if (debug) fprintf(stderr, "[COEFF] intra tx_type symbol=1 nsyms=7\n");
                rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8[intra_tx_mode], 7, debug);
            }
        }

        // EOB encoding
        int eob_extra;
        int eob_pt = get_eob_pos_token(eob, &eob_extra);
        if (debug) fprintf(stderr, "[COEFF] eob_pt=%d symbol=%d eob_extra=%d nsyms=7\n", eob_pt, eob_pt-1, eob_extra);
        // TX_8X8 → eob_multi_size=2 → eob_flag_cdf64 (7 symbols)
        rc.encode_symbol(eob_pt - 1, av1_eob_multi64_cdf[plane], 7, debug);

        int eob_ob = eob_offset_bits[eob_pt];
        if (eob_ob > 0) {
            int eob_ctx = eob_pt - 3;
            int eob_shift = eob_ob - 1;
            int bit = (eob_extra >> eob_shift) & 1;
            rc.encode_symbol(bit, av1_eob_extra_cdf[plane][eob_ctx], 2);
            for (int i = 1; i < eob_ob; i++) {
                eob_shift = eob_ob - 1 - i;
                bit = (eob_extra >> eob_shift) & 1;
                rc.encode_bit(bit);
            }
        }

        // Init levels buffer for context computation
        // Buffer: (8+4) stride × (8+2+4) height = 12 × 14
        uint8_t levels_buf[12 * 14 + 16];
        memset(levels_buf, 0, sizeof(levels_buf));
        uint8_t* levels = levels_buf + 2 * 12;  // skip 2 rows of padding
        init_levels_buf(qcoeff, 8, 8, levels);

        // Compute nz_map contexts for all scan positions
        int8_t coeff_contexts[64];
        for (int c = 0; c < eob; c++) {
            int pos = default_scan_8x8[c];
            if (c == eob - 1) {
                // EOB position: special context based on scan position
                if (c == 0) coeff_contexts[pos] = 0;
                else if (c <= (8 * 8) / 8) coeff_contexts[pos] = 1;
                else if (c <= (8 * 8) / 4) coeff_contexts[pos] = 2;
                else coeff_contexts[pos] = 3;
            } else {
                coeff_contexts[pos] = (int8_t)get_nz_map_ctx(levels, pos, 3);
            }
        }

        // Encode coefficient base levels (reverse scan order)
        for (int c = eob - 1; c >= 0; --c) {
            int pos = default_scan_8x8[c];
            int level = abs((int)qcoeff[pos]);
            int coeff_ctx = coeff_contexts[pos];

            if (c == eob - 1) {
                // EOB coefficient: 3 symbols (level-1: 0,1,2)
                int sym = std::min(level, 3) - 1;
                int ctx = coeff_ctx < 4 ? coeff_ctx : 3;
                if (debug) fprintf(stderr, "[COEFF] base_eob c=%d pos=%d level=%d sym=%d ctx=%d\n", c, pos, level, sym, ctx);
                rc.encode_symbol(sym, av1_coeff_base_eob_cdf[ctx], 3, debug);
            } else {
                // Non-EOB: 4 symbols (level: 0,1,2,3)
                int sym = std::min(level, 3);
                int ctx = coeff_ctx < 42 ? coeff_ctx : 41;
                if (debug) fprintf(stderr, "[COEFF] base c=%d pos=%d level=%d sym=%d ctx=%d\n", c, pos, level, sym, ctx);
                rc.encode_symbol(sym, av1_coeff_base_cdf[ctx], 4, debug);
            }

            // Bypass range (level > 2)
            if (level > 2) {
                int base_range = level - 1 - 2;
                int br_ctx = get_br_ctx_2d(levels, pos, 3);
                if (debug) fprintf(stderr, "[COEFF] br level=%d base_range=%d br_ctx=%d\n", level, base_range, br_ctx);
                for (int idx = 0; idx < 12; idx += 3) {
                    int k = std::min(base_range - idx, 3);
                    if (debug) fprintf(stderr, "[COEFF] br_sym=%d idx=%d ctx=%d\n", k, idx, br_ctx < 21 ? br_ctx : 20);
                    rc.encode_symbol(k, av1_coeff_br_cdf[br_ctx < 21 ? br_ctx : 20], 4, debug);
                    if (k < 3) break;
                }
            }
        }

        // Encode signs (forward scan order)
        for (int c = 0; c < eob; c++) {
            int pos = default_scan_8x8[c];
            int v = (int)qcoeff[pos];
            int level = abs(v);
            if (level) {
                int sign = (v < 0) ? 1 : 0;
                if (c == 0) {
                    if (debug) fprintf(stderr, "[COEFF] dc_sign=%d plane=%d ctx=%d\n", sign, plane, dc_sign_ctx);
                    rc.encode_symbol(sign, av1_dc_sign_cdf[plane][dc_sign_ctx], 2, debug);
                } else {
                    if (debug) fprintf(stderr, "[COEFF] ac_sign=%d c=%d\n", sign, c);
                    if (debug) fprintf(stderr, "  [RC] bit=%d prob_q15=16384 rng=%u low=%llu\n",
                                       sign, rc.rng_state(), (unsigned long long)rc.low_state());
                    rc.encode_bit(sign);
                    if (debug) fprintf(stderr, "  [RC] -> rng=%u low=%llu cnt=%d buf_sz=%zu\n",
                                       rc.rng_state(), (unsigned long long)rc.low_state(),
                                       rc.cnt_state(), rc.buf_size());
                }
                // Golomb for very large values (level > 14)
                if (level > 14) {
                    int remainder = level - 14 - 1;
                    int x = remainder + 1;
                    int length = 0;
                    int tmp = x;
                    while (tmp > 0) { length++; tmp >>= 1; }
                    for (int i = 0; i < length - 1; i++)
                        rc.encode_bit(0);
                    for (int i = length - 1; i >= 0; i--)
                        rc.encode_bit((x >> i) & 1);
                }
            }
        }
    }

    // ============================================================
    // OBU / Frame structure
    // ============================================================
    std::vector<uint8_t> build_frame() {
        std::vector<uint8_t> out;
        write_obu_header(out, 2, 0);
        if (include_sequence_header_) {
            auto seq_data = build_sequence_header();
            write_obu_header(out, 1, seq_data.size());
            out.insert(out.end(), seq_data.begin(), seq_data.end());
        }
        auto frame_obu = build_frame_obu();
        write_obu_header(out, 6, frame_obu.size());
        out.insert(out.end(), frame_obu.begin(), frame_obu.end());
        return out;
    }

    void write_obu_header(std::vector<uint8_t>& out, int type, size_t size) {
        out.push_back(((type & 0xF) << 3) | 0x02);
        write_leb128(out, size);
    }

    void write_leb128(std::vector<uint8_t>& out, size_t val) {
        do {
            uint8_t byte = val & 0x7F;
            val >>= 7;
            if (val > 0) byte |= 0x80;
            out.push_back(byte);
        } while (val > 0);
    }

    std::vector<uint8_t> build_sequence_header() {
        BitWriter bw;
        bw.write_bits(0, 3);
        if (still_picture_mode_) {
            bw.write_bit(1);
            bw.write_bit(1);
            bw.write_bits(4, 5);
        } else {
            bw.write_bit(0);      // still_picture = 0
            bw.write_bit(0);      // reduced_still_picture_header = 0
            bw.write_bit(0);      // timing_info_present_flag = 0
            bw.write_bit(0);      // initial_display_delay_present_flag = 0
            bw.write_bits(0, 5);  // operating_points_cnt_minus_1 = 0
            bw.write_bits(0, 12); // operating_point_idc = 0
            bw.write_bits(4, 5);  // seq_level_idx[0]
        }
        int w_bits = bits_needed(width_), h_bits = bits_needed(height_);
        bw.write_bits(w_bits - 1, 4);
        bw.write_bits(h_bits - 1, 4);
        bw.write_bits(width_ - 1, w_bits);
        bw.write_bits(height_ - 1, h_bits);
        if (!still_picture_mode_) {
            bw.write_bit(0); // frame_id_numbers_present_flag = 0
        }
        bw.write_bit(0); // use_128x128_superblock = 0
        bw.write_bit(0); // enable_filter_intra = 0
        bw.write_bit(0); // enable_intra_edge_filter = 0
        if (!still_picture_mode_) {
            bw.write_bit(0); // enable_interintra_compound = 0
            bw.write_bit(0); // enable_masked_compound = 0
            bw.write_bit(0); // enable_warped_motion = 0
            bw.write_bit(0); // enable_dual_filter = 0
            bw.write_bit(0); // enable_order_hint = 0
            // Keep video frames on explicit integer motion vectors. This
            // matches the current RTL ME/prediction subset.
            bw.write_bit(1); // seq_choose_screen_content_tools = 1
            bw.write_bit(1); // seq_choose_integer_mv = 1
        }
        bw.write_bit(0); // enable_superres = 0
        bw.write_bit(0); // enable_cdef = 0
        bw.write_bit(0); // enable_restoration = 0
        write_color_config(bw);
        bw.write_bit(0); // film_grain_params_present = 0
        bw.write_trailing_bits();
        return bw.get_bytes();
    }

    void write_color_config(BitWriter& bw) {
        bw.write_bit(0); bw.write_bit(0); bw.write_bit(0);
        bw.write_bit(0); bw.write_bits(0, 2); bw.write_bit(0);
    }

    std::vector<uint8_t> build_frame_obu() {
        if (!still_picture_mode_ && force_video_intra_only_) {
            return build_frame_obu_video_intra_only();
        }
        if (!still_picture_mode_ && !is_keyframe_) {
            return frame_has_inter_blocks() ? build_frame_obu_video_inter()
                                            : build_frame_obu_video_intra_only();
        }
        return still_picture_mode_ ? build_frame_obu_still_picture() : build_frame_obu_video_keyframe();
    }

    std::vector<uint8_t> build_frame_obu_still_picture() {
        BitWriter hdr_bw;
        hdr_bw.write_bit(1);  // disable_cdf_update = 1
        hdr_bw.write_bit(0);  // allow_screen_content_tools
        hdr_bw.write_bit(0);  // render_and_frame_size_different
        write_tile_info(hdr_bw);
        write_quantization_params(hdr_bw);
        hdr_bw.write_bit(0);  // segmentation_enabled
        hdr_bw.write_bit(0);  // delta_q_present
        write_loop_filter_params(hdr_bw);
        hdr_bw.write_bit(0);  // tx_mode_select = 0
        hdr_bw.write_bit(0);  // reduced_tx_set = 0
        auto hdr_bytes = hdr_bw.get_bytes();
        auto tile_data = build_tile_data();
        std::vector<uint8_t> frame_obu;
        frame_obu.insert(frame_obu.end(), hdr_bytes.begin(), hdr_bytes.end());
        frame_obu.insert(frame_obu.end(), tile_data.begin(), tile_data.end());
        return frame_obu;
    }

    std::vector<uint8_t> build_frame_obu_video_keyframe() {
        BitWriter hdr_bw;
        hdr_bw.write_bit(0);      // show_existing_frame = 0
        hdr_bw.write_bits(0, 2);  // frame_type = KEY_FRAME
        hdr_bw.write_bit(1);      // show_frame = 1
        hdr_bw.write_bit(1);      // disable_cdf_update = 1
        hdr_bw.write_bit(0);      // allow_screen_content_tools = 0
        hdr_bw.write_bit(0);      // frame_size_override_flag = 0
        hdr_bw.write_bit(0);      // render_and_frame_size_different = 0
        write_tile_info(hdr_bw);
        write_quantization_params(hdr_bw);
        hdr_bw.write_bit(0);      // segmentation_enabled = 0
        hdr_bw.write_bit(0);      // delta_q_present = 0
        write_loop_filter_params(hdr_bw);
        hdr_bw.write_bit(0);      // tx_mode_select = 0
        hdr_bw.write_bit(0);      // reduced_tx_set = 0
        auto hdr_bytes = hdr_bw.get_bytes();
        auto tile_data = build_tile_data();
        std::vector<uint8_t> frame_obu;
        frame_obu.insert(frame_obu.end(), hdr_bytes.begin(), hdr_bytes.end());
        frame_obu.insert(frame_obu.end(), tile_data.begin(), tile_data.end());
        return frame_obu;
    }

    std::vector<uint8_t> build_frame_obu_video_intra_only() {
        BitWriter hdr_bw;
        const uint8_t refresh_mask = 0x01;
        hdr_bw.write_bit(0);      // show_existing_frame = 0
        hdr_bw.write_bits(2, 2);  // frame_type = INTRA_ONLY_FRAME
        hdr_bw.write_bit(1);      // show_frame = 1
        hdr_bw.write_bit(0);      // error_resilient_mode = 0
        hdr_bw.write_bit(1);      // disable_cdf_update = 1
        hdr_bw.write_bit(0);      // allow_screen_content_tools = 0
        hdr_bw.write_bit(0);      // frame_size_override_flag = 0
        // Keep the mixed-sequence bootstrap conformant by refreshing only the
        // LAST slot. The inter-frame header maps all single-ref indices back
        // to this slot until a fuller reference manager exists.
        hdr_bw.write_bits(refresh_mask, 8);
        hdr_bw.write_bit(0);      // render_and_frame_size_different = 0
        write_tile_info(hdr_bw);
        write_quantization_params(hdr_bw);
        hdr_bw.write_bit(0);      // segmentation_enabled = 0
        hdr_bw.write_bit(0);      // delta_q_present = 0
        write_loop_filter_params(hdr_bw);
        hdr_bw.write_bit(0);      // tx_mode_select = 0
        hdr_bw.write_bit(0);      // reduced_tx_set = 0
        auto hdr_bytes = hdr_bw.get_bytes();
        auto tile_data = build_tile_data();
        std::vector<uint8_t> frame_obu;
        frame_obu.insert(frame_obu.end(), hdr_bytes.begin(), hdr_bytes.end());
        frame_obu.insert(frame_obu.end(), tile_data.begin(), tile_data.end());
        return frame_obu;
    }

    std::vector<uint8_t> build_frame_obu_video_inter() {
        BitWriter hdr_bw;
        hdr_bw.write_bit(0);      // show_existing_frame = 0
        hdr_bw.write_bits(1, 2);  // frame_type = INTER_FRAME
        hdr_bw.write_bit(1);      // show_frame = 1
        hdr_bw.write_bit(1);      // error_resilient_mode = 1
        hdr_bw.write_bit(1);      // disable_cdf_update = 1
        hdr_bw.write_bit(1);      // allow_screen_content_tools = 1
        hdr_bw.write_bit(1);      // force_integer_mv = 1
        hdr_bw.write_bit(0);      // frame_size_override_flag = 0
        hdr_bw.write_bits(0x01, 8); // refresh LAST slot only
        for (int ref = 0; ref < 7; ++ref)
            hdr_bw.write_bits(0, 3); // Map every ref type to the valid LAST slot
        hdr_bw.write_bit(0);      // render_and_frame_size_different = 0
        hdr_bw.write_bit(0);      // interpolation_filter == SWITCHABLE = 0
        hdr_bw.write_bits(0, 2);  // interpolation_filter = regular
        hdr_bw.write_bit(0);      // is_motion_mode_switchable = 0
        write_tile_info(hdr_bw);
        write_quantization_params(hdr_bw);
        hdr_bw.write_bit(0);      // segmentation_enabled = 0
        hdr_bw.write_bit(0);      // delta_q_present = 0
        write_loop_filter_params(hdr_bw);
        hdr_bw.write_bit(0);      // tx_mode_select = 0
        hdr_bw.write_bit(0);      // reference_select = SINGLE_REFERENCE
        hdr_bw.write_bit(0);      // reduced_tx_set = 0
        for (int ref = 0; ref < 7; ++ref)
            hdr_bw.write_bit(0);  // global motion type = IDENTITY
        auto hdr_bytes = hdr_bw.get_bytes();
        auto tile_data = build_tile_data();
        std::vector<uint8_t> frame_obu;
        frame_obu.insert(frame_obu.end(), hdr_bytes.begin(), hdr_bytes.end());
        frame_obu.insert(frame_obu.end(), tile_data.begin(), tile_data.end());
        return frame_obu;
    }

    void write_quantization_params(BitWriter& bw) {
        bw.write_bits(qindex_, 8);  // base_q_idx
        bw.write_bit(0);  // DeltaQYDc delta_coded = 0
        bw.write_bit(0);  // diff_uv_delta = 0 (NumPlanes>1, base_q_idx>0)
        bw.write_bit(0);  // DeltaQUDc delta_coded = 0
        bw.write_bit(0);  // DeltaQUAc delta_coded = 0
        bw.write_bit(0);  // using_qmatrix = 0
    }

    void write_loop_filter_params(BitWriter& bw) {
        bw.write_bits(0, 6); bw.write_bits(0, 6); bw.write_bits(0, 3); bw.write_bit(0);
    }

    static int tile_log2(int blkSize, int target) {
        int k = 0;
        while ((blkSize << k) < target) k++;
        return k;
    }

    void write_tile_info(BitWriter& bw) {
        // Compute sbCols/sbRows per AV1 spec (SVT-AV1: ALIGN_POWER_OF_TWO then >>)
        int log2_sb_mi = 4;  // 64x64 SB = 16 MI units, log2(16)=4
        int mi_cols_aligned = (mi_cols_ + ((1 << log2_sb_mi) - 1)) & ~((1 << log2_sb_mi) - 1);
        int mi_rows_aligned = (mi_rows_ + ((1 << log2_sb_mi) - 1)) & ~((1 << log2_sb_mi) - 1);
        int sb_cols = mi_cols_aligned >> log2_sb_mi;
        int sb_rows = mi_rows_aligned >> log2_sb_mi;

        int sb_size_log2 = log2_sb_mi + 2;  // MI_SIZE_LOG2=2, so 6 for 64x64
        int max_tile_width_sb = 4096 >> sb_size_log2;  // MAX_TILE_WIDTH=4096
        int max_tile_area_sb = (4096 * 2304) >> (2 * sb_size_log2);

        int min_log2_tile_cols = tile_log2(max_tile_width_sb, sb_cols);
        int max_log2_tile_cols = tile_log2(1, std::min(sb_cols, 64));
        int max_log2_tile_rows = tile_log2(1, std::min(sb_rows, 64));
        int min_log2_tiles = std::max(min_log2_tile_cols,
                                       tile_log2(max_tile_area_sb, sb_cols * sb_rows));

        bw.write_bit(1);  // uniform_tile_spacing_flag

        // Column increment bits
        int tile_cols_log2 = min_log2_tile_cols;
        while (tile_cols_log2 < max_log2_tile_cols) {
            bw.write_bit(0);  // don't increment (single tile column)
            break;
        }

        // Row increment bits
        int min_log2_tile_rows = std::max(min_log2_tiles - tile_cols_log2, 0);
        int tile_rows_log2 = min_log2_tile_rows;
        while (tile_rows_log2 < max_log2_tile_rows) {
            bw.write_bit(0);  // don't increment (single tile row)
            break;
        }
    }

    // ============================================================
    // Tile data — always split to 8x8 blocks
    // ============================================================
    std::vector<uint8_t> build_tile_data() {
        AV1RangeCoder rc;
        rc.init();
        init_contexts();

        int sb_cols = (width_ + 63) / 64;
        int sb_rows = (height_ + 63) / 64;
        int non_skip_count = 0;

        for (int sby = 0; sby < sb_rows; sby++) {
            for (int sbx = 0; sbx < sb_cols; sbx++) {
                encode_partition(rc, sbx * 64, sby * 64, 6);
            }
        }

        auto data = rc.finish();
        // Count non-skip blocks for diagnostics
        for (auto& bi : blocks_) {
            bool has_coeff = false;
            if (dc_only_mode_) {
                has_coeff = bi.qcoeff[0] != 0;
            } else {
                for (int i = 0; i < 64; i++) {
                    if (bi.qcoeff[i] != 0) { has_coeff = true; break; }
                }
            }
            if (has_coeff) non_skip_count++;
        }
        fprintf(stderr, "[BS] Tile data: %zu bytes, %d SBs (%dx%d), %d/%d blocks with coefficients\n",
                data.size(), sb_cols * sb_rows, sb_cols, sb_rows, non_skip_count, (int)blocks_.size());
        return data;
    }

    // ============================================================
    // Partition: always SPLIT to 8x8, then PARTITION_NONE
    // ============================================================
    void encode_partition(AV1RangeCoder& rc, int org_x, int org_y, int bsize_log2) {
        int mi_row = org_y >> 2, mi_col = org_x >> 2;
        if (mi_row >= mi_rows_ || mi_col >= mi_cols_) return;

        int bsize = 1 << bsize_log2;
        int half_bs = bsize >> 1;
        int bsl = bsize_log2 - 3;

        bool has_rows = (org_y + half_bs) < height_;
        bool has_cols = (org_x + half_bs) < width_;

        // At 8x8: always PARTITION_NONE
        // Above 8x8: always PARTITION_SPLIT (to reach 8x8)
        bool want_split = (bsize_log2 > 3);

        if (bsize_log2 < 3) return;

        int partition;

        if (has_rows && has_cols) {
            int ctx = get_partition_ctx(org_x, org_y, bsl);
            int nsyms = (bsl == 0) ? 4 : 10;
            partition = want_split ? 3 : 0;
            rc.encode_symbol(partition, av1_partition_cdf[ctx], nsyms);
        } else if (has_cols) {
            int ctx = get_partition_ctx(org_x, org_y, bsl);
            int nsyms = (bsl == 0) ? 4 : 10;
            uint16_t cdf[3];
            partition_gather_vert_alike(cdf, av1_partition_cdf[ctx], nsyms);
            rc.encode_symbol(1, cdf, 2);
            partition = 3;
        } else if (has_rows) {
            int ctx = get_partition_ctx(org_x, org_y, bsl);
            int nsyms = (bsl == 0) ? 4 : 10;
            uint16_t cdf[3];
            partition_gather_horz_alike(cdf, av1_partition_cdf[ctx], nsyms);
            rc.encode_symbol(1, cdf, 2);
            partition = 3;
        } else {
            partition = 3;
        }

        if (partition == 0) {
            encode_block(rc, org_x, org_y, bsize_log2);
            update_partition_ctx(org_x, org_y, bsize_log2);
        } else {
            int sub_log2 = bsize_log2 - 1;
            encode_partition(rc, org_x,           org_y,           sub_log2);
            encode_partition(rc, org_x + half_bs, org_y,           sub_log2);
            encode_partition(rc, org_x,           org_y + half_bs, sub_log2);
            encode_partition(rc, org_x + half_bs, org_y + half_bs, sub_log2);
        }
    }

    // ============================================================
    // Block encoding at 8x8 level
    // ============================================================
    void encode_block(AV1RangeCoder& rc, int org_x, int org_y, int bsize_log2) {
        int mi_row = org_y >> 2, mi_col = org_x >> 2;
        int mi_size = 1 << (bsize_log2 - 2);  // 2 for 8x8

        // Find the 8x8 block in our blocks array
        int blk_x = org_x / 8, blk_y = org_y / 8;
        int blk_idx = blk_y * blk_cols_ + blk_x;
        const int16_t* qcoeff = nullptr;
        const int16_t* enc_qcoeff = nullptr;
        bool has_coeff = false;
        int16_t dc_only_qcoeff[64] = {};

        if (blk_idx >= 0 && blk_idx < (int)blocks_.size()) {
            qcoeff = blocks_[blk_idx].qcoeff;
            enc_qcoeff = qcoeff;
            if (dc_only_mode_) {
                dc_only_qcoeff[0] = qcoeff[0];
                enc_qcoeff = dc_only_qcoeff;
            }
            for (int i = 0; i < 64; i++) {
                if (enc_qcoeff[i] != 0) { has_coeff = true; break; }
            }
        }

        const bool video_inter_frame = !still_picture_mode_ && !is_keyframe_ && frame_has_inter_blocks();

        // Skip flag: 1 if ALL planes are all-zero, 0 otherwise
        // For now, skip is based on luma only (chroma always all-zero)
        int skip = has_coeff ? 0 : 1;
        // Force skip=0 for first block if force_skip0_ is set (testing)
        if (force_skip0_ && blk_idx == 0) skip = 0;
        int skip_ctx = get_skip_ctx(mi_row, mi_col);
        rc.encode_symbol(skip, av1_skip_cdf[skip_ctx], 2);

        // Y mode (keyframe)
        int above_ctx, left_ctx;
        get_kf_y_mode_ctx(mi_row, mi_col, above_ctx, left_ctx);
        int y_mode = 0;  // DC_PRED
        if (blk_idx >= 0 && blk_idx < (int)blocks_.size()) {
            switch (blocks_[blk_idx].pred_mode) {
            case 0:   // DC_PRED
            case 1:   // V_PRED
            case 2:   // H_PRED
            case 3:   // D45_PRED
            case 4:   // D135_PRED
            case 5:   // D113_PRED
            case 6:   // D157_PRED
            case 7:   // D203_PRED
            case 8:   // D67_PRED
            case 9:   // SMOOTH_PRED
            case 12:  // PAETH_PRED
                y_mode = blocks_[blk_idx].pred_mode;
                break;
            default:
                y_mode = 0;
                break;
            }
        }
        bool is_inter_block = video_inter_frame && blk_idx >= 0 && blk_idx < (int)blocks_.size() && blocks_[blk_idx].is_inter;
        uint8_t ref_frame = REF_NONE;
        uint8_t inter_mode = REDUCED_INTER_NONE;
        int16_t block_mvx = 0;
        int16_t block_mvy = 0;
        if (is_inter_block) {
            block_mvx = blocks_[blk_idx].mvx;
            block_mvy = blocks_[blk_idx].mvy;
        }

        if (video_inter_frame) {
            const int intra_inter_ctx = get_intra_inter_ctx(mi_row, mi_col);
            if (coeff_debug_mode_) {
                fprintf(stderr, "[INTER] blk=%d mi=(%d,%d) intra_inter_ctx=%d is_inter=%d skip=%d\n",
                        blk_idx, mi_row, mi_col, intra_inter_ctx, is_inter_block ? 1 : 0, skip);
            }
            rc.encode_symbol(is_inter_block ? 1 : 0, av1_intra_inter_cdf[intra_inter_ctx], 2);

            if (is_inter_block) {
                ref_frame = REF_LAST;
                encode_last_frame_ref(rc, mi_row, mi_col);

                const int mode_ctx = get_reduced_single_ref_mode_ctx(blk_x, blk_y, ref_frame);
                const int newmv_ctx = mode_ctx & AV1_NEWMV_CTX_MASK;
                const int zeromv_ctx =
                    (mode_ctx >> AV1_GLOBALMV_OFFSET) & AV1_GLOBALMV_CTX_MASK;
                const int refmv_ctx =
                    (mode_ctx >> AV1_REFMV_OFFSET) & AV1_REFMV_CTX_MASK;
                const ReducedMvState mv_state = collect_reduced_single_ref_mv_state(blk_x, blk_y, ref_frame);
                const ReducedMvCandidate ref_mv = get_reduced_newmv_ref_mv(mv_state);
                const int ref_mvx = ref_mv.col / 8;
                const int ref_mvy = ref_mv.row / 8;
                if (coeff_debug_mode_) {
                    fprintf(stderr,
                            "[INTER] blk=%d ref=%u mv=(%d,%d) ref_mv=(%d,%d) mode_ctx=%d newmv_ctx=%d zeromv_ctx=%d refmv_ctx=%d\n",
                            blk_idx, ref_frame, block_mvx, block_mvy, ref_mvx, ref_mvy,
                            mode_ctx, newmv_ctx, zeromv_ctx, refmv_ctx);
                    fprintf(stderr, "[INTER] blk=%d stack_sz=%zu", blk_idx, mv_state.stack.size());
                    for (size_t si = 0; si < mv_state.stack.size() && si < 6; ++si) {
                        fprintf(stderr, " cand%zu=(%d,%d,w=%u)",
                                si,
                                mv_state.stack[si].col / 8,
                                mv_state.stack[si].row / 8,
                                mv_state.stack[si].weight);
                    }
                    fprintf(stderr, "\n");
                }
                if (block_mvx == 0 && block_mvy == 0) {
                    inter_mode = REDUCED_INTER_GLOBALMV;
                    rc.encode_symbol(1, av1_newmv_cdf[newmv_ctx], 2); // mode != NEWMV
                    rc.encode_symbol(0, av1_zeromv_cdf[zeromv_ctx], 2); // mode == GLOBALMV
                } else if (block_mvx == ref_mvx && block_mvy == ref_mvy) {
                    inter_mode = REDUCED_INTER_NEARESTMV;
                    rc.encode_symbol(1, av1_newmv_cdf[newmv_ctx], 2); // mode != NEWMV
                    rc.encode_symbol(1, av1_zeromv_cdf[zeromv_ctx], 2); // mode != GLOBALMV
                    rc.encode_symbol(0, av1_refmv_cdf[refmv_ctx], 2); // mode == NEARESTMV
                } else {
                    inter_mode = REDUCED_INTER_NEWMV;
                    rc.encode_symbol(0, av1_newmv_cdf[newmv_ctx], 2); // mode == NEWMV
                    if (mv_state.stack.size() > 1) {
                        const int drl_ctx = get_drl_ctx_from_weights(mv_state, 0);
                        rc.encode_symbol(0, av1_drl_cdf[drl_ctx], 2); // keep ref_mv_idx = 0
                    }
                    encode_mv(rc, static_cast<int>(block_mvy) * 8, static_cast<int>(block_mvx) * 8,
                              ref_mv.row, ref_mv.col, /*force_integer_mv=*/true);
                }
            } else {
                rc.encode_symbol(y_mode, av1_if_y_mode_cdf[1], 13);
                if (bsize_log2 >= 3 && is_directional_mode(y_mode))
                    rc.encode_symbol(3, av1_angle_delta_cdf[y_mode - 1], 7);

                // Keep chroma on a deterministic DC predictor until the writer
                // grows real 4x4 chroma residual coding.
                int uv_mode = 0;  // UV_DC_PRED
                rc.encode_symbol(uv_mode, av1_uv_mode_cdf_cfl[y_mode], 14);
            }
        } else {
            rc.encode_symbol(y_mode, av1_kf_y_mode_cdf[above_ctx][left_ctx], 13);
            if (bsize_log2 >= 3 && is_directional_mode(y_mode))
                rc.encode_symbol(3, av1_angle_delta_cdf[y_mode - 1], 7);

            // Keep chroma on a deterministic DC predictor until the writer grows
            // real 4x4 chroma residual coding. This matches the current RTL chroma
            // reconstruction path.
            int uv_mode = 0;  // UV_DC_PRED
            rc.encode_symbol(uv_mode, av1_uv_mode_cdf_cfl[y_mode], 14);
        }

        int dc_sign_ctx = get_dc_sign_ctx(mi_row, mi_col, mi_size);
        int dc_sign_code = 0;
        if (!skip && enc_qcoeff) {
            if (enc_qcoeff[0] > 0) dc_sign_code = 2;
            else if (enc_qcoeff[0] < 0) dc_sign_code = 1;
        }

        // If skip=0, encode coefficients
        if (!skip && enc_qcoeff) {
            bool debug = coeff_debug_mode_ && has_coeff;
            if (debug) fprintf(stderr, "[BLK] skip=0 blk_idx=%d mi_row=%d mi_col=%d\n", blk_idx, mi_row, mi_col);
            // Luma 8x8 TX block
            encode_coeffs_txb(rc, enc_qcoeff, 0, dc_sign_ctx, debug, is_inter_block, y_mode);

            // Chroma Cb/Cr 4x4 — all zero
            // Chroma TX_4X4: txs_ctx=0, use TX_4X4 CDF, ctx 0
            rc.encode_symbol(1, av1_txb_skip_cdf_4x4[7], 2);  // Cb all zero
            rc.encode_symbol(1, av1_txb_skip_cdf_4x4[7], 2);  // Cr all zero
        }

        update_block_ctx(mi_row, mi_col, mi_size, skip, y_mode, dc_sign_code, is_inter_block, ref_frame,
                         inter_mode, block_mvx, block_mvy);
    }

    // ============================================================
    // Utility functions
    // ============================================================
    static int bits_needed(int val) {
        int bits = 0, v = val - 1;
        while (v > 0) { bits++; v >>= 1; }
        return std::max(bits, 1);
    }
    bool frame_has_inter_blocks() const {
        for (const auto& bi : blocks_) {
            if (bi.is_inter) return true;
        }
        return false;
    }
    bool frame_has_only_zero_mv_inter_blocks() const {
        for (const auto& bi : blocks_) {
            if (bi.is_inter && (bi.mvx != 0 || bi.mvy != 0)) return false;
        }
        return true;
    }
    static bool is_directional_mode(int mode) {
        return mode >= 1 && mode <= 8;
    }
    static void write_bytes(std::vector<uint8_t>& v, const char* s, int n) {
        for (int i = 0; i < n; i++) v.push_back(s[i]);
    }
    static void write_le16(std::vector<uint8_t>& v, uint16_t val) {
        v.push_back(val & 0xFF); v.push_back((val >> 8) & 0xFF);
    }
    static void write_le32(std::vector<uint8_t>& v, uint32_t val) {
        for (int i = 0; i < 4; i++) { v.push_back(val & 0xFF); val >>= 8; }
    }
    static void write_le64(std::vector<uint8_t>& v, uint64_t val) {
        write_le32(v, val & 0xFFFFFFFF); write_le32(v, (val >> 32) & 0xFFFFFFFF);
    }
};
