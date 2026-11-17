/* SPDX-License-Identifier: GPL-3.0-only */
/* CONDUCTOR: an orchestra on the M-VAVE FM-1. You stand on the podium; the orchestra sits in front of you.
 * Built on Dinghy (zvenson/dinghy), on Felucca's platform (Leo Kuroshita, Hügelton Instruments) as SLOOP (isod89)
 * carries it. Everything below platform.c is theirs: updates, the USB rescue, MIDI, USB audio, panel, screen, flash.
 *
 * 14 parts, each with ten patterns of up to 64 steps (16ths), all on one clock, all in one hall:
 *   STRINGS, WOODWINDS, BRASS  HIGH and LOW each: three-voice polyphonic (a step: up to 3 notes and their length)
 *   PERCUSSION  BASS DRUM, CYMBALS, TRIANGLE, SNARE, TIMPANI 1-4 (each timpano tuned), one pattern for the kit
 * Patterns A-J (PRESETS, a click a letter; ARP held + PRESETS: a copy); the Composer chains them into songs. It
 * starts with an original eight-bar festive overture (the Composer's song A: patterns A, B; also in SAVE 1). The winds and brass are
 * wave tables from recordings' spectra (oboe, bassoon, trumpet, horn: tools/gen_winds.py).
 *
 * ANYWHERE   PLAY start / stop   REC tap: live recording, hold 1.5 s then REC again: clear all   SAVE the save page
 *            FX strings  SEL woodwinds  ENV brass (a page a part: again, the other)  LFO percussion   OCT- OCT+
 *            PRESETS the chosen part's pattern A-J
 * ORCHESTRA  SELECT the sections as they sit (percussion, brass, winds, strings)   KNOB1 expression   KNOB2 balance
 *            KNOB4 tempo   ARP section/solo/muted   SEQ the Composer   EDIT its pattern   GLO MIX   keys play it
 * EXPRESSION effective = the section's expression x the playing pattern's curve (each pattern its own, optional);
 *            balance (dB) is the mix, apart. All smoothed, on notes sounding (percussion: on new hits)
 *            REC + PLAY + KNOB1 on the orchestra: one pass of the pattern recorded as its curve
 * EDITING    (the setups, the roll, the music) ALGORITHM the page   KNOB1 the pattern's length   KNOB3 velocity
 *            KNOB4 the notes at the cursor (a drum: its level)
 *            REC + PLAY: what you play is recorded, on the nearest step, as long as you hold it   GLO twice: clear
 * SETUP      KNOB2 a timpano's (a tom's) tuning   EDIT the pattern   ARP section/solo/muted   (KNOB1, 4: as editing)
 *            percussion, a drum machine: the black keys choose a drum (its key lit), the white keys its 16 steps
 * PATTERN    the piano roll, or (EDIT) the music: SELECT the step   KNOB4 the notes   KNOB2 their length (named)
 *            keys held together: a chord at the cursor   ARP rest   SEQ tie
 * CURVE      (EDIT after the music; EDIT on the drum machine) KNOB1 point  KNOB2 its step  KNOB3 level  KNOB4 shape
 *            ARP add  SEQ delete  GLO on / bypassed  SELECT a start shape (flat, crescendo, diminuendo, swell)
 * COMPOSER   SELECT the row   KNOB1 the song's length   KNOB2 the box   KNOB3 its pattern (blank, A-J, X)
 *            KNOB4 on / off   PRESETS the song A-J (ARP held: a copy)   SEQ or HOME back
 * MIX        (GLO) SELECT a section (or the hall)   KNOB1 its expression (as on the orchestra)   KNOB2 its balance
 *            -30..+6 dB (the hall: KNOB1 or 2, the room: studio, chamber, concert hall, cathedral)   GLO back
 * SAVE       four slots (QUARTET's): KNOB1 the slot, REC save, PLAY load (each asks first: the same button again)
 * MIDI in: channels 1-6 play the melodic parts (strings, woodwinds, brass, high then low), channel 10 the
 * percussion (36 bass drum, 49 cymbals, 81 triangle, 38 snare, 41 43 45 47 timpani 1-4).
 *
 * The chip has no FPU: integers only, no float, no 64-bit division (docs/RULES.md). tests/host.c runs this file on
 * a computer: the screens as PNG, the sound as WAV. */
#define APP_NAME "CONDUCTOR"                    /* the rescue and crash screens */
#define STORE_MAGIC 0x52444E43u                 /* "CNDR": our own flash data, never another fork's */
#define CV_MAX (240u * 212u)                    /* the canvas: the orchestra page's body, 102 KB of the pool */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: MIDI in (platform.c) */
#include "platform.c"

/* ======================================================================== the orchestra */
#define NINS 14u
#define NSTEP 64u
#define NSEC 4u
#define NV 3u                                   /* voices a melodic part: a chord of up to 3 notes */
enum { S_HI, S_LO, W_HI, W_LO, B_HI, B_LO, P_BD, P_CYM, P_TRI, P_SN, P_T1, P_T2, P_T3, P_T4 };
enum { TY_STR, TY_OBOE, TY_BSN, TY_TPT, TY_HORN, TY_BD, TY_CYM, TY_TRI, TY_SN, TY_TIMP, TY_CLASH, TY_SN2, TY_TOM };
#define WT_TY(t) ((t) >= TY_OBOE && (t) <= TY_HORN)   /* the winds and brass: wave tables from recordings */
#define MELODIC(i) ((i) < P_BD)
/* the drum machine's sounds: any slot can be any of them (ALGORITHM on the percussion setup) */
enum { DT_BD, DT_CYM1, DT_CYM2, DT_TRI, DT_SN1, DT_SN2, DT_TOM, DT_TIMP1, DT_TIMP2, DT_TIMP3, DT_TIMP4, NDT };
static const struct { const char *name; uint8_t ty, dec; uint16_t level; } DTYPE[NDT] = {
    {"BASS DRUM", TY_BD, 38, 10400}, {"CYMBAL 1", TY_CLASH, 35, 17000}, {"CYMBAL 2", TY_CYM, 44, 12000},
    {"TRIANGLE", TY_TRI, 45, 3000}, {"SNARE 1", TY_SN, 24, 11700}, {"SNARE 2", TY_SN2, 11, 30000},
    {"TOMS", TY_TOM, 10, 21000}, {"TIMPANI 1", TY_TIMP, 47, 8950}, {"TIMPANI 2", TY_TIMP, 45, 5350},
    {"TIMPANI 3", TY_TIMP, 43, 3880}, {"TIMPANI 4", TY_TIMP, 41, 2690}};   /* (the timpani: as loud as the other drums to
                                                   the ear, A-weighted: the lower, the more level it needs) */   /* (levels: each as loud as the others,
                                                   K-weighted, at mf; the triangle a little under; snare 2 and the toms, all attack,
                                                   a little under too: louder, they would set off the limiter) */   /* (timpani 1 the biggest: lowest, longest) */
static const uint8_t TIMP_TUNE[4] = {36, 43, 48, 55};   /* each timpano as it starts: C2 G2 C3 G3, fifths and fourths
                                                   apart (KNOB 2 retunes it) */
#define DTY(i) (DTYPE[sc.dtype[(i) - P_BD] % NDT])   /* a drum slot's sound */
#define TIMPANI(i) (!MELODIC(i) && (DTY(i).ty == TY_TIMP || DTY(i).ty == TY_TOM))   /* (tuned: timpani, toms) */

typedef struct {
    const char *sec, *reg, *who;                /* "STRINGS", "HIGH"; (who: unused) */
    uint8_t ty, s;                              /* sound, section */
    int8_t oct;                                 /* the keyboard's octave as it starts (0: F3..G5) */
    uint8_t atk, dec, sus, rel;                 /* times as table indexes (10 ms * 2^(i/6)); sustain /255 */
    uint16_t cut, cut_env;                      /* filter (Hz): fixed, and how far the envelope opens it */
    uint16_t level;                             /* the loudest it gets, Q15 */
    uint8_t vib;                                /* vibrato depth, 1/4096 of the pitch */
    int32_t gl, gr;                             /* stereo: where it sits, seen from the podium (Q15) */
} ins_t;

static const ins_t INS[NINS] = {
    {"STRINGS", "HIGH", "",                   TY_STR,   0, 1, 25, 30, 235, 27, 0, 0, 2700, 20, 35715, 16449},
    {"STRINGS", "LOW", "",                    TY_STR,   0, -1, 26, 30, 235, 32, 0, 0, 4250, 20, 17585, 35169},
    {"WOODWINDS", "HIGH", "",                 TY_OBOE,  1, 0, 16, 24, 235, 18, 0, 0, 8600, 24, 31701, 23262},
    {"WOODWINDS", "LOW", "",                  TY_BSN,   1, -1, 9, 24, 230, 15, 0, 0, 8450, 14, 23262, 31701},
    {"BRASS", "HIGH", "",                     TY_TPT,   2, 0, 13, 20, 240, 18, 0, 0, 7600, 14, 32898, 21537},
    {"BRASS", "LOW", "",                      TY_HORN,  2, -1, 21, 30, 215, 21, 0, 0, 33800, 0, 20620, 33480},
    {"PERCUSSION", "BASS DRUM", "gran cassa", TY_BD,    3, 0, 0, 38, 0, 0, 0, 0, 7000, 0, 34052, 19660},
    {"PERCUSSION", "CYMBALS", "clash",        TY_CYM,   3, 0, 0, 44, 0, 0, 0, 0, 10000, 0, 32305, 22416},
    {"PERCUSSION", "TRIANGLE", "triangle",    TY_TRI,   3, 0, 0, 45, 0, 0, 0, 0, 3000, 0, 29816, 25634},
    {"PERCUSSION", "SNARE", "side drum",      TY_SN,    3, 0, 0, 24, 0, 0, 0, 0, 12000, 0, 27100, 28490},
    {"PERCUSSION", "TIMPANI 1", "timpano",    TY_TIMP,  3, 0, 0, 45, 0, 0, 0, 0, 6000, 0, 24868, 30457},
    {"PERCUSSION", "TIMPANI 2", "timpano",    TY_TIMP,  3, 0, 0, 45, 0, 0, 0, 0, 6000, 0, 23262, 31701},
    {"PERCUSSION", "TIMPANI 3", "timpano",    TY_TIMP,  3, 0, 0, 45, 0, 0, 0, 0, 6000, 0, 21537, 32898},
    {"PERCUSSION", "TIMPANI 4", "timpano",    TY_TIMP,  3, 0, 0, 45, 0, 0, 0, 0, 6000, 0, 19660, 34052}};
static const char *const SEC_NAME[NSEC] = {"STRINGS", "WOODWINDS", "BRASS", "PERCUSSION"};
static const uint8_t SEC_FIRST[NSEC + 1] = {S_HI, W_HI, B_HI, P_BD, NINS};

/* ---- what is kept in flash: the whole score. A melodic step holds up to 3 notes (a chord) and how many steps they
 * last (QUARTET's way); a new step with notes ends the last one's. A drum step: its hit and how hard. */
typedef struct { uint8_t n[NV], len, dyn; } mstep_t;   /* (dyn: bits 0-1 the velocity, 2-4 the articulation, ART_) */
enum { ART_NONE, ART_STAC, ART_ACC, ART_LEG, ART_MARC, ART_PIZZ, ART_TREM, ART_TRILL, NART };   /* (pizzicato, tremolo:
                                                   the strings only; the trill: to the note above in the key) */
#define ART_STR(a) ((a) == ART_PIZZ || (a) == ART_TREM)   /* (the strings' own) */   /* n: MIDI notes, 0 = none; len 1..64 steps; dyn 0..3: p mp mf f */
#define NPAT 10u                                /* patterns a part, A-J (PRESETS) */
#define NSONG 10u                               /* the Composer's songs, A-J */
#define NBOX 8u                                 /* the Composer: boxes a part has in a song */
#define NROW (P_BD + 1u)                        /* the six melodic parts, the percussion (one pattern for the kit) */
#define NDRUM (NINS - P_BD)
#define BOX_X 11u                               /* a box: 0 blank (the one before goes on), 1..10 A..J, 11 X (a rest) */
#define NCP 6u                                  /* an expression curve's points, at most */
enum { CS_LIN, CS_IN, CS_OUT, CS_SMOOTH };      /* the shape of a curve's segment: linear, ease-in, ease-out, smooth */
typedef struct { uint8_t pos, val, shape; } cpt_t;   /* a point: its step (0..the pattern's length), level 0..127, the
                                                   shape of the segment after it */
typedef struct { uint8_t on, n; cpt_t p[NCP]; } curve_t;   /* a pattern's expression curve: on (else bypassed), points */
typedef struct {
    uint8_t ver, tempo, hall, rsv;
    uint8_t vol[NINS], tune[NINS];              /* tune: a timpano's note; melodic: the keyboard's octave + 4 */
    uint8_t mute[NINS], solo[NINS];             /* solo: a melodic part as one player, not a section */
    uint8_t psel[NROW];                         /* each row's chosen pattern (PRESETS): edited, and played */
    uint8_t len[NROW][NPAT];
    uint8_t comp_on, comp_song;                 /* the Composer: on; the song it plays */
    uint8_t clen[NSONG];                        /* each song's length, 1..8 boxes */
    uint8_t chain[NSONG][NROW][NBOX];           /* each song: 8 boxes a row */
    uint8_t expr[NROW];                         /* each section's expression, 0..127 (80 = mf: as designed) */
    int8_t bal[NROW];                           /* each section's balance, -30..+6 dB */
    curve_t curve[NROW][NPAT];                  /* each pattern's expression curve (a multiplier on the section's) */
    uint8_t dtype[NDRUM];                       /* each drum slot's sound (DT_...) */
    uint8_t tsig;                               /* the time signature: TS_ (2, 3, 4: 2/4 3/4 4/4, as before) */
    uint8_t vib[NROW];                          /* (version 8: expr, bal, vib, the melodic mute/solo a section; read once,
                                                   into each of its patterns) */
    uint8_t pexpr[NROW][NPAT];                  /* each pattern's expression, 0..127 (80 = mf: as designed) */
    int8_t pbal[NROW][NPAT];                    /* each pattern's balance, -30..+6 dB */
    uint8_t pvib[NROW][NPAT];                   /* each pattern's vibrato: 0 off, 1 light, 2 normal, 3 wide */
    uint8_t pmute[P_BD][NPAT], psolo[P_BD][NPAT];   /* each melodic pattern's players: muted; solo (one player) */
    uint8_t swing, human;                       /* the song page: swing 0..6 (every second 16th later), humanise 0..6 */
    uint8_t click, clickvol, rsv2[2];           /* MIX: the click (CLK_), its level 1..8 */
    mstep_t mel[P_BD][NPAT][NSTEP];             /* (the patterns last: a save packs them, the rest as it is) */
    uint8_t drum[NDRUM][NPAT][NSTEP];           /* 0 none, 1..4 a hit (its dynamic + 1) */
} score_t;
#define PK(i) ((i) >= P_BD ? (uint32_t)P_BD : (uint32_t)(i))   /* the drums: the kit's row */
/* the time signatures. A step is always a 16th; a bar, a beat (what the tempo counts) and the grid's groups (the
 * roll's lines, the music's beams) in steps. Compound time (6/8, 12/8, 3/8): the beat a dotted quarter, beamed in
 * threes; 2/2: the beat a half note (written as 4/4 is) */
enum { TS_24 = 2, TS_34, TS_44, TS_54, TS_68, TS_128, TS_22, TS_38, TS_END };
static const struct { uint8_t num, den, bar, beat, grp; } TSIG[TS_END] = {
    {0, 0, 16, 4, 4}, {0, 0, 16, 4, 4}, {2, 4, 8, 4, 4}, {3, 4, 12, 4, 4}, {4, 4, 16, 4, 4}, {5, 4, 20, 4, 4},
    {6, 8, 12, 6, 6}, {12, 8, 24, 6, 6}, {2, 2, 16, 8, 4}, {3, 8, 6, 6, 6}};
static const uint8_t TS_ORDER[TS_END - 2] = {TS_24, TS_34, TS_44, TS_54, TS_22, TS_38, TS_68, TS_128};   /* (the song page) */
#define TSX (TSIG[sc.tsig >= TS_24 && sc.tsig < TS_END ? sc.tsig : TS_44])
#define BAR ((uint32_t)TSX.bar)                 /* steps a bar: 6 (3/8) .. 24 (12/8) */
#define BEAT ((uint32_t)TSX.beat)               /* steps a beat: 4, 6 (compound), 8 (2/2) */
#define GRP ((uint32_t)TSX.grp)                 /* steps a group: 4, or 6 (compound) */
#define PSEL(i) (sc.psel[PK(i)] % NPAT)
#define MS(i, st) (sc.mel[i][PSEL(i)][st])      /* the pattern you edit: a melodic step */
#define DH(i, st) (sc.drum[(i) - P_BD][PSEL(i)][st])   /* ... a drum's step */
#define LEN(i) (sc.len[PK(i)][PSEL(i)])
#define SCORE_VER 10u                           /* 10: swing, humanise, the click (9: expression, balance, vibrato, players
                                                   a pattern; 8: time signature, vibrato, articulations; 7: the drum slots) */
enum { CLK_OFF, CLK_COUNT, CLK_REC, CLK_ALWAYS, NCLK };   /* the click: off; a bar's count-in to recording; and a click
                                                   while recording; a click whenever it plays */
#define KEYSIG ((int32_t)(int8_t)sc.rsv2[0])    /* the key: sharps (+1..+7) or flats (-1..-7); 0 C major (the KEY page) */
#define KEY_LOCK (sc.rsv2[1] & 1u)              /* the keys play only the key's notes (scale lock) */
#define KEY_SHOW (sc.rsv2[1] >> 1 & 1u)         /* the key's notes glow on the keyboard (visualise) */
#define SORD(r, q) (sc.pvib[r][q] >> 7)         /* con sordino: a pattern's mute (the vibrato byte's top bit) */
#define EXPR(r) sc.pexpr[r][PSEL(r)]           /* the chosen pattern's (what the pages show and set): expression */
#define BALN(r) sc.pbal[r][PSEL(r)]            /* ... balance */
#define VIB(r) sc.pvib[r][PSEL(r)]             /* ... vibrato */
#define MUTE(i) (*(MELODIC(i) ? &sc.pmute[i][PSEL(i)] : &sc.mute[i]))   /* ... muted (a drum slot: for every pattern) */
#define SOLO(i) (*(MELODIC(i) ? &sc.psolo[i][PSEL(i)] : &sc.solo[i]))   /* ... solo */
static score_t sc;

/* ======================================================================== tables */
/* exp(-32 / (t * 44100)) in Q24, t = 10 ms * 2^(i/6): a level falls by this every 32 samples */
static const uint32_t DEC[64] = {
    15602941, 15726954, 15838267, 15938100, 16027570, 16107702, 16179429, 16243599,
    16300983, 16352276, 16398110, 16439051, 16475611, 16508252, 16537385, 16563383,
    16586580, 16607273, 16625730, 16642190, 16656869, 16669957, 16681626, 16692028,
    16701301, 16709567, 16716934, 16723500, 16729353, 16734568, 16739216, 16743357,
    16747048, 16750337, 16753267, 16755878, 16758205, 16760278, 16762125, 16763771,
    16765237, 16766544, 16767708, 16768745, 16769669, 16770492, 16771226, 16771879,
    16772461, 16772980, 16773442, 16773854, 16774221, 16774547, 16774838, 16775098,
    16775329, 16775535, 16775718, 16775882, 16776027, 16776157, 16776272, 16776375};
/* 2^24 * 32 / (t * 44100): a linear attack reaches full in t */
static const uint32_t ATK[64] = {
    1217394, 1084575, 966247, 860828, 766910, 683239, 608697, 542288,
    483123, 430414, 383455, 341620, 304349, 271144, 241562, 215207,
    191728, 170810, 152174, 135572, 120781, 107603, 95864, 85405,
    76087, 67786, 60390, 53802, 47932, 42702, 38044, 33893,
    30195, 26901, 23966, 21351, 19022, 16946, 15098, 13450,
    11983, 10676, 9511, 8473, 7549, 6725, 5991, 5338,
    4755, 4237, 3774, 3363, 2996, 2669, 2378, 2118,
    1887, 1681, 1498, 1334, 1189, 1059, 944, 841};
static uint32_t key_pc(void) { return (uint32_t)((KEYSIG * 7 % 12 + 12) % 12); }   /* the key's home note (0 C) */
static uint32_t in_key(uint32_t n)              /* the note is in the key's (major) scale */
{
    static const uint16_t MAJ = 0xAB5u;         /* (0 2 4 5 7 9 11) */
    return MAJ >> ((n + 12u - key_pc()) % 12u) & 1u;
}
static uint32_t key_up(uint32_t n)              /* the note above in the key (a trill's other note) */
{
    return in_key(n + 1u) && in_key(n) ? n + 1u : n + 2u;
}
static uint32_t key_lock(uint32_t n)            /* scale lock: a note off the key down to the one below it */
{
    return in_key(n) ? n : n - 1u;
}
static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9): f * 2^32 / 44100; lower octaves shift */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static const int32_t DYN[4] = {14000, 19500, 25500, 32767};   /* p mp mf f: -7, -4.5, -2 dB, 0 (a note's emphasis) */
static const uint16_t EXPR_G[128] = {   /* expression 0..127 -> gain, Q12, as you hear loudness (in dB, not a 0-100
                                           fader): mf (80) 0 dB, mp about -5, p -10, pp -16; ff +4 dB */
    0, 89, 183, 283, 389, 501, 620, 640, 660, 680, 701, 722, 744, 767, 790, 814,
    839, 864, 890, 917, 944, 972, 1001, 1031, 1061, 1092, 1124, 1157, 1190, 1225, 1260, 1296,
    1333, 1371, 1410, 1449, 1490, 1531, 1574, 1617, 1662, 1707, 1754, 1801, 1850, 1899, 1950, 2002,
    2054, 2108, 2163, 2218, 2275, 2333, 2392, 2452, 2513, 2575, 2637, 2701, 2766, 2832, 2898, 2965,
    3033, 3102, 3171, 3241, 3311, 3382, 3453, 3523, 3594, 3664, 3734, 3802, 3869, 3934, 3996, 4052,
    4096, 4136, 4177, 4218, 4260, 4302, 4344, 4387, 4430, 4474, 4518, 4562, 4607, 4652, 4698, 4744,
    4791, 4838, 4886, 4934, 4983, 5032, 5081, 5131, 5182, 5233, 5284, 5336, 5389, 5442, 5496, 5550,
    5604, 5660, 5715, 5772, 5828, 5886, 5944, 6002, 6061, 6121, 6181, 6242, 6304, 6366, 6428, 6492};
static const uint16_t BAL_G[37] = {     /* balance -30..+6 dB -> gain, Q12 */
    130, 145, 163, 183, 205, 230, 258, 290, 325, 365, 410, 460,
    516, 579, 649, 728, 817, 917, 1029, 1154, 1295, 1453, 1631, 1830,
    2053, 2303, 2584, 2900, 3254, 3651, 4096, 4596, 5157, 5786, 6492, 7284,
    8173};
static const char *const EXPR_NAME[6] = {"pp", "p", "mp", "mf", "f", "ff"};
static const uint8_t EXPR_STEP[6] = {12, 32, 54, 80, 98, 118};   /* pp p mp mf f ff */
static uint8_t expr_step(uint32_t e, int32_t d)  /* the next marking up or down */
{
    int32_t b = (int32_t)(e < 22u ? 0u : e < 43u ? 1u : e < 64u ? 2u : e < 86u ? 3u : e < 107u ? 4u : 5u) + d;
    return EXPR_STEP[b < 0 ? 0 : b > 5 ? 5 : b];
}
static uint32_t expr_band(uint32_t e) { return e < 22u ? 0u : e < 43u ? 1u : e < 64u ? 2u : e < 86u ? 3u : e < 107u ? 4u : 5u; }
static const uint8_t ORDER7[7] = {6, 4, 5, 2, 3, 0, 1};   /* the sections as they sit, back to front: percussion,
                                                   brass high, brass low, winds high, winds low, strings high, low */
static const char *const DYN_NAME[4] = {"p", "mp", "mf", "f"};
#define FULL (1 << 24)                          /* envelopes are Q24 */
#define CH 32u                                  /* the control rate: envelopes and filters every 32 samples */

static uint32_t note_inc(uint32_t n)            /* a MIDI note's phase step */
{
    if (n > 119u) n = 119u;
    return INC_C9[n % 12u] >> (10u - n / 12u);
}
static uint32_t mulq10(uint32_t v, uint32_t m) { return (uint32_t)(((uint64_t)v * m) >> 10); }
static int32_t lp_k(uint32_t fc)                /* a one-pole low-pass coefficient (Q14) for fc Hz: w / (1 + w) */
{
    uint32_t w = fc * 2334u / 1000u;            /* 2 pi fc / 44100 in Q14 */
    if (fc > 12000u) w = 12000u * 2334u / 1000u;
    return (int32_t)((w << 14) / (16384u + w));
}

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (a refined parabola) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static int32_t saw(uint32_t ph, uint32_t inc)   /* a rising saw with polyBLEP edges (no aliasing whine), Q15 */
{
    int32_t s = (int32_t)(ph >> 16) - 32768, x;
    uint32_t d = inc >> 12;
    if (!d) return s;
    if (ph < inc) {                             /* just wrapped: x = t / dt */
        x = (int32_t)(((ph >> 12) << 15) / d);
        s -= 2 * x - ((x * x) >> 15) - 32768;
    } else if (ph > ~inc) {                     /* about to wrap: x = (t - 1) / dt */
        x = -(int32_t)((((~ph) >> 12) << 15) / d);
        s -= ((x * x) >> 15) + 2 * x + 32768;
    }
    return s;
}
static uint32_t rnd = 0x1234567u;
static int32_t noise(void)                      /* white noise, Q15 */
{
    rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
    return (int32_t)rnd >> 16;
}

/* ======================================================================== the players (the audio interrupt) */
typedef struct {
    uint32_t ph[6], inc, inc_now, lfo[3];
    uint32_t cnt;                               /* samples since the note (or hit) began: the players come in one by one */
    int32_t env, env2, env3;                    /* Q24: main, second (upper partials, filter), third (a mallet) */
    int32_t amp;                                /* the level at the end of the last piece (Q15), for smooth changes */
    int32_t peak;                               /* level * dynamic * volume, Q15 */
    int32_t lp1, lp2, lp3, lp4, hp;
    int32_t bd[6];                              /* the strings' body: the resonances' states */
    uint8_t fast;                               /* a bow change: the next attack quick */
    uint32_t age;                               /* control ticks since the note began (vibrato comes in late) */
    volatile uint8_t stage;                     /* 0 silent, 1 attack, 2 sustain, 3 release, 4 ringing (percussion) */
    uint8_t dyn, note;
} voice_t;
static voice_t vo[NINS];                        /* drums: the drum; melodic parts: the part's filter, body, vibrato */
typedef struct {                                /* a melodic voice: a note of a part */
    uint32_t ph[3], inc, inc_now;
    int32_t env, env2, amp, peak;
    uint32_t age, cnt, stamp;
    volatile uint8_t stage;                     /* 0 silent, 1 attack, 2 sustain, 3 release */
    uint8_t dyn, note, seq, fast;               /* seq: started by the sequencer */
    uint8_t band;                               /* the winds, brass: the wave table's pitch band */
    uint8_t pl;                                 /* its players: 2 (a section), 1 (a chord, SOLO); 0: not yet decided */
    uint8_t art;                                /* its articulation (ART_) */
    uint16_t hold;                              /* humanise: samples before it begins */
    uint32_t inc_lo, inc_hi;                    /* a trill: its two notes */
} mvoice_t;
static mvoice_t mv[P_BD][NV];
static uint32_t mv_stamps;
static uint8_t mv_left[P_BD];                   /* steps until the sequenced notes end */

static volatile uint32_t run_req;               /* main: 1 = play */
static volatile int8_t live_tr;                 /* the orchestra transposed live (KNOB 3 on the orchestra), semitones;
                                                   not saved, the notes not changed */
static volatile uint8_t rec_live;               /* main: recording is on (the click, the count-in) */
static volatile uint32_t cin_left;              /* the count-in: steps still to count before the music starts */
static volatile uint8_t listen_only = 0xFFu;    /* ARP held on the orchestra: only this row is heard (0xFF all) */
static volatile uint8_t end_req;                /* SEQ held on the orchestra: end at the end of the song (this pass) */
static uint32_t end_done;                       /* the parts that have played their last pass */
static int32_t end_fer;                         /* the last chord held on (samples), then the end */
static uint32_t running;                        /* interrupt's copy */
static volatile uint32_t step_q8 = 661500u * 256u / 96u;   /* samples a step, Q8 (16ths): main writes */
static volatile int32_t to_step;                /* Q8 samples to the next step (main reads it: live recording) */
static uint8_t pos[NINS];                       /* the next step of each pattern */
static volatile uint8_t play_pat[NINS];         /* the pattern each part plays now (the interrupt writes) */
static volatile uint8_t chain_at[NINS];         /* the Composer: the box each part plays now */
static volatile uint32_t chain_rest;            /* the Composer: bit i = part i rests (an X, blanks at the start) */
static uint32_t seq_first;                      /* the first step after PLAY */
static volatile uint8_t cur[NINS];              /* the step each pattern plays now (for the screen) */
static volatile uint8_t hits[NINS];             /* +1 a note: the screen lights the player */
static volatile uint32_t gstep;                 /* steps since PLAY (the baton's beat) */
static volatile int32_t hall_q15;               /* the reverb's level */
static volatile uint8_t eff[NROW];              /* each section's expression now (the section's x its curve), 0..127 */
static volatile uint8_t rec_next[NROW] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};   /* recording: the
                                                   pattern each row goes on to when its own comes round (0xFF none) */
static volatile uint8_t xrec_row = 0xFFu;     /* recording expression into this section's curve (else 0xFF) */
static int32_t gain_t[NROW];                    /* ... and the gain it and the balance ask for (Q12) */

static int32_t peak_of(uint32_t i, uint32_t dyn)
{
    int32_t p = (int32_t)(MELODIC(i) ? INS[i].level : DTY(i).level) * DYN[dyn & 3u] >> 15;
    return p * (int32_t)sc.vol[i] / 10;         /* volume 0..20, 10 = as designed */
}

static void voice_on(uint32_t i, uint32_t note, uint32_t dyn)   /* a drum */
{
    voice_t *v = &vo[i];
    (void)note;
    v->dyn = (uint8_t)(dyn & 3u);
    v->peak = (int32_t)(((int64_t)peak_of(i, dyn) * gain_t[P_BD]) >> 12);   /* (the kit's expression and balance: as it
                                                   is when the hit comes) */
    hits[i]++;
    switch (DTY(i).ty) {
    case TY_BD:
        v->inc = 4869577u;                      /* 50 Hz, from a little higher */
        v->inc_now = v->inc + (v->inc >> 1);
        v->env = FULL; v->env3 = FULL; v->stage = 4;
        return;
    case TY_CYM:
        v->env = FULL; v->env2 = FULL; v->stage = 4;
        return;
    case TY_TRI:
        v->env = FULL; v->env2 = FULL; v->env3 = FULL; v->stage = 4;
        return;
    case TY_SN:                                 /* (the second drummer: an echo of the first, render_voice) */
        v->env = FULL; v->env2 = FULL; v->env3 = 0; v->stage = 4;
        return;
    case TY_CLASH:                              /* (inc: the knock's phase) */
        v->inc = 0; v->env = FULL; v->env2 = FULL; v->env3 = FULL; v->stage = 4;
        return;
    case TY_SN2:
        v->env = FULL; v->env2 = FULL; v->env3 = FULL; v->stage = 4;
        return;
    case TY_TOM:                                /* tuned: from a little sharp, settling */
        v->note = sc.tune[i];
        v->inc = note_inc(sc.tune[i]);
        v->inc_now = v->inc + (v->inc >> 4);
        v->env = FULL; v->env2 = FULL; v->env3 = FULL; v->stage = 4;
        return;
    default:                                    /* a timpano */
        if (v->stage && v->env > FULL / 8 && v->note == sc.tune[i]) {   /* still ringing: a roll. The skin keeps its
                                                   pitch and phase (no click, no wobble), the stroke adds to the ring */
            v->env = FULL; v->env3 = FULL;
            if (v->env2 < FULL / 2) v->env2 = FULL / 2;
            return;
        }
        v->note = sc.tune[i];
        v->inc = note_inc(sc.tune[i]);
        v->inc_now = v->inc + (v->inc >> 6);    /* the skin starts a little sharp and settles */
        v->env = FULL; v->env2 = FULL; v->env3 = FULL; v->stage = 4;
        return;
    }
}

/* a melodic part's voices: the same note again, a free voice, one fading out, else the oldest */
static mvoice_t *mel_pick(uint32_t i, uint32_t note)
{
    mvoice_t *best = &mv[i][0];
    uint32_t k, score = ~0u;
    for (k = 0; k < NV; k++) {
        mvoice_t *v = &mv[i][k];
        uint32_t sc_ = v->note == note && v->stage && v->stage < 3u ? 0u
                     : !v->stage ? 1u
                     : v->stage == 3u ? 2u + (uint32_t)(v->env >> 8)
                     : 0x80000000u + (mv_stamps - v->stamp > 0x7FFFFFFFu ? 0u : 0x7FFFFFFFu - (mv_stamps - v->stamp));
        if (sc_ < score) { score = sc_; best = v; }
    }
    return best;
}
/* ---- the winds and brass: wave tables made at start from each instrument's spectral envelope, measured from
 * recordings (tools/gen_winds.py: oboe, bassoon, trumpet, horn), soft (p) and loud (f) for each of ten pitch bands
 * (the envelope stays put in Hz, so a low note and a high one differ as the instruments do). A note reads its band's
 * two tables and blends them by its dynamic and the section's expression; the blend starts soft and opens through
 * the attack (each recording speaks darker first) */
#include "winds_env.h"
#define WT_N 256u                               /* samples a table: harmonics up to 127 */
#define WT_B 10u                                /* pitch bands a part */
static int16_t wt[4][WT_B][2][WT_N] __attribute__((section(".pool")));   /* 40 KB */
static const uint8_t WT_BASE[4] = {55, 31, 50, 31};   /* each part's lowest band (MIDI): G3, G1, D3, G1 */
static void wt_build(void)
{
    static int32_t acc[WT_N];
    uint32_t p, b, d, k, j;
    for (p = 0; p < 4u; p++)
        for (b = 0; b < WT_B; b++)
            for (d = 0; d < 2u; d++) {
                const int8_t *E = WENV[p][d];
                uint32_t note = WT_BASE[p] + 4u * b, seed = 0x2545F491u + p * 7919u;
                uint32_t ftop = (uint32_t)(((uint64_t)note_inc(note + 2u) * 44100u) >> 32), kmax = ftop ? 10000u / ftop : 1u;   /* (to
                                                   10 kHz, at most 64: past that a 256-sample table, read between its
                                                   samples, turns the top harmonics into hiss) */
                int32_t pk = 1;
                int16_t *T = wt[p][b][d];
                if (kmax > 64u) kmax = 64u;
                for (j = 0; j < WT_N; j++) acc[j] = 0;
                for (k = 1; k <= kmax; k++) {
                    int32_t gi = (int32_t)note * 8 - 251 + (int32_t)LOG2K[k], i = gi >> 4, fr = gi & 15, db, a;
                    uint32_t ph0;
                    seed = seed * 1664525u + 1013904223u; ph0 = seed;   /* (each harmonic its own phase, the same in
                                                   every table of the part: the blend never cancels) */
                    if (gi < 0) { i = 0; fr = 0; }
                    db = i >= 48 ? E[48] : E[i] + (((E[i + 1] - E[i]) * fr) >> 4);
                    db = -db;
                    if (db < 0) db = 0;
                    if (db > 80) continue;
                    a = DBQ15[db];
                    for (j = 0; j < WT_N; j++) acc[j] += (a * sine(ph0 + ((k * j) << 24))) >> 15;
                }
                for (j = 0; j < WT_N; j++) { int32_t x = acc[j] < 0 ? -acc[j] : acc[j]; if (x > pk) pk = x; }
                {                               /* (to a peak of 26000; 32-bit, no division but this one) */
                    int32_t scl = (int32_t)((26000u << 15) / (uint32_t)pk);
                    for (j = 0; j < WT_N; j++) T[j] = (int16_t)((acc[j] * scl) >> 15);
                }
            }
}
static const uint16_t WT_KA[4] = {2566, 934, 3032, 934};   /* the breath's band (Hz -> SVF): 1100, 400, 1300, 400 */
static const uint16_t WT_NG[4] = {350, 5900, 1400, 600};   /* its level (oboe -40 dB, bassoon -34, trumpet -40: at the
                                                   measured -30 it sounded breathy, horn -44) */
static const int16_t WT_W0[4] = {0, 0, 170, 0}, WT_WS[4] = {256, 256, 771, 256};   /* the blend from the dynamic: the
                                                   trumpet's soft table is its mf, its loud f */

