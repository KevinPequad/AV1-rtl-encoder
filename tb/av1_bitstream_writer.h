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

// ---- Intra mode context and partition context ----
static const uint8_t av1_intra_mode_context[13] = {0,1,2,3,4,4,4,4,3,0,1,2,0};
static const uint8_t av1_part_ctx_above[4] = {30, 28, 24, 16};
static const uint8_t av1_part_ctx_left[4]  = {30, 28, 24, 16};

// ============================================================
// Coefficient Coding CDF Tables (QP group 3: qindex > 120)
// Stored in ICDF format. Only TX_8X8 (txs_ctx=1) and luma (plane=0).
// ============================================================

// txb_skip CDF for TX_8X8 [13 contexts][3 values]
static const uint16_t av1_txb_skip_cdf[13][3] = {
    {AV1_ICDF(26726), AV1_ICDF(32768), 0},
    {AV1_ICDF( 1045), AV1_ICDF(32768), 0},
    {AV1_ICDF(11703), AV1_ICDF(32768), 0},
    {AV1_ICDF(20590), AV1_ICDF(32768), 0},
    {AV1_ICDF(18554), AV1_ICDF(32768), 0},
    {AV1_ICDF(25970), AV1_ICDF(32768), 0},
    {AV1_ICDF(31938), AV1_ICDF(32768), 0},
    {AV1_ICDF( 5583), AV1_ICDF(32768), 0},
    {AV1_ICDF(21313), AV1_ICDF(32768), 0},
    {AV1_ICDF(29390), AV1_ICDF(32768), 0},
    {AV1_ICDF(  641), AV1_ICDF(32768), 0},
    {AV1_ICDF(22265), AV1_ICDF(32768), 0},
    {AV1_ICDF(31452), AV1_ICDF(32768), 0},
};

// EOB multi CDF for 64 coefficients (TX_8X8): [2 planes][8 values]
static const uint16_t av1_eob_multi64_cdf[2][8] = {
    {AV1_ICDF(6307),AV1_ICDF(7541),AV1_ICDF(12060),AV1_ICDF(16358),AV1_ICDF(22553),AV1_ICDF(27865),AV1_ICDF(32768),0},
    {AV1_ICDF(24212),AV1_ICDF(25708),AV1_ICDF(28268),AV1_ICDF(30035),AV1_ICDF(31307),AV1_ICDF(32049),AV1_ICDF(32768),0},
};

// EOB extra CDF for TX_8X8 [2 planes][9 contexts][3 values]
static const uint16_t av1_eob_extra_cdf[2][9][3] = {
  {{AV1_ICDF(27399),AV1_ICDF(32768),0},{AV1_ICDF(16327),AV1_ICDF(32768),0},{AV1_ICDF(18071),AV1_ICDF(32768),0},
   {AV1_ICDF(19584),AV1_ICDF(32768),0},{AV1_ICDF(20721),AV1_ICDF(32768),0},{AV1_ICDF(18432),AV1_ICDF(32768),0},
   {AV1_ICDF(19560),AV1_ICDF(32768),0},{AV1_ICDF(10150),AV1_ICDF(32768),0},{AV1_ICDF( 8805),AV1_ICDF(32768),0}},
  {{AV1_ICDF(24932),AV1_ICDF(32768),0},{AV1_ICDF(20833),AV1_ICDF(32768),0},{AV1_ICDF(12027),AV1_ICDF(32768),0},
   {AV1_ICDF(16670),AV1_ICDF(32768),0},{AV1_ICDF(19914),AV1_ICDF(32768),0},{AV1_ICDF(15106),AV1_ICDF(32768),0},
   {AV1_ICDF(17662),AV1_ICDF(32768),0},{AV1_ICDF(13783),AV1_ICDF(32768),0},{AV1_ICDF(28756),AV1_ICDF(32768),0}},
};