static void mel_start(uint32_t i, mvoice_t *v, uint32_t note, uint32_t dyn, uint32_t seq)
{
    int32_t sn = (int32_t)note + live_tr;      /* (the sound transposed live; the note kept as written, so it ends) */
    uint32_t inc = note_inc((uint32_t)(sn < 12 ? 12 : sn > 120 ? 120 : sn)), art = dyn >> 2 & 7u;
    if (ART_STR(art) && INS[i].ty != TY_STR) art = ART_NONE;   /* (only strings pluck, or bow a tremolo) */
    if (art == ART_TRILL) {                     /* a trill: the note and the one above it in the key */
        int32_t un = (int32_t)key_up(note) + live_tr;
        v->inc_lo = inc; v->inc_hi = note_inc((uint32_t)(un < 12 ? 12 : un > 120 ? 120 : un));
    }
    v->dyn = (uint8_t)(dyn & 3u);
    v->peak = peak_of(i, dyn);
    v->hold = 0;
    if (seq && sc.human) {                      /* humanise: a little louder or softer (up to +-12 %), and late (up to
                                                   5 ms a step: 30 ms at 6) */
        v->peak += (int32_t)(((int64_t)v->peak * noise() * sc.human) >> 15) / 50;   /* (up to +-12 %) */
        if (v->stage == 0 || v->env < FULL / 16) v->hold = (uint16_t)(((uint32_t)noise() & 0x7FFFu) * (sc.human * 220u) >> 15);
    }
    if (art == ART_ACC) v->peak = v->peak * 45 / 32;    /* an accent: +3 dB, and quick (below) */
    if (art == ART_MARC) v->peak = v->peak * 51 / 32;   /* marcato: +4 dB, quick, short */
    if (art == ART_PIZZ) v->peak = v->peak * 3 / 2;     /* pizzicato: a pluck carries as far as a bow stroke */
    v->art = (uint8_t)art;
    v->pl = 0;                                  /* (its players: decided as it starts, each note, never in the middle) */
    v->note = (uint8_t)note;
    v->inc = inc;
    v->seq = (uint8_t)seq;
    v->stamp = ++mv_stamps;
    hits[i]++;
    if (v->stage == 0 || v->env < FULL / 16) {  /* a fresh note */
        v->age = 0; v->cnt = 0; v->fast = 0;
        v->inc_now = INS[i].ty == TY_HORN ? inc - (inc >> 6) : INS[i].ty == TY_TPT ? inc - (inc >> 7) : inc;   /* brass
                                                   scoops up into the note (horn ~27 cents, trumpet ~13, as measured) */
        v->env2 = FULL;
    } else if (art == ART_LEG) {                /* slurred (legato): the finger (the tongue) changes the note, nothing
                                                   new begins: no dip, no new attack */
        v->inc_now = inc;
        if (v->stage == 3u) v->stage = 2;
    } else if (INS[i].ty == TY_STR) {           /* legato on a string: the finger lands at once, the bow turns (the
                                                   level dips by about half, back in ~40 ms), the vibrato goes on */
        v->env -= (v->env >> 1) + (v->env >> 4);
        v->inc_now = inc;
        v->fast = 1;
    } else {                                    /* a new breath, a new tongue */
        v->env -= v->env >> 1; v->inc_now = INS[i].ty == TY_HORN ? inc - (inc >> 7) : inc;
        v->env2 = FULL;                         /* (a new tongue: its breath and bite) */
        if (v->age > 300u) v->age = 300u;
    }
    if (art == ART_ACC || art == ART_MARC) { v->fast = 1; v->env2 = FULL; }   /* (struck: quick, with a bite) */
    if (art == ART_PIZZ) { v->env = FULL; v->env2 = FULL; v->inc_now = inc; v->fast = 0; v->age = 0; v->cnt = 0; }   /* (plucked:
                                                   at once, bright as it is plucked) */
    if (WT_TY(INS[i].ty)) {                     /* the table for this pitch: bands 4 semitones apart */
        int32_t b = (sn - (int32_t)WT_BASE[i - W_HI] + 2) / 4;
        v->band = (uint8_t)(b < 0 ? 0 : b >= (int32_t)WT_B ? WT_B - 1u : (uint32_t)b);
    }
    v->stage = art == ART_PIZZ || (art == ART_LEG && v->stage == 2u) ? 2u : 1u;
}
static void mel_off(uint32_t i, uint32_t note, uint32_t seq_only)   /* note 0: every note of the part */
{
    uint32_t k;
    for (k = 0; k < NV; k++) {
        mvoice_t *v = &mv[i][k];
        if (v->stage && v->stage < 3u && (!note || v->note == note) && (!seq_only || v->seq)) v->stage = 3;
    }
}
static int mel_sounding(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < NV; k++) if (mv[i][k].stage == 1u || mv[i][k].stage == 2u) return 1;
    return 0;
}
static void voice_off(uint32_t i)               /* a part's notes end */
{
    if (MELODIC(i)) mel_off(i, 0, 0);
}

/* ---- the live queue: notes from the keys and MIDI (main loop) to the interrupt */
static volatile uint32_t lq[32], lq_w, lq_r;
static void live(uint32_t i, uint32_t note, uint32_t dyn, uint32_t on)   /* on 0: that note ends (melodic) */
{
    uint32_t w = lq_w;
    if (w - lq_r >= 32u) return;
    lq[w % 32u] = i | note << 8 | dyn << 16 | on << 24;
    lq_w = w + 1u;
}


/* the Composer (QUARTET's SUPER SEQ): each part plays its 8 boxes of the song in a row, its pattern once through each,
 * then from the first again. A blank box goes on with the one before (A...B.A. plays AAAABBAA). X is a rest, and
 * blanks after an X rest too; so do blanks at the start. A rest lasts as long as the pattern before it. */
static uint32_t chain_pat(uint32_t i, uint32_t b, uint32_t *rest)
{
    const uint8_t *c = sc.chain[sc.comp_song % NSONG][PK(i)];
    int32_t j;
    uint32_t k;
    *rest = 1;
    for (j = (int32_t)(b % NBOX); j >= 0; j--) {
        if (c[j] == BOX_X) break;
        if (c[j] && c[j] <= NPAT) { *rest = 0; return c[j] - 1u; }
    }
    for (; j >= 0; j--) if (c[j] && c[j] <= NPAT) return c[j] - 1u;
    for (k = 0; k < NBOX; k++) if (c[k] && c[k] <= NPAT) return c[k] - 1u;
    return PSEL(i);
}
static uint32_t chain_len(void)
{
    uint32_t n = sc.clen[sc.comp_song % NSONG];
    return n >= 1u && n <= NBOX ? n : NBOX;
}
static void chain_go(uint32_t i, uint32_t b)    /* part i starts box b */
{
    uint32_t rest;
    chain_at[i] = (uint8_t)b;
    play_pat[i] = (uint8_t)chain_pat(i, b, &rest);
    if (rest) chain_rest |= 1u << i; else chain_rest &= ~(1u << i);
}
static uint32_t shape_t(uint32_t t, uint32_t sh)   /* a segment's shape, t 0..256 -> 0..256 */
{
    switch (sh & 3u) {
    case CS_IN: return t * t >> 8;
    case CS_OUT: return 256u - ((256u - t) * (256u - t) >> 8);
    case CS_SMOOTH: return (t * t * (768u - 2u * t)) >> 16;
    default: return t;
    }
}
static uint32_t curve_at(const curve_t *c, uint32_t x)   /* the curve's level (0..127) at x steps (Q8); none: 127 */
{
    uint32_t k;
    if (!c->n) return 127u;
    if (x <= c->p[0].pos * 256u) return c->p[0].val;   /* (before the first point: its level) */
    for (k = 0; k + 1u < c->n; k++) {
        uint32_t a = c->p[k].pos * 256u, b = c->p[k + 1u].pos * 256u;
        if (x < b) {
            int32_t va = c->p[k].val, vb = c->p[k + 1u].val;
            uint32_t t = b > a ? (x - a) * 256u / (b - a) : 256u;
            return (uint32_t)(va + (((vb - va) * (int32_t)shape_t(t, c->p[k].shape)) >> 8));
        }
    }
    return c->p[c->n - 1u].val;                 /* (after the last: its level, to the pattern's end) */
}

static uint32_t plen(uint32_t i, uint32_t q)    /* a pattern's length */
{
    uint32_t n = sc.len[PK(i)][q % NPAT];
    return n >= 1u && n <= NSTEP ? n : 16u;
}

static uint32_t ck_ph, ck_inc;
static int32_t ck_env;                          /* the click: a short wooden tick, its level (Q15) */
static void click(uint32_t first)
{
    ck_inc = first ? 194783679u : 136348575u;  /* (2 kHz on the bar, 1.4 kHz on the beats) */
    ck_env = (int32_t)sc.clickvol * (first ? 1900 : 1200);
}
static uint32_t step_len(void)                  /* the step just played: its length (Q8 samples), swung */
{
    uint32_t q = step_q8;
    if (sc.swing) q = (uint32_t)(((uint64_t)q * ((gstep - 1u) & 1u ? 256u - sc.swing * 14u : 256u + sc.swing * 14u)) >> 8);
    return q;
}
static void seq_step(void)                      /* every 16th: each part plays its next step */
{
    uint32_t i, k;
    if (!sc.comp_on) chain_rest = 0;
    for (i = 0; i < NINS; i++) {
        uint32_t p = pos[i], len, q;
        if (p >= plen(i, play_pat[i])) p = 0;
        if (p == 0u && end_req && !seq_first && (!sc.comp_on || chain_at[i] + 1u >= chain_len()))
            end_done |= 1u << i;                /* (the song's end: this part's last pass played) */
        if (end_done >> i & 1u) continue;       /* (done: it rests, its last notes ringing on) */
        if (p == 0u) {                          /* round to step 1: the song's next box, or the pattern chosen */
            if (sc.comp_on) chain_go(i, seq_first ? 0u : (chain_at[i] + 1u) % chain_len());
            else {
                uint32_t r = PK(i);
                if (rec_next[r] < NPAT) { sc.psel[r] = rec_next[r]; rec_next[r] = 0xFFu; }   /* (recording: on to the next) */
                chain_at[i] = NBOX - 1u; play_pat[i] = (uint8_t)(PSEL(i));
            }
        }
        q = play_pat[i] % NPAT;
        len = plen(i, q);
        cur[i] = (uint8_t)p;
        pos[i] = (uint8_t)(p + 1u >= len ? 0u : p + 1u);
        if ((MELODIC(i) ? sc.pmute[i][q] : sc.mute[i]) || chain_rest >> i & 1u) {
            if (MELODIC(i)) { mel_off(i, 0, 1); mv_left[i] = 0; }
            continue;
        }
        if (MELODIC(i)) {
            const mstep_t *ms = &sc.mel[i][q][p];
            if (ms->n[0] | ms->n[1] | ms->n[2]) {   /* a chord: the last one's notes end, these begin. Winds and brass:
                                                   a note that follows another takes its voice (tongued legato). Strings:
                                                   as from the keys, the last notes ring on in their release while these
                                                   start on voices of their own (taking theirs cut each note short) */
                mvoice_t *was[NV];
                uint32_t nw = 0, art = ms->dyn >> 2 & 7u, take = INS[i].ty != TY_STR || art == ART_LEG;
                for (k = 0; k < NV; k++)
                    if (mv[i][k].seq && mv[i][k].stage && mv[i][k].stage < 3u) { if (take) was[nw++] = &mv[i][k]; mv[i][k].stage = 3; }
                for (k = 0; k < NV; k++)
                    if (ms->n[k]) {
                        mvoice_t *v = nw ? was[--nw] : mel_pick(i, ms->n[k]);
                        mel_start(i, v, ms->n[k], ms->dyn, 1);
                    }
                {
                    uint32_t L = ms->len ? ms->len : 1u;
                    mv_left[i] = (uint8_t)(art == ART_STAC ? (L + 1u) / 2u : art == ART_MARC ? (3u * L + 3u) / 4u : art == ART_LEG ? L + 1u : L);
                    if (end_req && (!sc.comp_on || chain_at[i] + 1u >= chain_len())) {   /* the song ending: its last notes
                                                   held on, to the end and over it */
                        uint32_t j, more = 0;
                        for (j = p + 1u; j < len; j++) if (sc.mel[i][q][j].n[0] | sc.mel[i][q][j].n[1] | sc.mel[i][q][j].n[2]) { more = 1; break; }
                        if (!more) mv_left[i] = 0;
                    }
                }
            } else if (mv_left[i] && --mv_left[i] == 0u)
                mel_off(i, 0, 1);               /* the notes have lasted as long as they should */
        } else if (sc.drum[i - P_BD][q][p]) {
            voice_on(i, 0, sc.drum[i - P_BD][q][p] - 1u);
            if (sc.human) vo[i].peak += (int32_t)(((int64_t)vo[i].peak * noise() * sc.human) >> 15) / 50;
        }
    }
    if (gstep % BEAT == 0u && (sc.click == CLK_ALWAYS || (sc.click == CLK_REC && rec_live)))
        click(gstep % BAR == 0u);               /* the click: on the beat, the bar's first louder */
    seq_first = 0;
    gstep++;
}

static void env_adsr(mvoice_t *v, const ins_t *I)  /* once a piece */
{
    if (v->stage == 1) {
        v->env += (int32_t)ATK[v->fast && I->atk > 12u ? 12u : I->atk];   /* (a bow change, an accent: ~40 ms) */
        if (v->env >= FULL) { v->env = FULL; v->stage = 2; v->fast = 0; }
    } else if (v->stage == 2 && v->art == ART_PIZZ) {   /* plucked: it dies away by itself, held or not */
        v->env = (int32_t)(((int64_t)v->env * DEC[I->rel - 3u]) >> 24);
        if (v->env < 2048) { v->env = 0; v->stage = 0; }
    } else if (v->stage == 2) {
        int32_t s = (int32_t)I->sus << 16;      /* /255 -> Q24 (near enough) */
        v->env = s + (int32_t)(((int64_t)(v->env - s) * DEC[I->dec]) >> 24);
    } else if (v->stage == 3) {
        v->env = (int32_t)(((int64_t)v->env * DEC[v->art == ART_STAC && I->rel > 8u ? I->rel - 8u : v->art == ART_PIZZ ? I->rel - 3u : I->rel]) >> 24);   /* (staccato:
                                                   let go quickly) */
        if (v->env < 2048) { v->env = 0; v->stage = 0; }
    }
}
static int32_t fall(int32_t e, uint32_t d) { return (int32_t)(((int64_t)e * DEC[d]) >> 24); }

/* ---- a section of players: three of them on every melodic part, each a little out of tune with the others, with
 * vibrato of its own, coming in a few ms apart, seated a little left, centre, right */
static const uint32_t DETUNE[5][3] = {          /* Q16: strings, (unused), reeds, horns, trumpets (closest: two apart
                                                   sounded like a second note) */
    {65196u, 65612u, 65916u}, {65309u, 65536u, 65726u}, {65347u, 65574u, 65764u}, {65272u, 65574u, 65840u},
    {65423u, 65536u, 65649u}};
static const uint32_t LFO_R[3] = {15582648u, 16985086u, 18387524u};   /* 5.0, 5.45, 5.9 Hz, per 32 samples */
static const uint32_t STR_LFO[2][3] = {         /* the strings' vibrato (a violin measured at 6.7-7.9 Hz) */
    {20569094u, 21503941u, 22438012u},          /* violins 6.6 .. 7.2 Hz */
    {17452566u, 18075870u, 18699177u}};         /* cellos 5.6 .. 6.0 Hz */
static const uint32_t DLY[3] = {0, 280, 600};   /* when each player comes in (samples) */
#ifndef ENS_PLAYERS
#define ENS_PLAYERS 3u                          /* players a part: 1 or 2 if the FM-1 runs short of time */
#endif
#define G_PAIR 15500                            /* each of a section's two players */
#define G_CHORD 17000                           /* a chord's notes: one player each */
#define G_SOLO 19000                            /* a solo player: a little louder than one of a section */

static void render_mel(uint32_t i, int32_t *out, uint32_t n)
{
    voice_t *P = &vo[i];                        /* the part: its filter, body and vibrato */
    const ins_t *I = &INS[i];
    uint32_t ty = I->ty, g = ty == TY_STR ? 0u : ty == TY_TPT ? 4u : ty == TY_HORN ? 3u : 2u;
    uint32_t nact = 0, k, f, p, f0 = 0, dyn = 0;
    uint32_t pl, pa, pb, live_n = 0;            /* players a voice: one (a chord, SOLO) or two (a section) */
    int32_t k1 = 0, emax = 0, e2max = 0, br, amax = 0;   /* amax: the loudest note's level (Q15): the noises' */
    mvoice_t *act[NV];
    uint32_t ip[NV][2];
    int32_t a[NV], da[NV], wv[NV];
    uint32_t plk[NV];                           /* each voice's players, read once a block (not each sample) */                 /* wv: the winds, brass: soft -> loud table (0..256) */
    int32_t g0 = P->amp, g1, dg, xe = eff[i];   /* the section's gain (Q12): glides to what it asks, a piece at a time */
    static const int16_t VIBM[4] = {0, 150, 330, 620};   /* the section's vibrato: off, light, normal, wide (normal a
                                                   little more than as first designed; the steps plainly apart) */
    uint32_t pq = play_pat[i] % NPAT, solo = sc.psolo[i][pq];   /* (the playing pattern's vibrato and players) */
    int32_t vd = ((int32_t)I->vib * VIBM[sc.pvib[i][pq] & 3u]) >> 8;   /* (the top bit: the mute) */
    for (k = 0; k < NV; k++) if (mv[i][k].stage) act[nact++] = &mv[i][k];
    for (p = 0; p < 3u; p++) P->lfo[p] += ty == TY_STR ? STR_LFO[i == S_LO][p] : LFO_R[p];
    g1 = g0 + ((gain_t[i] - g0) >> 3);          /* (~6 ms: a swell, never a jump) */
    P->amp = g1;
    dg = (g1 - g0) / (int32_t)n;
    if (!nact) return;
    for (k = 0; k < nact; k++) if (act[k]->stage < 3u) live_n++;   /* (the notes sounding, not those fading out) */
    for (k = 0; k < nact; k++)                  /* a new note: a section if it sounds alone, else one player; kept */
        if (!act[k]->pl) act[k]->pl = (uint8_t)(solo || live_n > 1u ? 1u : 2u);
    for (k = 0; k < nact; k++) {                /* each voice: its envelope, pitch, vibrato, level */
        mvoice_t *v = act[k];
        int32_t depth = 0, a1, gv;
        uint32_t inc;
        pl = v->pl ? v->pl : 1u; pa = pl == 2u ? 0u : 1u; pb = pl == 2u ? 2u : 1u; plk[k] = pl;
        gv = pl == 2u ? G_PAIR : solo ? G_SOLO : G_CHORD;
        if (v->hold) v->hold = (uint16_t)(v->hold > n ? v->hold - n : 0u);   /* (humanise: not yet) */
        else env_adsr(v, I);
        if (v->art == ART_TRILL) {              /* a trill: the two notes in turn, ~12 a second (violins, oboes), 10
                                                   (trumpets), 8 (cellos, bassoons, horns) */
            uint32_t per = i == S_HI || i == W_HI ? 3675u : i == B_HI ? 4410u : 5512u;
            v->inc = (v->cnt / per) & 1u ? v->inc_hi : v->inc_lo;
        }
        v->env2 = fall(v->env2, v->art == ART_PIZZ ? 3u : 18u);   /* (a pluck's bright edge: gone in ~20 ms) */
        v->inc_now += (uint32_t)((int32_t)(v->inc - v->inc_now) >> 4);
        inc = v->inc_now;
        v->age++;
        if (vd && v->art != ART_PIZZ) {         /* vibrato, fading in (strings from ~100 ms, the rest ~250 ms; none plucked) */
            int32_t t0 = ty == TY_STR ? 140 : ty == TY_TPT ? 1240 : ty == TY_OBOE ? 300 : 340;   /* (the trumpet: only
                                                   on a long note, as in the scale) */
            int32_t t1 = ty == TY_STR ? 360 : ty == TY_TPT ? 1650 : 700;
            depth = (int32_t)v->age >= t1 ? vd : (int32_t)v->age < t0 ? 0 : vd * ((int32_t)v->age - t0) / (t1 - t0);
        }
        for (p = 0; p < pl; p++) {
            uint32_t pi = p ? pb : pa, q = (uint32_t)(((uint64_t)inc * DETUNE[g][pi]) >> 16);
            if (depth) q += (uint32_t)(((int64_t)q * (sine(P->lfo[pi]) * depth >> 15)) >> 12);
            ip[k][p] = q;
        }
        if (pl == 2u && v->cnt < (ty == TY_TPT ? 90u : DLY[2])) ip[k][1] = 0;   /* (the second player comes in a moment
                                                   later; the trumpets together, within 2 ms) */
        if (v->cnt < (1u << 24)) v->cnt += n;
        a1 = (int32_t)(((int64_t)(v->env >> 9) * v->peak) >> 15);
        if (v->art == ART_TREM) {               /* tremolo: 15 bow strokes a second, the level dipping at each change */
            uint32_t ps = v->cnt % 2940u;
            if (ps < 640u) a1 = (int32_t)(((int64_t)a1 * (150 + (int32_t)ps * 106 / 640)) >> 8);
            if (ps < n) v->env2 = FULL / 2;     /* (each stroke's bite) */
        }
        if (ty == TY_STR && depth)              /* the level shimmers a little with the vibrato */
            a1 += (int32_t)(((int64_t)a1 * ((sine(P->lfo[pa]) * depth) >> 7)) >> 15);
        if (a1 > amax && v->art != ART_PIZZ) amax = a1;   /* (the noises: by the note's own level; a pluck has no bow) */
        a1 = (int32_t)(((int64_t)a1 * gv) >> 15);   /* (its players' level: a section's two, or one) */
        a[k] = v->amp; da[k] = (a1 - v->amp) / (int32_t)n; v->amp = a1;
        if (WT_TY(ty)) {                        /* the tone: its dynamic and the section's expression; through the
                                                   attack it opens from soft */
            int32_t w = (((int32_t)v->dyn * 85 + xe - 80 - WT_W0[i - W_HI]) * WT_WS[i - W_HI]) >> 8;
            w = w < 0 ? 0 : w > 256 ? 256 : w;
            if (v->stage == 1u && ty != TY_TPT) w = (w * (v->env >> 16)) >> 8;   /* (the trumpet keeps its tone from
                                                   the start: opening, it sounded like a second note coming in) */
            wv[k] = w;
        }
        if (v->env > emax) { emax = v->env; f0 = (uint32_t)(((uint64_t)inc * 44100u) >> 32); dyn = v->dyn; }
        if (v->env2 > e2max) e2max = v->env2;
        if (v->stage == 0u) { v->amp = 0; v->pl = 0; }
    }
    {                                           /* the part's tone: follows its loudest note and the dynamic; brass opens
                                                   as it swells */
        uint32_t fc = I->cut;
        if (fc)
            fc += 2u * f0 + ((uint32_t)(I->cut_env * (uint32_t)DYN[dyn] >> 15) * (uint32_t)(48 + xe) >> 7);
        if (fc) k1 = lp_k(fc);
    }
    br = e2max >> 10;
    if (ty == TY_STR) {                         /* a bowed string, from a violin's measurements: the saws through the
                                                   instrument's body (bridge peak, body resonance, the top cut, the lowest
                                                   notes' fundamental thinned; low notes darker) and a little bow noise */
        static const uint16_t BODY[2][5] = {
            {4600, 2000, 480, 350, 300},        /* violins: the top cut, the bridge, the body, the low cut (Hz); noise */
            {2600, 1250, 240, 95, 220}};        /* cellos & basses */
        const uint16_t *B = BODY[i == S_LO];
        int32_t fa = B[0] * 2334 / 1000, fb = B[1] * 2334 / 1000, fcb = B[2] * 2334 / 1000, kh = lp_k(B[3]);
        int32_t bow = ((e2max >> 14) + B[4]) * (64 + xe) >> 7;   /* (more bow, and brighter, as it swells) */
        uint32_t ft = (f0 * 4u + 400u) * (uint32_t)(56 + xe) / 136u;
        int32_t kt = lp_k(ft > 6500u ? 6500u : ft);
        for (f = 0; f < n; f++) {
            int32_t x = 0, hi, y, *b = P->bd;
            for (k = 0; k < nact; k++) {
                int32_t s2 = 0;
                mvoice_t *v = act[k];
                if (v->art == ART_PIZZ) {       /* plucked: round (a sine) under a bright edge that fades at once */
                    int32_t br2 = v->env2 >> 9;
                    s2 = sine(v->ph[0]) + ((saw(v->ph[0], ip[k][0]) * br2) >> 15); v->ph[0] += ip[k][0];
                    if (plk[k] == 2u && ip[k][1]) { s2 += sine(v->ph[1]) + ((saw(v->ph[1], ip[k][1]) * br2) >> 15); v->ph[1] += ip[k][1]; }
                } else {
                    s2 = saw(v->ph[0], ip[k][0]); v->ph[0] += ip[k][0];
                    if (plk[k] == 2u && ip[k][1]) { s2 += saw(v->ph[1], ip[k][1]); v->ph[1] += ip[k][1]; }
                }
                x += (s2 * a[k]) >> 15;
                a[k] += da[k];
            }
            x = x + ((((noise() * bow) >> 15) * amax) >> 15);   /* (the bow noise: as loud as the note) */
            P->lp3 += ((x - P->lp3) * kt) >> 14; x = P->lp3;
            b[0] += (fa * b[1]) >> 14;
            hi = x - b[0] - ((b[1] * 11700) >> 13);
            b[1] += (fa * hi) >> 14;
            y = b[0];
            b[2] += (fb * b[3]) >> 14;
            hi = y - b[2] - ((b[3] * 5460) >> 13);
            b[3] += (fb * hi) >> 14;
            b[4] += (fcb * b[5]) >> 14;
            hi = y - b[4] - ((b[5] * 4096) >> 13);
            b[5] += (fcb * hi) >> 14;
            y += ((b[3] * 34) >> 5) + ((b[5] * 20) >> 5);
            P->lp1 += ((y - P->lp1) * kh) >> 14; y -= P->lp1;
            P->lp2 += ((y - P->lp2) * kh) >> 14; y -= P->lp2;
            if (b[0] > 131071) b[0] = 131071; else if (b[0] < -131071) b[0] = -131071;
            y = (y * (g0 >> 2)) >> 10; g0 += dg;   /* (the section's expression and balance) */
            out[2u * f] += (y * I->gl) >> 15;
            out[2u * f + 1u] += (y * I->gr) >> 15;
        }
        return;
    }
    for (f = 0; f < n; f++) {
        int32_t x = 0;
        for (k = 0; k < nact; k++) {
            mvoice_t *v = act[k];
            int32_t s2 = 0;
            for (p = 0; p < plk[k]; p++) {
                uint32_t ph = v->ph[p], q = ip[k][p];
                if (!q) continue;
                {                               /* the band's two tables, each read between samples, blended */
                    const int16_t *t0 = wt[i - W_HI][v->band][0], *t1 = wt[i - W_HI][v->band][1];
                    uint32_t j = ph >> 24, j1 = (j + 1u) & (WT_N - 1u);
                    int32_t fr = (int32_t)((ph >> 9) & 0x7FFFu);
                    int32_t lo = t0[j] + (((t0[j1] - t0[j]) * fr) >> 15), hi = t1[j] + (((t1[j1] - t1[j]) * fr) >> 15);
                    s2 += lo + (((hi - lo) * wv[k]) >> 8);
                }
                v->ph[p] = ph + q;
            }
            x += (s2 * a[k]) >> 15;
            a[k] += da[k];
        }
        if (WT_TY(ty)) {                        /* breath, the reed's buzz: noise in a band at the formant, as loud
                                                   (against the tone) as measured, more as a note is tongued */
            int32_t ka = WT_KA[i - W_HI], hp = (noise() >> 2) - P->lp1 - P->lp2;
            P->lp1 += (ka * P->lp2) >> 14;
            P->lp2 += (ka * hp) >> 14;
            x += (((P->lp2 * ((int32_t)WT_NG[i - W_HI] * (16384 + 2 * br) >> 14)) >> 15) * amax) >> 15;
        } else if (k1) {
            P->lp1 += ((x - P->lp1) * k1) >> 14; P->lp2 += ((P->lp1 - P->lp2) * k1) >> 14; x = P->lp2;
        }
        x = (x * (g0 >> 2)) >> 10; g0 += dg;   /* (the section's expression and balance) */
        out[2u * f] += (x * I->gl) >> 15;
        out[2u * f + 1u] += (x * I->gr) >> 15;
    }
}

static void render_voice(uint32_t i, int32_t *out, uint32_t n)   /* n <= CH frames, added to out (stereo) */
{
    voice_t *v = &vo[i];
    const ins_t *I = &INS[i];
    int32_t a0 = v->amp, a1, da, a;
    uint32_t f, inc;
    if (MELODIC(i)) { render_mel(i, out, n); return; }   /* (its voices: mv[i]) */
    if (!v->stage) return;
    inc = 0;
    if (DTY(i).ty == TY_TIMP || DTY(i).ty == TY_BD || DTY(i).ty == TY_TOM) {
        v->inc_now += (uint32_t)((int32_t)(v->inc - v->inc_now) >> 5);
        inc = v->inc_now;
    }
    a1 = (int32_t)(((int64_t)(v->env >> 9) * v->peak) >> 15);
    da = (a1 - a0) / (int32_t)n;
    a = a0;

    switch (DTY(i).ty) {
    case TY_BD: {                               /* a deep round boom and the felt beater's thump */
        static int32_t kth;
        int32_t th = v->env3 >> 9;
        if (!kth) kth = lp_k(260);
        for (f = 0; f < n; f++) {
            int32_t s = (sine(v->ph[0]) * a) >> 15;
            v->ph[0] += inc;
            if (th > 64) {                      /* (the thump: only while it is there) */
                v->lp1 += ((noise() - v->lp1) * kth) >> 14;
                s += (((v->lp1 * th) >> 15) * v->peak) >> 14;
            }
            out[2u * f] += (s * I->gl) >> 15; out[2u * f + 1u] += (s * I->gr) >> 15;
            a += da;
        }
        v->env3 = fall(v->env3, 9);
        break;
    }
    case TY_CYM: {                              /* six clashing squares and noise, high-passed: brass plates */
        static const uint32_t CI[6] = {52591436u, 77913239u, 100800253u, 121739436u, 158748224u, 204522252u};
        static int32_t khp;
        int32_t burst = v->env2 >> 9;
        if (!khp) khp = lp_k(3500);
        for (f = 0; f < n; f++) {
            uint32_t m;
            int32_t s = 0, x;
            for (m = 0; m < 6u; m++) { v->ph[m] += CI[m]; s += (int32_t)(v->ph[m] >> 31) ? 2600 : -2600; }
            x = s + (noise() >> 1);
            v->lp1 += ((x - v->lp1) * khp) >> 14;   x -= v->lp1;    /* two high-passes */
            v->lp2 += ((x - v->lp2) * khp) >> 14;   x -= v->lp2;
            s = (x * (a + ((burst * v->peak) >> 16))) >> 15;
            out[2u * f] += (s * I->gl) >> 15; out[2u * f + 1u] += (s * I->gr) >> 15;
            a += da;
        }
        v->env2 = fall(v->env2, 21);
        break;
    }
    case TY_TRI: {                              /* a bright, long, inharmonic ring and the beater's tick */
        static const uint32_t TI[2] = {126609013u, 343110424u};   /* 1300 Hz * 1, 2.71 */
        int32_t up = v->env2 >> 9, tick = v->env3 >> 9;
        for (f = 0; f < n; f++) {
            int32_t s = sine(v->ph[0]) >> 1;
            v->ph[0] += TI[0];
            if (up > 64) { s += ((sine(v->ph[1]) >> 2) * up) >> 15; v->ph[1] += TI[1]; }   /* (the high partial a
                                                   quarter: it was shrill) */
            s = (s * a) >> 15;
            if (tick > 64) s += (((noise() * tick) >> 15) * v->peak) >> 17;
            out[2u * f] += (s * I->gl) >> 15; out[2u * f + 1u] += (s * I->gr) >> 15;
            a += da;
        }
        v->env2 = fall(v->env2, 36); v->env3 = fall(v->env3, 0);
        break;
    }
    case TY_SN: {                               /* side drums: two skin tones and the snares' rattle; the second
                                                   drummer plays it 9 ms later, a little softer, to the right */
        static int32_t kl, kh;
        static int16_t dls[NDRUM][512];
        static uint32_t dis[NDRUM];
        int16_t *dl = dls[i - P_BD];
        uint32_t di = dis[i - P_BD];
        int32_t tone = v->env2 >> 9;
        if (!kl) { kl = lp_k(7000); kh = lp_k(1200); }
        for (f = 0; f < n; f++) {
            int32_t s, x = noise(), e;
            v->lp1 += ((x - v->lp1) * kl) >> 14;
            v->lp2 += ((v->lp1 - v->lp2) * kh) >> 14;
            x = v->lp1 - v->lp2;
            if (tone > 64) {
                v->ph[0] += 18504394u; v->ph[1] += 32139211u;
                x += ((((sine(v->ph[0]) + (sine(v->ph[1]) >> 1)) >> 1) * tone) >> 15) >> 1;
            }
            s = (x * a) >> 15;
            e = dl[(di - 397u) & 511u];         /* the other drummer */
            dl[di & 511u] = (int16_t)(s > 32767 ? 32767 : s < -32767 ? -32767 : s);
            di++;
            out[2u * f] += ((s * I->gl) >> 15) + ((e * I->gl) >> 16);
            out[2u * f + 1u] += ((s * I->gr) >> 15) + ((e * I->gr * 3) >> 17);
            a += da;
        }
        dis[i - P_BD] = di;
        v->env2 = fall(v->env2, 6);
        break;
    }
    case TY_CLASH: {                            /* clash cymbals (from a recording): the plates meeting (a low knock), a
                                                   bloom peaking ~120 ms in, centred 3-6 kHz, then a long wash darkening
                                                   as it fades (-10 dB at 0.3 s, -30 at 1.6 s, -40 at 2.9 s) */
        static const uint32_t CC[6] = {167277000u, 228190000u, 270264000u, 297825000u, 344379000u, 455014000u};   /* the
                                                   recording's partials: 1.72 2.34 2.78 3.06 3.54 4.67 kHz */
        static int32_t khp;
        int32_t bloom = v->env3 >> 9, crash = v->env2 >> 9, kd;
        if (!khp) khp = lp_k(2200);
        kd = lp_k(4500u + ((uint32_t)(v->env >> 12) * 7000u >> 12));   /* (the top falls with the level) */
        for (f = 0; f < n; f++) {
            uint32_t m;
            int32_t s = 0, x, g;
            for (m = 0; m < 6u; m++) { v->ph[m] += CC[m]; s += (int32_t)(v->ph[m] >> 31) ? 1700 : -1700; }
            x = s + (noise() * 3 >> 2);
            v->lp1 += ((x - v->lp1) * kd) >> 14; x = v->lp1;
            v->lp2 += ((x - v->lp2) * khp) >> 14; x -= v->lp2;
            v->lp3 += ((noise() - v->lp3) * 1500) >> 14; v->lp4 += ((v->lp3 - v->lp4) * 450) >> 14;
            x += (v->lp3 - v->lp4) >> 2;        /* (a little body, ~250-700 Hz) */
            g = (a * 16384 + ((crash * v->peak) >> 15) * 16384) >> 15;   /* the wash and the crash, half each */
            g = (g * (32768 - ((bloom * 29491) >> 15))) >> 15;            /* ... blooming in over ~120 ms */
            s = (x * g) >> 15;
            if (bloom > 2000) {                 /* the knock: 400 Hz as the plates meet */
                v->inc += 38956000u;
                s += (((sine(v->inc) * ((bloom * bloom) >> 15)) >> 15) * v->peak) >> 18;
            }
            out[2u * f] += (s * I->gl) >> 15; out[2u * f + 1u] += (s * I->gr) >> 15;
            a += da;
        }
        v->env2 = fall(v->env2, 23); v->env3 = fall(v->env3, 13);
        break;
    }
    case TY_SN2: {                              /* a snare played hard (from a recording): a crack, two shell tones (233,
                                                   433 Hz) and the snares' rattle (0.8-5 kHz), gone in a fifth of a second;
                                                   one drummer */
        static int32_t kl2, kh2;
        int32_t tone = v->env2 >> 9, crack = v->env3 >> 9;
        if (!kl2) { kl2 = lp_k(3500); kh2 = lp_k(600); }
        for (f = 0; f < n; f++) {
            int32_t s, x = noise();
            v->lp1 += ((x - v->lp1) * kl2) >> 14;
            v->lp2 += ((v->lp1 - v->lp2) * kh2) >> 14;
            x = (v->lp1 - v->lp2) * 3 >> 1;     /* (the snares: as loud as the shell at first, as recorded) */
            if (tone > 64) {
                v->ph[0] += 22691000u; v->ph[1] += 42170000u;
                x += (((sine(v->ph[0]) + (sine(v->ph[1]) * 3 >> 2)) * tone) >> 16);
            }
            s = (x * (a >> 2)) >> 13;           /* (32-bit headroom at its level) */
            if (crack > 64) s += (((noise() * crack) >> 15) * v->peak) >> 16;
            out[2u * f] += (s * I->gl) >> 15; out[2u * f + 1u] += (s * I->gr) >> 15;
            a += da;
        }
        v->env2 = fall(v->env2, 13); v->env3 -= v->env3 >> 2;
        break;
    }
    case TY_TOM: {                              /* a tom (from a recording): a tuned skin, a little sharp as struck and
                                                   settling, two overtones (x1.56, x2.14) gone at once, the stick's click;
                                                   -20 dB in a thirtieth of a second */
        static int32_t ks;
        uint32_t i1 = mulq10(inc, 1597u), i2 = mulq10(inc, 2191u), i3 = mulq10(inc, 2867u);
        int32_t up = v->env2 >> 9, stk = v->env3 >> 9, fa = (up * v->peak) >> 15;   /* fa: the fast layer's level */
        if (!ks) ks = lp_k(2500);
        for (f = 0; f < n; f++) {
            int32_t s = sine(v->ph[0]), o = 0;
            v->ph[0] += inc;
            o = (sine(v->ph[1]) * 6 + sine(v->ph[2]) * 4 + sine(v->ph[3]) * 6) >> 4;   /* the overtones: x1.56, 2.14, 2.8 */
            v->ph[1] += i1; v->ph[2] += i2; v->ph[3] += i3;
            s = ((s * ((a * 11469 + fa * 21299) >> 15)) >> 15) + ((o * (fa + (a >> 1))) >> 15);   /* tail 0.35, fast 0.65;
                                                   the overtones in both */
            if (stk > 64) {
                v->lp1 += ((noise() - v->lp1) * ks) >> 14;
                s += (((v->lp1 * stk) >> 15) * v->peak) >> 15;
            }
            out[2u * f] += (s * I->gl) >> 15; out[2u * f + 1u] += (s * I->gr) >> 15;
            a += da;
        }
        v->env2 = fall(v->env2, 0); v->env3 -= v->env3 >> 2;
        break;
    }
    case TY_TIMP: {                             /* a tuned kettle drum: its principal tone, the skin's overtones, the mallet */
        static int32_t km;
        uint32_t i1 = mulq10(inc, 1540u), i2 = mulq10(inc, 2048u), i3 = mulq10(inc, 2560u);
        int32_t up = v->env2 >> 9, mal = v->env3 >> 9;
        if (!km) km = lp_k(500);
        for (f = 0; f < n; f++) {
            int32_t s = sine(v->ph[0]);
            v->ph[0] += inc;
            if (up > 64) {                      /* (the overtones, which give a timpano its note: x1.5, 2, 2.5) */
                s += (((sine(v->ph[1]) * 9 + sine(v->ph[2]) * 7 + sine(v->ph[3]) * 4) >> 4) * up) >> 15;
                v->ph[1] += i1; v->ph[2] += i2; v->ph[3] += i3;
            }
            s = (s * a) >> 15;
            if (mal > 64) {                     /* (the mallet: softer, so the note comes through) */
                v->lp1 += ((noise() - v->lp1) * km) >> 14;
                s += (((v->lp1 * mal) >> 15) * v->peak) >> 15;
            }
            out[2u * f] += (s * I->gl) >> 15; out[2u * f + 1u] += (s * I->gr) >> 15;
            a += da;
        }
        v->env2 = fall(v->env2, 38); v->env3 = fall(v->env3, 3);
        break;
    }
    }
    v->amp = a1;
    if (!MELODIC(i)) {                          /* percussion rings out on its own */
        v->env = fall(v->env, DTY(i).dec);
        if (a1 < 12 && v->env2 < FULL / 2048 && v->env3 < FULL / 2048) {
            v->env = 0; v->amp = 0; v->stage = 0;   /* under hearing */
        }
    }
    if (v->cnt < (1u << 24)) v->cnt += n;
}

/* ---- the hall: four combs and two all-passes a side (Freeverb's shape, small) */
#define RV_C0 778u                              /* (the hall runs at 22 kHz: half Freeverb's lengths, the same times) */
#define RV_C1 808u
#define RV_C2 745u
#define RV_C3 711u
static int16_t rv_c0[RV_C0] __attribute__((section(".pool")));
static int16_t rv_c1[RV_C1] __attribute__((section(".pool")));
static int16_t rv_c2[RV_C2] __attribute__((section(".pool")));
static int16_t rv_c3[RV_C3] __attribute__((section(".pool")));
static int16_t rv_al0[278], rv_al1[220], rv_ar0[290], rv_ar1[232];
static int32_t rv_pl, rv_pr;
static uint32_t rv_i0, rv_i1, rv_i2, rv_i3, rv_j0, rv_j1, rv_j2, rv_j3;
static int32_t rv_f0, rv_f1, rv_f2, rv_f3;

#define NHALL 4u
static const struct { const char *name; int32_t fb, damp, wet, dry; } HALLS[NHALL] = {   /* (~0.4, 1.0, 2.1, 4.8 s) */
    {"STUDIO", 19500, 10000, 1500, 32767}, {"CHAMBER", 25500, 14000, 5500, 30000},
    {"CONCERT HALL", 29000, 18000, 10000, 26000}, {"CATHEDRAL", 31200, 22500, 15000, 21000}};
static volatile int32_t rv_fb = 29000, rv_damp = 18000, rv_dry = 26000;
static int32_t shi_m[256];                      /* the high strings alone (L+R), this block: they send the hall less */
#define SHI_LESS 128                            /* (half of theirs held back: they rang too long) */
static int16_t sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32767 ? -32767 : x); }
static int32_t comb(int16_t *b, uint32_t *ix, uint32_t len, int32_t *flt, int32_t x)
{
    int32_t y = b[*ix];
    *flt += ((y - *flt) * rv_damp) >> 15;       /* the damping: walls soak up the highs */
    b[*ix] = sat16(x + ((*flt * rv_fb) >> 15)); /* the feedback: the hall's size */
    if (++*ix >= len) *ix = 0;
    return y;
}
static int32_t allpass(int16_t *b, uint32_t *ix, uint32_t len, int32_t x)
{
    int32_t y = b[*ix];
    b[*ix] = sat16(x + (y >> 1));
    if (++*ix >= len) *ix = 0;
    return y - x;
}
static void hall(int32_t *out, uint32_t n)
{
    int32_t g = hall_q15, d = rv_dry;
    uint32_t f;
    if (!g) return;
    d >>= 7;                                    /* (the direct sound, 0..255: further back in a bigger room) */
    for (f = 0; f + 1u < n; f += 2u) {          /* every other sample (the tail is soft: half the work) */
        int32_t x = (out[2u * f] + out[2u * f + 1u] + out[2u * f + 2u] + out[2u * f + 3u]
                     - (((shi_m[f] + shi_m[f + 1u]) * SHI_LESS) >> 8)) >> 5, c, l, r;
        if (d < 255) {                          /* (the hall hears it all; then the direct sound set back) */
            out[2u * f] = (out[2u * f] * d) >> 8; out[2u * f + 1u] = (out[2u * f + 1u] * d) >> 8;
            out[2u * f + 2u] = (out[2u * f + 2u] * d) >> 8; out[2u * f + 3u] = (out[2u * f + 3u] * d) >> 8;
        }
        c = comb(rv_c0, &rv_i0, RV_C0, &rv_f0, x) + comb(rv_c1, &rv_i1, RV_C1, &rv_f1, x)
          + comb(rv_c2, &rv_i2, RV_C2, &rv_f2, x) + comb(rv_c3, &rv_i3, RV_C3, &rv_f3, x);
        c >>= 1;
        l = (allpass(rv_al1, &rv_j1, 220u, allpass(rv_al0, &rv_j0, 278u, c)) * g) >> 13;
        r = (allpass(rv_ar1, &rv_j3, 232u, allpass(rv_ar0, &rv_j2, 290u, c)) * g) >> 13;
        out[2u * f] += (l + rv_pl) >> 1; out[2u * f + 1u] += (r + rv_pr) >> 1;   /* (between the last and this) */
        out[2u * f + 2u] += l; out[2u * f + 3u] += r;
        rv_pl = l; rv_pr = r;
    }
}

static void sordino(uint32_t i, int32_t *b, uint32_t n)   /* con sordino: the strings veiled (darker, softer); the
                                                   trumpets thin and nasal; the horns distant */
{
    static int32_t lp[P_BD][2], hp[P_BD][2], ks[P_BD][2];
    uint32_t f, c, ty = INS[i].ty;
    if (!ks[i][0]) {
        ks[i][0] = lp_k(ty == TY_STR ? (i == S_LO ? 900u : 1600u) : ty == TY_HORN ? 1300u : 3600u);
        ks[i][1] = lp_k(ty == TY_TPT ? 650u : 60u);
    }
    for (f = 0; f < 2u * n; f++) {
        int32_t x = b[f];
        c = f & 1u;
        lp[i][c] += ((x - lp[i][c]) * ks[i][0]) >> 14; x = lp[i][c];
        hp[i][c] += ((x - hp[i][c]) * ks[i][1]) >> 14; x -= hp[i][c];
        b[f] = ty == TY_STR ? (x * 5) >> 3 : ty == TY_TPT ? (x * 3) >> 2 : (x * 9) >> 4;   /* (-4, -2.5, -5 dB) */
    }
}
static int32_t cin_t;                           /* the count-in's own clock (Q8 samples to its next step) */
static uint32_t cin_done;                       /* (counted in once for this PLAY) */
static void app_audio(int32_t *out, uint32_t n)
{
    int32_t *o = out;
    uint32_t left = n, i, f;
    {                                           /* expression: the section's x its pattern's curve (where the music is in
                                                   it); x the balance: the gain each section asks for, this block */
        int32_t ts = to_step;
        uint32_t sq = (step_q8 >> 8) | 1u, fr = running && ts > 0 ? 256u - ((uint32_t)ts / sq > 256u ? 256u : (uint32_t)ts / sq) : 0u, r;
        for (r = 0; r < NROW; r++) {
            uint32_t pt = r, q = play_pat[pt] % NPAT, c = 127u, e;
            const curve_t *cv = &sc.curve[r][q];
            if (cv->on && cv->n && r != xrec_row) c = curve_at(cv, running ? cur[pt] * 256u + fr : 0u);
            e = (uint32_t)sc.pexpr[r][q] * c / 127u;   /* (the playing pattern's) */
            eff[r] = (uint8_t)e;
            gain_t[r] = (int32_t)(((uint32_t)EXPR_G[e > 127u ? 127u : e] * BAL_G[(uint32_t)(sc.pbal[r][q] + 30) % 37u]) >> 12);
        }
    }
    while (lq_r != lq_w) {                      /* the keys and MIDI first */
        uint32_t e = lq[lq_r % 32u], ins = e & 0xFFu;
        lq_r++;
        if (ins >= NINS) continue;
        if (!MELODIC(ins)) { if (e >> 24) voice_on(ins, 0, e >> 16 & 3u); }
        else if (e >> 24) mel_start(ins, mel_pick(ins, e >> 8 & 0xFFu), e >> 8 & 0xFFu, e >> 16 & 0x1Fu, 0);
        else mel_off(ins, e >> 8 & 0xFFu, 0);
    }
    if (!running && !run_req) {                 /* stopped: a pattern chosen is the one that will play (the Composer:
                                                   the first box) */
        if (sc.comp_on) for (i = 0; i < NINS; i++) chain_go(i, 0);
        else { chain_rest = 0; for (i = 0; i < NINS; i++) play_pat[i] = (uint8_t)(PSEL(i)); }
    }
    if (run_req && !running && !cin_left && sc.click != CLK_OFF && rec_live && !cin_done) {   /* recording: a bar's count-in */
        cin_left = BAR + 1u; cin_t = 0; cin_done = 1;
    }
    if (!run_req) { cin_left = 0; cin_done = 0; }
    if (run_req && !running && !cin_left) {     /* PLAY: everyone from the top */
        running = 1; to_step = 0; gstep = 0; seq_first = 1; end_done = 0; end_fer = 0;
        for (i = 0; i < NINS; i++) pos[i] = 0;
    } else if (!run_req && running) {           /* STOP: the strings lift their bows */
        running = 0;
        for (i = 0; i < P_BD; i++) { mel_off(i, 0, 1); mv_left[i] = 0; }
    }
    while (left) {
        uint32_t m = left < CH ? left : CH;
        if (cin_left) {                         /* counting in: a click a beat, then the music, on the bar's first beat */
            if (cin_t <= 0) {                   /* (cin_left: BAR + 1 at first; 1: the bar counted, the music now) */
                if (cin_left == 1u) { cin_left = 0; running = 1; to_step = cin_t; gstep = 0; seq_first = 1; for (i = 0; i < NINS; i++) pos[i] = 0; }
                else {
                    uint32_t at = BAR + 1u - cin_left;
                    if (at % BEAT == 0u) click(at == 0u);
                    cin_t += (int32_t)step_q8; cin_left--;
                }
            }
            if (cin_left) {
                if ((uint32_t)cin_t < m * 256u) m = ((uint32_t)cin_t + 255u) >> 8;
                if (!m) m = 1;
                cin_t -= (int32_t)(m * 256u);
            }
        }
        if (running && end_done == (1u << NINS) - 1u) {   /* the song has ended: the last chord held ~1.5 s, then away */
            if (!end_fer) end_fer = 66150;
            end_fer -= (int32_t)m;
            if (end_fer <= 0) {
                for (i = 0; i < P_BD; i++) mel_off(i, 0, 0);
                end_req = 0; end_done = 0; end_fer = 0; run_req = 0;
            }
        } else if (running) {
            if (to_step <= 0) { seq_step(); to_step += (int32_t)step_len(); }
            if ((uint32_t)to_step < m * 256u) m = ((uint32_t)to_step + 255u) >> 8;   /* the next step on its sample */
            if (!m) m = 1;
            to_step -= (int32_t)(m * 256u);
        }
        {                                       /* every part into the mix; the high strings first apart (the hall); a
                                                   part con sordino through its mute; listening alone, only that row */
            static int32_t tmp[2u * CH];
            uint32_t f2, at = (uint32_t)(o - out) / 2u;
            for (i = 0; i < NINS; i++) {
                uint32_t heard = listen_only >= NROW || PK(i) == listen_only, ty = INS[i].ty;
                uint32_t sord = MELODIC(i) && SORD(i, play_pat[i] % NPAT) && (ty == TY_STR || ty == TY_TPT || ty == TY_HORN);
                if (i != S_HI && !sord && heard) { render_voice(i, o, m); continue; }
                for (f2 = 0; f2 < 2u * m; f2++) tmp[f2] = 0;
                render_voice(i, tmp, m);
                if (sord) sordino(i, tmp, m);
                if (i == S_HI) for (f2 = 0; f2 < m; f2++) if (at + f2 < 256u) shi_m[at + f2] = heard ? tmp[2u * f2] + tmp[2u * f2 + 1u] : 0;
                if (heard) for (f2 = 0; f2 < 2u * m; f2++) o[f2] += tmp[f2];
            }
        }
        o += 2u * m;
        left -= m;
    }
    {                                           /* warmth: the top softened (air), a little more body underneath */
        static int32_t air[2], body[2], ka, kb;
        if (!ka) { ka = lp_k(11000); kb = lp_k(220); }
        for (f = 0; f < 2u * n; f++) {
            uint32_t c = f & 1u;
            air[c] += ((out[f] - air[c]) >> 2) * ka >> 12;
            body[c] += ((air[c] - body[c]) >> 2) * kb >> 12;
            out[f] = air[c] + (body[c] >> 2) + (body[c] >> 3);
        }
    }
    hall(out, n);
    if (ck_env > 32)                            /* the click: dry, over the hall */
        for (f = 0; f < n; f++) {
            int32_t c = (sine(ck_ph) * ck_env) >> 15;
            ck_ph += ck_inc; ck_env -= (ck_env >> 8) + 1;   /* (+1: it reaches silence; below 256 it stalled, a faint
                                                   tone that never ended) */
            if (ck_env < 0) ck_env = 0;
            out[2u * f] += c; out[2u * f + 1u] += c;
        }
    if (ck_env <= 32) ck_env = 0;
    {                                           /* 7 dB up (the orchestra was quiet), then a limiter: the block's peak
                                                   known before it plays, the gain eased down within 64 samples and back
                                                   up over a fifth of a second; a soft knee after, for what slips by */
        static int32_t lg = 4096;               /* the limiter's gain, Q12 */
        int32_t pk = 1, tg, g0 = lg, x;
        for (f = 0; f < 2u * n; f++) { x = out[f] < 0 ? -out[f] : out[f]; if (x > pk) pk = x; }
        pk = (pk >> 2) * 9;                     /* (x 9/4) */
        tg = pk > 27000 ? (int32_t)((27000u << 12) / (uint32_t)pk) : 4096;
        if (tg < lg) lg = tg; else lg += (4096 - lg) >> 5;
        for (f = 0; f < 2u * n; f++) {
            int32_t gg = f < 128u ? g0 + (((lg - g0) * (int32_t)f) >> 7) : lg;
            x = (((out[f] * 9) >> 2) * gg) >> 12;   /* (32-bit: |out| well under 2^17) */
            if (x > 28000) x = 28000 + ((x - 28000) >> 2);
            else if (x < -28000) x = -28000 + ((x + 28000) >> 2);
            out[f] = x;
        }
    }
}

static int quiet(void)                          /* quiet enough to save: no note held, only faint drum tails */
{
    uint32_t i;
    for (i = 0; i < NINS; i++)
        if (MELODIC(i) ? mv[i][0].stage | mv[i][1].stage | mv[i][2].stage : vo[i].amp > 300) return 0;
    return 1;
}

/* ======================================================================== the score as it starts */
static uint8_t xrec_ins, xrec_pat, xrec_last, xrec_n, xbuf[NSTEP];
static uint64_t xrec_mask;                      /* the steps a take has heard */
static void xrec_start(uint32_t r)              /* KNOB 1 turned, recording and playing: a take of the playing pattern */
{
    xrec_ins = (uint8_t)(r < P_BD ? r : P_BD);
    xrec_pat = (uint8_t)(play_pat[xrec_ins] % NPAT);
    xrec_last = (uint8_t)(cur[xrec_ins] % NSTEP); xrec_n = 0;
    xbuf[xrec_last] = EXPR(r); xrec_mask = (uint64_t)1 << xrec_last;
    xrec_row = (uint8_t)r;
}
static int xrec_finish(void)                    /* the take as a curve: its loudest is the section's expression, the
                                                   rest the curve under it, in up to six points where it bends most */
{
    uint32_t r = xrec_row, len = plen(xrec_ins, xrec_pat), k, n, first, base = 1, last;
    uint8_t v[NSTEP], keep[NSTEP];
    curve_t *c = &sc.curve[r % NROW][xrec_pat];
    xrec_row = 0xFFu;
    if (r >= NROW || xrec_n < 2u || !xrec_mask) return 0;
    for (first = 0; !(xrec_mask >> first & 1u); first++) {}
    first %= len; last = xbuf[first];
    for (k = 0; k < len; k++) {                 /* a step not heard: the one before it holds */
        uint32_t j = (first + k) % len;
        if (xrec_mask >> j & 1u) last = xbuf[j];
        v[j] = (uint8_t)last;
    }
    for (k = 0; k < len; k++) if (v[k] > base) base = v[k];
    for (k = 0; k < len; k++) { v[k] = (uint8_t)(v[k] * 127u / base); keep[k] = 0; }
    keep[0] = keep[len - 1u] = 1;
    for (n = len > 1u ? 2u : 1u; n < NCP; n++) {   /* add the step furthest from the line, while it is far */
        uint32_t a = 0, best = 0, bk = 0;
        for (k = 1; k < len; k++) {
            uint32_t b = k, j;
            if (!keep[k]) continue;
            for (j = a + 1u; j < b; j++) {
                int32_t li = v[a] + ((int32_t)v[b] - v[a]) * (int32_t)(j - a) / (int32_t)(b - a), d = (int32_t)v[j] - li;
                if (d < 0) d = -d;
                if ((uint32_t)d > best) { best = (uint32_t)d; bk = j; }
            }
            a = b;
        }
        if (best < 4u) break;
        keep[bk] = 1;
    }
    for (c->n = 0, k = 0; k < len; k++)
        if (keep[k]) { c->p[c->n].pos = (uint8_t)k; c->p[c->n].val = v[k]; c->p[c->n].shape = CS_LIN; c->n++; }
    c->on = 1;
    sc.pexpr[r % NROW][xrec_pat] = (uint8_t)base;
    return 1;
}

static void curve_preset(curve_t *c, uint32_t kind, uint32_t len)   /* flat, crescendo, diminuendo, swell */
{
    static const uint8_t V[4][3] = {{127, 127, 0}, {48, 127, 0}, {127, 48, 0}, {48, 127, 48}};
    c->n = kind == 3u ? 3u : 2u;
    c->p[0].pos = 0; c->p[0].val = V[kind & 3u][0]; c->p[0].shape = kind ? CS_SMOOTH : CS_LIN;
    if (kind == 3u) { c->p[1].pos = (uint8_t)(len / 2u); c->p[1].val = 127; c->p[1].shape = CS_SMOOTH; }
    c->p[c->n - 1u].pos = (uint8_t)len; c->p[c->n - 1u].val = V[kind & 3u][kind == 3u ? 2 : 1]; c->p[c->n - 1u].shape = CS_LIN;
    c->on = 1;
}
static void putp(uint32_t i, uint32_t q, uint32_t st, uint32_t note, uint32_t len, uint32_t dyn)   /* into pattern q */
{
    if (st >= NSTEP) return;
    if (MELODIC(i)) {
        mstep_t *m = &sc.mel[i][q][st];
        uint32_t k;
        for (k = 0; k < NV && m->n[k]; k++) {}
        if (k < NV) m->n[k] = (uint8_t)note;
        m->len = (uint8_t)len; m->dyn = (uint8_t)dyn;
    } else sc.drum[i - P_BD][q][st] = (uint8_t)(dyn + 1u);
}
static void line(uint32_t i, uint32_t q, uint32_t st, const uint8_t *nl, uint32_t dyn)   /* note, length pairs, 0-ended */
{
    for (; nl[1]; nl += 2) { if (nl[0]) putp(i, q, st, nl[0], nl[1], dyn); st += nl[1]; }
}
/* the piece CONDUCTOR starts with: a festive overture in C, eight bars at 120 (an original tune), the Composer's song
 * A playing pattern A then B. A: the violins lead with a bowed tune over held cellos and soft oboes, the horns' counter-
 * melody under it (soft, and in more from bar 2: their curve). B: the violins and cellos plucked (pizzicato) for two
 * bars, then bowed staccato over a timpani roll, trumpets, and the last chord with the bass drum and cymbal. It shows
 * the bowed, staccato and plucked strings, a counter-melody in the low brass, the timpani, and each pattern's own
 * expression and balance. (SAVE 1 holds it too, until something is saved there) */
static void score_default(void)
{
    static const uint8_t VA[] = {79,4, 76,2, 79,2, 84,6, 83,2,  81,4, 84,2, 81,2, 76,8,  77,4, 81,2, 84,2, 86,4, 84,4,
                                 83,4, 81,2, 83,2, 79,8, 0,0};          /* the violins' tune: note (MIDI), length (16ths) */
    static const uint8_t HA[] = {60,6, 62,2, 64,4, 67,4,  69,4, 67,4, 64,8,  65,6, 64,2, 62,4, 60,4,  59,6, 60,2, 62,8, 0,0};
    static const uint8_t VB[] = {72,2,76,2,79,2,84,2,79,2,76,2,79,2,76,2,  72,2,77,2,81,2,84,2,81,2,77,2,81,2,77,2,
                                 74,2,79,2,83,2,86,2,83,2,79,2,83,2,79,2, 0,0};
    static const uint8_t CB[] = {48,4,48,4,48,4,48,4,  41,4,41,4,41,4,41,4,  43,4,43,4,43,4,43,4, 0,0};
    static const uint8_t HB[] = {64,8, 67,8,  69,8, 72,4, 69,4,  71,4, 69,4, 67,4, 65,4,  64,8, 0,0};
    static const uint8_t OA[] = {76,16, 72,16, 69,16, 71,16, 0,0};
    static const uint8_t OB[] = {76,16, 77,16, 74,16, 76,8, 0,0};
    static const uint8_t CA[4] = {48, 45, 41, 43};   /* the cellos' held roots in A: C A F G */
    uint32_t i, c, q, k;
    for (i = 0; i < sizeof sc; i++) ((uint8_t *)&sc)[i] = 0;
    sc.ver = SCORE_VER; sc.tempo = 120; sc.hall = 6; sc.rsv = 2;   /* the concert hall */
    for (i = 0; i < NINS; i++) { sc.vol[i] = 10; sc.tune[i] = (uint8_t)(4 + INS[i].oct); }
    for (i = 0; i < NROW; i++) for (q = 0; q < NPAT; q++) { sc.pexpr[i][q] = 80; sc.pbal[i][q] = 0; sc.pvib[i][q] = 2; }   /* mf,
                                                   0 dB, vibrato normal: every pattern */
    sc.tsig = 4; sc.clickvol = 5;
    for (i = 0; i < NROW; i++) for (q = 0; q < NPAT; q++) sc.len[i][q] = q < 2u ? 64u : 16u;   /* (A, B: the piece) */
    for (i = 0; i < 4u; i++) sc.tune[P_T1 + i] = TIMP_TUNE[i];   /* C2 G2 C3 G3 */
    {                                           /* the kit: bass drum, cymbal 1, triangle, snare 1, four timpani */
        static const uint8_t KIT[NDRUM] = {DT_BD, DT_CYM1, DT_TRI, DT_SN1, DT_TIMP1, DT_TIMP2, DT_TIMP3, DT_TIMP4};
        for (i = 0; i < NDRUM; i++) sc.dtype[i] = KIT[i];
    }
    /* ---- A: the tune */
    line(S_HI, 0, 0, VA, 2);
    for (k = 0; k < 4u; k++) putp(S_LO, 0, k * 16u, CA[k], 16, 1);
    line(B_LO, 0, 0, HA, 1);
    line(W_HI, 0, 0, OA, 0);
    putp(P_T3, 0, 0, 0, 1, 2); putp(P_T2, 0, 48, 0, 1, 2); putp(P_T2, 0, 56, 0, 1, 1);
    /* ---- B: plucked, then bowed staccato over the roll, the last chord */
    line(S_HI, 1, 0, VB, 3u | ART_PIZZ << 2); putp(S_HI, 1, 48, 84, 8, 3u | ART_MARC << 2);
    line(S_LO, 1, 0, CB, 3u | ART_PIZZ << 2); putp(S_LO, 1, 48, 48, 8, 3);
    for (k = 32; k < 48u; k++) {                /* (bar 7: bowed staccato) */
        if (sc.mel[S_HI][1][k].n[0]) sc.mel[S_HI][1][k].dyn = (uint8_t)(3u | ART_STAC << 2);
        if (sc.mel[S_LO][1][k].n[0]) sc.mel[S_LO][1][k].dyn = (uint8_t)(2u | ART_STAC << 2);
    }
    line(B_LO, 1, 0, HB, 1);
    for (k = 32; k < 48u; k++) if (sc.mel[B_LO][1][k].n[0]) sc.mel[B_LO][1][k].dyn = 0;   /* (bar 7: p, under the staccato) */
    line(W_HI, 1, 0, OB, 1);
    putp(B_HI, 1, 32, 67, 2, 2); putp(B_HI, 1, 34, 67, 2, 2); putp(B_HI, 1, 36, 74, 4, 2); putp(B_HI, 1, 40, 79, 8, 3);
    putp(B_HI, 1, 48, 72, 8, 3); putp(B_HI, 1, 48, 76, 8, 3);   /* trumpets: the fanfare, the last chord (C, E) */
    putp(P_T3, 1, 0, 0, 1, 2); putp(P_T3, 1, 16, 0, 1, 1);
    for (k = 0; k < 16u; k++) putp(P_T2, 1, 32 + k, 0, 1, k / 4u);   /* the timpani roll on G, p to f, */
    putp(P_T3, 1, 48, 0, 1, 3); putp(P_BD, 1, 48, 0, 1, 3); putp(P_CYM, 1, 48, 0, 1, 3);   /* into C with the drums */
    /* ---- each pattern's settings: the violins lead, the horns under them */
    sc.pexpr[S_HI][0] = sc.pexpr[S_HI][1] = 104; sc.pbal[S_HI][0] = sc.pbal[S_HI][1] = 3;
    sc.pexpr[B_LO][0] = 54; sc.pexpr[B_LO][1] = 58; sc.pbal[B_LO][0] = sc.pbal[B_LO][1] = -9;
    {                                           /* the horns in A: bar 1 quieter, in more from bar 2 */
        curve_t *cv = &sc.curve[B_LO][0];
        cv->on = 1; cv->n = 3;
        cv->p[0].pos = 0; cv->p[0].val = 80; cv->p[0].shape = CS_LIN;
        cv->p[1].pos = 12; cv->p[1].val = 80; cv->p[1].shape = CS_SMOOTH;
        cv->p[2].pos = 18; cv->p[2].val = 127; cv->p[2].shape = CS_LIN;
    }
    for (c = 0; c < NSONG; c++) {               /* the Composer: song A plays A then B; the others one box, A */
        sc.clen[c] = c ? 1u : 2u;
        for (i = 0; i < NROW; i++) { sc.chain[c][i][0] = 1; if (!c) sc.chain[c][i][1] = 2; }
    }
    sc.comp_on = 1; sc.comp_song = 0;
}
static int score_ok(void);
#define NPROJ 4u
#define WORK_SLOT 4u                            /* the work as you leave it (slots 0-3: the save page's) */
static uint8_t proj_bpm[NPROJ];                 /* each slot's tempo (0: empty, or not readable) */
/* the save slots: a load goes into a spare copy first and is checked whole before it replaces the work. A slot holds
 * the score packed: everything as it is, then only the steps that play (a melodic step 5 bytes, a hit 1), the rest of
 * the 16 KB zero. About 3000 note-steps fit: more than a full arrangement uses (a full one would not: SAVE says so) */
static score_t proj_buf __attribute__((section(".pool")));
static score_t proj_keep __attribute__((section(".pool")));
static uint8_t slot_buf[SLOT_MAX] __attribute__((section(".pool")));
#define SC_HEAD ((uint32_t)((const uint8_t *)&sc.mel - (const uint8_t *)&sc))   /* the score before its patterns */
static uint32_t has_notes(const mstep_t *m);
static uint16_t rec_take = 1, rec_tk[P_BD][NSTEP];   /* live recording: the take, and the take each step was written in */
static int transpose(uint32_t i, int32_t d)     /* the chosen pattern's notes up or down; 0: one would leave 24..108 */
{
    uint32_t q = sc.psel[i] % NPAT, k, j;
    for (k = 0; k < NSTEP; k++) for (j = 0; j < NV; j++) {
        int32_t n = sc.mel[i][q][k].n[j];
        if (n && (n + d < 24 || n + d > 108)) return 0;
    }
    for (k = 0; k < NSTEP; k++) for (j = 0; j < NV; j++) if (sc.mel[i][q][k].n[j]) sc.mel[i][q][k].n[j] = (uint8_t)(sc.mel[i][q][k].n[j] + d);
    return 1;
}
/* ---- the battery: a low-battery warning (CLAVIOLINE's). The battery reaches the ADC (ch3) through a divider whose
 * values no document gives: BATT_LOW is about 10 % left for a lithium cell (~3.6 V) through an assumed 1:2 divider,
 * an ESTIMATE. The reading does not follow
 * USB power, so the warning hides while a computer is connected */
#define BATT_LOW 558
#define BATT_OK 570                             /* (the warning clears above this) */
static int32_t batt_avg = -1;                   /* the raw reading x16, smoothed over ~4 s; -1 = none yet */
static uint32_t batt_low;
static int32_t batt_read(void)
{
#ifdef DINGHY_HOST
#ifdef HOST_BATT
    return HOST_BATT;                           /* (a host build can pretend: -DHOST_BATT=500) */
#else
    return -1;
#endif
#else
    return fm1_adc_read(FM1_ADC_BATT);
#endif
}
static uint32_t usb_powered(void)              /* plugged in to a computer: the host is sending USB frames */
{
#ifdef DINGHY_HOST
#ifdef HOST_USB
    return 1u;
#else
    return 0u;
#endif
#else
    return usb.up && usb.config && !usb.suspended;
#endif
}
static void batt_poll(void)                     /* main loop, 4 times a second */
{
    static uint32_t t;
    int32_t v;
    if (fm1_ms - t < 250u) return;
    t = fm1_ms;
    if ((v = batt_read()) < 0) return;
    if (batt_avg < 0) batt_avg = v * 16;
    batt_avg += (v * 16 - batt_avg) / 16;
    if (!batt_low && batt_avg < BATT_LOW * 16) batt_low = 1;
    else if (batt_low && batt_avg > BATT_OK * 16) batt_low = 0;
}
static int rec_grow(uint32_t i, uint32_t q, uint32_t st, uint32_t g0, uint32_t gnow)   /* a recorded note as long as it has
                                                   been held, its end on the nearest step (so the gaps come out even); the
                                                   notes it covers from before this take give way to it. 1: it changed */
{
    mstep_t *m = &sc.mel[i][q][st];
    uint32_t len = plen(i, q), g1 = (gnow + 128u) >> 8, L = g1 > g0 ? g1 - g0 : 1u, j, ch = 0;
    if (st + L > len) L = len - st;
    if (L < 1u) L = 1u;
    if (L == m->len) return 0;
    if (L > m->len)
        for (j = st + 1u; j < st + L; j++)
            if (rec_tk[i][j] != rec_take && has_notes(&sc.mel[i][q][j])) { sc.mel[i][q][j].n[0] = sc.mel[i][q][j].n[1] = sc.mel[i][q][j].n[2] = 0; ch = 1; }
    m->len = (uint8_t)L;
    (void)ch;
    return 1;
}
static void bytes_copy(void *to, const void *from, uint32_t n)
{
    uint8_t *d = (uint8_t *)to;
    const uint8_t *f = (const uint8_t *)from;
    while (n--) *d++ = *f++;
}
static int pack(const score_t *s)               /* the score into slot_buf; -1: too much to fit */
{
    uint8_t *b = slot_buf;
    uint32_t n = SC_HEAD, r, q, k;
    bytes_copy(b, s, SC_HEAD);                  /* the head, then zeros after it (never a byte written twice: GCC 16 at
                                                   -O2 made the zeroing and the copy memset / memcpy and lost the copy) */
    for (k = SC_HEAD; k < SLOT_MAX; k++) b[k] = 0;
    for (r = 0; r < P_BD; r++)
        for (q = 0; q < NPAT; q++) {
            uint32_t at = n++, c = 0;
            for (k = 0; k < NSTEP; k++) {
                const mstep_t *m = &s->mel[r][q][k];
                if (!m->n[0] && !m->n[1] && !m->n[2]) continue;
                if (n + 6u > SLOT_MAX) return -1;   /* (5 bytes; 6 with an articulation: the length's top bit says) */
                b[n++] = (uint8_t)(k << 2 | (m->dyn & 3u)); b[n++] = m->n[0]; b[n++] = m->n[1]; b[n++] = m->n[2];
                b[n++] = (uint8_t)(m->len | (m->dyn >> 2 ? 0x80u : 0u));
                if (m->dyn >> 2) b[n++] = (uint8_t)(m->dyn >> 2);
                c++;
            }
            b[at] = (uint8_t)c;
        }
    for (r = 0; r < NDRUM; r++)
        for (q = 0; q < NPAT; q++) {
            uint32_t at = n++, c = 0;
            if (n > SLOT_MAX) return -1;
            for (k = 0; k < NSTEP; k++)
                if (s->drum[r][q][k]) {
                    if (n + 1u > SLOT_MAX) return -1;
                    b[n++] = (uint8_t)(k << 2 | ((s->drum[r][q][k] - 1u) & 3u)); c++;
                }
            b[at] = (uint8_t)c;
        }
    return 0;
}
static int unpack(score_t *s)                   /* slot_buf into a score; -1: not one */
{
    const uint8_t *b = slot_buf;
    uint32_t n = SC_HEAD, r, q, k, c;
    bytes_copy(s, b, SC_HEAD);                  /* the head, then the patterns zeroed (as in pack: no byte written twice) */
    for (k = SC_HEAD; k < sizeof *s; k++) ((uint8_t *)s)[k] = 0;
    if (s->ver == 8u) {                         /* version 8: the head ended before the patterns' settings; a section's
                                                   settings go into each of its patterns */
        n = (uint32_t)((const uint8_t *)&s->pexpr - (const uint8_t *)s);
        for (r = 0; r < NROW; r++)
            for (q = 0; q < NPAT; q++) {
                s->pexpr[r][q] = s->expr[r]; s->pbal[r][q] = s->bal[r]; s->pvib[r][q] = s->vib[r];
                if (r < P_BD) { s->pmute[r][q] = s->mute[r]; s->psolo[r][q] = s->solo[r]; }
            }
    } else if (s->ver == 9u) n = (uint32_t)((const uint8_t *)&s->swing - (const uint8_t *)s);
    else if (s->ver != SCORE_VER) return -1;
    if (n < SC_HEAD) {                          /* (an older score: the settings after its head start as they would) */
        s->swing = s->human = 0; s->click = CLK_OFF; s->clickvol = 5; s->rsv2[0] = s->rsv2[1] = 0;
        s->ver = SCORE_VER;
    }
    for (r = 0; r < P_BD; r++)
        for (q = 0; q < NPAT; q++)
            for (c = b[n++]; c; c--) {
                mstep_t *m;
                if (n + 5u > SLOT_MAX || b[n] >> 2 >= NSTEP) return -1;
                m = &s->mel[r][q][b[n] >> 2];
                m->dyn = b[n] & 3u; m->n[0] = b[n + 1u]; m->n[1] = b[n + 2u]; m->n[2] = b[n + 3u]; m->len = b[n + 4u] & 0x7Fu;
                if (m->len > NSTEP || m->n[0] > 127u || m->n[1] > 127u || m->n[2] > 127u) return -1;
                if (b[n + 4u] & 0x80u) {        /* an articulation */
                    if (n + 6u > SLOT_MAX || b[n + 5u] >= NART) return -1;
                    m->dyn = (uint8_t)(m->dyn | b[n + 5u] << 2); n++;
                }
                n += 5u;
            }
    for (r = 0; r < NDRUM; r++)
        for (q = 0; q < NPAT; q++) {
            if (n >= SLOT_MAX) return -1;
            for (c = b[n++]; c; c--) {
                if (n >= SLOT_MAX) return -1;
                s->drum[r][q][b[n] >> 2] = (uint8_t)((b[n] & 3u) + 1u); n++;
            }
        }
    return 0;
}
static int slot_put(uint32_t i, const score_t *s) { return pack(s) ? -2 : slot_save(i, slot_buf, SLOT_MAX); }
static int slot_get(uint32_t i, score_t *s) { return slot_load(i, slot_buf, SLOT_MAX) ? -1 : unpack(s); }
static void proj_scan(void)                     /* which slots hold a score (at boot) */
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        proj_bpm[i] = slot_get(i, &proj_buf) == 0 ? proj_buf.tempo : 0u;
}
static int proj_save(uint32_t i)                /* 0 = saved; -2: too much to fit; else the flash failed */
{
    int rc = slot_put(i, &sc);
    if (rc) return rc;
    proj_bpm[i] = sc.tempo;
    return 0;
}
static int proj_load(uint32_t i)                /* 0 = loaded; -1 = empty or broken (the work is untouched) */
{
    if (slot_get(i, &proj_buf)) return -1;
    bytes_copy(&proj_keep, &sc, sizeof sc);
    bytes_copy(&sc, &proj_buf, sizeof sc);
    if (!score_ok()) { bytes_copy(&sc, &proj_keep, sizeof sc); return -1; }
    return 0;
}
static int score_ok(void)
{
    uint32_t i;
    if (sc.ver != SCORE_VER || sc.tempo < 30u || sc.tempo > 240u || sc.hall > 10u) return 0;
    if (sc.rsv >= NHALL) sc.rsv = 2;            /* (a score from before the halls) */
    if (sc.comp_on > 1u || sc.comp_song >= NSONG) return 0;
    for (i = 0; i < NINS; i++) if (sc.vol[i] > 20u) return 0;
    for (i = 0; i < NDRUM; i++) if (sc.dtype[i] >= NDT) return 0;
    if (sc.tsig < TS_24 || sc.tsig >= TS_END) return 0;
    if (sc.swing > 6u || sc.human > 6u || sc.click >= NCLK || sc.clickvol < 1u || sc.clickvol > 8u) return 0;
    if (KEYSIG < -7 || KEYSIG > 7 || sc.rsv2[1] > 3u) return 0;
    for (i = 0; i < NROW * NPAT; i++) {
        if (((&sc.pvib[0][0])[i] & 0x7Fu) > 3u || (&sc.pexpr[0][0])[i] > 127u || (&sc.pbal[0][0])[i] < -30 || (&sc.pbal[0][0])[i] > 6) return 0;
        if (i < P_BD * NPAT && ((&sc.pmute[0][0])[i] > 1u || (&sc.psolo[0][0])[i] > 1u)) return 0;
    }
    for (i = 0; i < NROW; i++) {
        uint32_t q;
        if (sc.psel[i] >= NPAT) return 0;
        for (q = 0; q < NPAT; q++) if (sc.len[i][q] < 1u || sc.len[i][q] > NSTEP) return 0;
    }
    for (i = 0; i < NSONG; i++) if (sc.clen[i] < 1u || sc.clen[i] > NBOX) return 0;
    for (i = 0; i < sizeof sc.chain; i++) if ((&sc.chain[0][0][0])[i] > BOX_X) return 0;
    for (i = 0; i < NROW; i++) {
        uint32_t q, k;
        for (q = 0; q < NPAT; q++) {
            const curve_t *c = &sc.curve[i][q];
            if (c->on > 1u || c->n > NCP) return 0;
            for (k = 0; k < c->n; k++)
                if (c->p[k].pos > NSTEP || c->p[k].val > 127u || c->p[k].shape > 3u || (k && c->p[k].pos < c->p[k - 1u].pos)) return 0;
        }
    }
    return 1;
}