// coeff_base CDF for TX_8X8, luma plane [42 contexts][5 values]
static const uint16_t av1_coeff_base_cdf[42][5] = {
    {AV1_ICDF(6041),AV1_ICDF(11854),AV1_ICDF(15927),AV1_ICDF(32768),0},
    {AV1_ICDF(20326),AV1_ICDF(30905),AV1_ICDF(32251),AV1_ICDF(32768),0},
    {AV1_ICDF(14164),AV1_ICDF(26831),AV1_ICDF(30725),AV1_ICDF(32768),0},
    {AV1_ICDF(9760),AV1_ICDF(20647),AV1_ICDF(26585),AV1_ICDF(32768),0},
    {AV1_ICDF(6416),AV1_ICDF(14953),AV1_ICDF(21219),AV1_ICDF(32768),0},
    {AV1_ICDF(2966),AV1_ICDF(7151),AV1_ICDF(10891),AV1_ICDF(32768),0},
    {AV1_ICDF(23567),AV1_ICDF(31374),AV1_ICDF(32254),AV1_ICDF(32768),0},
    {AV1_ICDF(14978),AV1_ICDF(27416),AV1_ICDF(30946),AV1_ICDF(32768),0},
    {AV1_ICDF(9434),AV1_ICDF(20225),AV1_ICDF(26254),AV1_ICDF(32768),0},
    {AV1_ICDF(6658),AV1_ICDF(14558),AV1_ICDF(20535),AV1_ICDF(32768),0},
    {AV1_ICDF(3916),AV1_ICDF(8677),AV1_ICDF(12989),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(18088),AV1_ICDF(29545),AV1_ICDF(31587),AV1_ICDF(32768),0},
    {AV1_ICDF(13062),AV1_ICDF(25843),AV1_ICDF(30073),AV1_ICDF(32768),0},
    {AV1_ICDF(8940),AV1_ICDF(16827),AV1_ICDF(22251),AV1_ICDF(32768),0},
    {AV1_ICDF(7654),AV1_ICDF(13220),AV1_ICDF(17973),AV1_ICDF(32768),0},
    {AV1_ICDF(5733),AV1_ICDF(10316),AV1_ICDF(14456),AV1_ICDF(32768),0},
    {AV1_ICDF(22879),AV1_ICDF(31388),AV1_ICDF(32114),AV1_ICDF(32768),0},
    {AV1_ICDF(15215),AV1_ICDF(27993),AV1_ICDF(30955),AV1_ICDF(32768),0},
    {AV1_ICDF(9397),AV1_ICDF(19445),AV1_ICDF(24978),AV1_ICDF(32768),0},
    {AV1_ICDF(3442),AV1_ICDF(9813),AV1_ICDF(15344),AV1_ICDF(32768),0},
    {AV1_ICDF(1368),AV1_ICDF(3936),AV1_ICDF(6532),AV1_ICDF(32768),0},
    {AV1_ICDF(25494),AV1_ICDF(32033),AV1_ICDF(32406),AV1_ICDF(32768),0},
    {AV1_ICDF(16772),AV1_ICDF(27963),AV1_ICDF(30718),AV1_ICDF(32768),0},
    {AV1_ICDF(9419),AV1_ICDF(18165),AV1_ICDF(23260),AV1_ICDF(32768),0},
    {AV1_ICDF(2677),AV1_ICDF(7501),AV1_ICDF(11797),AV1_ICDF(32768),0},
    {AV1_ICDF(1516),AV1_ICDF(4344),AV1_ICDF(7170),AV1_ICDF(32768),0},
    {AV1_ICDF(26556),AV1_ICDF(31454),AV1_ICDF(32101),AV1_ICDF(32768),0},
    {AV1_ICDF(17128),AV1_ICDF(27035),AV1_ICDF(30108),AV1_ICDF(32768),0},
    {AV1_ICDF(8324),AV1_ICDF(15344),AV1_ICDF(20249),AV1_ICDF(32768),0},
    {AV1_ICDF(1903),AV1_ICDF(5696),AV1_ICDF(9469),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
    {AV1_ICDF(8192),AV1_ICDF(16384),AV1_ICDF(24576),AV1_ICDF(32768),0},
};

// coeff_base_eob CDF for TX_8X8, luma [4 contexts][4 values]
static const uint16_t av1_coeff_base_eob_cdf[4][4] = {
    {AV1_ICDF(21457),AV1_ICDF(31043),AV1_ICDF(32768),0},
    {AV1_ICDF(31951),AV1_ICDF(32483),AV1_ICDF(32768),0},
    {AV1_ICDF(32153),AV1_ICDF(32562),AV1_ICDF(32768),0},
    {AV1_ICDF(31473),AV1_ICDF(32215),AV1_ICDF(32768),0},
};

// coeff_br (LPS) CDF for TX_8X8, luma [21 contexts][5 values]
static const uint16_t av1_coeff_br_cdf[21][5] = {
    {AV1_ICDF(14995),AV1_ICDF(21341),AV1_ICDF(24749),AV1_ICDF(32768),0},
    {AV1_ICDF(13158),AV1_ICDF(20289),AV1_ICDF(24601),AV1_ICDF(32768),0},
    {AV1_ICDF(8941),AV1_ICDF(15326),AV1_ICDF(19876),AV1_ICDF(32768),0},
    {AV1_ICDF(6297),AV1_ICDF(11541),AV1_ICDF(15807),AV1_ICDF(32768),0},
    {AV1_ICDF(4817),AV1_ICDF(9029),AV1_ICDF(12776),AV1_ICDF(32768),0},
    {AV1_ICDF(3731),AV1_ICDF(7273),AV1_ICDF(10627),AV1_ICDF(32768),0},
    {AV1_ICDF(1847),AV1_ICDF(3617),AV1_ICDF(5354),AV1_ICDF(32768),0},
    {AV1_ICDF(14472),AV1_ICDF(19659),AV1_ICDF(22343),AV1_ICDF(32768),0},
    {AV1_ICDF(16806),AV1_ICDF(24162),AV1_ICDF(27533),AV1_ICDF(32768),0},
    {AV1_ICDF(12900),AV1_ICDF(20404),AV1_ICDF(24713),AV1_ICDF(32768),0},
    {AV1_ICDF(9411),AV1_ICDF(16112),AV1_ICDF(20797),AV1_ICDF(32768),0},
    {AV1_ICDF(7056),AV1_ICDF(12697),AV1_ICDF(17148),AV1_ICDF(32768),0},
    {AV1_ICDF(5544),AV1_ICDF(10339),AV1_ICDF(14460),AV1_ICDF(32768),0},
    {AV1_ICDF(2954),AV1_ICDF(5704),AV1_ICDF(8319),AV1_ICDF(32768),0},
    {AV1_ICDF(12464),AV1_ICDF(18071),AV1_ICDF(21354),AV1_ICDF(32768),0},
    {AV1_ICDF(15482),AV1_ICDF(22528),AV1_ICDF(26034),AV1_ICDF(32768),0},
    {AV1_ICDF(12070),AV1_ICDF(19269),AV1_ICDF(23624),AV1_ICDF(32768),0},
    {AV1_ICDF(8953),AV1_ICDF(15406),AV1_ICDF(20106),AV1_ICDF(32768),0},
    {AV1_ICDF(7027),AV1_ICDF(12730),AV1_ICDF(17220),AV1_ICDF(32768),0},
    {AV1_ICDF(5887),AV1_ICDF(10913),AV1_ICDF(15140),AV1_ICDF(32768),0},
    {AV1_ICDF(3793),AV1_ICDF(7278),AV1_ICDF(10447),AV1_ICDF(32768),0},
};

// DC sign CDF [2 planes][3 contexts][3 values]
static const uint16_t av1_dc_sign_cdf[2][3][3] = {
  {{AV1_ICDF(16000),AV1_ICDF(32768),0},{AV1_ICDF(13056),AV1_ICDF(32768),0},{AV1_ICDF(18816),AV1_ICDF(32768),0}},
  {{AV1_ICDF(15232),AV1_ICDF(32768),0},{AV1_ICDF(12928),AV1_ICDF(32768),0},{AV1_ICDF(17280),AV1_ICDF(32768),0}},
};

// Intra TX type CDF: eset=1, TX_8X8, DC_PRED — 7 symbols
// DCT_DCT maps to symbol 1 (av1_ext_tx_ind[3][0] = 1)
static const uint16_t av1_intra_tx_type_cdf_8x8_dc[8] = {
    AV1_ICDF(1870),AV1_ICDF(13742),AV1_ICDF(14530),AV1_ICDF(16498),AV1_ICDF(23770),AV1_ICDF(27698),AV1_ICDF(32768),0
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

    void encode_symbol(int symbol, const uint16_t *icdf, int nsyms) {
        unsigned r = rng_;
        uint64_t l = low_;
        int s = symbol;
        int N = nsyms - 1;
        unsigned fl = (s > 0) ? (unsigned)icdf[s - 1] : 32768u;
        unsigned fh = (unsigned)icdf[s];
        if (fl < 32768u) {
            unsigned u = ((r >> 8) * (uint32_t)(fl >> 6) >> 1) + 4 * (N - (s - 1));
            unsigned v = ((r >> 8) * (uint32_t)(fh >> 6) >> 1) + 4 * (N - s);
            l += r - u;
            r = u - v;
        } else {
            r -= ((r >> 8) * (uint32_t)(fh >> 6) >> 1) + 4 * (N - s);
        }
        normalize(l, r);
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
    struct BlockInfo {
        int16_t qcoeff[64];
        uint8_t pred_mode;
        bool    is_inter;
    };

    AV1BitstreamWriter(int width, int height, int qindex)
        : width_(width), height_(height), qindex_(qindex),
          blk_cols_(width / 8), blk_rows_(height / 8),
          mi_cols_(width / 4), mi_rows_(height / 4) {}

    void add_block(const BlockInfo& blk) {
        blocks_.push_back(blk);
    }

    std::vector<uint8_t> write_ivf_frame() {
        auto frame_data = build_frame();
        std::vector<uint8_t> ivf;
        ivf.reserve(32 + 12 + frame_data.size());
        write_bytes(ivf, "DKIF", 4);
        write_le16(ivf, 0);
        write_le16(ivf, 32);
        write_bytes(ivf, "AV01", 4);
        write_le16(ivf, width_);
        write_le16(ivf, height_);
        write_le32(ivf, 30);
        write_le32(ivf, 1);
        write_le32(ivf, 1);
        write_le32(ivf, 0);
        write_le32(ivf, (uint32_t)frame_data.size());
        write_le64(ivf, 0);
        ivf.insert(ivf.end(), frame_data.begin(), frame_data.end());
        return ivf;
    }

private:
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
    std::vector<BlockInfo> blocks_;

    // Context arrays
    std::vector<uint8_t> part_ctx_above_, part_ctx_left_;
    std::vector<uint8_t> skip_above_, skip_left_;
    std::vector<uint8_t> mode_above_, mode_left_;

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

    void update_block_ctx(int mi_row, int mi_col, int mi_size, int skip, int mode) {
        for (int i = 0; i < mi_size && (mi_col + i) < mi_cols_; i++) {
            skip_above_[mi_col + i] = skip;
            mode_above_[mi_col + i] = mode;
        }
        for (int i = 0; i < mi_size && (mi_row + i) < mi_rows_; i++) {
            skip_left_[mi_row + i] = skip;
            mode_left_[mi_row + i] = mode;
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

    void encode_coeffs_txb(AV1RangeCoder& rc, const int16_t* qcoeff, int plane) {
        int eob = compute_eob(qcoeff);

        // txb_skip: 0 = has coefficients, 1 = all zero
        // Use context 0 for simplicity (first block default)
        rc.encode_symbol(eob == 0 ? 1 : 0, av1_txb_skip_cdf[0], 2);

        if (eob == 0) return;

        // TX type (luma only, qindex > 0)
        // DCT_DCT maps to symbol 1 in ext_tx_ind[EXT_TX_SET_DTT4_IDTX_1DDCT]
        if (plane == 0) {
            rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8_dc, 7);
        }

        // EOB encoding
        int eob_extra;
        int eob_pt = get_eob_pos_token(eob, &eob_extra);
        // TX_8X8 → eob_multi_size=2 → eob_flag_cdf64 (7 symbols)
        rc.encode_symbol(eob_pt - 1, av1_eob_multi64_cdf[plane], 7);

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
            coeff_contexts[pos] = (int8_t)get_nz_map_ctx(levels, pos, 3);
        }

        // Encode coefficient base levels (reverse scan order)
        for (int c = eob - 1; c >= 0; --c) {
            int pos = default_scan_8x8[c];
            int level = abs((int)qcoeff[pos]);
            int coeff_ctx = coeff_contexts[pos];

            if (c == eob - 1) {
                // EOB coefficient: 3 symbols (level-1: 0,1,2)
                rc.encode_symbol(std::min(level, 3) - 1,
                    av1_coeff_base_eob_cdf[coeff_ctx < 4 ? coeff_ctx : 3], 3);
            } else {
                // Non-EOB: 4 symbols (level: 0,1,2,3)
                rc.encode_symbol(std::min(level, 3),
                    av1_coeff_base_cdf[coeff_ctx < 42 ? coeff_ctx : 41], 4);
            }

            // Bypass range (level > 2)
            if (level > 2) {
                int base_range = level - 1 - 2;
                int br_ctx = get_br_ctx_2d(levels, pos, 3);
                for (int idx = 0; idx < 12; idx += 3) {
                    int k = std::min(base_range - idx, 3);
                    rc.encode_symbol(k, av1_coeff_br_cdf[br_ctx < 21 ? br_ctx : 20], 4);
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
                    // DC sign with context (use ctx=0)
                    rc.encode_symbol(sign, av1_dc_sign_cdf[plane][0], 2);
                } else {
                    rc.encode_bit(sign);
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
        auto seq_data = build_sequence_header();
        write_obu_header(out, 1, seq_data.size());
        out.insert(out.end(), seq_data.begin(), seq_data.end());
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
        bw.write_bit(1);
        bw.write_bit(1);
        bw.write_bits(4, 5);
        int w_bits = bits_needed(width_), h_bits = bits_needed(height_);
        bw.write_bits(w_bits - 1, 4);
        bw.write_bits(h_bits - 1, 4);
        bw.write_bits(width_ - 1, w_bits);
        bw.write_bits(height_ - 1, h_bits);
        bw.write_bit(0); // use_128x128_superblock = 0
        bw.write_bit(0); // enable_filter_intra = 0
        bw.write_bit(0); // enable_intra_edge_filter = 0
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

    void write_quantization_params(BitWriter& bw) {
        bw.write_bits(qindex_, 8);
        bw.write_bit(0); bw.write_bit(0); bw.write_bit(0); bw.write_bit(0);
    }

    void write_loop_filter_params(BitWriter& bw) {
        bw.write_bits(0, 6); bw.write_bits(0, 6); bw.write_bits(0, 3); bw.write_bit(0);
    }

    void write_tile_info(BitWriter& bw) {
        bw.write_bit(1); bw.write_bit(0); bw.write_bit(0);
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
            for (int i = 0; i < 64; i++) if (bi.qcoeff[i] != 0) { has_coeff = true; break; }
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
        bool has_coeff = false;

        if (blk_idx >= 0 && blk_idx < (int)blocks_.size()) {
            qcoeff = blocks_[blk_idx].qcoeff;
            for (int i = 0; i < 64; i++) {
                if (qcoeff[i] != 0) { has_coeff = true; break; }
            }
        }

        // Skip flag: 1 if ALL planes are all-zero, 0 otherwise
        // For now, skip is based on luma only (chroma always all-zero)
        int skip = has_coeff ? 0 : 1;
        int skip_ctx = get_skip_ctx(mi_row, mi_col);
        rc.encode_symbol(skip, av1_skip_cdf[skip_ctx], 2);

        // Y mode (keyframe)
        int above_ctx, left_ctx;
        get_kf_y_mode_ctx(mi_row, mi_col, above_ctx, left_ctx);
        int y_mode = 0;  // DC_PRED
        rc.encode_symbol(y_mode, av1_kf_y_mode_cdf[above_ctx][left_ctx], 13);

        // UV mode (CFL allowed for 8x8 blocks since max(8,8) <= 32)
        int uv_mode = 0;  // UV_DC_PRED
        rc.encode_symbol(uv_mode, av1_uv_mode_cdf_cfl[y_mode], 14);

        // If skip=0, encode coefficients
        if (!skip && qcoeff) {
            // Luma 8x8 TX block
            encode_coeffs_txb(rc, qcoeff, 0);

            // Chroma Cb 4x4 — all zero
            static const int16_t zero_coeff[16] = {};
            // For chroma, we just encode txb_skip=1
            // txb_skip_ctx for chroma: use ctx 0
            rc.encode_symbol(1, av1_txb_skip_cdf[0], 2);  // Cb all zero
            rc.encode_symbol(1, av1_txb_skip_cdf[0], 2);  // Cr all zero
        }

        update_block_ctx(mi_row, mi_col, mi_size, skip, y_mode);
    }

    // ============================================================
    // Utility functions
    // ============================================================
    static int bits_needed(int val) {
        int bits = 0, v = val - 1;
        while (v > 0) { bits++; v >>= 1; }
        return std::max(bits, 1);
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