/* ======================================================================== the screen */
#include "serif_font.h"                         /* DejaVu Serif Bold, three sizes; the clef (tools/gen_serif.py) */
#define C_PAPER RGB(243, 234, 211)              /* the page */
#define C_CARD RGB(251, 246, 232)               /* a seat, a cell */
#define C_INK RGB(43, 32, 24)
#define C_INK2 RGB(122, 104, 86)
#define C_RULE RGB(190, 172, 142)
#define C_FAINT RGB(228, 216, 190)
#define C_WINE RGB(112, 16, 32)                 /* the chosen one */
#define C_GOLD RGB(186, 146, 52)
#define C_GOLD_L RGB(242, 202, 96)
#define C_KEYW RGB(253, 251, 244)
#define C_ROWB RGB(235, 225, 200)
static const uint16_t WINES[4] = {RGB(214, 160, 160), RGB(176, 92, 102), RGB(134, 34, 52), RGB(92, 10, 24)};   /* p mp mf f */
static const char *const SEC_ABBR[NSEC] = {"STRINGS", "WINDS", "BRASS", "PERCUSSION"};
#define HEAD_H 28
#define BODY_Y 28                               /* the body: y 28..211 */
#define BODY_H 184
#define HOME_H 212                              /* the orchestra page: no footer, its seats to the bottom */
#define FOOT_Y 212
#define FOOT_H 28

static void fmt_u(char *s, uint32_t v, uint32_t w)   /* v as w digits (w 0: as many as it needs) */
{
    char t[12];
    uint32_t n = 0, i;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11u);
    while (n < w) t[n++] = '0';
    for (i = 0; i < n; i++) s[i] = t[n - 1u - i];
    s[n] = 0;
}
static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *catu(char *d, uint32_t v, uint32_t w) { fmt_u(d, v, w); while (*d) d++; return d; }
static char *note_name(char *d, uint32_t n)
{
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const NF[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    d = cat(d, (KEYSIG < 0 ? NF : NM)[n % 12u]);   /* (spelled as the key spells it) */
    if (n < 12u) return cat(d, "-");
    return catu(d, n / 12u - 1u, 0);
}

/* ---- drawing on paper: blending, text, rings */
static uint16_t mix565(uint32_t a, uint32_t b, uint32_t t)   /* a -> b, t 0..16 */
{
    uint32_t r = ((a >> 11) * (16u - t) + (b >> 11) * t) >> 4;
    uint32_t g = (((a >> 5) & 63u) * (16u - t) + ((b >> 5) & 63u) * t) >> 4;
    uint32_t bl = ((a & 31u) * (16u - t) + (b & 31u) * t) >> 4;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static void cv_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)   /* c over the canvas, a 0..16 */
{
    uint16_t *p;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || !a) return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    *p = swap16(a >= 16u ? c : mix565(swap16(*p), c, a));
}
static int32_t ctext(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)   /* text on paper */
{
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v) cv_blend(x - f->pad + (int32_t)gx, y + (int32_t)gy, c, v + (v >> 3));
            }
        x += f->adv[gi];
    }
    return x;
}
static void ctext_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { ctext(cx - text_w(f, s) / 2, y, f, s, c); }
static void ctext_r(int32_t rx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { ctext(rx - text_w(f, s), y, f, s, c); }

static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y, x;
    for (y = -r; y <= r; y++) {
        for (x = r; x * x + y * y > r * r + r; x--) {}
        cv_rect(cx - x, cy + y, 2 * x + 1, 1, c);
    }
}
static void cv_ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y, x;
    for (y = -r; y <= r; y++) {
        for (x = r; x * x + y * y > r * r + r; x--) {}
        cv_pset(cx - x, cy + y, c); cv_pset(cx + x, cy + y, c);
    }
    for (x = -r; x <= r; x++) {
        for (y = r; x * x + y * y > r * r + r; y--) {}
        cv_pset(cx + x, cy - y, c); cv_pset(cx + x, cy + y, c);
    }
}
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c); cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c); cv_rect(x + w - 1, y, 1, h, c);
}
static int32_t lbox(int32_t x, int32_t y, char ch, const felucca_font_t *f, uint16_t ink, uint16_t frame, uint16_t fill)
{                                               /* a pattern's letter in a box; returns the x after it */
    char t[2] = {ch, 0};
    int32_t w = f == &SERIF_M ? 17 : 13, h = f == &SERIF_M ? 19 : 15;
    if (fill != C_PAPER) cv_rect(x, y, w, h, fill);
    cv_frame(x, y, w, h, frame);
    ctext_c(x + w / 2 + 1, y + (f == &SERIF_M ? 0 : 1), f, t, ink);
    return x + w;
}
static void sides(int32_t h)                    /* the page's double rule, left and right */
{
    cv_rect(1, 0, 1, h, C_INK); cv_rect(3, 0, 1, h, C_RULE);
    cv_rect(236, 0, 1, h, C_RULE); cv_rect(238, 0, 1, h, C_INK);
}
static void play_mark(int32_t x, int32_t y, uint32_t run, uint16_t c)   /* a triangle (playing) or a square */
{
    int32_t k;
    if (run) for (k = 0; k < 11; k++) cv_rect(x, y + k, 1 + (k < 5 ? k : 10 - k), 1, c);
    else cv_rect(x, y + 1, 9, 9, c);
}
static void knob(int32_t x, int32_t y, uint16_t c)   /* a little knob for the legends */
{
    cv_ring(x, y, 5, c);
    cv_line(x, y, x + 2, y - 4, c);
}

/* ---- the header and the footer */
static void head_begin(void)
{
    cv_begin(240, HEAD_H, C_PAPER);
    sides(HEAD_H);
    cv_rect(1, 1, 238, 1, C_INK); cv_rect(3, 3, 234, 1, C_RULE);
    cv_rect(1, HEAD_H - 1, 238, 1, C_INK);
}
static const char *const ROWNAME[NROW] = {"STRINGS HIGH", "STRINGS LOW", "WINDS HIGH", "WINDS LOW", "BRASS HIGH",
                                          "BRASS LOW", "PERCUSSION"};
static void draw_status(uint32_t sel, uint32_t run, uint32_t rec, const char *msg)   /* the orchestra's header */
{
    char s[16], *p;
    uint32_t r = PK(sel);
    int32_t x;
    head_begin();
    if (msg) ctext(10, 7, &SERIF_S, msg, C_WINE);   /* (a message, a moment) */
    else {                                      /* the section chosen, its pattern, its expression */
        x = ctext(10, 5, &SERIF_M, ROWNAME[r], C_INK);
        x = lbox(x + 5, 5, (char)('A' + PSEL(sel)), &SERIF_M, C_INK, C_INK2, C_PAPER);
        ctext(x + 6, 3, &ITAL_M, EXPR_NAME[expr_band(EXPR(r))], C_WINE);
    }
    p = catu(s, sc.tempo, 0); (void)p;
    if (rec) cv_disc(211, 14, 5, C_WINE);       /* recording: the dot in the tempo's place (the name keeps its room) */
    else if (!msg) ctext_r(218, 8, &SERIF_S, s, C_INK2);   /* (a message: the tempo makes room) */
    if (live_tr && !msg) {                      /* transposed live: the shift, in wine, before the tempo */
        char tt[6], *q = tt;
        *q++ = live_tr > 0 ? '+' : '-';
        catu(q, (uint32_t)(live_tr > 0 ? live_tr : -live_tr), 0);
        ctext_r(rec ? 202 : 212 - text_w(&SERIF_S, s), 8, &SERIF_S, tt, C_WINE);
    }
    play_mark(224, 9, run, run ? C_WINE : C_RULE);
    cv_blit(0, 0);
}
static const char *title_right = "MUSIC";      /* (what the title says on the right when it has no bar to show) */
static void draw_title(const char *t, char letter, uint32_t run, uint32_t rec, uint32_t bar, uint32_t bars)
{
    char s[12], *p;
    int32_t x;
    head_begin();
    x = ctext(10, 5, &SERIF_M, t, C_INK);
    if (letter) x = lbox(x + 6, 5, letter, &SERIF_M, C_INK, C_INK2, C_PAPER);
    if (bar) { p = cat(s, "BAR "); p = catu(p, bar, 0); p = cat(p, "/"); catu(p, bars, 0); }
    else cat(s, title_right);
    ctext_r(216, 8, &SERIF_S, s, C_INK2);
    play_mark(222, 9, run, run ? C_WINE : C_RULE);
    if (rec) cv_disc(x + 9, 14, 4, C_WINE);    /* (recording) */
    cv_blit(0, 0);
}
static void foot_begin(void)
{
    cv_begin(240, FOOT_H, C_PAPER);
    sides(FOOT_H);
    cv_rect(1, 0, 238, 1, C_INK);
    cv_rect(3, FOOT_H - 4, 234, 1, C_RULE); cv_rect(1, FOOT_H - 2, 238, 1, C_INK);
}
static int foot_msg(const char *msg)
{
    if (!msg) return 0;
    ctext_c(120, 5, &SERIF_M, msg, C_WINE);
    return 1;
}

/* ---- the orchestra: a seating plan, strings nearest, percussion at the back. Each section's name level on the
 * paper, and under it a gentle arc of seven chairs (an arc around the conductor, below the screen): the chosen
 * section's chairs wine, a muted one's faded, a SOLO part's all faded but the middle chair (one player) */
#define NCHAIR 7
static uint32_t sel_all;                        /* the orchestra: the whole percussion chosen (not one drum) */
static int32_t kit_x, kit_y;                    /* where the kit's pattern letter goes (after PERCUSSION) */
static uint16_t chart_bg[240u * HOME_H] __attribute__((section(".pool")));
static uint32_t chart_k1 = ~0u, chart_k2;
static int16_t dot_x[NINS], dot_y[NINS];

static void chair(int32_t x, int32_t y, uint16_t fill, uint16_t ink)   /* 10 x 8, rounded, its backrest darker */
{
    cv_rect(x + 1, y, 8, 1, ink); cv_rect(x, y + 1, 10, 6, ink); cv_rect(x + 1, y + 7, 8, 1, ink);
    cv_rect(x + 1, y + 3, 8, 4, fill);
    cv_rect(x + 1, y + 1, 8, 2, mix565(ink, fill, 5));
}
static void chairs(int32_t xc, int32_t y, uint32_t on, uint32_t mute, uint32_t solo)   /* seven, centred on xc */
{
    int32_t k;
    for (k = 0; k < NCHAIR; k++) {
        int32_t x = xc - 5 + (k - NCHAIR / 2) * 14, dx = x + 5 - 120;
        uint32_t faint = mute || (solo && k != NCHAIR / 2);
        uint16_t fill = on ? (faint ? RGB(214, 170, 170) : C_WINE) : faint ? C_FAINT : C_CARD;
        uint16_t ink = on ? (faint ? RGB(214, 170, 170) : C_WINE) : faint ? C_RULE : C_INK;
        chair(x, y - dx * dx / 1100, fill, ink);
    }
}
#define ROW_TOP(s) (50 + (int32_t)(s) * 51)     /* brass, winds, strings (back to front): a block each */
static void build_chart(uint32_t sel)           /* the plan with its labels: again only when the choice changes */
{
    uint32_t s, psel = !MELODIC(sel);
    int32_t x, k;
    cv_begin(240, HOME_H, C_PAPER);
    sides(HOME_H);
    for (x = 12; x < 228; x++) { int32_t dx = x - 120; cv_pset(x, 206 - dx * dx / 1700, C_INK2); }   /* the stage */
    {                                           /* the back: PERCUSSION and its pattern, its drums' dots, its chairs */
        const char *t = "PERCUSSION";
        int32_t w = text_w(&SERIF_M, t), x0 = 120 - (w + 6 + 17) / 2;
        ctext(x0, 2, &SERIF_M, t, psel ? C_WINE : C_INK);
        kit_x = x0 + w + 6; kit_y = 2;
        for (k = 0; k < 8; k++) { dot_x[P_BD + k] = (int16_t)(64 + k * 16); dot_y[P_BD + k] = 28; }
        chairs(120, 37, psel, 0, 0);
    }
    for (s = 0; s < 3u; s++) {                  /* brass, winds, strings; HIGH on the left, LOW on the right */
        uint32_t c, rs = 2u - s;                /* (SEC_FIRST: 0 strings, 1 winds, 2 brass) */
        int32_t top = ROW_TOP(s);
        for (c = 0; c < 2u; c++) {
            uint32_t i = SEC_FIRST[rs] + c, on = i == sel, mute = MUTE(i);
            int32_t xc = c ? 175 : 65, w2 = text_w(&SERIF_M, INS[i].reg), x0 = xc - (w2 + 34) / 2;   /* (the register,
                                                   its dot and its pattern, centred together) */
            ctext_c(x0 + w2 / 2, top, &SERIF_S, SEC_ABBR[rs], on ? C_WINE : mute ? C_RULE : C_INK2);   /* (above it) */
            ctext(x0, top + 11, &SERIF_M, INS[i].reg, on ? C_WINE : mute ? C_RULE : C_INK);
            dot_x[i] = (int16_t)(x0 + w2 + 8); dot_y[i] = (int16_t)(top + 21);
            if (SOLO(i)) {                   /* SOLO: a gold S before the register */
                cv_disc(x0 - 10, top + 20, 6, on ? C_GOLD_L : C_GOLD);
                ctext(x0 - 13, top + 13, &SERIF_S, "S", on ? C_WINE : C_INK);
            }
            chairs(xc, top + 38, on, mute, SOLO(i));
        }
    }
    for (x = 0; x < (int32_t)(240u * HOME_H); x++) chart_bg[x] = cv_px[x];
}

static char pat_ch(uint32_t i)                  /* the pattern a part plays now: A-J, or - (the Composer: resting) */
{
    if (sc.comp_on && chain_rest >> i & 1u) return '-';
    return (char)('A' + play_pat[i] % NPAT);
}
static void draw_dot(uint32_t i, int32_t x, int32_t y, uint32_t on, uint32_t sel)   /* bright while it plays */
{
    char t[4] = {pat_ch(i), 0, 0, 0};
    if (!sc.comp_on && PSEL(i) != play_pat[i]) { t[1] = '>'; t[2] = (char)('A' + PSEL(i)); }   /* (queued) */
    if (MELODIC(i)) {                           /* (the drums: one letter for the kit, by PERCUSSION) */
        uint16_t ink = i == sel ? C_WINE : C_INK2;
        int32_t xx = lbox(x + 8, y - 10, t[0], &SERIF_M, ink, ink, C_PAPER);
        if (t[1]) { ctext(xx + 1, y - 10, &SERIF_M, ">", ink); lbox(xx + 11, y - 10, t[2], &SERIF_M, ink, ink, C_PAPER); }
    }
    if (MUTE(i)) cv_ring(x, y, 2, C_RULE);
    else if (on) { cv_disc(x, y, 5, C_GOLD_L); cv_ring(x, y, 5, C_GOLD); }   /* (playing: big and bright) */
    else cv_disc(x, y, 2, C_GOLD);
}
static void draw_kit_letter(uint32_t sel)
{
    char t[4] = {pat_ch(P_BD), 0, 0, 0};
    if (!sc.comp_on && PSEL(P_BD) != play_pat[P_BD]) { t[1] = '>'; t[2] = (char)('A' + PSEL(P_BD)); }
    {
        uint16_t ink = !MELODIC(sel) ? C_WINE : C_INK2;
        int32_t xx = lbox(kit_x, kit_y, t[0], &SERIF_M, ink, ink, C_PAPER);
        if (t[1]) { ctext(xx + 2, kit_y, &SERIF_M, ">", ink); lbox(xx + 12, kit_y, t[2], &SERIF_M, ink, ink, C_PAPER); }
    }
}
static void draw_orchestra(uint32_t sel, uint32_t flash, uint32_t full)
{
    static uint32_t shown = ~0u, shown_l[NINS], kit_shown = ~0u;
    uint32_t i, k1 = sel | sel_all << 4, k2 = 0;
    for (i = 0; i < NINS; i++) { k1 |= (uint32_t)MUTE(i) << (5u + i); k2 |= (uint32_t)SOLO(i) << i; }
    if (k1 != chart_k1 || k2 != chart_k2) { build_chart(sel); chart_k1 = k1; chart_k2 = k2; full = 1; }
    if (full) {                                 /* the whole stage */
        cv_begin(240, HOME_H, C_PAPER);
        for (i = 0; i < 240u * HOME_H; i++) cv_px[i] = chart_bg[i];
        for (i = 0; i < NINS; i++) draw_dot(i, dot_x[i], dot_y[i], flash >> i & 1u, sel);
        draw_kit_letter(sel);
        cv_blit(0, BODY_Y);
    } else                                      /* only the dots that changed: a few pixels, not the stage */
        for (i = 0; i < NINS; i++) {            /* (a dot and its pattern letters: 7 px left of it .. 51 right) */
            int32_t x0 = dot_x[i] - 7, y0 = dot_y[i] - 10, xx, yy, w = MELODIC(i) ? 63 : 14;   /* (drums: not into the next) */
            uint32_t lt = MELODIC(i) ? (uint32_t)pat_ch(i) | (uint32_t)PSEL(i) << 8 : 0u;
            if (!((flash ^ shown) >> i & 1u) && lt == (MELODIC(i) ? shown_l[i] : 0u)) continue;
            if (x0 < 0) x0 = 0;
            if (x0 + w > 236) w = 236 - x0;
            if (y0 < 0 || y0 + 21 > HOME_H) continue;
            cv_begin((uint32_t)w, 21, C_PAPER);
            for (yy = 0; yy < 21; yy++)
                for (xx = 0; xx < w; xx++) cv_px[yy * w + xx] = chart_bg[(uint32_t)(y0 + yy) * 240u + (uint32_t)(x0 + xx)];
            draw_dot(i, dot_x[i] - x0, 10, flash >> i & 1u, sel);
            cv_blit((uint32_t)x0, (uint32_t)(BODY_Y + y0));
            shown_l[i] = lt;
        }
    if (!full && ((uint32_t)pat_ch(P_BD) | PSEL(P_BD) << 8) != kit_shown) {   /* the kit's letter changed */
        int32_t xx, yy, w = 50, h = 20;
        cv_begin((uint32_t)w, (uint32_t)h, C_PAPER);
        for (yy = 0; yy < h; yy++)
            for (xx = 0; xx < w; xx++) cv_px[yy * w + xx] = chart_bg[(uint32_t)(kit_y + yy) * 240u + (uint32_t)(kit_x + xx)];
        cv_oy = 0;
        {
            int32_t ox = kit_x, oy = kit_y;
            kit_x = 0; kit_y = 0; draw_kit_letter(sel); kit_x = ox; kit_y = oy;
        }
        cv_blit((uint32_t)kit_x, (uint32_t)(BODY_Y + kit_y));
    }
    kit_shown = (uint32_t)pat_ch(P_BD) | PSEL(P_BD) << 8;
    shown = flash;
    for (i = 0; i < NINS; i++) shown_l[i] = (uint32_t)pat_ch(i) | (uint32_t)PSEL(i) << 8;
}

static void draw_foot_home(uint32_t sel, const char *msg)
{
    char s[32], *p;
    foot_begin();
    if (!foot_msg(msg)) {
        uint32_t i, all = 1, mute = MUTE(sel);
        for (i = P_BD; i < NINS; i++) all &= MUTE(i);
        if (sel_all) { cat(s, "PERCUSSION"); mute = all; }
        else { p = cat(s, MELODIC(sel) ? SEC_ABBR[INS[sel].s] : ""); if (MELODIC(sel)) p = cat(p, " "); cat(p, MELODIC(sel) ? INS[sel].reg : DTY(sel).name); }
        lbox(ctext(10, 5, &SERIF_M, s, C_INK) + 5, 5, (char)('A' + PSEL(sel)), &SERIF_M, C_INK, C_INK2, C_PAPER);
        cv_rect(162, 6, 1, 15, C_RULE);
        if (mute) cat(s, "TACET");
        else if (sel_all) cat(s, "8 DRUMS");
        else { p = cat(s, MELODIC(sel) ? (SOLO(sel) ? "SOLO " : "TUTTI ") : "KIT "); cat(p, EXPR_NAME[expr_band(EXPR(PK(sel)))]); }
        ctext_c(199, 8, &SERIF_S, s, mute || (MELODIC(sel) && SOLO(sel)) ? C_WINE : C_INK2);
    }
    cv_blit(0, FOOT_Y);
}

/* ---- a section's setup: its parts as rows, each with its steps */
#define KEY0 53u                                /* key 0 = F3 */
static int black_key(uint32_t n) { return (0x54Au >> (n % 12u)) & 1u; }
static uint32_t has_notes(const mstep_t *m) { return (uint32_t)(m->n[0] | m->n[1] | m->n[2]); }
static uint32_t eff_len(uint32_t i, uint32_t q, uint32_t st)   /* how long a step's notes sound: their length, cut by the
                                                   next step with notes, and by the pattern's end */
{
    const mstep_t *m = sc.mel[i][q % NPAT];
    uint32_t L = m[st].len ? m[st].len : 1u, len = plen(i, q), k;
    for (k = 1; k < L && st + k < len; k++) if (has_notes(&m[st + k])) return k;
    return st + L > len ? len - st : L;
}
static int32_t cover(uint32_t i, uint32_t q, uint32_t st)   /* the step whose notes sound at st, or -1 */
{
    int32_t s0;
    for (s0 = (int32_t)st; s0 >= 0; s0--)
        if (has_notes(&sc.mel[i][q % NPAT][s0])) return (uint32_t)s0 + eff_len(i, q, (uint32_t)s0) > st ? s0 : -1;
    return -1;
}
static char *len_name(char *d, uint32_t L)      /* a length in steps, as music calls it */
{
    static const char *const NM[17] = {"", "16th", "8th", "dotted 8th", "quarter", "", "dotted quarter", "", "half", "",
                                       "", "", "dotted half", "", "", "", "whole"};
    if (L <= 16u && NM[L][0]) return cat(d, NM[L]);
    if (L == 32u) return cat(d, "2 whole");
    if (L == 64u) return cat(d, "4 whole");
    d = catu(d, L, 0);
    return cat(d, " steps");
}

static uint32_t pat_used(uint32_t r, uint32_t q);
static void slider(int32_t x, int32_t y, int32_t w, int32_t v, int32_t vmax, int on);
static void draw_setup_part(uint32_t i)         /* a melodic part's page: how it plays, and its patterns */
{
    static const char *const VN[4] = {"off", "light", "normal", "wide"};
    uint32_t q = PSEL(i), silent = MUTE(i), k, e = EXPR(i), v = VIB(i) & 3u;
    int32_t db = BALN(i);
    char t[16], *p;
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    ctext_c(66, 4, &SERIF_S, "EXPRESSION", C_INK2);   /* how strongly it plays; who plays */
    ctext_c(66, 16, &ITAL_L, EXPR_NAME[expr_band(e)], silent ? C_RULE : C_WINE);
    ctext_c(180, 4, &SERIF_S, "PLAYERS", C_INK2);
    for (k = 0; k < 7u; k++) {                 /* the players: seven chairs, those playing wine (solo: the middle one) */
        uint32_t on = !silent && (!SOLO(i) || k == 3u);
        chair(131 + (int32_t)k * 14, 18, on ? C_WINE : C_FAINT, on ? C_WINE : C_RULE);
    }
    ctext_c(180, 25, &ITAL_M, silent ? "tacet" : SOLO(i) ? "solo" : "tutti", silent ? C_RULE : C_WINE);   /* (as a score writes
                                                   it: everyone, one player, silent) */
    cv_rect(10, 62, 220, 1, C_RULE);
    ctext(14, 70, &SERIF_S, "VIBRATO", C_INK2);       /* its vibrato: a wave, as wide as it is (off: flat) */
    {
        static const int8_t SIN16[16] = {0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24};
        int32_t amp = (int32_t)v * 2, x;
        for (x = 0; x < 64; x++)
            cv_rect(100 + x, 76 + SIN16[x & 15] * amp / 64, 1, 2, v ? C_WINE : C_FAINT);
    }
    ctext_r(230, 70, &SERIF_S, VN[v], v ? C_INK : C_RULE);
    if (SORD(i, q) && INS[i].ty != TY_OBOE && INS[i].ty != TY_BSN)   /* con sordino: muted (the strings, the brass) */
        ctext_c(180, 42, &ITAL_M, "con sordino", C_WINE);
    ctext(14, 94, &SERIF_S, "BALANCE", C_INK2);       /* its balance: a slider, 0 dB marked */
    slider(100, 102, 88, db + 30, 36, 0);
    cv_rect(100 + 30 * 88 / 36, 96, 1, 13, C_INK2);
    p = t; if (db > 0) *p++ = '+'; else if (db < 0) *p++ = '-';
    p = catu(p, (uint32_t)(db < 0 ? -db : db), 0); cat(p, " dB");
    ctext_r(230, 94, &SERIF_S, t, db ? C_INK : C_RULE);
    cv_rect(10, 118, 220, 1, C_RULE);
    ctext(14, 124, &SERIF_S, "PATTERNS", C_INK2);     /* A-J: the chosen one wine, one with notes dark, an empty one pale */
    for (k = 0; k < NPAT; k++) {
        int32_t x = 14 + (int32_t)k * 21, on = k == q, used = (int32_t)pat_used(i, k);
        cv_rect(x, 142, 19, 26, on ? C_WINE : used ? C_ROWB : C_PAPER);
        cv_frame(x, 142, 19, 26, on ? C_WINE : used ? C_INK2 : C_FAINT);
        if (play_pat[i] % NPAT == k && running) cv_rect(x + 3, 170, 13, 2, C_GOLD);   /* (the one playing) */
        t[0] = (char)('A' + k); t[1] = 0;
        ctext_c(x + 10, 146, &SERIF_M, t, on ? C_CARD : used ? C_INK : C_RULE);
    }
    cv_blit(0, BODY_Y);
}
static void draw_setup(uint32_t sel, uint32_t cursor, uint32_t run)
{
    uint32_t sec = INS[sel].s, i0 = SEC_FIRST[sec], n = SEC_FIRST[sec + 1u] - i0, r, k, page = cursor / BAR;
    if (MELODIC(sel)) { draw_setup_part(sel); return; }
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    for (r = 0; r < n; r++) {
        uint32_t i = i0 + r, len = LEN(i), q = PSEL(i), silent = MUTE(i), on = i == sel;
        int32_t ry, rh, x0, pitch, lw;
        if (MELODIC(i)) { ry = 8 + (int32_t)r * 88; rh = 78; x0 = 66; pitch = 10; lw = 56; }
        else { ry = 4 + (int32_t)r * 19; rh = 17; x0 = 98; pitch = 128 / (int32_t)BAR; lw = 87; }
        cv_rect(7, ry, lw, rh, on ? C_WINE : C_CARD);
        cv_frame(7, ry, lw, rh, on ? C_WINE : C_INK2);
        if (MELODIC(i)) {                       /* the label: the register, its pattern, section / solo / muted */
            ctext_c(7 + lw / 2, ry + 12, &SERIF_M, INS[i].reg, on ? C_CARD : silent ? C_RULE : C_INK);
            lbox(7 + lw / 2 - 6, ry + 36, (char)('A' + q), &SERIF_S, on ? C_CARD : C_INK, on ? C_CARD : C_INK2, on ? C_WINE : C_CARD);
            ctext_c(7 + lw / 2, ry + 56, &SERIF_S, MUTE(i) ? "tacet" : SOLO(i) ? "solo" : "tutti",
                    on ? RGB(230, 180, 180) : silent ? C_RULE : C_INK2);
        } else
            ctext(10, ry + 2, &SERIF_S, DTY(i).name, on ? C_CARD : silent ? C_RULE : C_INK);   /* (its sound: ALGORITHM) */
        cv_rect(x0 - 1, ry, (int32_t)BAR * pitch + 1, rh, C_CARD);
        for (k = 0; k <= BAR; k++) cv_rect(x0 - 1 + (int32_t)k * pitch, ry, 1, rh, k % GRP == 0u ? C_RULE : C_FAINT);
        cv_frame(x0 - 1, ry, (int32_t)BAR * pitch + 1, rh, C_INK2);
        if (MELODIC(i)) {                       /* each note a bar as long as it sounds, at its height */
            const mstep_t *m = sc.mel[i][q];
            int32_t pmin = 127, pmax = 0;
            uint32_t j;
            for (k = 0; k < len; k++) for (j = 0; j < NV; j++) if (m[k].n[j]) {
                if (m[k].n[j] < pmin) pmin = m[k].n[j];
                if (m[k].n[j] > pmax) pmax = m[k].n[j];
            }
            for (k = 0; k < len; k++) {
                uint32_t L, a, b;
                if (!has_notes(&m[k])) continue;
                L = eff_len(i, q, k);
                a = k > page * BAR ? k : page * BAR; b = k + L < page * BAR + BAR ? k + L : page * BAR + BAR;
                if (a >= b) continue;
                for (j = 0; j < NV; j++) if (m[k].n[j]) {
                    int32_t y = pmax > pmin ? ry + rh - 8 - (int32_t)(m[k].n[j] - (uint32_t)pmin) * (rh - 14) / (pmax - pmin) : ry + rh / 2 - 2;
                    cv_rect(x0 + (int32_t)(a - page * BAR) * pitch, y, (int32_t)(b - a) * pitch - 1, 4, silent ? C_RULE : WINES[m[k].dyn & 3u]);
                }
            }
        } else
            for (k = 0; k < BAR; k++) {
                uint32_t st = page * BAR + k, h = st < len ? sc.drum[i - P_BD][q][st] : 0u;
                if (h) cv_rect(x0 + (int32_t)k * pitch, ry + 2, pitch - 1, rh - 4, silent ? C_RULE : WINES[(h - 1u) & 3u]);
            }
        for (k = 0; k < BAR; k++) {             /* past the end; where the music is; the cursor */
            uint32_t st = page * BAR + k;
            int32_t x = x0 + (int32_t)k * pitch;
            if (st >= len) cv_rect(x, ry + 1, pitch - 1, rh - 2, C_FAINT);
            if (run && play_pat[i] == q && cur[i] == st) cv_rect(x - 1, ry - 2, pitch + 1, 2, C_INK);
            if (on && st == cursor) { cv_frame(x - 2, ry - 1, pitch + 3, rh + 2, C_GOLD); cv_frame(x - 1, ry, pitch + 1, rh, C_GOLD); }
        }
    }
    ctext(8, 157, &SERIF_S, "K1 bars  K2 tune  K3 dynamic  K4 level", C_INK2);   /* (the knobs, then the buttons) */
    ctext(8, 170, &SERIF_S, "ALG sound  OCT page  EDIT curve", C_INK2);
    cv_blit(0, BODY_Y);
}

static void draw_foot_setup(uint32_t sel, uint32_t cursor, uint32_t dyn, const char *msg)   /* a part: its knobs; the drum
                                                   machine (its knobs in the body, as on the roll): what is at the cursor */
{
    char s[48], *p;
    foot_begin();
    if (!foot_msg(msg)) {
        if (MELODIC(sel))
            ctext_c(120, 8, &SERIF_S, INS[sel].ty == TY_OBOE || INS[sel].ty == TY_BSN ? "K1 expr  K2 vibrato  K3 bal"
                                                   : "K1 expr  K2 vibrato  K3 bal  K4 sord", C_INK2);   /* (the winds: no mute) */
        else {
            uint32_t h = cursor < LEN(sel) ? DH(sel, cursor) : 0u;
            p = catu(s, cursor + 1u, 0); p = cat(p, "/"); p = catu(p, LEN(sel), 0); p = cat(p, "  ");
            if (h) { cat(p, DYN_NAME[(h - 1u) & 3u]); ctext(10, 8, &SERIF_S, s, C_INK); }
            else { p = cat(p, "rest  "); cat(p, DYN_NAME[dyn & 3u]); ctext(10, 8, &SERIF_S, s, C_INK2); }
            p = s;
            if (TIMPANI(sel)) { p = note_name(p, sc.tune[sel]); p = cat(p, "  "); }
            p = cat(p, "level "); catu(p, sc.vol[sel], 0);
            ctext_r(230, 8, &SERIF_S, s, C_INK);
        }
    }
    cv_blit(0, FOOT_Y);
}

/* ---- a melodic pattern: a piano roll, two octaves by 16 steps */
static uint32_t ed_base(uint32_t i)             /* the note key 0 plays for instrument i */
{
    int32_t o = (int32_t)sc.tune[i] - 4;
    return (uint32_t)((int32_t)KEY0 + 12 * o);
}
#define ROLL_N 24u                              /* notes shown */
static uint8_t view_lo[NINS];                   /* the lowest note the roll shows (0: not set yet) */
static void view_fit(uint32_t i, uint32_t n)    /* scroll so note n shows */
{
    if (!view_lo[i]) view_lo[i] = (uint8_t)(ed_base(i) - 5u);   /* from the C under key 0 */
    if (n > 0u && n < 128u) {
        if (n < view_lo[i]) view_lo[i] = (uint8_t)n;
        if (n > view_lo[i] + ROLL_N - 1u) view_lo[i] = (uint8_t)(n - ROLL_N + 1u);
    }
}

static void draw_roll(uint32_t i, uint32_t cursor, uint32_t run)
{
    uint32_t page = cursor / BAR, len = LEN(i), q = PSEL(i), lo, k, r, p0 = page * BAR, j;
    const mstep_t *m = sc.mel[i][q];
    int32_t kx = 4, kw = 32, gx = 38, gy = 15, pw = 192 / (int32_t)BAR, rh = 6, gw = (int32_t)BAR * pw, gh = (int32_t)ROLL_N * rh, cs;
    cs = cover(i, q, cursor);
    view_fit(i, cs >= 0 ? (m[cs].n[0] ? m[cs].n[0] : m[cs].n[1] ? m[cs].n[1] : m[cs].n[2]) : 0u);
    lo = view_lo[i];
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    for (k = 0; k < BAR / BEAT; k++) {          /* the beats */
        char b[3];
        fmt_u(b, k + 1u, 0);
        ctext(gx + (int32_t)(k * BEAT) * pw + 2, 0, &SERIF_S, b, C_INK2);
    }
    for (r = 0; r < ROLL_N; r++) {              /* the rows: a key each, the black keys' rows darker */
        uint32_t n = lo + ROLL_N - 1u - r;
        int32_t y = gy + (int32_t)r * rh;
        cv_rect(gx, y, gw, rh, black_key(n) ? C_ROWB : C_CARD);
        cv_rect(kx, y, kw, rh, C_KEYW);
        if (black_key(n)) cv_rect(kx, y, 15, rh, C_INK);
        if (n % 12u == 0u || n % 12u == 5u) cv_rect(kx, y + rh - 1, kw, 1, C_RULE);
        if (n % 12u == 0u) cv_rect(gx, y + rh - 1, gw, 1, C_RULE);
    }
    for (k = 0; k <= BAR; k++)
        cv_rect(gx + (int32_t)k * pw, gy, 1, gh, k % BAR == 0u ? C_INK2 : k % GRP == 0u ? C_RULE : C_FAINT);
    for (k = 0; k < BAR; k++)
        if (p0 + k >= len) cv_rect(gx + (int32_t)k * pw + 1, gy, pw - 1, gh, C_FAINT);
    {                                           /* the cursor: a gold column */
        int32_t x = gx + (int32_t)(cursor % BAR) * pw;
        for (r = 0; r < (uint32_t)gh; r++)
            for (k = 1; k < (uint32_t)pw; k++) {
                uint16_t *px = &cv_px[(uint32_t)(gy + (int32_t)r) * cv_w + (uint32_t)(x + (int32_t)k)];
                *px = swap16(mix565(swap16(*px), C_GOLD_L, 5));
            }
        cv_rect(x, gy - 1, 1, gh + 2, C_GOLD); cv_rect(x + pw, gy - 1, 1, gh + 2, C_GOLD);
    }
    for (k = 0; k < len; k++) {                 /* the notes: bars as long as they sound, a chord's together */
        uint32_t L, a, b;
        int32_t x, w;
        if (!has_notes(&m[k])) continue;
        L = eff_len(i, q, k);
        a = k > p0 ? k : p0; b = k + L < p0 + BAR ? k + L : p0 + BAR;
        if (a >= b) continue;
        x = gx + (int32_t)(a - p0) * pw + 1; w = (int32_t)(b - a) * pw - 1;
        for (j = 0; j < NV; j++) {
            uint32_t nn = m[k].n[j];
            int32_t y;
            if (!nn) continue;
            if (nn < lo) { cv_rect(x, gy + gh - 2, w, 2, WINES[m[k].dyn & 3u]); continue; }
            if (nn > lo + ROLL_N - 1u) { cv_rect(x, gy, w, 2, WINES[m[k].dyn & 3u]); continue; }
            y = gy + (int32_t)(lo + ROLL_N - 1u - nn) * rh;
            cv_rect(x, y, w, rh, WINES[m[k].dyn & 3u]);
            cv_frame(x, y, w, rh, (int32_t)k == cs ? C_GOLD : C_WINE);
            if (a == k && j == 0u) {            /* its articulation, over its start: . > ^, or a line along it */
                uint32_t ar = m[k].dyn >> 2 & 7u;
                if (ar == ART_STAC) cv_rect(x + 2, y - 3, 2, 2, C_INK);
                else if (ar == ART_ACC) { cv_line(x + 1, y - 6, x + 5, y - 4, C_INK); cv_line(x + 5, y - 4, x + 1, y - 2, C_INK); }
                else if (ar == ART_MARC) { cv_line(x + 1, y - 2, x + 3, y - 6, C_INK); cv_line(x + 3, y - 6, x + 5, y - 2, C_INK); }
                else if (ar == ART_LEG) cv_rect(x, y - 2, w + 3, 1, C_INK);
                else if (ar == ART_PIZZ) cv_ring(x + 3, y - 4, 2, C_INK);   /* (plucked: a small ring) */
                else if (ar == ART_TREM) { int32_t z; for (z = 0; z < 3; z++) cv_line(x + z * 3, y - 2, x + 2 + z * 3, y - 6, C_INK); }   /* (///) */
                else if (ar == ART_TRILL) { int32_t z; for (z = 0; z < 4; z++) cv_line(x + z * 3, y - (z & 1 ? 6 : 3), x + 3 + z * 3, y - (z & 1 ? 3 : 6), C_INK); }   /* (a wavy line) */
            }
        }
    }
    for (r = 0; r < ROLL_N; r++) {
        uint32_t n = lo + ROLL_N - 1u - r;
        if (n % 12u == 0u) { char t[5]; note_name(t, n); ctext(kx + 16, gy + (int32_t)r * rh - 5, &SERIF_S, t, C_INK2); }
    }
    cv_frame(gx, gy, gw + 1, gh, C_INK2);
    if (run && play_pat[i] == q && cur[i] / BAR == page) {   /* where the music is */
        int32_t x = gx + (int32_t)(cur[i] % BAR) * pw;
        cv_rect(x, gy - 3, 2, gh + 3, C_INK);
    }
    ctext(8, gy + gh + 7, &SERIF_S, "K1 bars  K2 length  K3 dynamic  K4 note", C_INK2);
    cv_blit(0, BODY_Y);
}

/* the footer of the roll and of the music: the step, its notes, and the length KNOB 2 sets (as music names it) */
static void draw_foot_roll(uint32_t i, uint32_t cursor, uint32_t dyn, uint32_t nlen, const char *msg)
{
    char t[48], *p;
    foot_begin();
    if (!foot_msg(msg)) {
        uint32_t q = PSEL(i), j;
        int32_t s0 = cover(i, q, cursor);
        p = catu(t, cursor + 1u, 0); p = cat(p, "/"); p = catu(p, LEN(i), 0); p = cat(p, "  ");
        if (s0 >= 0) {
            const mstep_t *m = &sc.mel[i][q][s0];
            static const char *const AN[NART] = {"", " stacc.", " accent", " legato", " marc.", " pizz.", " trem.", " trill"};
            for (j = 0; j < NV; j++) if (m->n[j]) { p = note_name(p, m->n[j]); p = cat(p, " "); }
            p = cat(p, DYN_NAME[m->dyn & 3u]); cat(p, AN[(m->dyn >> 2) % NART]);
            ctext(10, 8, &SERIF_S, t, C_INK);
            p = cat(t, "K2: "); len_name(p, m->len ? m->len : 1u);
        } else {
            p = cat(p, "rest  "); cat(p, DYN_NAME[dyn & 3u]);
            ctext(10, 8, &SERIF_S, t, C_INK2);
            p = cat(t, "next: "); len_name(p, nlen);
        }
        ctext_r(230, 8, &SERIF_S, t, s0 >= 0 ? C_INK : C_INK2);   /* (as the rest of the line) */
    }
    cv_blit(0, FOOT_Y);
}

/* ---- a melodic pattern as music: two bars, a staff each. QUARTET's SCORE page, made for one voice a part:
 * treble clef (bass clef for the LOW parts), 4/4, black keys as flats, a bar far off the staff written 1-2 octaves
 * nearer under 8va / 15ma / 8vb / 15mb, notes spaced by their length, ties across beats and bars, the cursor as
 * a gold mark under its note, the note playing in wine. Symbols: Noto Music (music_sym.h, SIL OFL 1.1). */
#include "music_sym.h"
#define SC_LS 8                                 /* the staff: 8 px between its lines */
#define SC_TOP 26                               /* its top line, in the strip */
#define KS_W (KEYSIG ? 7 * (KEYSIG < 0 ? -KEYSIG : KEYSIG) + 4 : 0)   /* the key signature's width */
#define SC_X0 (42 + KS_W)                       /* the notes start here (the clef, the key, the time signature before) */
#define SC_STEM 24
#define SC_H 84                                 /* a strip: one bar (two, and the knobs' guide under them) */
typedef struct {
    uint8_t a, d, n, tie;                       /* start, length (16ths), notes (0: a rest); tie: 1 to the next piece,
                                                   2 from before the bar, 4 on past the bar */
    uint8_t m[NV];                              /* the notes as drawn */
    int8_t arr[NV];                             /* octaves each really is above (+) / below (-) where it is drawn */
    uint8_t acc;                                /* bit j: note j an accidental; bit 4 + j: a natural (else a flat) */
    uint8_t art;                                /* its articulation (the note's first piece only) */
    int16_t x;
} sc_ev_t;

static int32_t sc_dia(uint32_t m)               /* a note's place on the staff, a line or space a step (flats: Db = D) */
{
    static const uint8_t L[12] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6}, LS[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    return (int32_t)(m / 12u) * 7 + (KEYSIG > 0 ? LS : L)[m % 12u] - 7;   /* (a sharp key: C# on the C line) */
}
static int32_t sc_y(int32_t p) { return SC_TOP + 4 * SC_LS - p * (SC_LS / 2); }   /* p 0 = the bottom line */
static void sc_sharp(int32_t x, int32_t yc, uint16_t c)   /* a sharp, centred on yc (the symbols have none) */
{
    int32_t k;
    cv_rect(x + 1, yc - 7, 1, 14, c); cv_rect(x + 4, yc - 8, 1, 14, c);
    for (k = 0; k < 6; k++) { cv_rect(x - 1 + k, yc - 3 - k / 3, 1, 2, c); cv_rect(x - 1 + k, yc + 2 - k / 3, 1, 2, c); }
}
static void sc_sym(int32_t x, int32_t y, uint32_t id, uint16_t c)   /* a music symbol, top-left at x, y */
{
    const msym_t *m = &MSYM[id];
    const uint8_t *d = MSYM_DATA + m->off;
    uint32_t bpr = (m->w + 1u) / 2u, gx, gy;
    for (gy = 0; gy < m->h; gy++)
        for (gx = 0; gx < m->w; gx++) {
            uint32_t v = d[gy * bpr + gx / 2u];
            v = (gx & 1u) ? (v & 15u) : (v >> 4);
            if (v) cv_blend(x + (int32_t)gx, y + (int32_t)gy, c, v + (v >> 3));
        }
}
static void sc_head(int32_t x, int32_t y, uint16_t c, int hollow)   /* a notehead, 9 x 7, centred on x, y */
{
    static const uint16_t FULLH[7] = {0x078, 0x0FE, 0x1FE, 0x1FF, 0x0FF, 0x0FE, 0x03C};
    static const uint16_t OPENH[7] = {0x07C, 0x0C6, 0x183, 0x183, 0x183, 0x0C6, 0x07C};
    int32_t i, j;
    for (j = 0; j < 7; j++)
        for (i = 0; i < 9; i++)
            if ((hollow ? OPENH : FULLH)[j] >> (8 - i) & 1u) cv_pset(x - 4 + i, y - 3 + j, c);
}
static void sc_ledgers(int32_t x, int32_t p, uint16_t c)
{
    int32_t q;
    for (q = -2; q >= p; q -= 2) cv_rect(x - 7, sc_y(q), 15, 1, c);
    for (q = 10; q <= p; q += 2) cv_rect(x - 7, sc_y(q), 15, 1, c);
}
static void sc_vline(int32_t x, int32_t y0, int32_t y1, uint16_t c) { cv_rect(x, y0 < y1 ? y0 : y1, 1, (y0 < y1 ? y1 - y0 : y0 - y1) + 1, c); }
static void sc_flag(int32_t sx, int32_t tip, int up, uint32_t n, uint16_t c)   /* a single 8th (n 1) / 16th (n 2): its flags */
{
    static const int8_t FL[11][2] = {{1, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {5, 6}, {5, 7}, {5, 8}, {4, 9}, {4, 10}};
    uint32_t k, i;
    for (k = 0; k < n; k++)
        for (i = 0; i < (k ? 9u : 11u); i++) {
            int32_t dy = (int32_t)k * 6 + FL[i][1];
            cv_rect(sx + FL[i][0], up ? tip + dy : tip - dy, 2, 1, c);
        }
}
static void sc_tie(int32_t x0, int32_t x1, int32_t y, int above, uint16_t c)   /* a tie's arc from x0 to x1 */
{
    int32_t n = x1 - x0, i;
    if (n < 2) return;
    for (i = 0; i <= n; i++) {
        int32_t d = 12 * i * (n - i) / (n * n), yy = above ? y - 3 - d : y + 3 + d;
        cv_pset(x0 + i, yy, c);
        if (i * 7 > n && i * 7 < n * 6) cv_pset(x0 + i, yy + (above ? -1 : 1), c);
    }
}
static char *ts_name(char *p, uint32_t ts)       /* "6/8" */
{
    p = catu(p, TSIG[ts].num, 0); *p++ = '/'; return catu(p, TSIG[ts].den, 0);
}
static void sc_rest(int32_t x, uint32_t d, uint16_t c)
{
    int32_t mid = sc_y(4);
    if (d >= 16u) cv_rect(134, sc_y(6), 10, 4, c);          /* a whole bar's rest: in its middle */
    else if (d >= 8u) cv_rect(x - 4, mid - 4, 10, 4, c);
    else if (d >= 4u) sc_sym(x - 3, mid - 11, MS_QREST, c);
    else if (d >= 2u) sc_sym(x - 3, mid - 7, MS_EREST, c);
    else sc_sym(x - 3, mid - 7, MS_SREST, c);
}
static void sc_mark(int32_t x, uint16_t c)      /* the cursor: a dotted gold box round the note (or rest) */
{
    int32_t k, x0 = x - 10, x1 = x + 10, y0 = SC_TOP - 10, y1 = SC_TOP + 4 * SC_LS + 10;
    for (k = x0; k <= x1; k += 3) { cv_rect(k, y0, 2, 1, c); cv_rect(k, y1, 2, 1, c); }
    for (k = y0; k <= y1; k += 3) { cv_rect(x0, k, 1, 2, c); cv_rect(x1, k, 1, 2, c); }
}

/* one note or rest, a bar's piece of it: the lengths notation writes from where it starts (a note across a beat
 * is tied: 16th, 8th, dotted 8th inside a beat; quarter, dotted quarter, half, dotted half, whole from a beat) */
static uint32_t sc_split(uint32_t a, uint32_t d, int rest, uint8_t *pa, uint8_t *pd)
{
    uint32_t n = 0;
    while (d && n < 16u) {
        uint32_t c[8], nc = 0, i, l = 1;
        if (GRP == 6u) {                        /* compound: a dotted half, a dotted quarter from a beat; inside one, a
                                                   quarter from an 8th, a dotted 8th, an 8th, a 16th */
            uint32_t r = 6u - a % 6u;
            if (r == 6u && a % 12u == 0u && !rest) c[nc++] = 12;
            if (r == 6u && !rest) c[nc++] = 6;
            if (r >= 4u && a % 2u == 0u) c[nc++] = 4;
            if (!rest && r >= 3u) c[nc++] = 3;
            if (r >= 2u) c[nc++] = 2;
            c[nc++] = 1;
        } else if (a % 4u == 0u) {
            if (a == 0u) c[nc++] = 16;
            if (a == 0u && !rest) c[nc++] = 12;
            if (a % 8u == 0u) c[nc++] = 8;
            if (a % 8u == 0u && !rest) c[nc++] = 6;
            c[nc++] = 4;
            if (!rest) c[nc++] = 3;
            c[nc++] = 2; c[nc++] = 1;
        } else {
            uint32_t r = 4u - a % 4u;
            if (!rest && r >= 3u) c[nc++] = 3;
            if (r >= 2u) c[nc++] = 2;
            c[nc++] = 1;
        }
        for (i = 0; i < nc; i++) if (c[i] <= d) { l = c[i]; break; }
        pa[n] = (uint8_t)a; pd[n] = (uint8_t)l; n++;
        a += l; d -= l;
    }
    return n;
}
static int sc_has(uint32_t i, uint32_t st)     /* notes start here */
{
    return st < LEN(i) && has_notes(&MS(i, st));
}
static uint32_t sc_rests(sc_ev_t *ev, uint32_t n, uint32_t a, uint32_t d)
{
    uint8_t pa[16], pd[16];
    uint32_t k, np = sc_split(a, d, 1, pa, pd);
    for (k = 0; k < np && n < 32u; k++, n++) { ev[n].a = pa[k]; ev[n].d = pd[k]; ev[n].n = 0; ev[n].tie = 0; ev[n].acc = 0; ev[n].art = 0; }
    return n;
}
static uint32_t sc_bar(uint32_t i, uint32_t s0, sc_ev_t *ev, uint32_t cursor)   /* the bar's notes and rests, in pieces;
                                                   split at the cursor too (its box is then where a note goes) */
{
    uint32_t n = 0, s = 0, cont = 0, crem = 0, cq = 0, q = PSEL(i), j, midc = 0;
    uint32_t cut = cursor >= s0 && cursor < s0 + BAR ? cursor - s0 : 99u;
    uint8_t pa[16], pd[16];
    if (s0 < LEN(i) && !has_notes(&MS(i, s0))) {   /* a chord from before the bar still sounding at its start */
        int32_t c = cover(i, q, s0);
        if (c >= 0) { cont = 1; cq = (uint32_t)c; crem = eff_len(i, q, cq) - (s0 - cq); }
    }
    while (s < BAR && n < 32u) {
        uint32_t st = s0 + s, nx = s + 1u, src = st, k, np, d, tl = 0, isn = (uint32_t)sc_has(i, st), first = s == 0u && cont && !isn;
        uint32_t mid = midc && !isn;            /* (a note going on past the cursor: its second piece, tied) */
        while (nx < BAR && !sc_has(i, s0 + nx) && nx != cut) nx++;
        if (isn || first || mid) {
            uint32_t ln = first || mid ? crem : eff_len(i, q, st);
            if (first || mid) src = cq;
            midc = 0;
            d = ln < nx - s ? ln : nx - s;
            if (!d) d = 1;
            if (ln > BAR - s && nx >= BAR) tl = 4;
            if (ln > d && s + d == cut) { midc = 1; crem = ln - d; cq = src; tl = 1; }
            np = sc_split(s, d, 0, pa, pd);
            for (k = 0; k < np && n < 32u; k++, n++) {
                const mstep_t *m = &MS(i, src);
                ev[n].a = pa[k]; ev[n].d = pd[k]; ev[n].n = 0; ev[n].acc = 0;
                ev[n].art = (uint8_t)(k == 0u && !first && !mid ? (m->dyn >> 2 & 7u) : 0u);
                ev[n].tie = (uint8_t)((k + 1u < np ? 1u : tl) | (k == 0u && first ? 2u : 0u));
                for (j = 0; j < NV; j++) if (m->n[j]) { ev[n].arr[ev[n].n] = 0; ev[n].m[ev[n].n++] = m->n[j]; }
            }
            s += d;
        } else { n = sc_rests(ev, n, s, nx - s); s = nx; }
        if (s < nx) { n = sc_rests(ev, n, s, nx - s); s = nx; }
    }
    return n;
}
static uint32_t sc_wd(uint32_t d)               /* the room a length takes */
{
    return d >= 16u ? 40u : d >= 12u ? 34u : d >= 8u ? 28u : d >= 6u ? 24u : d >= 4u ? 20u : d >= 3u ? 17u : d >= 2u ? 15u : 11u;
}

static int32_t sc_xat(const sc_ev_t *ev, uint32_t n, uint32_t rel)   /* a step in the bar: where it is on the strip */
{
    uint32_t k;
    for (k = 0; k < n; k++)
        if (rel >= ev[k].a && rel < ev[k].a + ev[k].d) {
            int32_t x1 = k + 1u < n ? ev[k + 1u].x : 228, d = ev[k].d ? ev[k].d : 1;
            return ev[k].x - 6 + (x1 - ev[k].x) * (int32_t)(rel - ev[k].a) / d;
        }
    return 228;
}
static void score_system(uint32_t i, uint32_t b, int32_t y, uint32_t play, uint32_t step)   /* bar b, on its strip */
{
    sc_ev_t ev[32];
    uint32_t n, k, j, s0 = b * BAR, bass = INS[i].oct < 0, bot = bass ? 18u : 30u;
    int32_t ok = 0, x;
    char t[4];
    cv_begin(240, SC_H, C_PAPER);
    sides(SC_H);
    for (k = 0; k < 5u; k++) cv_rect(6, SC_TOP + (int32_t)k * SC_LS, 228, 1, C_INK2);
    cv_rect(6, SC_TOP, 1, 4 * SC_LS + 1, C_INK2); cv_rect(233, SC_TOP, 1, 4 * SC_LS + 1, C_INK2);
    if (bass) sc_sym(9, SC_TOP, MS_BASS, C_INK);
    else sc_sym(9, SC_TOP - 11, MS_TREBLE, C_INK);
    {                                           /* the key signature, in its standard places (bass: 2 steps lower) */
        static const int8_t SH[7] = {8, 5, 9, 6, 3, 7, 4}, FL[7] = {4, 7, 3, 6, 2, 5, 1};
        int32_t a, nks = KEYSIG < 0 ? -KEYSIG : KEYSIG;
        for (a = 0; a < nks; a++) {
            int32_t pp = (KEYSIG > 0 ? SH[a] : FL[a]) - (bass ? 2 : 0), kx = 31 + a * 7;
            if (KEYSIG > 0) sc_sharp(kx, sc_y(pp), C_INK);
            else sc_sym(kx - 2, sc_y(pp) - 11, MS_FLAT, C_INK);
        }
    }
    catu(t, TSX.num, 0);                        /* the time signature */
    ctext_c(35 + KS_W, SC_TOP - 2, &SERIF_M, t, C_INK);
    catu(t, TSX.den, 0);
    ctext_c(35 + KS_W, SC_TOP + 14, &SERIF_M, t, C_INK);
    fmt_u(t, b + 1u, 0);
    ctext(8, 2, &SERIF_S, t, C_INK2);           /* the bar's number */
    if (s0 >= LEN(i)) {                      /* past the pattern's end: it goes round, "as bar 1" (the repeat sign) */
        int32_t cx = 138, cy = sc_y(4);
        for (k = 0; k < 17u; k++) cv_rect(cx - ((int32_t)k - 8) - 2, cy + (int32_t)k - 8, 4, 1, C_INK);
        cv_disc(cx - 7, cy - 5, 2, C_INK); cv_disc(cx + 7, cy + 5, 2, C_INK);
        ctext_c(cx, SC_TOP + 4 * SC_LS + 6, &SERIF_S, "as bar 1", C_INK2);
        cv_blit(0, (uint32_t)y);
        return;
    }
    n = sc_bar(i, s0, ev, step);
    {                                           /* the bar 1-2 octaves nearer if it is off the staff, with 8va ... */
        static const int8_t KS[5] = {0, 1, -1, 2, -2}, LO[5] = {-4, -4, -2, -4, -2}, HI[5] = {12, 10, 12, 10, 12};
        uint32_t q, best = ~0u, bq = 0;
        for (q = 0; q < 5u; q++) {              /* the first that fits every note; else the one leaving them nearest */
            uint32_t off = 0;
            for (k = 0; k < n; k++)
                for (j = 0; j < ev[k].n; j++) {
                    int32_t pp = sc_dia(ev[k].m[j]) - (int32_t)bot - 7 * KS[q];
                    off += pp > HI[q] ? (uint32_t)(pp - HI[q]) : pp < LO[q] ? (uint32_t)(LO[q] - pp) : 0u;
                }
            if (!off) { bq = q; break; }
            if (off < best) { best = off; bq = q; }
        }
        ok = KS[bq];
        if (ok) {
            static const char *const LAB[5] = {"15mb", "8vb", "", "8va", "15ma"};
            int32_t ty = ok > 0 ? SC_TOP - 25 : SC_TOP + 4 * SC_LS + 6, ly = ok > 0 ? SC_TOP - 16 : SC_TOP + 4 * SC_LS + 15;
            int32_t tx = ctext(SC_X0 - 2, ty, &SERIF_S, LAB[ok + 2], C_INK) + 3;
            for (x = tx; x < 230; x += 5) cv_rect(x, ly, 3, 1, C_INK);
            cv_rect(230, ok > 0 ? ly : ly - 5, 1, 6, C_INK);
            for (k = 0; k < n; k++) for (j = 0; j < ev[k].n; j++) ev[k].m[j] = (uint8_t)((int32_t)ev[k].m[j] - 12 * ok);
        }
        for (k = 0; k < n; k++)                 /* a note still off (a bar spread wide): nearer, with an arrow an octave */
            for (j = 0; j < ev[k].n; j++) {
                int32_t m = ev[k].m[j], a = 0, lo = LO[bq] + (int32_t)bot, hi = HI[bq] + (int32_t)bot;
                while (m > 12 && sc_dia((uint32_t)m) > hi) { m -= 12; a++; }
                while (m < 115 && sc_dia((uint32_t)m) < lo) { m += 12; a--; }
                ev[k].m[j] = (uint8_t)m; ev[k].arr[j] = (int8_t)a;
            }
    }
    {                                           /* accidentals (a flat or natural holds to the bar's end), then the spacing */
        int8_t st[96];
        uint32_t tot = 0, cum = 0, need[32];
        for (k = 0; k < 96u; k++) {             /* the key: its letters sharp or flat in every octave */
            static const uint8_t SHL[7] = {3, 0, 4, 1, 5, 2, 6}, FLL[7] = {6, 2, 5, 1, 4, 0, 3};   /* F C G D A E B; B E A D G C F */
            int32_t a, nks = KEYSIG < 0 ? -KEYSIG : KEYSIG;
            st[k] = 0;
            for (a = 0; a < nks; a++) if (k % 7u == (KEYSIG > 0 ? SHL[a] : FLL[a])) st[k] = (int8_t)(KEYSIG > 0 ? 1 : -1);
        }
        for (k = 0; k < n; k++) {
            uint32_t any = 0;
            for (j = 0; j < ev[k].n; j++) {
                int32_t di = sc_dia(ev[k].m[j]), ac = black_key(ev[k].m[j]) ? (KEYSIG > 0 ? 1 : -1) : 0;
                if (di < 0 || di >= 96) continue;
                if (ev[k].tie & 2u || (k && ev[k - 1].n && ev[k - 1].tie & 1u)) { st[di] = (int8_t)ac; continue; }
                if (st[di] != ac) { ev[k].acc |= (uint8_t)(1u << j | (ac ? 0u : 16u << j)); any = 1; }
                st[di] = (int8_t)ac;
            }
            need[k] = ev[k].n ? sc_wd(ev[k].d) + (any ? 9u : 0u) : sc_wd(ev[k].d) * 3u / 4u + 4u;
            tot += need[k];
        }
        if (!tot) tot = 1;
        for (k = 0; k < n; k++) {
            uint32_t ar = ev[k].n && need[k] > sc_wd(ev[k].d) ? 8u : 0u;
            ev[k].x = (int16_t)(SC_X0 + 2 + (int32_t)((cum + ar) * 180u / tot) + 5);
            cum += need[k];
        }
    }
    /* the notes and rests; stems, flags, beams a group at a time (notes shorter than a beat inside one beat) */
    for (k = 0; k < n;) {
        uint32_t g = k + 1u, q, up;
        int32_t sum = 0, cnt = 0, tip, yt[8] = {0}, yb[8] = {0}, sx[8] = {0};
        if (!ev[k].n) {
            sc_rest(ev[k].x, ev[k].d, C_INK);
            if (step >= s0 + ev[k].a && step < s0 + ev[k].a + ev[k].d) sc_mark(ev[k].x, C_GOLD);
            k++;
            continue;
        }
        if (ev[k].d < 4u)
            while (g < n && g - k < 8u && ev[g].n && ev[g].d < 4u && ev[g].a / GRP == ev[k].a / GRP && ev[g - 1u].a + ev[g - 1u].d == ev[g].a) g++;
        for (q = k; q < g; q++) for (j = 0; j < ev[q].n; j++) { sum += sc_dia(ev[q].m[j]) - (int32_t)bot; cnt++; }
        up = sum < 4 * cnt;
        for (q = k; q < g; q++) {
            sc_ev_t *e = &ev[q];
            int32_t pmin = 99, pmax = -99;
            uint16_t col = play >= s0 + e->a && play < s0 + e->a + e->d ? C_WINE : C_INK;
            for (j = 0; j < e->n; j++) {
                int32_t pp = sc_dia(e->m[j]) - (int32_t)bot, yy = sc_y(pp), a;
                sc_ledgers(e->x, pp, C_INK2);
                if (e->acc >> j & 1u) {
                    if (e->acc >> (4u + j) & 1u) sc_sym(e->x - 12, yy - 9, MS_NAT, col);
                    else if (KEYSIG > 0) sc_sharp(e->x - 11, yy, col);
                    else sc_sym(e->x - 12, yy - 11, MS_FLAT, col);
                }
                sc_head(e->x, yy, col, e->d >= 8u);
                if (e->d == 3u || e->d == 6u || e->d == 12u) cv_rect(e->x + 7, yy - (pp & 1 ? 1 : 4), 3, 3, col);
                for (a = 0; a < (e->arr[j] < 0 ? -e->arr[j] : e->arr[j]); a++) {   /* an arrow an octave it really is */
                    int32_t ay = yy + (e->arr[j] > 0 ? -10 - a * 6 : 5 + a * 6), ax = e->arr[j] > 0 ? e->x - 1 : e->x + 1, r;
                    for (r = 0; r < 4; r++) {
                        int32_t w = 1 + 2 * (e->arr[j] > 0 ? r : 3 - r);
                        cv_rect(ax - w / 2, ay + r, w, 1, C_GOLD);
                    }
                }
                if (e->tie & 1u && q + 1u < n) sc_tie(e->x + 4, ev[q + 1u].x - 4, yy, !up, col);   /* ties */
                if (e->tie & 2u) sc_tie(e->x - 13, e->x - 5, yy, !up, col);
                if (e->tie & 4u) sc_tie(e->x + 5, e->x + 13 < 232 ? e->x + 13 : 232, yy, !up, col);
                if (pp < pmin) pmin = pp;
                if (pp > pmax) pmax = pp;
            }
            if (step >= s0 + e->a && step < s0 + e->a + e->d) sc_mark(e->x, C_GOLD);
            if (e->art) {                       /* the articulation: under the notes (stem up), over them (stem down) */
                int32_t ym = up ? sc_y(pmin) + 9 : sc_y(pmax) - 9, sx = up ? 1 : -1;
                if (e->art == ART_STAC) cv_disc(e->x, ym, 1, col);
                else if (e->art == ART_ACC) { cv_line(e->x - 4, ym - 3, e->x + 4, ym, col); cv_line(e->x + 4, ym, e->x - 4, ym + 3, col); }
                else if (e->art == ART_MARC) { cv_line(e->x - 3, ym + 3 * sx, e->x, ym - 3 * sx, col); cv_line(e->x, ym - 3 * sx, e->x + 3, ym + 3 * sx, col); }
                else if (e->art == ART_LEG && q + 1u < n && ev[q + 1u].n) sc_tie(e->x + 3, ev[q + 1u].x - 3, up ? sc_y(pmin) + 3 : sc_y(pmax) - 3, !up, col);
                else if (e->art == ART_LEG) cv_rect(e->x - 4, ym, 9, 1, col);
                else if (e->art == ART_PIZZ && !(q > 0u && ev[q - 1u].art == ART_PIZZ)) ctext(e->x - 8, SC_TOP - 24, &SERIF_S, "pizz.", col);
                else if (e->art == ART_TREM) { int32_t z, yc = up ? sc_y(pmax) - 16 : sc_y(pmin) + 14; for (z = 0; z < 3; z++) cv_line(e->x - 3 + (up ? 3 : -4), yc + z * 4, e->x + 3 + (up ? 3 : -4), yc + z * 4 - 3, col); }   /* (three strokes on the stem) */
                else if (e->art == ART_TRILL) {   /* tr: just over the note (and its stem), clear of an 8va line */
                    int32_t ty = sc_y(pmax) - (up ? 42 : 22);
                    ctext(e->x - 5, ty < SC_TOP - 14 ? SC_TOP - 14 : ty, &ITAL_M, "tr", col);
                }
            }
            yt[q - k] = sc_y(up ? pmax : pmin); yb[q - k] = sc_y(up ? pmin : pmax);
            sx[q - k] = up ? e->x + 4 : e->x - 4;
        }
        if (ev[k].d >= 16u) { k = g; continue; }   /* (a whole note: no stem) */
        if (g - k == 1u) {
            uint16_t col = play >= s0 + ev[k].a && play < s0 + ev[k].a + ev[k].d ? C_WINE : C_INK;
            tip = up ? yt[0] - SC_STEM : yt[0] + SC_STEM;
            sc_vline(sx[0], yb[0], tip, col);
            if (ev[k].d < 4u) sc_flag(sx[0], tip, (int)up, ev[k].d == 1u ? 2u : 1u, col);
        } else {
            tip = yt[0];
            for (q = 0; q < g - k; q++) tip = up ? (yt[q] < tip ? yt[q] : tip) : (yt[q] > tip ? yt[q] : tip);
            tip = up ? tip - SC_STEM : tip + SC_STEM;
            for (q = k; q < g; q++) {
                uint16_t col = play >= s0 + ev[q].a && play < s0 + ev[q].a + ev[q].d ? C_WINE : C_INK;
                sc_vline(sx[q - k], yb[q - k], tip, col);
            }
            cv_rect(sx[0], up ? tip : tip - 3, sx[g - k - 1u] - sx[0] + 1, 4, C_INK);
            for (q = k; q < g; q++) {           /* 16ths: a second beam (a stub where alone) */
                int32_t y2 = up ? tip + 7 : tip - 10;
                uint32_t z = q - k;
                if (ev[q].d != 1u) continue;
                if (q + 1u < g && ev[q + 1u].d == 1u) cv_rect(sx[z], y2, sx[z + 1u] - sx[z] + 1, 4, C_INK);
                else if (!(q > k && ev[q - 1u].d == 1u)) {
                    if (q + 1u < g) cv_rect(sx[z], y2, 6, 4, C_INK);
                    else cv_rect(sx[z] - 5, y2, 6, 4, C_INK);
                }
            }
        }
        k = g;
    }
    {                                           /* hairpins: where the pattern's curve rises (<) or falls (>) in the bar */
        const curve_t *cv = &sc.curve[PK(i)][PSEL(i)];
        if (cv->on && cv->n >= 2u && n)
            for (k = 0; k + 1u < cv->n; k++) {
                int32_t pa = cv->p[k].pos, pb = cv->p[k + 1u].pos, va = cv->p[k].val, vb = cv->p[k + 1u].val;
                int32_t from = pa > (int32_t)s0 ? pa : (int32_t)s0, to = pb < (int32_t)(s0 + BAR) ? pb : (int32_t)(s0 + BAR);
                int32_t xa, xb, ha, hb, hy = SC_TOP + 4 * SC_LS + 18;
                if (pb <= pa || from >= to || (vb > va ? vb - va : va - vb) < 10) continue;
                xa = sc_xat(ev, n, (uint32_t)(from - (int32_t)s0)); xb = to >= (int32_t)(s0 + BAR) ? 228 : sc_xat(ev, n, (uint32_t)(to - (int32_t)s0));
                ha = (from - pa) * 6 / (pb - pa); hb = (to - pa) * 6 / (pb - pa);   /* (its opening: 0..6 px each side) */
                if (vb < va) { ha = 6 - ha; hb = 6 - hb; }   /* (a diminuendo closes) */
                cv_line(xa, hy - ha, xb, hy - hb, C_INK);
                cv_line(xa, hy + ha, xb, hy + hb, C_INK);
            }
    }
    if (play >= s0 && play < s0 + BAR && n) {   /* where the music is: a line, moving from note to note with it */
        int32_t px = SC_X0;
        for (k = 0; k < n; k++)
            if (play >= s0 + ev[k].a && play < s0 + ev[k].a + ev[k].d) {
                int32_t x1 = k + 1u < n ? ev[k + 1u].x : 228, d = ev[k].d ? ev[k].d : 1;
                px = ev[k].x - 6 + (x1 - ev[k].x) * (int32_t)(play - s0 - ev[k].a) / d;
                break;
            }
        cv_rect(px, SC_TOP - 12, 2, 4 * SC_LS + 24, C_INK);
    }
    cv_blit(0, (uint32_t)y);
}
static void draw_music(uint32_t i, uint32_t cursor, uint32_t run)
{
    uint32_t play = run && play_pat[i] == PSEL(i) ? cur[i] : 0xFFFFu, pg = cursor / (2u * BAR) * 2u;
    score_system(i, pg, BODY_Y, play, cursor);
    score_system(i, pg + 1u, BODY_Y + SC_H, play, cursor);
    cv_begin(240, BODY_H - 2u * SC_H, C_PAPER);   /* the knobs' jobs, as under the piano roll */
    sides(BODY_H - 2 * SC_H);
    ctext(8, 1, &SERIF_S, "K1 bars  K2 length  K3 dynamic  K4 note", C_INK2);
    cv_blit(0, BODY_Y + 2u * SC_H);
}

/* ---- the save page (SAVE; SAVE again: back): four slots, each the whole score. KNOB1 (or SELECT) picks the slot;
 * REC saves to it, PLAY loads it, each asking first; the same button again within 3 s does it (QUARTET's way) */
static void draw_save(uint32_t slot, uint32_t confirm)
{
    uint32_t i;
    char t[24], *p;
    head_begin();
    ctext(10, 5, &SERIF_M, "SAVE", C_INK);
    cv_rect(208, 5, 20, 18, C_WINE);
    t[0] = (char)('1' + slot); t[1] = 0;
    ctext_c(218, 5, &SERIF_M, t, C_CARD);
    cv_blit(0, 0);
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    for (i = 0; i < NPROJ; i++) {
        int32_t y = 8 + (int32_t)i * 43, on = i == slot;
        cv_rect(14, y, 212, 36, on ? C_WINE : C_CARD);
        cv_frame(14, y, 212, 36, on ? C_WINE : C_INK2);
        cv_rect(20, y + 5, 26, 26, on ? C_CARD : C_FAINT);
        t[0] = (char)('1' + i); t[1] = 0;
        ctext_c(33, y + 9, &SERIF_M, t, on ? C_WINE : C_INK);
        p = cat(t, "SAVE "); *p++ = (char)('1' + i); *p = 0;
        ctext(56, y + 3, &SERIF_M, t, on ? C_CARD : C_INK);
        if (proj_bpm[i]) { p = catu(t, proj_bpm[i], 0); cat(p, " BPM"); } else cat(t, "empty");
        ctext(56, y + 20, &SERIF_S, t, on ? RGB(230, 180, 180) : proj_bpm[i] ? C_INK2 : C_RULE);
    }
    if (confirm) {                              /* "sure?" */
        p = cat(t, confirm == 1u ? "SAVE " : "LOAD "); *p++ = (char)('1' + slot); *p++ = '?'; *p = 0;
        cv_rect(22, 40, 196, 100, C_CARD);
        cv_frame(22, 40, 196, 100, C_GOLD); cv_frame(25, 43, 190, 94, C_GOLD);
        ctext_c(120, 50, &SERIF_L, t, C_WINE);
        ctext_c(120, 92, &SERIF_S, confirm == 1u ? "press REC again" : "press PLAY again", C_INK);
        ctext_c(120, 110, &SERIF_S, confirm == 1u ? (proj_bpm[slot] ? "replaces what is there" : "the whole score")
                                                    : "replaces your work", C_INK2);
    }
    cv_blit(0, BODY_Y);
}
static void draw_foot_save(const char *msg)
{
    foot_begin();
    if (!foot_msg(msg)) ctext_c(120, 8, &SERIF_S, "K1 slot  REC save  PLAY load", C_INK2);
    cv_blit(0, FOOT_Y);
}

/* ---- the MIX page (GLO on the orchestra): the seven sections back to front, each its expression (the same value as
 * KNOB 1 on the orchestra page) as a slider with its marking, and its balance in dB; and the hall */
static const char *const ROWSHORT[NROW] = {"STRINGS H", "STRINGS L", "WINDS H", "WINDS L", "BRASS H", "BRASS L", "PERC"};
#define MIX_HALL 7u                             /* the MIX page: the hall, then the click */
#define MIX_CLICK 8u
static const char *const CLK_NAME[NCLK] = {"off", "count-in", "+ recording", "always"};
static void slider(int32_t x, int32_t y, int32_t w, int32_t v, int32_t vmax, int on)   /* a slim slider and its marker */
{
    int32_t m = x + v * w / vmax;
    cv_rect(x, y, m - x, 2, on ? C_CARD : C_INK);          /* (up to the marker: ink; past it: gold) */
    cv_rect(m, y, x + w - m, 2, on ? RGB(170, 110, 110) : C_GOLD);
    cv_rect(m - 3, y - 5, 7, 12, on ? C_CARD : C_WINE);
}
static void draw_mix(uint32_t row)
{
    uint32_t r;
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    for (r = 0; r <= MIX_CLICK; r++) {
        int32_t y = 4 + (int32_t)r * 19 + (r >= MIX_HALL ? 4 : 0), on = r == row;
        uint16_t ink = on ? C_CARD : C_INK;
        if (on) cv_rect(4, y - 2, 232, 19, C_WINE);
        else if (r == MIX_HALL) cv_rect(8, y - 5, 224, 1, C_RULE);
        else if (r) cv_rect(8, y - 3, 224, 1, C_FAINT);
        if (r < MIX_HALL) {                     /* a section: its expression (and its marking), its balance */
            uint32_t sec = ORDER7[r];
            char t[8], *p = t;
            int32_t db = BALN(sec);
            ctext(12, y + 1, &SERIF_S, ROWSHORT[sec], MUTE(sec) ? C_RULE : ink);
            slider(88, y + 8, 84, EXPR(sec), 127, on);
            ctext_r(204, y - 2, &ITAL_M, EXPR_NAME[expr_band(EXPR(sec))], on ? C_CARD : C_WINE);
            if (db > 0) *p++ = '+'; else if (db < 0) *p++ = '-';
            catu(p, (uint32_t)(db < 0 ? -db : db), 0);
            ctext_r(230, y + 1, &SERIF_S, t, on ? C_CARD : db ? C_INK : C_RULE);
        } else if (r == MIX_HALL) {             /* the hall: which room */
            ctext(12, y + 1, &SERIF_S, "HALL", ink);
            ctext_c(150, y + 1, &SERIF_S, HALLS[sc.rsv % NHALL].name, on ? C_CARD : C_WINE);
        } else {                                /* the click: when, how loud */
            char t[4];
            ctext(12, y + 1, &SERIF_S, "METRONOME", ink);
            ctext_c(150, y + 1, &SERIF_S, CLK_NAME[sc.click % NCLK], on ? C_CARD : sc.click ? C_WINE : C_RULE);
            catu(t, sc.clickvol, 0);
            ctext_r(230, y + 1, &SERIF_S, t, on ? C_CARD : C_INK2);
        }
    }
    cv_blit(0, BODY_Y);
}
static void draw_foot_mix(uint32_t row, const char *msg)   /* the chosen row's knobs (its values are on the row) */
{
    foot_begin();
    if (!foot_msg(msg))
        ctext_c(120, 8, &SERIF_S, row == MIX_HALL ? "K1 room" : row == MIX_CLICK ? "K1 mode  K2 level"
                                                   : "K1 expression  K2 balance", C_INK2);
    cv_blit(0, FOOT_Y);
}

/* ---- the song page (GLO after MIX): the time signature. SELECT chooses 2/4, 3/4 or 4/4; REC asks (a change clears
 * every pattern), REC again changes it; anything else keeps the one in force */
enum { SONG_SWING = 8, SONG_HUMAN, SONG_TAP, NSONGIT };   /* the song page's items after the eight time signatures */
static void draw_song(uint32_t it, uint32_t ask)
{
    static const char *const LBL[3] = {"SWING", "HUMANISE", "TAP TEMPO"};
    static const uint8_t BX[3] = {6, 66, 142}, BW[3] = {54, 70, 92};   /* (the tap box wide: its tempo marking) */
    uint32_t k, pend = it < SONG_SWING ? TS_ORDER[it] : 0u;
    char t[16];
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    ctext_c(120, 0, &SERIF_S, "TIME SIGNATURE", C_INK2);
    for (k = 0; k < TS_END - 2u; k++) {         /* simple time, then 2/2 and compound: the one in force wine, the one
                                                   chosen ringed in gold */
        uint32_t ts = TS_ORDER[k];
        int32_t x = 12 + (int32_t)(k % 4u) * 56, y = 17 + (int32_t)(k / 4u) * 50, now = ts == sc.tsig;
        cv_rect(x, y, 48, 44, now ? C_WINE : C_CARD);
        cv_frame(x, y, 48, 44, now ? C_WINE : C_RULE);
        if (k == it) { cv_frame(x - 3, y - 3, 54, 50, C_GOLD); cv_frame(x - 4, y - 4, 56, 52, C_GOLD); }
        catu(t, TSIG[ts].num, 0);
        ctext_c(x + 24, y + 1, &SERIF_M, t, now ? C_CARD : C_INK);
        cv_rect(x + 12, y + 22, 24, 2, now ? C_CARD : C_INK2);
        catu(t, TSIG[ts].den, 0);
        ctext_c(x + 24, y + 24, &SERIF_M, t, now ? C_CARD : C_INK);
    }
    ctext_c(120, 115, &SERIF_S, "PERFORMANCE", C_INK2);
    for (k = 0; k < 3u; k++) {                  /* swing, humanise, tap tempo: none of them clears anything */
        int32_t x = BX[k], w = BW[k], y = 132, on = SONG_SWING + k == it;
        uint32_t v = k == 0u ? sc.swing : k == 1u ? sc.human : sc.tempo;
        cv_rect(x, y, w, 48, C_CARD);
        cv_frame(x, y, w, 48, v && k < 2u ? C_WINE : C_RULE);
        if (on) { cv_frame(x - 3, y - 3, w + 6, 54, C_GOLD); cv_frame(x - 4, y - 4, w + 8, 56, C_GOLD); }
        ctext_c(x + w / 2, y + 3, &SERIF_S, LBL[k], C_INK2);
        if (k < 2u) {
            if (!v) cat(t, "off"); else catu(t, v, 0);
            ctext_c(x + w / 2, y + 21, &SERIF_M, t, !v ? C_RULE : C_WINE);
        } else {                                /* the tempo: its Italian marking, then the number */
            static const uint8_t UP[8] = {40, 60, 66, 76, 108, 120, 156, 168};
            static const char *const TM[9] = {"Grave", "Largo", "Larghetto", "Adagio", "Andante", "Moderato", "Allegro",
                                               "Vivace", "Presto"};
            uint32_t j = 0;
            while (j < 8u && v >= UP[j]) j++;
            ctext_c(x + w / 2, y + 13, &ITAL_M, TM[j], C_WINE);
            cat(catu(t, v, 0), " BPM");
            ctext_c(x + w / 2, y + 33, &SERIF_S, t, C_INK2);
        }
    }
    if (ask) {                                  /* "sure?": it clears every pattern */
        char q[24], *p;
        cv_rect(18, 28, 204, 128, C_CARD);
        cv_frame(18, 28, 204, 128, C_WINE); cv_frame(21, 31, 198, 122, C_GOLD);
        p = cat(q, "CHANGE TO "); p = ts_name(p, pend); cat(p, "?");
        ctext_c(120, 40, &SERIF_M, q, C_WINE);
        ctext_c(120, 68, &SERIF_S, "every pattern is cleared", C_INK);
        ctext_c(120, 92, &SERIF_M, "press REC again", C_INK);
        p = cat(q, "anything else: keep "); ts_name(p, sc.tsig);
        ctext_c(120, 122, &SERIF_S, q, C_INK2);
    }
    cv_blit(0, BODY_Y);
}

/* ---- the KEY page (GLO after the song page): the key, its relative minor, its signature and scale on a staff, and its
 * scale on a piano from its first note (the notes being played light in gold); scale lock and the keyboard's lights */
static const char *const KMAJ[15] = {"C-flat", "G-flat", "D-flat", "A-flat", "E-flat", "B-flat", "F", "C", "G", "D", "A", "E",
                                      "B", "F-sharp", "C-sharp"};
static const char *const KMIN[15] = {"A-flat", "E-flat", "B-flat", "F", "C", "G", "D", "A", "E", "B", "F-sharp", "C-sharp",
                                      "G-sharp", "D-sharp", "A-sharp"};
static void draw_key(uint32_t played)          /* played: the pitch classes held (bit 0 C) */
{
    static const uint8_t MAJ[8] = {0, 2, 4, 5, 7, 9, 11, 12};
    static const char *const WN[12] = {"C", "", "D", "", "E", "F", "", "G", "", "A", "", "B"};
    static const char *const SHN[12] = {"", "C#", "", "D#", "", "", "F#", "", "G#", "", "A#", ""};
    static const char *const FLN[12] = {"", "Db", "", "Eb", "", "", "Gb", "", "Ab", "", "Bb", ""};
    int32_t key = KEYSIG, nks = key < 0 ? -key : key, pc = (int32_t)key_pc(), k, a, x0, st = 60, top = 110;
    char t[40], *p;
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    p = cat(t, KMAJ[key + 7]); cat(p, " major");
    ctext_c(120, 0, text_w(&SERIF_L, t) <= 216 ? &SERIF_L : &SERIF_M, t, C_WINE);
    p = cat(t, KMIN[key + 7]); p = cat(p, " minor   ");
    if (!nks) cat(p, "no sharps or flats");
    else { p = catu(p, (uint32_t)nks, 0); cat(p, key > 0 ? (nks > 1 ? " sharps" : " sharp") : (nks > 1 ? " flats" : " flat")); }
    ctext_c(120, 33, &SERIF_S, t, C_INK2);
    for (k = 0; k < 5; k++) cv_rect(8, st + k * 8, 224, 1, C_INK2);   /* the staff: the clef, the signature, the scale */
    cv_rect(8, st, 1, 33, C_INK2); cv_rect(231, st, 1, 33, C_INK2);
    sc_sym(10, st - 11, MS_TREBLE, C_INK);
    {
        static const int8_t SH[7] = {8, 5, 9, 6, 3, 7, 4}, FL[7] = {4, 7, 3, 6, 2, 5, 1};
        for (a = 0; a < nks; a++) {
            int32_t pp = key > 0 ? SH[a] : FL[a], x = 34 + a * 7, yy = st + 32 - pp * 4;
            if (key > 0) sc_sharp(x, yy, C_INK); else sc_sym(x - 2, yy - 11, MS_FLAT, C_INK);
        }
    }
    x0 = 34 + 7 * nks + 14;
    for (k = 0; k < 8; k++) {
        uint32_t n = (uint32_t)(60 + pc + MAJ[k]);
        int32_t pp = sc_dia(n) - 30, x = x0 + k * (220 - x0) / 8 + 6, q, yy = st + 32 - pp * 4;
        uint16_t c = k == 0 || k == 7 ? C_WINE : C_INK;
        for (q = -2; q >= pp; q -= 2) cv_rect(x - 7, st + 32 - q * 4, 15, 1, C_INK2);
        for (q = 10; q <= pp; q += 2) cv_rect(x - 7, st + 32 - q * 4, 15, 1, C_INK2);
        sc_head(x, yy, c, 0);
        if (pp < 4) cv_rect(x + 4, yy - 22, 1, 22, c); else cv_rect(x - 4, yy, 1, 22, c);
    }
    {                                           /* the piano: the scale's first note to its last (a black first note: the
                                                   white key below it too); lit, named; a key played: gold */
        int32_t lo = black_key((uint32_t)pc) ? pc - 1 : pc, hi = black_key((uint32_t)pc) ? pc + 13 : pc + 12, nw = 0, kw, x, n, wx[16];
        for (n = lo; n <= hi; n++) if (!black_key((uint32_t)(n + 12) % 12u)) nw++;
        kw = 216 / nw;
        for (n = lo, k = 0; n <= hi; n++) {
            uint32_t c = (uint32_t)(n + 12) % 12u, on = in_key(c), hit = played >> c & 1u;
            if (black_key(c)) continue;
            x = 12 + (216 - nw * kw) / 2 + k * kw; wx[n - lo] = x; k++;
            cv_rect(x, top, kw - 1, 50, hit ? C_GOLD : on ? RGB(232, 190, 192) : C_KEYW);
            cv_frame(x, top, kw - 1, 50, C_INK2);
            if (on) ctext_c(x + kw / 2, top + 31, &SERIF_S, WN[c], hit ? C_CARD : C_WINE);
            if ((int32_t)c == pc && (n == pc || n == pc + 12)) cv_disc(x + kw / 2 - 1, top + 45, 2, hit ? C_CARD : C_GOLD);
        }
        for (n = lo + 1; n < hi; n++) {
            uint32_t c = (uint32_t)(n + 12) % 12u, on = in_key(c), hit = played >> c & 1u;
            if (!black_key(c)) continue;
            x = wx[n - 1 - lo] + kw - 10;
            cv_rect(x, top, 20, 30, hit ? C_GOLD : on ? RGB(160, 74, 86) : C_INK);
            if (on) ctext_c(x + 10, top + 2, &SERIF_S, key > 0 ? SHN[c] : FLN[c], C_CARD);
            if ((int32_t)c == pc) cv_disc(x + 9, top + 24, 2, hit ? C_CARD : C_GOLD);
        }
    }
    ctext(12, 167, &SERIF_S, "SCALE LOCK", C_INK2);   /* the options: KNOB 1, KNOB 2 */
    ctext(88, 167, &SERIF_S, KEY_LOCK ? "on" : "off", KEY_LOCK ? C_WINE : C_RULE);
    ctext(128, 167, &SERIF_S, "LIGHT KEYS", C_INK2);
    ctext(204, 167, &SERIF_S, KEY_SHOW ? "on" : "off", KEY_SHOW ? C_WINE : C_RULE);
    cv_blit(0, BODY_Y);
}
static int32_t clampi(int32_t v, int32_t lo, int32_t hi);
static int key_move(int32_t from, int32_t to)   /* the music moved into another key (the nearer way); 0: a note would leave
                                                   the range (nothing moved) */
{
    int32_t d = ((to - from) * 7 % 12 + 12) % 12, n;
    uint32_t i, q, k, j;
    if (d > 6) d -= 12;
    for (i = 0; i < P_BD; i++) for (q = 0; q < NPAT; q++) for (k = 0; k < NSTEP; k++) for (j = 0; j < NV; j++)
        if ((n = sc.mel[i][q][k].n[j]) != 0 && (n + d < 24 || n + d > 108)) return 0;
    for (i = 0; i < P_BD; i++) for (q = 0; q < NPAT; q++) for (k = 0; k < NSTEP; k++) for (j = 0; j < NV; j++)
        if (sc.mel[i][q][k].n[j]) sc.mel[i][q][k].n[j] = (uint8_t)(sc.mel[i][q][k].n[j] + d);
    for (i = P_BD; i < NINS; i++)               /* the timpani (and toms) retuned with it */
        if (TIMPANI(i)) sc.tune[i] = (uint8_t)clampi((int32_t)sc.tune[i] + d, DTY(i).ty == TY_TOM ? 45 : 36, DTY(i).ty == TY_TOM ? 72 : 60);
    return 1;
}

/* ---- the copy page (SEQ on an editing page): every part's patterns A-J, the one chosen in wine, the one being
 * edited (where the copy goes) ringed in gold; a pattern with notes dark, an empty one pale; rows of the other kind
 * (percussion for a melodic part, and the other way) faint. SEQ asks, SEQ again copies */
static uint32_t pat_used(uint32_t r, uint32_t q)
{
    uint32_t k, d;
    for (k = 0; k < sc.len[r][q] && k < NSTEP; k++) {
        if (r == P_BD) { for (d = 0; d < NDRUM; d++) if (sc.drum[d][q][k]) return 1; }
        else if (has_notes(&sc.mel[r][q][k])) return 1;
    }
    return 0;
}
static void draw_copy(uint32_t row, uint32_t pat, uint32_t rd, uint32_t qd, uint32_t ask)
{
    uint32_t r, q;
    char t[32], *p;
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    for (q = 0; q < NPAT; q++) { t[0] = (char)('A' + q); t[1] = 0; ctext_c(83 + (int32_t)q * 15, 2, &SERIF_S, t, C_INK2); }
    for (r = 0; r < NROW; r++) {
        uint32_t rs = ORDER7[r], other = (rs == P_BD) != (rd == P_BD);
        int32_t y = 18 + (int32_t)r * 21;
        ctext(10, y + 2, &SERIF_S, ROWSHORT[rs], other ? C_FAINT : r == row ? C_WINE : C_INK);
        for (q = 0; q < NPAT; q++) {
            int32_t x = 76 + (int32_t)q * 15, on = r == row && q == pat, used = (int32_t)pat_used(rs, q);
            uint16_t fill = on ? C_WINE : other ? C_PAPER : used ? C_ROWB : C_CARD;
            cv_rect(x, y, 14, 18, fill);
            cv_frame(x, y, 14, 18, other ? C_FAINT : C_RULE);
            if (used && !on && !other) cv_rect(x + 3, y + 13, 8, 2, C_INK2);   /* (notes in it) */
            if (rs == rd && q == qd) { cv_frame(x - 1, y - 1, 16, 20, C_GOLD); cv_frame(x - 2, y - 2, 18, 22, C_GOLD); }
            t[0] = (char)('A' + q); t[1] = 0;
            ctext_c(x + 8, y + 1, &SERIF_S, t, on ? C_CARD : other ? C_FAINT : used ? C_INK : C_RULE);
        }
    }
    p = cat(t, ROWSHORT[ORDER7[row % NROW]]); p = cat(p, " "); *p++ = (char)('A' + pat); p = cat(p, "  into  ");
    p = cat(p, ROWSHORT[rd]); p = cat(p, " "); *p++ = (char)('A' + qd); *p = 0;
    ctext_c(120, 168, &SERIF_S, t, C_INK);
    if (ask) {                                  /* "sure?" */
        cv_rect(22, 36, 196, 108, C_CARD);
        cv_frame(22, 36, 196, 108, C_WINE); cv_frame(25, 39, 190, 102, C_GOLD);
        ctext_c(120, 46, &SERIF_L, "COPY?", C_WINE);
        ctext_c(120, 86, &SERIF_S, t, C_INK);
        ctext_c(120, 104, &SERIF_M, "press SEQ again", C_INK);
        ctext_c(120, 124, &SERIF_S, "it replaces this pattern", C_INK2);
    }
    cv_blit(0, BODY_Y);
}

/* ---- REC held: "clear everything?", big, over any page, until REC again (clear), anything else, or 4 s */
static void draw_ask(int32_t h)
{
    cv_begin(240, (uint32_t)h, C_PAPER);
    sides(h);
    cv_rect(14, 12, 212, h - 24, C_CARD);
    cv_frame(14, 12, 212, h - 24, C_WINE); cv_frame(17, 15, 206, h - 30, C_GOLD); cv_frame(19, 17, 202, h - 34, C_GOLD);
    ctext_c(120, h / 2 - 66, &SERIF_L, "CLEAR", C_WINE);
    ctext_c(120, h / 2 - 34, &SERIF_L, "ALL?", C_WINE);
    cv_rect(50, h / 2 + 4, 140, 1, C_GOLD);
    ctext_c(120, h / 2 + 12, &SERIF_M, "press REC again", C_INK);
    ctext_c(120, h / 2 + 36, &SERIF_S, "every pattern and the Composer", C_INK2);
    ctext_c(120, h / 2 + 52, &SERIF_S, "any other button: keep it all", C_INK2);
    cv_blit(0, BODY_Y);
}

/* ---- the expression on the orchestra page: a card over the seats while KNOB 1 (expression) or KNOB 2 (balance)
 * turns, then the seats again */
static void draw_overlay(uint32_t row, uint32_t kind)
{
    char t[32], *p;
    uint32_t e = EXPR(row), now = eff[row], k;
    const curve_t *cv = &sc.curve[row][play_pat[row] % NPAT];
    cv_begin(240, HOME_H, C_PAPER);
    sides(HOME_H);
    cv_rect(16, 12, 208, 188, C_CARD);
    cv_frame(16, 12, 208, 188, C_INK2); cv_frame(19, 15, 202, 182, C_GOLD);
    ctext_c(120, 22, &SERIF_M, ROWNAME[row], C_INK);
    cv_rect(40, 45, 160, 1, C_GOLD);
    if (kind == 1u) {                           /* the expression: its marking (a range of one continuous value) */
        int32_t x = 30;
        ctext_c(120, 50, &ITAL_L, EXPR_NAME[expr_band(e)], C_WINE);
        ctext_c(120, 100, &SERIF_S, "EXPRESSION", C_INK);
        for (k = 0; k < 6u; k++) {              /* pp p mp mf f ff, the one it is in boxed */
            int32_t w = text_w(&ITAL_M, EXPR_NAME[k]);
            if (k == expr_band(e)) { cv_rect(x - 4, 122, w + 8, 22, C_WINE); ctext(x, 122, &ITAL_M, EXPR_NAME[k], C_CARD); }
            else ctext(x, 122, &ITAL_M, EXPR_NAME[k], C_INK2);
            x += w + 13;
        }
        if (xrec_row == row) {                  /* a take: what it writes into */
            p = cat(t, "RECORDING INTO PATTERN "); *p++ = (char)('A' + xrec_pat); *p = 0;
            ctext_c(120, 162, &SERIF_S, t, C_WINE);
        } else if (cv->on && cv->n) {           /* with a curve: what plays now (the setting x the curve) */
            p = cat(t, "with its curve now  "); cat(p, EXPR_NAME[expr_band(now)]);
            ctext_c(120, 162, &SERIF_S, t, C_WINE);
        } else ctext_c(120, 162, &SERIF_S, "no curve on this pattern", C_RULE);
    } else {                                    /* the balance (the mix) */
        int32_t db = BALN(row);
        p = t; if (db > 0) *p++ = '+'; else if (db < 0) *p++ = '-';
        p = catu(p, (uint32_t)(db < 0 ? -db : db), 0); cat(p, " dB");
        ctext_c(120, 58, &SERIF_L, t, C_WINE);
        ctext_c(120, 100, &SERIF_S, "BALANCE", C_INK);
        slider(44, 136, 152, db + 30, 36, 0);
        cv_rect(44 + 30 * 152 / 36, 146, 1, 6, C_INK2);
        ctext_c(44 + 30 * 152 / 36, 152, &SERIF_S, "0", C_INK2);
    }
    cv_blit(0, BODY_Y);
}

/* ---- a pattern's expression curve (EDIT, after the roll and the music; on the drum machine: EDIT) */
static const char *const SHAPE_NAME[4] = {"linear", "ease in", "ease out", "smooth"};
static const char *const PRESET_NAME[4] = {"FLAT", "CRESCENDO", "DIMINUENDO", "SWELL"};
static void draw_curve(uint32_t i, uint32_t ptsel, uint32_t run)
{
    uint32_t row = PK(i), q = PSEL(i), len = LEN(i), k;
    const curve_t *c = &sc.curve[row][q];
    int32_t gx = 34, gy = 8, gw = 194, gh = 112, px, lx = 0, ly = 0;
    static const uint8_t MARK[6] = {11, 32, 53, 75, 96, 117};   /* the markings: the middle of each range */
    uint16_t ink = c->on && c->n ? C_WINE : C_RULE;
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    cv_rect(gx, gy, gw + 1, gh + 1, C_CARD);
    for (k = 0; k < 6u; k++) {                  /* pp .. ff, at full section expression */
        int32_t y = gy + gh - (int32_t)MARK[k] * gh / 127;
        cv_rect(gx + 1, y, gw - 1, 1, C_FAINT);
        ctext_r(gx - 4, y - 9, &ITAL_M, EXPR_NAME[k], C_INK2);
    }
    for (k = 0; k <= len; k += GRP)             /* the beats (groups), the bars */
        cv_rect(gx + (int32_t)(k * (uint32_t)gw / len), gy, 1, gh + 1, k % BAR == 0u ? C_INK2 : C_RULE);
    cv_frame(gx, gy, gw + 1, gh + 1, C_INK2);
    for (px = 0; px <= gw; px++) {              /* the curve (a bypassed one, pale: it plays as flat) */
        uint32_t x = (uint32_t)px * len * 256u / (uint32_t)gw, v = c->n ? curve_at(c, x) : 127u;
        int32_t y = gy + gh - (int32_t)v * gh / 127;
        if (px) { cv_line(gx + lx, ly, gx + px, y, ink); cv_line(gx + lx, ly + 1, gx + px, y + 1, ink); }
        lx = px; ly = y;
    }
    for (k = 0; k < c->n; k++) {                /* the points, the chosen one ringed in gold */
        int32_t x = gx + (int32_t)(c->p[k].pos * (uint32_t)gw / len), y = gy + gh - (int32_t)c->p[k].val * gh / 127;
        if (k == ptsel) { cv_disc(x, y, 6, C_GOLD_L); cv_ring(x, y, 6, C_GOLD); }
        cv_disc(x, y, 3, C_INK);
    }
    if (run && play_pat[i] == q) {              /* where the music is */
        int32_t x = gx + (int32_t)(cur[i] * (uint32_t)gw / len);
        cv_rect(x, gy - 4, 2, gh + 5, C_INK);
    }
    ctext(8, 126, &SERIF_S, "K1 point  K2 place  K3 level  K4 shape", C_INK2);
    ctext(8, 143, &SERIF_S, "ARP add  SEQ delete  GLO on/off", C_INK2);
    ctext(8, 160, &SERIF_S, "SELECT start shape", C_INK2);
    cv_blit(0, BODY_Y);
}
static void draw_foot_curve(uint32_t i, uint32_t ptsel, const char *msg)
{
    char t[48], *p;
    const curve_t *c = &sc.curve[PK(i)][PSEL(i)];
    foot_begin();
    if (!foot_msg(msg)) {
        if (!c->n) ctext_c(120, 8, &SERIF_S, "no curve: ARP adds one, SELECT a shape", C_INK2);
        else {
            const cpt_t *pt = &c->p[ptsel % c->n];
            p = cat(t, "point "); p = catu(p, ptsel + 1u, 0); p = cat(p, "/"); p = catu(p, c->n, 0);
            p = cat(p, "  step "); p = catu(p, pt->pos + 1u, 0); p = cat(p, "  "); p = cat(p, EXPR_NAME[expr_band(pt->val)]);
            p = cat(p, " "); p = catu(p, pt->val, 0); p = cat(p, "  "); cat(p, SHAPE_NAME[pt->shape & 3u]);
            ctext_c(120, 8, &SERIF_S, t, c->on ? C_INK : C_RULE);
        }
    }
    cv_blit(0, FOOT_Y);
}

/* ---- the Composer (HOME on the orchestra): a song of patterns, 8 boxes a part (QUARTET's SUPER SEQ) */
static const char *const SHORT[NROW] = {"strings h", "strings l", "winds h", "winds l", "brass h", "brass l", "percussion"};
static void draw_composer(uint32_t row, uint32_t box, uint32_t run)
{
    const uint8_t (*c)[NBOX] = sc.chain[sc.comp_song % NSONG];
    uint32_t r, k, cl = chain_len(), live = sc.comp_on && run;
    char *p = 0;
    head_begin();
    ctext(10, 5, &SERIF_M, "COMPOSER", C_INK);
    ctext(124, 8, &SERIF_S, sc.comp_on ? "ON" : "OFF", sc.comp_on ? C_WINE : C_RULE);
    (void)p;
    ctext_r(208, 5, &SERIF_M, "PIECE", C_INK);
    lbox(212, 5, (char)('A' + sc.comp_song % NSONG), &SERIF_M, C_CARD, C_WINE, C_WINE);
    cv_blit(0, 0);
    cv_begin(240, BODY_H, C_PAPER);
    sides(BODY_H);
    for (r = 0; r < NROW; r++) {
        int32_t y = 6 + (int32_t)r * 25, me = r == row;
        uint32_t last = 0, rr = ORDER7[r];      /* (r: the place on the page; rr: the section) */
        if (me) cv_rect(6, y, 66, 20, C_WINE);
        ctext(9, y + 3, &SERIF_S, SHORT[rr], me ? C_CARD : MUTE(rr) ? C_RULE : C_INK);
        for (k = 0; k < NBOX; k++) {
            int32_t x = 76 + (int32_t)k * 19;
            uint32_t bv = c[rr][k];
            uint16_t bg, ink;
            char v[2] = {'-', 0};
            if (k >= cl) { cv_rect(x, y, 17, 20, C_FAINT); continue; }   /* past the song's end */
            if (bv) last = bv <= NPAT ? bv : 0u;
            if (bv && bv <= NPAT) { bg = me ? C_WINE : WINES[1]; ink = C_CARD; v[0] = (char)('A' + bv - 1u); }
            else if (bv == BOX_X) { bg = C_ROWB; ink = C_INK2; v[0] = 'X'; }
            else if (last) { bg = C_CARD; ink = C_RULE; v[0] = (char)('A' + last - 1u); }   /* (goes on) */
            else { bg = C_CARD; ink = C_FAINT; }
            cv_rect(x, y, 17, 20, bg);
            cv_frame(x, y, 17, 20, me && k == box ? C_GOLD : C_RULE);
            if (me && k == box) cv_frame(x - 1, y - 1, 19, 22, C_GOLD);
            ctext_c(x + 9, y + 3, &SERIF_S, v, ink);
            if (live && chain_at[rr] % NBOX == k) cv_rect(x + 2, y + 17, 13, 2, C_INK);   /* playing */
        }
    }
    cv_blit(0, BODY_Y);
}
static void draw_foot_composer(uint32_t box, const char *msg)
{
    (void)box;
    foot_begin();
    if (!foot_msg(msg)) ctext_c(120, 8, &SERIF_S, "K1 length  K2 box  K3 pattern  K4 on/off", C_INK2);
    cv_blit(0, FOOT_Y);
}

/* ======================================================================== MIDI in */
static uint32_t idle_ok;
static const uint8_t GM_PERC[128] = {           /* channel 10's notes -> percussion (0: none) */
    [35] = P_BD + 1, [36] = P_BD + 1, [49] = P_CYM + 1, [57] = P_CYM + 1, [52] = P_CYM + 1, [55] = P_CYM + 1,
    [80] = P_TRI + 1, [81] = P_TRI + 1, [38] = P_SN + 1, [40] = P_SN + 1, [37] = P_SN + 1,
    [41] = P_T1 + 1, [43] = P_T2 + 1, [45] = P_T3 + 1, [47] = P_T4 + 1};
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {
        uint32_t ty = st & 0xF0u, ch = st & 15u, dyn = d2 > 100u ? 3u : d2 > 72u ? 2u : d2 > 44u ? 1u : 0u;
        if (st == 0xFAu) { run_req = 1; continue; }   /* START / STOP from a DAW */
        if (st == 0xFCu) { run_req = 0; continue; }
        if (ty != 0x90u && ty != 0x80u) continue;
        if (ch < P_BD) {
            if (ty == 0x90u && d2) live(ch, d1, dyn, 1);
            else live(ch, d1, 0, 0);
        } else if (ch == 9u && ty == 0x90u && d2 && d1 < 128u && GM_PERC[d1])
            live(GM_PERC[d1] - 1u, 0, dyn, 1);
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

/* ======================================================================== the main loop */
enum { SCR_HOME, SCR_SETUP, SCR_EDIT, SCR_MIX, SCR_SAVE, SCR_COMP, SCR_CURVE, SCR_COPY, SCR_SONG, SCR_KEY };
static uint32_t flash_until[NINS];
static uint32_t lit_mask(void)                  /* the players sounding now */
{
    uint32_t i, m = 0;
    static uint8_t seen[NINS];
    for (i = 0; i < NINS; i++) {
        uint8_t h = hits[i];
        if (h != seen[i]) { seen[i] = h; flash_until[i] = fm1_ms + 140u; }
        if (MELODIC(i) ? mel_sounding(i) : (int32_t)(flash_until[i] - fm1_ms) > 0) m |= 1u << i;
    }
    return m;
}
static void set_tempo(void) { step_q8 = 2646000u * 256u / (sc.tempo * BEAT); }   /* 44100 * 60 samples a beat, a beat
                                                   of BEAT 16ths: samples a 16th, Q8 */
static void set_hall(void)                      /* one room for the whole orchestra: studio, chamber, concert hall,
                                                   cathedral (nothing else to set) */
{
    rv_fb = HALLS[sc.rsv % NHALL].fb; rv_damp = HALLS[sc.rsv % NHALL].damp; rv_dry = HALLS[sc.rsv % NHALL].dry;
    hall_q15 = HALLS[sc.rsv % NHALL].wet;
}
static void clear_all(void)                      /* REC held (and pressed again): every pattern empty and 16 steps;
                                                   every song of the Composer one box, A; the Composer off */
{
    uint32_t i, q, c, b;
    for (i = 0; i < sizeof sc.mel; i++) ((uint8_t *)sc.mel)[i] = 0;
    for (i = 0; i < sizeof sc.drum; i++) ((uint8_t *)sc.drum)[i] = 0;
    for (i = 0; i < NROW; i++) { sc.psel[i] = 0; for (q = 0; q < NPAT; q++) { sc.len[i][q] = (uint8_t)BAR; sc.curve[i][q].on = 0; sc.curve[i][q].n = 0; } }
    for (c = 0; c < NSONG; c++) {
        sc.clen[c] = 1u;                        /* (one box: A) */
        for (i = 0; i < NROW; i++) for (b = 0; b < NBOX; b++) sc.chain[c][i][b] = (uint8_t)(b == 0u);
    }
    sc.comp_on = 0;
}
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t wrapi(int32_t v, uint32_t n) { v %= (int32_t)n; return (uint32_t)(v < 0 ? v + (int32_t)n : v); }
static uint32_t bars_all(void)                  /* the longest pattern, in bars of 16 steps */
{
    uint32_t i, m = 1;
    for (i = 0; i < NINS; i++) if (plen(i, play_pat[i]) > m) m = plen(i, play_pat[i]);
    return (m + BAR - 1u) / BAR;
}


/* ---- undo (ARP held + REC on an editing page): the pattern as it was before the last burst of edits (a pause of
 * over 1.2 s ends a burst); again: redo. One pattern, its notes (or the kit's hits), length and curve */
typedef struct { uint8_t ok, r, q, len; curve_t cv; mstep_t mel[NSTEP]; uint8_t drum[NDRUM][NSTEP]; } undo_t;
static undo_t un_snap __attribute__((section(".pool")));   /* the pattern as it is when edits pause */
static undo_t un_buf __attribute__((section(".pool")));    /* ... as it was before the last burst: what undo restores */
static uint32_t un_fresh;                       /* the snapshot is from before any edit of this burst */
static void un_take(undo_t *u, uint32_t r, uint32_t q)
{
    u->ok = 1; u->r = (uint8_t)r; u->q = (uint8_t)q; u->len = sc.len[r][q]; u->cv = sc.curve[r][q];
    if (r < P_BD) bytes_copy(u->mel, sc.mel[r][q], sizeof u->mel);
    else { uint32_t d; for (d = 0; d < NDRUM; d++) bytes_copy(u->drum[d], sc.drum[d][q], NSTEP); }
}
static void un_put(const undo_t *u)
{
    uint32_t r = u->r, q = u->q;
    sc.len[r][q] = u->len; sc.curve[r][q] = u->cv;
    if (r < P_BD) bytes_copy(sc.mel[r][q], u->mel, sizeof u->mel);
    else { uint32_t d; for (d = 0; d < NDRUM; d++) bytes_copy(sc.drum[d][q], u->drum[d], NSTEP); }
}
static void un_keep(uint32_t r, uint32_t q, uint32_t idle)   /* each pass on an editing page */
{
    if (!un_snap.ok || un_snap.r != r || un_snap.q != q || (idle && !un_fresh)) { un_take(&un_snap, r, q); un_fresh = 1; }
}
static void un_mark(uint32_t r, uint32_t q)     /* a change: the first of a burst keeps what was there before it */
{
    if (un_fresh && un_snap.ok && un_snap.r == r && un_snap.q == q) { un_buf = un_snap; un_fresh = 0; }
}
static int un_swap(uint32_t r, uint32_t q)      /* undo (and, again, redo): 0 = nothing kept for this pattern */
{
    if (!un_buf.ok || un_buf.r != r || un_buf.q != q) return 0;
    un_take(&un_snap, r, q);                    /* (what is there now: what redo brings back) */
    un_put(&un_buf);
    un_buf = un_snap;
    un_take(&un_snap, r, q); un_fresh = 1;
    return 1;
}
#define TITLE_VER "v1.0"                       /* the version on the title page */
static void title_strip(int32_t y0)             /* the title page, like a printed score's: the part from y0, 120 rows */
{
    static const int32_t NX[4] = {100, 122, 144, 166}, NY[4] = {152, 146, 140, 134};   /* a rising phrase */
    int32_t k;
    cv_begin(240, 120, C_PAPER);
    cv_frame(10, 10 - y0, 220, 220, C_INK2); cv_frame(14, 14 - y0, 212, 212, C_GOLD);
    cv_rect(70, 36 - y0, 100, 1, C_RULE);
    ctext_c(120, 50 - y0, &SERIF_L, "CONDUCTOR", C_INK);
    ctext_c(120, 92 - y0, &ITAL_M, "a pocket orchestra", C_WINE);
    cv_rect(90, 124 - y0, 60, 2, C_GOLD);
    for (k = 0; k < 5; k++) cv_rect(56, 140 + k * 6 - y0, 128, 1, C_INK2);   /* the staff */
    sc_sym(58, 129 - y0, MS_TREBLE, C_INK);
    for (k = 0; k < 4; k++) {                   /* stems up, then down above the middle line; the last in wine */
        uint16_t c = k == 3 ? C_WINE : C_INK;
        cv_disc(NX[k], NY[k] - y0, 3, c);
        if (k < 2) cv_rect(NX[k] + 2, NY[k] - 18 - y0, 1, 18, c); else cv_rect(NX[k] - 3, NY[k] - y0, 1, 18, c);
    }
    ctext_c(120, 184 - y0, &SERIF_S, TITLE_VER, C_INK2);
    cv_blit(0, (uint32_t)y0);
}
static void app_main(void)
{
    uint32_t scr = SCR_HOME, sel = S_HI, cursor = 0, cur_dyn = 2, nlen = 4, rec = 0, prev = 0, k, music = 0;
    uint32_t dirty = 0, t_change = 0, redraw = 1, foot = 1, head = 1, clear_arm = 0, msg_until = 0;
    uint32_t rec_t0 = 0, rec_down = 0, rec_done = 0, save_back = SCR_HOME, t_nav = 0, clear_ask = 0, ask_shown = 0;
    uint32_t oct_t0 = 0, oct_used = 0;          /* OCT held: when, and whether it has transposed */
    uint32_t batt_shown = 0;                    /* the battery's warning as shown (0 none, 1 off, 2 on) */
    int32_t oct_dir = 0;   /* clear_ask: REC
                                                   held, when (waiting for REC again) */
    uint32_t slot = 0, confirm = 0, confirm_t = 0, proj_go = 0, proj_go_t = 0;   /* the save page */
    uint32_t crow = 0, cbox = 0, arp_used = 0;   /* the Composer: the row and box; ARP held for a copy */
    uint32_t cp_row = 0, cp_pat = 0, cp_ask = 0, cp_back = SCR_EDIT;
#define ARTW(i) (ART_STR(art_cur) && INS[i].ty != TY_STR ? ART_NONE : art_cur)   /* (pizzicato, tremolo: strings only) */
    int32_t key_from = 0;                       /* the KEY page: the key the music is in (REC moves it to the one shown) */
    uint32_t key_ask = 0, arp_t0 = 0, seq_t0 = 0, seq_down = 0, seq_used = 0;   /* ... asking; ARP / SEQ held (listen alone,
                                                   the ending) */
    uint32_t song_it = 2, tap_n = 0, tap_last = 0, tap_iv[3] = {0, 0, 0}, cin_shown = 0;   /* the song page's item; tap
                                                   tempo: taps so far, the last, the gaps; the count-in shown */
    uint32_t ts_pend = 4, ts_ask = 0, art_cur = ART_NONE;   /* the song page: the time signature chosen, asking;
                                                   art_cur: the articulation the next notes get */             /* the song page: its row; a time signature chosen, not yet accepted */   /* the copy page: the row (as on the page) and
                                                   pattern chosen, asking, the page to go back to */
    uint32_t drawn_flash = ~0u, drawn_play = ~0u, drawn_bar = ~0u, t_frame = 0;
    uint32_t chord = 0;                          /* step entry: keys held for the chord at the cursor (bit k) */
    uint32_t mrow = 0, ptsel = 0, preset = 0, ov_kind = 0, ov_until = 0;   /* MIX: the row; the curve: its point, the
                                                   start shape last given; the orchestra's card: which, until when */
    uint8_t part[NSEC] = {S_HI, W_HI, B_HI, P_BD};   /* the part last chosen in each section */
    uint8_t held[27] = {0}, held_by[27] = {0}, bk[27];   /* held: the note a key plays, and for which part; bk: a black
                                                   key's number (0..10), else 255 */
    uint8_t rec_st[27], rec_q[27];              /* live recording: where a held key's note went */
    uint32_t rec_g[27];                         /* ... the step it began on, counted from PLAY (to the nearest) */
    const char *msg = 0;
    static char msgbuf[24];

    if (slot_load(0, slot_buf, SLOT_MAX)) { score_default(); slot_put(0, &sc); }   /* SAVE 1 never written (a new
                                                   FM-1): the start-up piece is put there, to come back to; a save
                                                   over it replaces it */
    if (slot_get(WORK_SLOT, &proj_buf) == 0) {   /* the work, as it was at power-off */
        bytes_copy(&sc, &proj_buf, sizeof sc);
        if (!score_ok()) score_default();
    } else score_default();
    set_tempo(); set_hall();
    proj_scan();
    for (k = 0, prev = 0; k < 27u; k++) { bk[k] = black_key(KEY0 + k) ? (uint8_t)prev++ : 255u; rec_st[k] = 255u; }
    prev = 0;

    title_strip(0); title_strip(120);           /* the title page: 2 s, or until a button once the sound is ready */
    {
        uint32_t t0 = fm1_ms, rl;
        wt_build();                             /* (the winds' and brass' tables: while the title shows) */
        (void)fm1_input_edges(&rl);             /* (a press while it loads does not count) */
        while (fm1_ms - t0 < 2000u) {
            platform_poll();
            if (fm1_input_edges(&rl)) break;    /* (taken: the press only skips the title) */
        }
    }
    lcd_fill(0, 0, 240, 240, C_PAPER);

    for (;;) {
        uint32_t keys, down, up, rel, b, run, flash, play_now, bar, bars, edit_scr, frac, gnow = 0;
        int32_t e;
#define CHANGED() (un_mark(PK(sel), PSEL(sel)), dirty = 1, t_change = fm1_ms)
#define SAY(s, ms) (msg = (s), msg_until = fm1_ms + (ms), foot = 1, head = 1)
#define GOTO(x) (scr = (x), redraw = foot = head = 1)
        platform_poll();
        b = fm1_input_edges(&rel);

        /* ---- the save page: SAVE opens and closes it; there REC saves, PLAY loads (each asks first) */
        if (b & (1u << BTN_SAVE)) {
            if (scr == SCR_SAVE) GOTO(save_back); else { save_back = scr; GOTO(SCR_SAVE); }
            confirm = 0; b &= ~(1u << BTN_SAVE);
        }
        if (scr == SCR_SAVE) {
            uint32_t mask = 1u << BTN_PLAY | 1u << BTN_REC, op = b >> BTN_REC & 1u ? 1u : b >> BTN_PLAY & 1u ? 2u : 0u;
            if (b & ~mask && confirm) { confirm = 0; redraw = 1; }   /* another button: no */
            if (op && !proj_go) {
                if (op == 2u && !proj_bpm[slot]) { SAY("EMPTY", 1500u); confirm = 0; }
                else if (confirm != op || fm1_ms - confirm_t > 3000u) { confirm = op; confirm_t = fm1_ms; }
                else {                          /* sure: stop, let it ring out, then do it (below) */
                    confirm = 0; proj_go = op; proj_go_t = fm1_ms; run_req = 0;
                    SAY(op == 1u ? "SAVING" : "LOADING", 3000u);
                }
                redraw = 1;
            }
            if (confirm && fm1_ms - confirm_t > 3000u) { confirm = 0; redraw = 1; }
            b &= ~mask; rel &= ~mask; rec_down = 0;
        }
        if (proj_go && (quiet() || fm1_ms - proj_go_t > 1500u)) {   /* (writing the flash stalls the sound: in silence) */
            char *q;
            int rc = proj_go == 2u ? proj_load(slot) : proj_save(slot);
            q = cat(msgbuf, rc == -2 ? "TOO MUCH TO SAVE" : rc ? (proj_go == 2u ? "LOAD FAILED" : "SAVE FAILED") : (proj_go == 2u ? "LOADED " : "SAVED "));
            if (!rc) { *q++ = (char)('1' + slot); *q = 0; }
            SAY(msgbuf, 2000u);
            if (!rc && proj_go == 2u) { set_tempo(); set_hall(); CHANGED(); chart_k1 = ~0u; }
            proj_go = 0; redraw = head = 1;
        }
        edit_scr = scr == SCR_SETUP || scr == SCR_EDIT;   /* the editing pages: the same knobs for the pattern */
        if (edit_scr || scr == SCR_CURVE) un_keep(PK(sel), PSEL(sel), fm1_ms - t_change > 1200u);   /* (undo: the pattern as
                                                   it was before each burst of edits) */

        /* ---- buttons */
        if (b & (1u << BTN_PLAY)) { run_req = !run_req; head = 1; }
        if (seq_down && scr == SCR_HOME && fm1_in.buttons >> BTN_SEQ & 1u && !seq_used && fm1_ms - seq_t0 > 600u) {   /* SEQ
                                                   held: end at the end of the song (the Composer's, or the patterns'), the
                                                   last notes held on; held again: it goes on */
            seq_used = 1;
            if (run_req && !cin_left) { end_req = (uint8_t)!end_req; SAY(end_req ? "FINE - FERMATA" : "NO FINE: IT GOES ON", 1500u); }
            else SAY("PLAY, THEN HOLD SEQ TO END", 1200u);
        }
        if (rel & (1u << BTN_SEQ) && seq_down) {   /* let go: a tap was the Composer, on this part's row */
            if (!seq_used && scr == SCR_HOME) { for (crow = 0; crow < 6u && ORDER7[crow] != PK(sel); crow++) {} GOTO(SCR_COMP); }
            seq_down = 0;
        }
        if (!run_req && end_req) end_req = 0;   /* (stopped: no ending waiting) */
        if (scr == SCR_SONG && ts_ask && (b & ~(1u << BTN_REC))) { ts_ask = 0; redraw = 1; SAY("KEPT", 800u); }
        if (scr == SCR_KEY && key_ask && (b & ~(1u << BTN_REC))) { key_ask = 0; SAY("THE MUSIC STAYS", 900u); }
        if (scr == SCR_KEY && (b & (1u << BTN_REC))) {   /* the KEY page: REC asks, REC again moves the music into the key */
            if (KEYSIG == key_from) SAY("THE MUSIC IS IN THIS KEY", 1200u);
            else if (!key_ask) { key_ask = 1; SAY("REC AGAIN: MOVE THE MUSIC", 4000u); }
            else {
                key_ask = 0;
                if (key_move(key_from, KEYSIG)) { key_from = KEYSIG; CHANGED(); redraw = 1; SAY("THE MUSIC MOVED", 1500u); }
                else SAY("A NOTE WOULD LEAVE THE RANGE", 1500u);
            }
            b &= ~(1u << BTN_REC);
        }
        if (scr == SCR_SONG && (b & (1u << BTN_REC))) {   /* the song page: REC asks, REC again changes the time signature */
            if (song_it >= SONG_SWING) {}       /* (swing, humanise, tap tempo: nothing to confirm) */
            else if (ts_pend == sc.tsig) SAY("ALREADY THIS ONE", 1000u);
            else if (!ts_ask) { ts_ask = 1; redraw = 1; }
            else {                              /* a new time signature, accepted: every pattern cleared */
                static char tm[24];
                uint32_t b0 = BEAT;
                sc.tsig = (uint8_t)ts_pend; ts_ask = 0;
                sc.tempo = (uint8_t)clampi((int32_t)((sc.tempo * b0 + BEAT / 2u) / BEAT), 30, 240);   /* (the same speed
                                                   of 16ths: the tempo counts the new beat) */
                set_tempo();
                clear_all(); cursor = 0; CHANGED(); redraw = head = 1;
                cat(ts_name(tm, sc.tsig), ": PATTERNS CLEARED");
                SAY(tm, 2000u);
            }
            b &= ~(1u << BTN_REC);              /* (not live recording, not the clear) */
        }
        if (b & (1u << BTN_REC) && fm1_in.buttons >> BTN_ARP & 1u && (edit_scr || scr == SCR_CURVE)) {   /* ARP held + REC:
                                                   undo the last edit (again: redo it) */
            arp_used = 1; b &= ~(1u << BTN_REC);
            if (un_swap(PK(sel), PSEL(sel))) {
                if (cursor >= LEN(sel)) cursor = 0;
                dirty = 1; t_change = fm1_ms; redraw = foot = head = 1; SAY("UNDONE  (ARP+REC: REDO)", 1200u);
            } else SAY("NOTHING TO UNDO", 900u);
        }
        if (b & (1u << BTN_REC) && clear_ask && fm1_ms - clear_ask < 4000u) {   /* asked, and REC again: start again */
            clear_all();
            sc.swing = sc.human = 0;            /* (everything from scratch: no swing, no humanise either, and the mix in
                                                   the middle: every pattern mf at 0 dB, the concert hall, the
                                                   metronome off at its middle level; vibrato normal, every part a
                                                   section, nothing muted) */
            for (k = 0; k < NROW * NPAT; k++) { (&sc.pexpr[0][0])[k] = 80; (&sc.pbal[0][0])[k] = 0; (&sc.pvib[0][0])[k] = 2; }
            for (k = 0; k < P_BD * NPAT; k++) { (&sc.pmute[0][0])[k] = 0; (&sc.psolo[0][0])[k] = 0; }   /* (each part a
                                                   section, vibrato normal, nothing muted: the drum slots neither) */
            for (k = 0; k < NINS; k++) { sc.mute[k] = 0; sc.solo[k] = 0; }
            sc.rsv = 2; set_hall(); sc.click = CLK_OFF; sc.clickvol = 5;
            sc.rsv2[0] = 0; sc.rsv2[1] = 0;     /* (C major; scale lock and the keyboard's lights off) */
            clear_ask = 0; rec_t0 = fm1_ms; rec_down = 1; rec_done = 1;
            cursor = 0; CHANGED(); redraw = foot = head = 1; SAY("ALL PATTERNS CLEARED", 2000u);
        } else if (b & (1u << BTN_REC)) { rec_t0 = fm1_ms; rec_down = 1; rec_done = 0; clear_ask = 0; }
        else if (b && clear_ask) { clear_ask = 0; redraw = 1; SAY("NOT CLEARED", 900u); }
        if (rec_down && !rec_done && fm1_in.buttons >> BTN_REC & 1u && fm1_ms - rec_t0 > 1500u) {   /* held: ask first */
            clear_ask = fm1_ms; rec_done = 1; ask_shown = 0;
        }
        if (clear_ask && fm1_ms - clear_ask >= 4000u) { clear_ask = 0; redraw = foot = head = 1; }   /* (not asked again) */
        if (rel & (1u << BTN_REC) && rec_down) {    /* a tap: live recording on / off */
            if (!rec_done && fm1_ms - rec_t0 < 600u) { rec = !rec; head = 1; if (rec) rec_take++; SAY(rec ? "RECORDING: PLAY TO RECORD" : "RECORDING OFF", 1200u); }
            rec_down = 0;
        }
        for (k = 0; k < NSEC; k++) {                /* FX SEL ENV LFO: a section's setup */
            static const uint8_t SB[NSEC] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO};
            if (b & (1u << SB[k])) {
                if (scr == SCR_SETUP && INS[sel].s == k && k < 3u)   /* again: the section's other part */
                    sel = SEC_FIRST[k] + (sel - SEC_FIRST[k] + 1u) % (SEC_FIRST[k + 1u] - SEC_FIRST[k]);
                else { part[INS[sel].s] = (uint8_t)sel; sel = part[k]; }
                part[k] = (uint8_t)sel; sel_all = 0; GOTO(SCR_SETUP);
                if (cursor >= LEN(sel)) cursor = 0;
            }
        }
        if (scr == SCR_HOME && !MELODIC(sel)) sel_all = 1;   /* (on the orchestra the percussion is one) */
        if (b & (1u << BTN_HOME)) {
            GOTO(SCR_HOME);                     /* (the Composer: SEQ) */
        }
        if (b & (1u << BTN_EDIT)) {
            if (scr == SCR_MIX) GOTO(SCR_HOME);
            else if (scr == SCR_COPY) GOTO(cp_back);   /* (EDIT: back, nothing copied) */
            else if (scr == SCR_EDIT && !music) music = 1;   /* the pattern: piano roll -> music -> its curve -> roll */
            else if (scr == SCR_EDIT) { music = 0; ptsel = 0; GOTO(SCR_CURVE); }
            else if (scr == SCR_CURVE) GOTO(MELODIC(sel) ? SCR_EDIT : SCR_SETUP);
            else if (scr == SCR_SETUP && !MELODIC(sel)) { ptsel = 0; GOTO(SCR_CURVE); }   /* the kit's curve */
            else if (MELODIC(sel)) GOTO(SCR_EDIT);
            else { sel_all = 0; GOTO(SCR_SETUP); }
            if (cursor >= LEN(sel)) cursor = 0;
            redraw = foot = head = 1;
        }
        if (b & (1u << BTN_ARP)) { arp_used = 0; arp_t0 = fm1_ms; }
        if (scr == SCR_HOME && fm1_in.buttons >> BTN_ARP & 1u && !arp_used && fm1_ms - arp_t0 > 500u) {   /* ARP held: only
                                                   the chosen part heard, while it is held (no section/solo/muted then) */
            static char lb[28];
            arp_used = 1; listen_only = (uint8_t)PK(sel);
            {
                static const char *const SECT[NROW] = {"HIGH STRINGS", "LOW STRINGS", "HIGH WINDS", "LOW WINDS", "HIGH BRASS",
                                                        "LOW BRASS", "PERCUSSION"};
                cat(cat(lb, "SECTIONAL: "), SECT[PK(sel)]);   /* (a rehearsal of one section alone) */
            }
            SAY(lb, 60000u);
        }
        if (listen_only != 0xFFu && !(fm1_in.buttons >> BTN_ARP & 1u)) { listen_only = 0xFFu; msg_until = fm1_ms; head = foot = 1; }
        if (rel & (1u << BTN_ARP) && !arp_used && scr == SCR_CURVE) {   /* a point added, after the chosen one */
            curve_t *c = &sc.curve[PK(sel)][PSEL(sel)];
            uint32_t len = LEN(sel);
            if (!c->n) { curve_preset(c, 0, len); ptsel = 0; head = 1; }
            else if (c->n < NCP) {
                uint32_t a = c->p[ptsel].pos, bpos = ptsel + 1u < c->n ? c->p[ptsel + 1u].pos : len, np = (a + bpos) / 2u, j;
                if (bpos <= a + 1u && ptsel + 1u >= c->n) np = a;   /* (at the very end: on the same step) */
                for (j = c->n; j > ptsel + 1u; j--) c->p[j] = c->p[j - 1u];
                c->p[ptsel + 1u].pos = (uint8_t)np;
                c->p[ptsel + 1u].val = (uint8_t)curve_at(c, np * 256u);
                c->p[ptsel + 1u].shape = c->p[ptsel].shape;
                c->n++; ptsel++;
            } else SAY("6 POINTS AT MOST", 1200u);
            CHANGED(); redraw = foot = 1;
        } else if (rel & (1u << BTN_ARP) && !arp_used) {
            if (scr == SCR_EDIT) {              /* rest: the notes here go; inside a longer note, it ends here */
                uint32_t q = PSEL(sel);
                int32_t s0 = cover(sel, q, cursor);
                if (has_notes(&MS(sel, cursor))) { MS(sel, cursor).n[0] = MS(sel, cursor).n[1] = MS(sel, cursor).n[2] = 0; }
                else if (s0 >= 0) MS(sel, s0).len = (uint8_t)(cursor - (uint32_t)s0);
                cursor = (cursor + 1u) % LEN(sel);
                CHANGED(); redraw = foot = 1; t_nav = fm1_ms;
            } else if ((scr == SCR_HOME || scr == SCR_SETUP) && MELODIC(sel)) {   /* section -> solo (one player) ->
                                                   muted -> section */
                if (MUTE(sel)) { MUTE(sel) = 0; SOLO(sel) = 0; }
                else if (SOLO(sel)) { SOLO(sel) = 0; MUTE(sel) = 1; }
                else SOLO(sel) = 1;
                SAY(MUTE(sel) ? "TACET: NONE" : SOLO(sel) ? "SOLO: ONE PLAYER" : "TUTTI: WHOLE SECTION", 1200u);
                CHANGED(); redraw = foot = 1;
            } else if (scr == SCR_HOME && sel_all) {   /* the percussion: every drum muted, or none */
                uint32_t any = 0;
                for (k = P_BD; k < NINS; k++) any |= !MUTE(k);
                for (k = P_BD; k < NINS; k++) MUTE(k) = (uint8_t)any;
                SAY(any ? "TACET: NONE" : "TUTTI: WHOLE SECTION", 1200u);
                CHANGED(); redraw = foot = 1;
            } else if (scr == SCR_SETUP) {      /* a drum: muted or not */
                MUTE(sel) = !MUTE(sel);
                CHANGED(); redraw = foot = 1;
            }
        }
        if (b & (1u << BTN_SEQ) && scr == SCR_CURVE) {   /* the chosen point deleted (one stays) */
            curve_t *c = &sc.curve[PK(sel)][PSEL(sel)];
            uint32_t j;
            if (c->n > 1u) {
                for (j = ptsel; j + 1u < c->n; j++) c->p[j] = c->p[j + 1u];
                c->n--;
                if (ptsel >= c->n) ptsel = c->n - 1u;
                CHANGED(); redraw = foot = 1;
            }
        } else if (b & (1u << BTN_SEQ)) {
            if (edit_scr) {                     /* the copy page: this part's row, its pattern */
                for (cp_row = 0; cp_row < 6u && ORDER7[cp_row] != PK(sel); cp_row++) {}
                cp_pat = PSEL(sel); cp_ask = 0; cp_back = scr;
                GOTO(SCR_COPY);
            } else if (scr == SCR_COPY) {       /* ask; SEQ again: copy it into the pattern being edited */
                uint32_t rs = ORDER7[cp_row % NROW], rd = PK(sel), qd = PSEL(sel), d;
                if (rs == rd && cp_pat == qd) SAY("THAT IS THIS PATTERN", 1200u);
                else if ((rs == P_BD) != (rd == P_BD)) SAY("NOT THE SAME KIND", 1200u);
                else if (!cp_ask) { cp_ask = 1; redraw = 1; }
                else {
                    if (rd == P_BD) for (d = 0; d < NDRUM; d++) bytes_copy(sc.drum[d][qd], sc.drum[d][cp_pat], NSTEP);
                    else bytes_copy(sc.mel[rd][qd], sc.mel[rs][cp_pat], sizeof sc.mel[0][0]);
                    sc.len[rd][qd] = sc.len[rs][cp_pat];
                    sc.curve[rd][qd] = sc.curve[rs][cp_pat];   /* (its curve too, and its settings) */
                    sc.pexpr[rd][qd] = sc.pexpr[rs][cp_pat]; sc.pbal[rd][qd] = sc.pbal[rs][cp_pat]; sc.pvib[rd][qd] = sc.pvib[rs][cp_pat];
                    if (rd < P_BD) { sc.pmute[rd][qd] = sc.pmute[rs][cp_pat]; sc.psolo[rd][qd] = sc.psolo[rs][cp_pat]; }
                    if (cursor >= LEN(sel)) cursor = 0;
                    cp_ask = 0; CHANGED(); GOTO(cp_back); SAY("COPIED", 1200u);
                }
            } else if (scr == SCR_HOME) {       /* (a tap: the Composer, as it is let go; held: the ending, below) */
                seq_t0 = fm1_ms; seq_down = 1; seq_used = 0;
            } else if (scr == SCR_COMP) GOTO(SCR_HOME);
        }
        if (scr == SCR_COPY && cp_ask && (b & ~(1u << BTN_SEQ))) { cp_ask = 0; redraw = 1; }   /* (anything else: no) */
        if (b & (1u << BTN_GLO) && scr == SCR_HOME) {   /* GLO: MIX (on the chosen section's row), the song page, back */
            for (mrow = 0; mrow < 6u && ORDER7[mrow] != PK(sel); mrow++) {}
            GOTO(SCR_MIX);
        }
        else if (b & (1u << BTN_GLO) && scr == SCR_MIX) {
            ts_pend = sc.tsig; ts_ask = 0;
            for (song_it = 0; song_it < SONG_SWING - 1u && TS_ORDER[song_it] != sc.tsig; song_it++) {}
            GOTO(SCR_SONG);
        }
        else if (b & (1u << BTN_GLO) && scr == SCR_SONG) { key_from = KEYSIG; key_ask = 0; GOTO(SCR_KEY); }
        else if (b & (1u << BTN_GLO) && scr == SCR_KEY) {
            GOTO(SCR_HOME);
        } else if (b & (1u << BTN_GLO) && scr == SCR_CURVE) {   /* the curve on (it plays) / bypassed (flat) */
            curve_t *c = &sc.curve[PK(sel)][PSEL(sel)];
            if (!c->n) curve_preset(c, 0, LEN(sel));
            else c->on = !c->on;
            SAY(c->on ? "CURVE ON" : "CURVE BYPASSED", 1000u);
            CHANGED(); redraw = head = 1;
        } else if (b & (1u << BTN_GLO) && edit_scr) {
            if (clear_arm && fm1_ms - clear_arm < 2000u) {   /* the pattern emptied (the kit's: every drum's) */
                uint32_t q = PSEL(sel);
                if (MELODIC(sel)) for (k = 0; k < NSTEP; k++) { mstep_t z = {{0, 0, 0}, 0, 0}; sc.mel[sel][q][k] = z; }
                else for (k = 0; k < NDRUM * NSTEP; k++) sc.drum[k / NSTEP][q][k % NSTEP] = 0;
                sc.curve[PK(sel)][q].on = 0; sc.curve[PK(sel)][q].n = 0;
                clear_arm = 0; CHANGED(); redraw = 1; SAY("PATTERN CLEARED", 1500u);
            } else { clear_arm = fm1_ms; SAY("GLO AGAIN: CLEAR", 2000u); }
        }
        if (!MELODIC(sel) && scr == SCR_SETUP && (b & (1u << BTN_OCT_UP | 1u << BTN_OCT_DOWN))) {   /* the drum
                                                   machine: OCT- / OCT+ the page (ALGORITHM is the sound there) */
            uint32_t np = (LEN(sel) + BAR - 1u) / BAR, pg = wrapi((int32_t)(cursor / BAR) + (int32_t)(b >> BTN_OCT_UP & 1u) - (int32_t)(b >> BTN_OCT_DOWN & 1u), np);
            cursor = pg * BAR + cursor % BAR;
            if (cursor >= LEN(sel)) cursor = pg * BAR;
            t_nav = fm1_ms; redraw = foot = head = 1;
        }
        if (MELODIC(sel)) {                         /* OCT- / OCT+: let go after a tap, the keyboard's octave for this
                                                   part; held on an editing page, the pattern an octave down / up */
            uint32_t ob = 1u << BTN_OCT_UP | 1u << BTN_OCT_DOWN;
            if (b & ob) { oct_t0 = fm1_ms; oct_dir = b >> BTN_OCT_UP & 1u ? 1 : -1; oct_used = 0; }
            if (oct_dir && !oct_used && fm1_in.buttons & ob && fm1_ms - oct_t0 > 600u && (scr == SCR_EDIT || scr == SCR_SETUP)) {
                oct_used = 1;
                if (transpose(sel, 12 * oct_dir)) { SAY(oct_dir > 0 ? "PATTERN UP AN OCTAVE" : "PATTERN DOWN AN OCTAVE", 1200u); CHANGED(); redraw = 1; }
                else SAY("OUT OF RANGE", 1000u);
            }
            if (rel & ob && oct_dir) {
                if (!oct_used) {
                    int32_t o = (int32_t)sc.tune[sel] + oct_dir;
                    if (o >= 2 && o <= 6) {
                        sc.tune[sel] = (uint8_t)o;
                        view_lo[sel] = (uint8_t)(ed_base(sel) - 5u);   /* the roll follows the octave */
                        CHANGED(); redraw = 1;
                    }
                }
                oct_dir = 0;
            }
        }
        run = run_req && !cin_left;             /* (counting in: not yet playing) */
        { static uint32_t run_was; if (run != run_was) { run_was = run; head = redraw = foot = 1; } }   /* (stopped by itself too:
                                                   the song's end) */
        rec_live = (uint8_t)rec;
        if (cin_left) {                         /* the count-in: 4 3 2 1 (as many as the bar's beats) */
            uint32_t bt = (cin_left - 1u + BEAT - 1u) / BEAT;
            if (bt != cin_shown && bt) {
                static char cb[16];
                cb[0] = 0; catu(cat(cb, "COUNT-IN  "), bt, 0);
                SAY(cb, 2000u); cin_shown = bt;
            }
        } else if (cin_shown) { cin_shown = 0; msg_until = 0; head = foot = 1; }

        /* ---- encoders. On every editing page: ALGORITHM the page (bar), KNOB4 the pattern's length */
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
            if (scr == SCR_COPY) { cp_row = wrapi((int32_t)cp_row + e, NROW); cp_ask = 0; }   /* the copy page: the part */
            else if (scr == SCR_KEY) {          /* the key: a sharp or flat a click (the notation; REC moves the music) */
                sc.rsv2[0] = (uint8_t)(int8_t)clampi(KEYSIG + e, -7, 7); key_ask = 0; CHANGED(); head = 1;
            }
            else if (scr == SCR_SONG) {         /* (along the page: the time signatures, swing, humanise, tap tempo) */
                song_it = (uint32_t)clampi((int32_t)song_it + e, 0, NSONGIT - 1);
                if (song_it < SONG_SWING) ts_pend = TS_ORDER[song_it];
                ts_ask = 0; tap_n = 0;
            }   /* the song page: the
                                                   time signature chosen */
            else if (scr == SCR_SAVE) { slot = wrapi((int32_t)slot + e, NPROJ); confirm = 0; }
            else if (scr == SCR_COMP) crow = wrapi((int32_t)crow + e, NROW);
            else if (scr == SCR_MIX) {          /* the row; the section there is the one chosen (the keys play it, and
                                                   the orchestra keeps it) */
                mrow = wrapi((int32_t)mrow + e, MIX_CLICK + 1u);
                if (mrow < MIX_HALL) { sel = ORDER7[mrow]; sel_all = sel == P_BD; }
            }
            else if (scr == SCR_CURVE) {        /* a start shape: flat, crescendo, diminuendo, swell (it replaces the curve) */
                preset = wrapi((int32_t)preset + (e > 0 ? 1 : -1), 4u);
                curve_preset(&sc.curve[PK(sel)][PSEL(sel)], preset, LEN(sel));
                ptsel = 0; SAY(PRESET_NAME[preset], 1000u); CHANGED(); head = 1;
            }
            else if (scr == SCR_HOME) {         /* through all of them, as they sit: percussion, brass, winds, strings */
                uint32_t pos = 0;
                part[INS[sel].s] = (uint8_t)sel;
                while (pos < 6u && ORDER7[pos] != PK(sel)) pos++;
                pos = wrapi((int32_t)pos + e, NROW);
                sel = ORDER7[pos]; sel_all = sel == P_BD;
                ov_until = 0; head = 1;
            } else if (scr == SCR_SETUP) {      /* the part (row) */
                uint32_t s = INS[sel].s, n = SEC_FIRST[s + 1u] - SEC_FIRST[s];
                sel = SEC_FIRST[s] + wrapi((int32_t)(sel - SEC_FIRST[s]) + e, n);
                if (cursor >= LEN(sel)) cursor = 0;
                head = 1;
            } else if (scr == SCR_EDIT && fm1_in.buttons >> BTN_ARP & 1u) {   /* ARP held: a beat a click (to the next
                                                   beat's start; back: this beat's start first), no rest on letting go */
                int32_t at = (int32_t)(cursor / BEAT * BEAT);
                at += e > 0 || (uint32_t)at == cursor ? e * (int32_t)BEAT : (e + 1) * (int32_t)BEAT;
                cursor = wrapi(at, LEN(sel)); t_nav = fm1_ms; arp_used = 1;
            } else if (scr == SCR_EDIT) { cursor = wrapi((int32_t)cursor + e, LEN(sel)); t_nav = fm1_ms; }   /* the step */
            redraw = foot = 1;
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            if (scr == SCR_COPY) { cp_pat = wrapi((int32_t)cp_pat + e, NPAT); cp_ask = 0; }   /* the copy page: the pattern */
            else if (scr == SCR_SAVE) { slot = wrapi((int32_t)slot + e, NPROJ); confirm = 0; }
            else if (scr == SCR_COMP) crow = wrapi((int32_t)crow + e, NROW);
            else if (scr == SCR_MIX) {
                mrow = wrapi((int32_t)mrow + e, MIX_CLICK + 1u);
                if (mrow < MIX_HALL) { sel = ORDER7[mrow]; sel_all = sel == P_BD; }
            }
            else if (scr == SCR_CURVE) { uint32_t n = sc.curve[PK(sel)][PSEL(sel)].n; if (n) ptsel = wrapi((int32_t)ptsel + e, n); }
            else if (scr == SCR_EDIT) {         /* the roll, the music: the articulation, for the note here and the
                                                   notes written after (until it is regular again) */
                static const char *const AS[NART] = {"REGULAR", "STACCATO", "ACCENT", "LEGATO", "MARCATO", "PIZZICATO", "TREMOLO", "TRILL"};
                int32_t s0 = cover(sel, PSEL(sel), cursor), st1 = e > 0 ? 1 : -1, nn = e > 0 ? e : -e;
                while (nn--) do art_cur = wrapi((int32_t)art_cur + st1, NART);   /* (pizzicato, tremolo: strings only) */
                    while (INS[sel].ty != TY_STR && ART_STR(art_cur));
                if (s0 >= 0) { mstep_t *m = &MS(sel, (uint32_t)s0); m->dyn = (uint8_t)((m->dyn & 3u) | ARTW(sel) << 2); CHANGED(); }
                SAY(AS[art_cur], 900u); redraw = 1;
            }
            else if (scr == SCR_SETUP && !MELODIC(sel)) {   /* the drum machine: this slot's sound */
                uint8_t *t = &sc.dtype[sel - P_BD];
                *t = (uint8_t)wrapi((int32_t)*t + e, NDT);
                if (*t == DT_TOM && (sc.tune[sel] < 45u || sc.tune[sel] > 72u)) sc.tune[sel] = 62;   /* (D4: as recorded) */
                if (*t >= DT_TIMP1) sc.tune[sel] = TIMP_TUNE[*t - DT_TIMP1];   /* (each timpano its own pitch) */
                live(sel, 0, cur_dyn, 1);
                SAY(DTYPE[*t].name, 900u); CHANGED(); head = 1;
            }
            else if (edit_scr && !(scr == SCR_SETUP && MELODIC(sel))) {   /* the page (bar): the cursor keeps its place */
                uint32_t np = (LEN(sel) + BAR - 1u) / BAR, pg = wrapi((int32_t)(cursor / BAR) + e, np);
                cursor = pg * BAR + cursor % BAR;
                if (cursor >= LEN(sel)) cursor = pg * BAR;
                t_nav = fm1_ms; head = 1;
            }
            redraw = foot = 1;
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0 && scr != SCR_SAVE && scr != SCR_MIX) {
            int32_t step = e;                   /* (one click a letter) */
            if (step) {
                uint32_t copy = fm1_in.buttons >> BTN_ARP & 1u;
                if (copy) arp_used = 1;
                ptsel = 0;
                if (scr == SCR_COMP) {          /* the song A-J (ARP held: this song copied into it) */
                    uint32_t to = wrapi((int32_t)sc.comp_song + step, NSONG);
                    if (copy) bytes_copy(sc.chain[to], sc.chain[sc.comp_song % NSONG], sizeof sc.chain[0]);
                    sc.comp_song = (uint8_t)to;
                } else {                        /* the chosen part's pattern A-J (a drum: the kit's) */
                    uint32_t k0 = PK(sel), queue = rec && run && !sc.comp_on && !copy, i;
                    uint32_t to = wrapi((int32_t)(queue && rec_next[k0] < NPAT ? rec_next[k0] : sc.psel[k0]) + step, NPAT), from = PSEL(sel);
                    if (copy) {                 /* the pattern copied into the new letter */
                        if (k0 == P_BD) for (i = 0; i < NDRUM; i++) bytes_copy(sc.drum[i][to], sc.drum[i][from], NSTEP);
                        else bytes_copy(sc.mel[k0][to], sc.mel[k0][from], sizeof sc.mel[0][0]);
                        sc.len[k0][to] = sc.len[k0][from];
                        sc.curve[k0][to] = sc.curve[k0][from];   /* (its curve too, and its settings) */
                        sc.pexpr[k0][to] = sc.pexpr[k0][from]; sc.pbal[k0][to] = sc.pbal[k0][from]; sc.pvib[k0][to] = sc.pvib[k0][from];
                        if (k0 < P_BD) { sc.pmute[k0][to] = sc.pmute[k0][from]; sc.psolo[k0][to] = sc.psolo[k0][from]; }
                    }
                    if (queue) {                /* recording: this pattern plays (and records) to its end first */
                        static char nq[24];
                        char *q;
                        if (!pat_used(k0, to)) sc.len[k0][to] = sc.len[k0][from];   /* (empty: as long as this one) */
                        rec_next[k0] = (uint8_t)(to == sc.psel[k0] ? 0xFFu : to);
                        q = cat(nq, "NEXT: "); *q++ = (char)('A' + to); cat(q, ", AFTER THIS ONE");
                        SAY(rec_next[k0] < NPAT ? nq : "STAYING ON THIS ONE", 1500u);
                    } else {
                        sc.psel[k0] = (uint8_t)to;
                        if (cursor >= LEN(sel)) cursor = 0;
                    }
                    if (copy) SAY("COPIED", 900u);
                }
                CHANGED(); redraw = foot = head = 1;
            }
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            if (scr == SCR_SAVE) { slot = wrapi((int32_t)slot + e, NPROJ); confirm = 0; redraw = 1; }
            else if (scr == SCR_KEY) { sc.rsv2[1] = (uint8_t)((sc.rsv2[1] & ~1u) | (e > 0 ? 1u : 0u)); CHANGED(); redraw = 1; }   /* scale lock */
            else if (scr == SCR_SONG && (song_it == SONG_SWING || song_it == SONG_HUMAN)) {
                uint8_t *v = song_it == SONG_SWING ? &sc.swing : &sc.human;
                *v = (uint8_t)clampi((int32_t)*v + e, 0, 6);
                CHANGED(); redraw = foot = 1;
            }
            else if (scr == SCR_HOME) {         /* the section's expression (continuous: the markings are ranges) */
                uint32_t r = PK(sel);
                EXPR(r) = expr_step(EXPR(r), e);   /* (a marking a click: pp p mp mf f ff) */
                if (rec && run && xrec_row == 0xFFu) xrec_start(r);   /* recording: the take begins */
                else if (xrec_row == r) xbuf[xrec_last] = EXPR(r);
                ov_kind = 1; ov_until = fm1_ms + 1500u; redraw = 1; head = 1;
            }
            else if (scr == SCR_MIX) {          /* the expression, as KNOB 1 on the orchestra (the hall: how much of it) */
                if (mrow == MIX_HALL) { sc.rsv = (uint8_t)clampi((int32_t)sc.rsv + e, 0, NHALL - 1); set_hall(); }
                else if (mrow == MIX_CLICK) sc.click = (uint8_t)clampi((int32_t)sc.click + e, 0, NCLK - 1);
                else { uint32_t r = ORDER7[mrow]; EXPR(r) = expr_step(EXPR(r), e); }
                redraw = foot = 1;
            }
            else if (scr == SCR_CURVE) { uint32_t n = sc.curve[PK(sel)][PSEL(sel)].n; if (n) ptsel = wrapi((int32_t)ptsel + e, n); redraw = foot = 1; }
            else if (scr == SCR_COMP) {         /* the song's length, 1-8 boxes */
                uint8_t *cl = &sc.clen[sc.comp_song % NSONG];
                *cl = (uint8_t)clampi((int32_t)*cl + e, 1, NBOX);
                if (cbox >= *cl) cbox = *cl - 1u;
                redraw = foot = 1;
            }
            else if (scr == SCR_SETUP && MELODIC(sel)) {   /* a part's page: its expression, a marking a click */
                EXPR(sel) = expr_step(EXPR(sel), e);
                redraw = foot = 1;
            }
            else if (edit_scr) {                /* the roll, the music, the drum machine: the pattern's length in bars */
                LEN(sel) = (uint8_t)(BAR * (uint32_t)clampi((int32_t)((LEN(sel) + BAR - 1u) / BAR) + e, 1, (int32_t)(NSTEP / BAR)));
                if (cursor >= LEN(sel)) cursor = LEN(sel) - 1u;
                redraw = foot = head = 1;
            }
            if (scr != SCR_SAVE) CHANGED();
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0 && scr == SCR_CURVE) {   /* the point's place (between its neighbours) */
            curve_t *c = &sc.curve[PK(sel)][PSEL(sel)];
            if (c->n) {
                int32_t lo = ptsel ? c->p[ptsel - 1u].pos : 0, hi = ptsel + 1u < c->n ? c->p[ptsel + 1u].pos : (int32_t)LEN(sel);
                c->p[ptsel].pos = (uint8_t)clampi((int32_t)c->p[ptsel].pos + e, lo, hi);
                CHANGED(); redraw = foot = 1;
            }
        } else if (e && scr == SCR_KEY) {       /* the keyboard's lights: the key's notes glow */
            sc.rsv2[1] = (uint8_t)((sc.rsv2[1] & ~2u) | (e > 0 ? 2u : 0u)); CHANGED(); redraw = 1;
        } else if (e && scr == SCR_MIX) {       /* a section's balance; the hall row: the kind of hall */
            if (mrow == MIX_HALL) { sc.rsv = (uint8_t)clampi((int32_t)sc.rsv + e, 0, NHALL - 1); set_hall(); }
            else if (mrow == MIX_CLICK) { sc.clickvol = (uint8_t)clampi((int32_t)sc.clickvol + e, 1, 8); click(1u); }   /* (heard) */
            else { uint32_t r = ORDER7[mrow]; BALN(r) = (int8_t)clampi(BALN(r) + e, -30, 6); }
            CHANGED(); redraw = foot = 1;
        } else if (e && scr == SCR_COMP) {      /* the box */
            cbox = wrapi((int32_t)cbox + e, chain_len());
            redraw = foot = 1;
        } else if (e && scr == SCR_SETUP && MELODIC(sel)) {   /* a part's page: its vibrato (off, light, normal, wide) */
            VIB(sel) = (uint8_t)((VIB(sel) & 0x80u) | (uint32_t)clampi((int32_t)(VIB(sel) & 3u) + e, 0, 3));   /* (the mute kept) */
            CHANGED(); redraw = foot = 1;
        } else if (e && scr == SCR_SETUP && TIMPANI(sel)) {   /* a timpano's tuning */
            sc.tune[sel] = (uint8_t)clampi((int32_t)sc.tune[sel] + e, DTY(sel).ty == TY_TOM ? 45 : 36, DTY(sel).ty == TY_TOM ? 72 : 60);
            live(sel, 0, cur_dyn, 1); CHANGED(); foot = 1;
        } else if (e && scr == SCR_HOME) {      /* the section's balance (the mix; the MIX page shows all seven) */
            uint32_t r = PK(sel);
            BALN(r) = (int8_t)clampi(BALN(r) + e, -30, 6);
            ov_kind = 2; ov_until = fm1_ms + 1500u; redraw = 1; CHANGED();
        } else if (e && scr == SCR_EDIT) {      /* LENGTH: how long the notes at the cursor last (and the next ones) */
            int32_t s0 = cover(sel, PSEL(sel), cursor);
            if (s0 >= 0) {
                mstep_t *m = &MS(sel, (uint32_t)s0);
                nlen = (uint32_t)clampi((int32_t)(m->len ? m->len : 1u) + e, 1, NSTEP);
                m->len = (uint8_t)nlen;
                CHANGED();
            } else nlen = (uint32_t)clampi((int32_t)nlen + e, 1, NSTEP);
            redraw = foot = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0 && scr == SCR_SETUP && MELODIC(sel)) {   /* a part's page: its balance */
            BALN(sel) = (int8_t)clampi(BALN(sel) + e, -30, 6);
            CHANGED(); redraw = foot = 1;
        } else if (e && scr == SCR_HOME) {      /* the whole orchestra transposed live, a semitone a click (not saved) */
            static char tb[24];
            char *q;
            live_tr = (int8_t)clampi(live_tr + e, -12, 12);
            q = cat(tb, "TRANSPOSED ");
            if (live_tr) { *q++ = live_tr > 0 ? '+' : '-'; catu(q, (uint32_t)(live_tr > 0 ? live_tr : -live_tr), 0); }
            else cat(tb, "AS WRITTEN");
            SAY(tb, 1000u); head = 1;
        } else if (e && scr == SCR_CURVE) {      /* the point's level */
            curve_t *c = &sc.curve[PK(sel)][PSEL(sel)];
            if (c->n) { c->p[ptsel].val = (uint8_t)clampi((int32_t)c->p[ptsel].val + 2 * e, 0, 127); CHANGED(); redraw = foot = 1; }
        } else if (e && scr == SCR_COMP) {      /* the box's pattern: blank, A-J, X */
            uint8_t *bv = &sc.chain[sc.comp_song % NSONG][ORDER7[crow % NROW]][cbox % NBOX];   /* (crow: the place on the page) */
            *bv = (uint8_t)clampi((int32_t)*bv + e, 0, BOX_X);
            CHANGED(); redraw = 1;
        } else if (e && edit_scr) {             /* VELOCITY: p mp mf f (the step at the cursor, and what you write next) */
            cur_dyn = (uint32_t)clampi((int32_t)cur_dyn + e, 0, 3);
            if (MELODIC(sel)) { int32_t s0 = cover(sel, PSEL(sel), cursor); if (s0 >= 0) MS(sel, (uint32_t)s0).dyn = (uint8_t)((MS(sel, (uint32_t)s0).dyn & ~3u) | cur_dyn); }
            else if (DH(sel, cursor)) DH(sel, cursor) = (uint8_t)(cur_dyn + 1u);
            CHANGED(); redraw = foot = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) {
            if (scr == SCR_CURVE) {             /* the shape of the segment after the point */
                curve_t *c = &sc.curve[PK(sel)][PSEL(sel)];
                if (c->n) { c->p[ptsel].shape = (uint8_t)wrapi((int32_t)c->p[ptsel].shape + e, 4u); CHANGED(); redraw = foot = 1; }
            }
            else if (scr == SCR_HOME) { sc.tempo = (uint8_t)clampi((int32_t)sc.tempo + e, 30, 240); set_tempo(); head = 1; CHANGED(); }
            else if (scr == SCR_COMP) { sc.comp_on = e > 0; CHANGED(); redraw = 1; }   /* on (right) / off (left) */
            else if (scr == SCR_SETUP && MELODIC(sel)) {   /* a part's page: con sordino (the strings, the brass) */
                if (INS[sel].ty != TY_OBOE && INS[sel].ty != TY_BSN) {   /* (the winds: KNOB 4 does nothing) */
                    VIB(sel) = (uint8_t)((VIB(sel) & 0x7Fu) | (e > 0 ? 0x80u : 0u));
                    SAY(e > 0 ? "CON SORDINO" : "SENZA SORDINO", 900u); CHANGED(); redraw = foot = 1;
                }
            }
            else if (scr == SCR_EDIT && MELODIC(sel) && fm1_in.buttons >> BTN_ARP & 1u) {   /* ARP held: the note at the
                                                   cursor a step earlier / later (its length, pitch, articulation kept) */
                int32_t at = cover(sel, PSEL(sel), cursor), from = at, d = e > 0 ? 1 : -1, n = e > 0 ? e : -e;
                arp_used = 1;
                if (at >= 0) {
                    while (n--) {
                        int32_t to = at + d;
                        mstep_t z = {{0, 0, 0}, 0, 0};
                        if (to < 0 || to >= (int32_t)LEN(sel) || has_notes(&MS(sel, (uint32_t)to))) break;
                        MS(sel, (uint32_t)to) = MS(sel, (uint32_t)at); MS(sel, (uint32_t)at) = z;
                        at = to;
                    }
                    if (at != from) { cursor = (uint32_t)at; t_nav = fm1_ms; CHANGED(); redraw = foot = 1; }
                    else SAY(d > 0 ? "CAN'T: A NOTE OR THE END" : "CAN'T: A NOTE OR THE START", 900u);
                }
            }
            else if (edit_scr && !MELODIC(sel)) { sc.vol[sel] = (uint8_t)clampi((int32_t)sc.vol[sel] + e, 0, 20); CHANGED(); foot = 1; }   /* (a drum's
                                                   own level in the kit) */
            else if (edit_scr && cover(sel, PSEL(sel), cursor) >= 0) {   /* NOTE: the notes at the cursor a semitone a click */
                mstep_t *m = &MS(sel, (uint32_t)cover(sel, PSEL(sel), cursor));
                for (k = 0; k < NV; k++) if (m->n[k]) m->n[k] = (uint8_t)clampi((int32_t)m->n[k] + e, 24, 108);
                for (k = 0; k < NV; k++) if (m->n[k]) live(sel, m->n[k], m->dyn, 1);
                live(sel, 0, 0, 0);
                CHANGED(); redraw = foot = 1;
            }
        }

        if (xrec_row != 0xFFu) {                /* a take of expression: a value each step, for one pass of the pattern */
            uint32_t r = xrec_row, c = cur[xrec_ins] % NSTEP, done = 0;
            if (!run || !rec || play_pat[xrec_ins] % NPAT != xrec_pat || scr != SCR_HOME) done = 1;
            else if (c != xrec_last) {
                xrec_last = (uint8_t)c; xbuf[c] = EXPR(r); xrec_mask |= (uint64_t)1 << c;
                if (++xrec_n >= plen(xrec_ins, xrec_pat)) done = 1;
            }
            if (!done && ov_kind == 1u) ov_until = fm1_ms + 1500u;   /* (the card stays while it records) */
            if (done) {
                char *q;
                uint32_t pq = xrec_pat;
                if (xrec_finish()) {
                    q = cat(msgbuf, "EXPRESSION RECORDED: "); *q++ = (char)('A' + pq); *q = 0;
                    SAY(msgbuf, 2000u); CHANGED();
                }
                redraw = head = 1;
            }
        }

        /* ---- while it plays, the editing pages follow: the pattern playing (the Composer moving on), the bar playing
         * (unless you moved lately) */
        if (run && sc.comp_on && (edit_scr || scr == SCR_CURVE) && fm1_ms - t_nav > 1500u && !(chain_rest >> sel & 1u)
            && play_pat[sel] % NPAT != PSEL(sel)) {
            sc.psel[PK(sel)] = (uint8_t)(play_pat[sel] % NPAT);
            cursor = cur[sel] / BAR * BAR;
            if (cursor >= LEN(sel)) cursor = 0;
            ptsel = 0; redraw = foot = head = 1;
        }
        if (run && edit_scr && play_pat[sel] == PSEL(sel) && fm1_ms - t_nav > 1500u && cur[sel] / BAR != cursor / BAR) {
            cursor = cur[sel] / BAR * BAR + cursor % BAR;
            if (cursor >= LEN(sel)) cursor = cur[sel] / BAR * BAR;
            redraw = foot = head = 1;
        }

        /* ---- the keys. Melodic: notes (in the pattern page: a chord, held together, written at the cursor). The
         * drums, a drum machine: the black keys choose (and sound) a drum, the white keys its steps on the setup page.
         * REC on and playing: what you play is recorded into the pattern playing, on the nearest step, as long as
         * you hold it */
        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        {
            int32_t ts = to_step;               /* where in the step the music is (live recording), 0..256 */
            uint32_t sq = (step_q8 >> 8) | 1u;
            frac = run && ts > 0 ? 256u - ((uint32_t)ts / sq > 256u ? 256u : (uint32_t)ts / sq) : 0u;
            gnow = (gstep ? (gstep - 1u) * 256u : 0u) + frac;   /* where the music is, in steps from PLAY (Q8) */
        }
        if (scr == SCR_SONG && song_it == SONG_TAP && down) {   /* tap tempo: any key, in time (the beat the tempo counts) */
            uint32_t now = fm1_ms, gap = now - tap_last;
            down = 0;
            if (!tap_n || gap > 2000u) tap_n = 1;
            else {
                uint32_t m = tap_n - 1u < 3u ? tap_n - 1u : 3u, tot = 0, j;
                tap_iv[0] = tap_iv[1]; tap_iv[1] = tap_iv[2]; tap_iv[2] = gap;
                for (j = 0; j <= m && j < 3u; j++) tot += tap_iv[2u - j];
                tot /= (m + 1u < 3u ? m + 1u : 3u);
                if (tot) {
                    sc.tempo = (uint8_t)clampi((int32_t)((60000u + tot / 2u) / tot), 30, 240);
                    set_tempo(); CHANGED(); redraw = foot = 1;
                }
                tap_n++;
            }
            tap_last = now;
        }
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u && scr != SCR_SAVE && scr != SCR_COMP && scr != SCR_CURVE && !(scr == SCR_MIX && mrow >= MIX_HALL)) {
                uint32_t n = ed_base(sel) + k, live_rec = rec && run;
                if (KEY_LOCK && MELODIC(sel)) n = key_lock(n);   /* (scale lock: the key's notes only) */
                if (MELODIC(sel)) {
                    held[k] = (uint8_t)n; held_by[k] = (uint8_t)sel;
                    live(sel, n, scr == SCR_HOME ? 2u : cur_dyn | (scr == SCR_EDIT ? ARTW(sel) << 2 : 0u), 1);
                    if (live_rec) {             /* recorded where the music is (the nearest step) */
                        uint32_t q = play_pat[sel] % NPAT, len = plen(sel, q), st = cur[sel] + (frac >= 128u ? 1u : 0u), j;
                        mstep_t *m;
                        if (st >= len) st = 0;
                        m = &sc.mel[sel][q][st];
                        if (!(keys & ~(1u << k) & ~down)) { m->n[0] = m->n[1] = m->n[2] = 0; }   /* (a new chord) */
                        for (j = 0; j < NV && m->n[j] && m->n[j] != n; j++) {}
                        m->n[j < NV ? j : NV - 1u] = (uint8_t)n;
                        m->len = 1; m->dyn = (uint8_t)((scr == SCR_HOME ? 2u : cur_dyn) | ARTW(sel) << 2);
                        rec_st[k] = (uint8_t)st; rec_q[k] = (uint8_t)q; rec_g[k] = (gnow + 128u) >> 8;
                        rec_tk[sel][st] = rec_take;  /* (written in this take: a later note of it leaves it be) */
                        CHANGED(); redraw = 1;
                    } else if (scr == SCR_EDIT && !rec && !run) {   /* step entry (stopped, not recording): the keys held
                                                   together are the chord at the cursor */
                        mstep_t *m = &MS(sel, cursor);
                        uint32_t j;
                        if (!chord) { m->n[0] = m->n[1] = m->n[2] = 0; m->len = (uint8_t)nlen; m->dyn = (uint8_t)(cur_dyn | ARTW(sel) << 2); }
                        for (j = 0; j < NV && m->n[j] && m->n[j] != n; j++) {}
                        if (j < NV) m->n[j] = (uint8_t)n;
                        chord |= 1u << k;
                        view_fit(sel, n);
                        CHANGED(); redraw = foot = 1;
                    }
                } else if (bk[k] != 255u) {         /* a black key: a drum */
                    if (bk[k] < NDRUM) {
                        uint32_t d = P_BD + bk[k];
                        live(d, 0, scr == SCR_HOME ? 2u : cur_dyn, 1);
                        if (live_rec) {             /* recorded on the nearest step of the kit's pattern playing */
                            uint32_t q = play_pat[P_BD] % NPAT, st = cur[P_BD] + (frac >= 128u ? 1u : 0u);
                            if (st >= plen(P_BD, q)) st = 0;
                            sc.drum[bk[k]][q][st] = (uint8_t)(cur_dyn + 1u);
                            CHANGED(); redraw = 1;
                        }
                        if (scr == SCR_SETUP) { sel = d; redraw = foot = 1; }
                    }
                } else if (scr == SCR_SETUP && live_rec) {   /* a white key, recording: the chosen drum where the music is */
                    uint32_t q = play_pat[P_BD] % NPAT, st = cur[P_BD] + (frac >= 128u ? 1u : 0u);
                    if (st >= plen(P_BD, q)) st = 0;
                    sc.drum[sel - P_BD][q][st] = (uint8_t)(cur_dyn + 1u);
                    live(sel, 0, cur_dyn, 1);
                    CHANGED(); redraw = 1;
                } else if (scr == SCR_SETUP) {      /* white key w: step w of this page on / off */
                    uint32_t w = 0, j, st;
                    for (j = 0; j < k; j++) w += bk[j] == 255u;
                    st = (cursor / BAR) * BAR + w;
                    if (w < BAR && st < LEN(sel)) {
                        if (DH(sel, st)) DH(sel, st) = 0;
                        else { DH(sel, st) = (uint8_t)(cur_dyn + 1u); live(sel, 0, cur_dyn, 1); }
                        cursor = st; t_nav = fm1_ms;
                        CHANGED(); redraw = foot = 1;
                    }
                } else live(sel, 0, 2u, 1);
            }
            if (up >> k & 1u && held[k]) {          /* let go: the note ends, on the part that played it */
                live(held_by[k], held[k], 0, 0);
                if (rec_st[k] != 255u) {            /* recorded: it ends on the step nearest where it was let go */
                    if (rec_grow(held_by[k], rec_q[k], rec_st[k], rec_g[k], gnow)) redraw = 1;
                    rec_st[k] = 255u;
                }
                held[k] = 0;
            }
        }
        if (scr == SCR_KEY && (down | up)) redraw = 1;   /* (the KEY page: the notes played light) */
        if (!(rec && run))                          /* not recording: a next pattern still waiting is chosen now */
            for (k = 0; k < NROW; k++) if (rec_next[k] < NPAT) { sc.psel[k] = rec_next[k]; rec_next[k] = 0xFFu; redraw = head = 1; }
        if (rec && run)                             /* recording: a held note grows on the screen as it is held */
            for (k = 0; k < 27u; k++)
                if (held[k] && rec_st[k] != 255u && rec_grow(held_by[k], rec_q[k], rec_st[k], rec_g[k], gnow)) redraw = 1;
        if (chord && !(keys & chord)) {             /* the chord let go: the cursor past it */
            cursor = (cursor + nlen) % LEN(sel);
            chord = 0; t_nav = fm1_ms; redraw = foot = 1;
        }

        midi_in();
        batt_poll();
        {                                       /* the battery: LOW BATTERY blinking in the header (1 s on, 1 s off) */
            uint32_t bw = batt_low && !usb_powered() ? 1u + ((fm1_ms / 1000u) & 1u) : 0u;
            if (bw != batt_shown) { if (batt_shown == 2u || bw == 2u) head = 1; batt_shown = bw; }
        }

        /* ---- the LEDs */
        fm1_led_key(BTN_PLAY, run);
        fm1_led_key(BTN_REC, rec || (scr == SCR_SAVE && confirm == 1u));
        fm1_led_key(BTN_SAVE, scr == SCR_SAVE);
        fm1_led_key(BTN_EDIT, scr == SCR_EDIT || scr == SCR_CURVE);
        fm1_led_key(BTN_HOME, scr == SCR_HOME);
        fm1_led_key(BTN_GLO, scr == SCR_MIX || scr == SCR_SONG || scr == SCR_KEY);
        fm1_led_key(BTN_FX, scr == SCR_SETUP && INS[sel].s == 0u);
        fm1_led_key(BTN_SEL, scr == SCR_SETUP && INS[sel].s == 1u);
        fm1_led_key(BTN_ENV, scr == SCR_SETUP && INS[sel].s == 2u);
        fm1_led_key(BTN_LFO, scr == SCR_SETUP && INS[sel].s == 3u);
        {                                       /* ARP: lit while the part is solo or muted (the kit: all muted) */
            uint32_t m = MUTE(sel) || (MELODIC(sel) && SOLO(sel));
            if (scr == SCR_HOME && sel_all) for (k = P_BD, m = 1; k < NINS; k++) m &= MUTE(k);
            fm1_led_key(BTN_ARP, (scr == SCR_HOME || scr == SCR_SETUP) && m);
        }
        fm1_led_key(BTN_SEQ, end_req ? (fm1_ms >> 8 & 1u) : scr == SCR_COMP || scr == SCR_COPY || sc.comp_on);   /* (blinking:
                                                   the song will end) */   /* (SEQ: the Composer, and lit on every
                                                   page while it is on; the copy page) */
        fm1_led_key(BTN_OCT_DOWN, MELODIC(sel) ? sc.tune[sel] < 4u : scr == SCR_SETUP && cursor >= BAR);
        fm1_led_key(BTN_OCT_UP, MELODIC(sel) ? sc.tune[sel] > 4u : scr == SCR_SETUP && cursor / BAR + 1u < (LEN(sel) + BAR - 1u) / BAR);
        {                                       /* the keys' lights: as in Dinghy, the notes that sound (the chosen part's;
                                                   the drums: the keys held); on the drum machine page the black key of the
                                                   drum edited and the white keys where it plays (the step playing blinks) */
            uint32_t base = ed_base(sel), w = 0, page = cursor / BAR;
            for (k = 0; k < 27u; k++) {
                uint32_t on = 0, j;
                if (scr == SCR_SETUP && !MELODIC(sel)) {
                    if (bk[k] != 255u) on = bk[k] < NDRUM && sel == P_BD + bk[k];
                    else {
                        uint32_t st = page * BAR + w;
                        if (w < BAR && st < LEN(sel)) {
                            on = DH(sel, st) != 0u;
                            if (run && play_pat[sel] == PSEL(sel) && cur[sel] == st) on = !on;
                        }
                        w++;
                    }
                } else if (MELODIC(sel)) {
                    for (j = 0; j < NV; j++)
                        if ((mv[sel][j].stage == 1u || mv[sel][j].stage == 2u) && mv[sel][j].note == (KEY_LOCK ? key_lock(base + k) : base + k)) on = 1;
                } else on = keys >> k & 1u;
                fm1_led_key(KEY_LED(k), on);
                fm1_led_key_glow(KEY_LED(k), KEY_SHOW && MELODIC(sel) && in_key(base + k));   /* (visualise: the key's notes) */
            }
        }

        /* ---- the screen: what changed, at most ~30 times a second */
        flash = lit_mask();
        play_now = run ? gstep : ~0u;
        if (scr == SCR_HOME) { bars = bars_all(); bar = run ? (gstep ? (gstep - 1u) / BAR % bars : 0u) + 1u : 1u; }
        else { bars = (LEN(sel) + BAR - 1u) / BAR; bar = cursor / BAR + 1u; }
        if ((bar | bars << 8) != drawn_bar && scr != SCR_SAVE && scr != SCR_COMP) { drawn_bar = bar | bars << 8; head = 1; }
        if (msg && (int32_t)(fm1_ms - msg_until) > 0) { msg = 0; foot = 1; head = 1; }
        idle_ok = 1;
        if (head && scr != SCR_SAVE && scr != SCR_COMP) {
            char t[24];
            head = 0;
            if (batt_shown == 2u && !msg) {     /* the battery: LOW BATTERY (blinking) */
                head_begin();
                cv_frame(12, 8, 22, 12, C_WINE); cv_rect(34, 11, 3, 6, C_WINE); cv_rect(14, 10, 4, 8, C_WINE);   /* (an empty battery) */
                ctext(44, 5, &SERIF_M, "LOW BATTERY", C_WINE);
                cv_blit(0, 0);
            }
            else if (scr == SCR_HOME) draw_status(sel, run, rec, msg);
            else if (scr == SCR_MIX) { title_right = ""; draw_title("MIX", 0, run, rec, 0, 0); }
            else if (scr == SCR_COPY) { title_right = ""; draw_title("COPY INTO", (char)('A' + PSEL(sel)), run, rec, 0, 0); }
            else if (scr == SCR_SONG) { title_right = ""; draw_title("PIECE", 0, run, rec, 0, 0);   /* (the piece page: its time signature, its performance) */ }
            else if (scr == SCR_KEY) { title_right = ""; draw_title("KEY", 0, run, rec, 0, 0); }
            else if (scr == SCR_CURVE) {
                const curve_t *c = &sc.curve[PK(sel)][PSEL(sel)];
                title_right = c->on && c->n ? "ON" : "OFF";
                cat(t, MELODIC(sel) ? ROWNAME[sel] : "PERCUSSION");   /* (STRINGS HIGH ...: fits beside the bar) */
                draw_title(t, (char)('A' + PSEL(sel)), run, rec, 0, 0);
            }
            else {
                cat(t, MELODIC(sel) ? ROWNAME[sel] : SEC_NAME[INS[sel].s]);
                if (scr == SCR_EDIT && music) { bar = cursor / (2u * BAR) + 1u; bars = (LEN(sel) + 2u * BAR - 1u) / (2u * BAR); }
                title_right = scr == SCR_SETUP && MELODIC(sel) ? "" : "MUSIC";
                if (scr == SCR_SETUP && MELODIC(sel)) bar = 0;   /* (a part's page: no bars to show) */
                draw_title(t, (char)('A' + PSEL(sel)), run, rec, bar, bars);
            }
        }
        if (fm1_ms - t_frame >= 33u) {
            if (clear_ask) {                    /* REC held: the big question, over whatever page */
                if (!ask_shown) { t_frame = fm1_ms; draw_ask(scr == SCR_HOME ? HOME_H : BODY_H); ask_shown = 1; }
            } else if (scr == SCR_SAVE && (redraw || head)) {
                t_frame = fm1_ms; redraw = head = 0;
                draw_save(slot, confirm);
            } else if (scr == SCR_COMP && (redraw || head || play_now != drawn_play)) {
                t_frame = fm1_ms; redraw = head = 0; drawn_play = play_now;
                draw_composer(crow, cbox, run);
            } else if (scr == SCR_HOME && ov_until && (int32_t)(ov_until - fm1_ms) > 0) {   /* the card (KNOB 1 / 2) */
                if (redraw || play_now != drawn_play) {
                    t_frame = fm1_ms; redraw = 0; drawn_play = play_now;
                    draw_overlay(PK(sel), ov_kind);
                    foot = 1;
                }
            } else if (scr == SCR_HOME && (redraw || ov_until || flash != drawn_flash || play_now != drawn_play)) {
                if (ov_until) { ov_until = 0; redraw = 1; chart_k1 = ~0u; }   /* (the card gone: the seats again) */
                drawn_play = play_now;
                t_frame = fm1_ms; drawn_flash = flash;
                draw_orchestra(sel, flash, redraw);
                redraw = 0;
            } else if (scr == SCR_KEY && redraw) {
                uint32_t pm = 0;
                t_frame = fm1_ms; redraw = 0;
                for (k = 0; k < 27u; k++) if (held[k]) pm |= 1u << (held[k] % 12u);
                draw_key(pm);
            } else if (scr == SCR_SONG && redraw) {
                t_frame = fm1_ms; redraw = 0;
                draw_song(song_it, ts_ask);
            } else if (scr == SCR_COPY && redraw) {
                t_frame = fm1_ms; redraw = 0;
                draw_copy(cp_row, cp_pat, PK(sel), PSEL(sel), cp_ask);
            } else if (scr != SCR_HOME && scr != SCR_SAVE && scr != SCR_COMP && scr != SCR_COPY && scr != SCR_SONG && scr != SCR_KEY && (redraw || play_now != drawn_play || (scr == SCR_CURVE && head))) {
                t_frame = fm1_ms; redraw = 0; drawn_play = play_now;
                if (scr == SCR_EDIT && music) draw_music(sel, cursor, run);
                else if (scr == SCR_EDIT) draw_roll(sel, cursor, run);
                else if (scr == SCR_MIX) draw_mix(mrow);
                else if (scr == SCR_CURVE) draw_curve(sel, ptsel, run);
                else draw_setup(sel, cursor, run);
            }
        }
        if (foot) {
            foot = 0;
            if (scr == SCR_HOME) {}             /* (the orchestra page: no footer; its header says it) */
            else if (scr == SCR_SAVE) draw_foot_save(msg);
            else if (scr == SCR_COMP) draw_foot_composer(cbox, msg);
            else if (scr == SCR_SETUP) draw_foot_setup(sel, cursor, cur_dyn, msg);
            else if (scr == SCR_MIX) draw_foot_mix(mrow, msg);
            else if (scr == SCR_KEY) {
                foot_begin();
                if (!foot_msg(msg)) ctext_c(120, 8, &SERIF_S, KEYSIG != key_from ? "REC: move the music to this key"
                                            : "K1 lock  K2 lights  SELECT key", C_INK2);
                cv_blit(0, FOOT_Y);
            }
            else if (scr == SCR_SONG) {
                foot_begin();
                if (!foot_msg(msg)) ctext_c(120, 8, &SERIF_S, song_it < SONG_SWING ? "SELECT choose  REC confirm  GLO next"
                                            : song_it == SONG_TAP ? "tap any key in time" : "K1 amount  GLO next", C_INK2);
                cv_blit(0, FOOT_Y);
            }
            else if (scr == SCR_COPY) { foot_begin(); if (!foot_msg(msg)) ctext_c(120, 8, &SERIF_S, "SELECT part  ALG pattern  SEQ copy", C_INK2); cv_blit(0, FOOT_Y); }
            else if (scr == SCR_CURVE) draw_foot_curve(sel, ptsel, msg);
            else draw_foot_roll(sel, cursor, cur_dyn, nlen, msg);
        }
        idle_ok = 0;

        /* ---- keep the work: 2 s after a change, when stopped and quiet (an erase stalls the sound) */
        if (dirty && !run && !proj_go && quiet() && fm1_ms - t_change > 2000u) {
            slot_put(WORK_SLOT, &sc);
            dirty = 0;
        }
    }
}
