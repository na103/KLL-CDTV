#!/usr/bin/env python3
"""Interface geometry: the single source for the tools and for the program.

The coordinates are those of the original screens, 640x480; the CDTV screen
is 320x256, so x/2 and y/2 shifted down by TOP lines (the 240 useful lines
centred on 256).

  layout.py header OUT.h     write the constants for the C code

The cutouts of the highlighted buttons come from the HOLH.BMP and MAINH.BMP
sheets of the original CD, which collect the "lit" versions of the commands.
"""
import argparse

SCREEN_W, SCREEN_H = 320, 256
TOP = (SCREEN_H - 240) // 2


def sx(v):
    return v // 2


def sy(v):
    return v // 2 + TOP


def rect(x, y, w, h):
    """A 640x480 rectangle -> 320x256."""
    return (sx(x), sy(y), (w + 1) // 2, (h + 1) // 2)


# ------------------------------------------------------------------ screens

# areas of the HOL screen (measured on the black rectangles of HOLB.BMP)
HOL_IMG = rect(202, 104, 236, 176)	# video clip / picture
HOL_KTEXT = rect(48, 111, 147, 128)	# pannello di sinistra, klingon
HOL_ETEXT = rect(445, 109, 151, 130)	# pannello di destra, inglese
# the category name goes under the CATEGORY command, in black on the orange
# panel (LabCat in the Mac port, 93,296,75,20): the rectangle is a little
# wider than the original because "Commands" would not fit
HOL_CATNAME = rect(76, 296, 114, 20)
# the CD writes nothing in the two black boxes at the bottom; we do want to
# know where we are in the list, which is not obvious with the remote
HOL_POS = rect(323, 378, 113, 24)

# hot spots (the same rectangles as the controls of the Mac port)
HOT = {
    'HOL_CATEGORY': rect(79, 279, 91, 18),
    'HOL_ILLU': rect(203, 290, 104, 13),
    'HOL_TUTOR': rect(348, 289, 62, 17),
    'HOL_MORE': rect(488, 279, 59, 21),
    'HOL_NEXT': rect(458, 352, 42, 47),
    'HOL_PREV': rect(145, 351, 44, 43),
    'HOL_DRILL': rect(173, 73, 50, 19),
    'HOL_PHONE': rect(395, 73, 81, 19),
    'PRON_PHON': rect(110, 350, 88, 64),
    'PRON_MORE': rect(200, 410, 240, 34),
    'PRON_EXAM': rect(441, 350, 90, 61),
    'PRON_HOL': rect(88, 62, 43, 20),
    'PRON_DRILL': rect(173, 71, 51, 21),
    'HELP': rect(496, 60, 64, 24),
    'EXIT': rect(294, 29, 52, 46),
    'CREDITS': rect(5, 405, 76, 27),
}

# rows of the main menu: the two columns are slanted, and the row follows
# from t = Y - slope * (X - Xref), in 640x480 coordinates
MENU = {
    'L': dict(x0=40, x1=206, xref=45, slope=385, y0=81, step=30, first=1),
    'R': dict(x0=438, x1=604, xref=445, slope=-400, y0=138, step=30, first=5),
}

# mouse pointer sprite colours: Intuition rewrites them whenever it likes, so
# neither the clips nor the pictures may use them (same values in src/main.c)
POINTER_COLORS = {17: 0xF80, 18: 0x000, 19: 0xFD8}

# interface sound effects, from the WAV directory of the CD
SOUNDS = ['RECON01', 'RECON02', 'RECON05', 'COMP1', 'COMP2', 'COMP4',
          'CHIRP', 'MUSIC01', 'MUSIC', 'RIGHT01', 'WRONG01']

# the category name written under CATEGORY: these are the menu entries, with
# "Commands" shortened because the panel narrows there and it would not fit
CATEGORIES = [
    ('USEFUL', 'Phrases'),
    ('MYTHS', 'Myths'),
    ('CURSING', 'Curses'),
    ('BATTLE', 'Hol'),
    ('COMMAND', 'Comm'),
    ('FOOD', 'Food'),
    ('WEAPON', 'Weapons'),
    ('RITUAL', 'Rituals'),
]

# ----------------------------------------------------------------- cutouts

# background screens the cutouts land on (the converter needs them to halve
# each cutout together with what surrounds it)
HOL_BG = 'HOLB'
MAIN_BG = 'MAINB'

# name: (sheet, cutout in 640x480, destination in 640x480)
HOL_CROPS = [
    # the background already has HOL lit: this is the help-mode bar, with
    # HELP lit next to it
    ('BAR_HELP', 'HOLH', (4, 158, 595, 74), (28, 28)),
    ('BAR_DRILL', 'HOLH', (4, 3, 595, 75), (28, 28)),
    ('BAR_PHONE', 'HOLH', (4, 81, 595, 74), (28, 28)),
    ('CATEGORY', 'HOLH', (7, 289, 91, 17), (79, 279)),
    ('ILLU', 'HOLH', (6, 313, 104, 13), (203, 290)),
    ('TUTOR', 'HOLH', (7, 335, 62, 17), (348, 289)),
    ('MORE', 'HOLH', (134, 391, 59, 21), (488, 279)),
    ('COVER', 'HOLH', (209, 401, 59, 21), (488, 279)),
    ('PREV', 'HOLH', (75, 335, 44, 46), (145, 351)),
    ('NEXT', 'HOLH', (128, 335, 38, 46), (458, 352)),
]

# menu panels with one entry lit, one per category (separate files)
MAIN_CROPS = [
    ('MH1', 'MAINH', (546, 179, 168, 187), (39, 78)),	# Phrases
    ('MH2', 'MAINH', (371, 5, 164, 186), (39, 78)),	# Myths
    ('MH3', 'MAINH', (194, 5, 164, 187), (39, 78)),	# Cursing
    ('MH4', 'MAINH', (13, 4, 164, 186), (39, 78)),	# Hol
    ('MH5', 'MAINH', (544, 5, 150, 167), (437, 102)),	# Commands
    ('MH6', 'MAINH', (372, 202, 167, 164), (437, 101)),	# Food
    ('MH7', 'MAINH', (194, 201, 169, 163), (437, 102)),	# Weapons
    ('MH8', 'MAINH', (14, 201, 167, 161), (437, 102)),	# Rituals
]

# ---------------------------------------------------- pronunciation (PRONUN)

# the clip rectangle (the same as on the HOL screen, so the clips fit exactly)
# and the panel of the example text
PRON_IMG = HOL_IMG
PRON_TEXT = rect(212, 292, 217, 95)

# the 34 phonemes: audio file, example clip, audio of the "more" commentary.
# The example text lives in TXTFILES/P<nn>.TXT (P34 reuses the one of P32,
# as in the Mac port).
PHONEMES = [
    ('P01', 'B002', 'P001T'),
    ('P02', 'R005', 'P002T'),
    ('P03', 'C007', 'P003T'),
    ('P04', 'U002', 'P004T'),
    ('P05', 'B001', 'P005T'),
    ('P06', 'B006', 'P006T'),
    ('P07', 'R004', 'P007T'),
    ('P08', 'M012', 'P008T'),
    ('P09', 'CU005', 'P009T'),
    ('P10', 'U003', 'P010T'),
    ('P11', 'W014', 'P011T'),
    ('P12', 'M001', 'P012T'),
    ('P13', 'U004', 'P013T'),
    ('P14', 'U012', 'P014T'),
    ('P15', 'W010', 'P015T'),
    ('P16', 'FD002', 'P016T'),
    ('P17', 'U003', 'P017T'),
    ('P18', 'CU002', 'P018T'),
    ('P19', 'FD015', 'P019T'),
    ('P20', 'FD005', 'P020T'),
    ('P21', 'C007', 'P021T'),
    ('P22', 'CU002', 'P022T'),
    ('P23', 'U010', 'P023T'),
    ('P24', 'B001', 'P024T'),
    ('P25', 'W009', 'P025T'),
    ('P26', '105', 'P026T'),
    ('P27', 'R005', 'P027T'),
    ('P28', '106', 'P028T'),
    ('P29', 'M012', 'P029T'),
    ('P30', 'R013', 'P030T'),
    ('P31', 'B003', 'P031T'),
    ('P32', 'R009', 'P032T'),
    ('P33', 'B004', 'P033T'),
    ('P34', 'R009', 'APOST'),]

PHON_TXT = {34: 'P32'}

# cutout in the sheet (x, y, width, height) and destination on screen: the
# width and height stop short of the white that separates the cells of the
# sheet, otherwise the lit button drags it along
PRON_BTN = [
    ((6, 4, 55, 21), (83, 102, 55, 21)),
    ((77, 4, 52, 20), (76, 123, 55, 20)),
    ((6, 30, 52, 20), (68, 143, 55, 20)),
    ((77, 29, 53, 20), (59, 163, 55, 20)),
    ((6, 54, 55, 20), (49, 184, 55, 20)),
    ((77, 54, 55, 20), (44, 204, 55, 20)),
    ((6, 77, 50, 20), (40, 224, 55, 20)),
    ((6, 101, 51, 20), (147, 103, 53, 20)),
    ((77, 79, 53, 20), (137, 124, 53, 20)),
    ((6, 125, 53, 20), (130, 143, 53, 20)),
    ((77, 103, 53, 20), (123, 163, 53, 20)),
    ((6, 151, 53, 20), (114, 184, 53, 20)),
    ((77, 128, 53, 20), (108, 204, 53, 20)),
    ((6, 176, 53, 21), (99, 223, 53, 21)),
    ((77, 152, 52, 20), (91, 245, 53, 20)),
    ((6, 202, 53, 21), (80, 264, 53, 21)),
    ((77, 175, 53, 20), (74, 286, 53, 21)),
    ((6, 229, 53, 20), (448, 124, 53, 20)),
    ((77, 199, 53, 19), (456, 144, 53, 19)),
    ((6, 257, 53, 19), (464, 164, 53, 19)),
    ((6, 281, 53, 19), (478, 204, 53, 19)),
    ((77, 224, 53, 20), (486, 224, 53, 20)),
    ((6, 305, 53, 20), (494, 244, 53, 20)),
    ((77, 250, 53, 20), (504, 265, 53, 20)),
    ((6, 331, 52, 20), (510, 286, 53, 20)),
    ((6, 355, 53, 20), (509, 124, 53, 20)),
    ((77, 275, 53, 20), (518, 144, 53, 20)),
    ((6, 379, 53, 20), (525, 164, 53, 20)),
    ((77, 301, 52, 20), (536, 184, 53, 20)),
    ((6, 402, 53, 20), (542, 204, 53, 20)),
    ((77, 326, 53, 20), (550, 224, 53, 20)),
    ((77, 377, 51, 19), (441, 104, 53, 19)),
    ((77, 401, 51, 19), (504, 104, 53, 19)),
    ((77, 351, 51, 19), (473, 184, 53, 19)),
]

# commands and bar of the screen: the three bars in the sheet have lit the
# section about to be opened, or HELP for help mode
PRON_CROPS = [
    ('BAR_PRON_HELP', 'PRONUNH', (170, 366, 595, 74), (28, 28)),
    ('BAR_PRON_HOL', 'PRONUNH', (170, 204, 595, 74), (28, 28)),
    ('BAR_PRON_DRILL', 'PRONUNH', (170, 286, 595, 74), (28, 28)),
    ('PHON', 'PRONUNH', (206, 7, 88, 64), (110, 350)),
    ('PMORE', 'PRONUNH', (137, 77, 240, 34), (200, 410)),
    ('EXAM', 'PRONUNH', (206, 117, 90, 61), (441, 350)),
]

PRON_BG = 'PRONUNB'
PRON_OFF = ['BAR_PRON_HELP', 'PHON', 'PMORE', 'EXAM']


def pron_atlas():
    """Cutouts of the pronunciation screen, collected into a single image."""
    tiles = []
    for name, sheet, (cx, cy, cw, ch), (dx, dy) in PRON_CROPS:
        tiles.append(dict(name=name, sheet=sheet, bg=PRON_BG, crop=(cx, cy, cw, ch),
                          full_dest=(dx, dy), dest=(sx(dx), sy(dy)),
                          size=((cw + 1) // 2, (ch + 1) // 2)))
        if name in PRON_OFF:
            tiles.append(dict(name='OFF_' + name, sheet=PRON_BG, bg=PRON_BG,
                              crop=(dx, dy, cw, ch), full_dest=(dx, dy),
                              dest=(sx(dx), sy(dy)), size=((cw + 1) // 2, (ch + 1) // 2)))
    for i, ((cx, cy, cw, ch), (dx, dy, dw, dh)) in enumerate(PRON_BTN, 1):
        size = ((cw + 1) // 2, (ch + 1) // 2)
        tiles.append(dict(name='PB%02d' % i, sheet='PRONUNH', bg=PRON_BG,
                          crop=(cx, cy, cw, ch), full_dest=(dx, dy),
                          dest=(sx(dx), sy(dy)), size=size))
        tiles.append(dict(name='OFF_PB%02d' % i, sheet=PRON_BG, bg=PRON_BG,
                          crop=(dx, dy, cw, ch), full_dest=(dx, dy),
                          dest=(sx(dx), sy(dy)), size=size))
    pos, height = shelf_pack([t['size'] for t in tiles])
    for t, p in zip(tiles, pos):
        t['at'] = p
    return tiles, height

# -------------------------------------------------------------- drills (DRILL)

# Every category has its own screens, in the category directory: the "naked"
# one and one for each of the three drill kinds of the original CD.
#   1  read the Klingon and pick one of four translations
#   2  read the English, listen to four entries and confirm with CHOOSE
#   3  listen to the Klingon and pick one of four translations
DRILL_SCREENS = [('N', 'NAKED'), ('1', 'DRILL02'), ('2', 'DRILL01'), ('3', 'DRILL08')]
DRILL_QUESTIONS = 11	# questions before the score

# kinds 1 and 2: four boxes side by side, the question below
DRILL_BOX = [rect(35, 136, 135, 131), rect(178, 136, 137, 131),
             rect(323, 136, 136, 131), rect(467, 136, 140, 131)]
DRILL_TEXT = [rect(47, 151, 116, 102), rect(189, 151, 114, 102),
              rect(336, 151, 112, 102), rect(485, 151, 109, 102)]
DRILL_Q = rect(210, 280, 220, 59)

# kind 3: two boxes above and two below, with the REPEAT command between
DRILL3_BOX = [rect(162, 113, 158, 71), rect(320, 113, 158, 71),
              rect(142, 185, 178, 91), rect(320, 185, 178, 91)]
DRILL3_TEXT = [rect(174, 122, 134, 54), rect(332, 122, 134, 54),
               rect(174, 199, 134, 65), rect(332, 199, 134, 65)]
DRILL3_REPEAT = rect(269, 275, 102, 18)
DRILL_CHOOSE = rect(200, 352, 240, 26)
# the hint takes the whole band under the boxes, wider than the question's:
# the texts of the CD would not fit otherwise
DRILL_HINT = rect(120, 280, 400, 70)
DRILL_HINTS = ['D03HLP', 'D06HLP', 'D08HLP']	# one per drill kind
# the same band serves help mode: it is already clear of the boxes and is
# restored by copying it back from the screen held in memory

# -------------------------------------------------------------------- help

# Pressing HELP makes the CD write in the panel how the help works, then
# describe the command that is touched. The texts are in TXTFILES, one per
# command.
HELP_TEXTS = [
    ('INTRO', 'HELP'),		# "Move the mouse to a button and click..."
    ('EXIT', 'EXIT'),
    ('HOL', 'HOL'),		# the two bar entries, used in the drills
    ('PHONEME', 'PRONUN'),
    ('HOL_CATEGORY', 'CHOLHLP'),
    ('HOL_ILLU', 'IHOLHLP'),
    ('HOL_TUTOR', 'THOLHLP'),
    ('HOL_MORE', 'MHOLHLP'),
    ('HOL_NEXT', 'FHOLHLP'),
    ('HOL_PREV', 'BHOLHLP'),
    ('HOL_DRILL', 'DHOLHLP'),
    ('HOL_PHONE', 'PHOLHLP'),
    ('PRON_PHON', 'PPROHLP'),
    ('PRON_MORE', 'MPROHLP'),
    ('PRON_EXAM', 'EPROHLP'),
    ('PRON_HOL', 'HPROHLP'),
    ('PRON_DRILL', 'DPROHLP'),
    ('PRON_BTN', '1PROHLP'),	# the 34 phoneme buttons
]


# shared: counters, "right"/"wrong" boxes and the score meter
DRILL_WRONG_N = rect(165, 380, 29, 20)
DRILL_RIGHT_N = rect(446, 381, 29, 20)
DRILL_METER = rect(210, 407, 220, 20)

# the score screen
SCORE_TEXT = rect(211, 298, 217, 77)
SCORE_WRONG_N = rect(163, 381, 29, 20)
SCORE_CAT = rect(277, 418, 85, 11)	# back to the category menu
SCORE_RIGHT_N = rect(448, 381, 29, 20)

# the commander's clip, depending on how it went (0 to 11 right answers)
SCORE_CLIPS = [(1, 'CU009'), (5, 'CU003'), (10, 'CU011'), (11, 'U004')]

# lit cutouts of the drills, from the DRILL01H sheet of the category
DRILL_CROPS = [
    # the lit boxes live in a band of the sheet 131 rows tall: taken any
    # taller (as the Mac port does) they drag in the white underneath
    ('DBOX1', 'DRILL01H', (3, 268, 138, 131), (35, 136)),
    ('DBOX2', 'DRILL01H', (146, 268, 138, 131), (178, 136)),
    # in the sheet the third and fourth sit 2 and 3 pixels further right
    # than the Mac port says (checked on every category); the fourth stops
    # at 138 because beyond that the white of the sheet begins
    ('DBOX3', 'DRILL01H', (291, 268, 137, 131), (323, 136)),
    ('DBOX4', 'DRILL01H', (435, 268, 138, 131), (467, 136)),
    ('DCHOOSE', 'DRILL01H', (95, 451, 240, 26), (200, 352)),
    ('DWRONG', 'DRILL01H', (3, 423, 85, 26), (113, 352)),
    ('DRIGHT', 'DRILL01H', (3, 451, 84, 26), (442, 352)),
    # help-mode bar: DRILL stays lit and HELP is added. In the drill sheets
    # the bars are not where they are in the other sheets: two rows lower (at
    # 158 there is still the white between cutouts) and one pixel further
    # left. With this cutout the bar matches the one of the background screen
    # exactly (zero difference on seven categories out of eight)
    ('BAR_DHELP', 'DRILL01H', (3, 160, 595, 74), (28, 28)),
]

DRILL_BG = 'DRILL01'
DRILL_OFF = ['DBOX1', 'DBOX2', 'DBOX3', 'DBOX4', 'DCHOOSE']


def drill_atlas():
    """Lit cutouts of a drill; the sheets are the ones of the category."""
    tiles = []
    for name, sheet, (cx, cy, cw, ch), (dx, dy) in DRILL_CROPS:
        tiles.append(dict(name=name, sheet=sheet, bg=DRILL_BG, crop=(cx, cy, cw, ch),
                          full_dest=(dx, dy), dest=(sx(dx), sy(dy)),
                          size=((cw + 1) // 2, (ch + 1) // 2)))
        if name in DRILL_OFF:
            tiles.append(dict(name='OFF_' + name, sheet=DRILL_BG, bg=DRILL_BG,
                              crop=(dx, dy, cw, ch), full_dest=(dx, dy),
                              dest=(sx(dx), sy(dy)), size=((cw + 1) // 2, (ch + 1) // 2)))
    pos, height = shelf_pack([t['size'] for t in tiles])
    for t, p in zip(tiles, pos):
        t['at'] = p
    return tiles, height



def shelf_pack(sizes, width=SCREEN_W):
    """Lay the cutouts out in successive rows; returns positions and height."""
    pos = []
    x = y = row_h = 0
    for w, h in sizes:
        if x + w > width:
            x, y, row_h = 0, y + row_h, 0
        pos.append((x, y))
        x += w
        row_h = max(row_h, h)
    return pos, y + row_h


# commands that switch off: the "unlit" cutout is the same rectangle taken
# from the background screen, so clearing the highlight is one more copy
HOL_OFF = ['BAR_HELP', 'CATEGORY', 'ILLU', 'TUTOR', 'MORE', 'PREV', 'NEXT']


def hol_atlas():
    """Cutouts of the HOL screen, collected into a single image."""
    tiles = []
    for name, sheet, (cx, cy, cw, ch), (dx, dy) in HOL_CROPS:
        # full_dest lets the converter halve the sheet with the same parity
        # as the background screen (see convimg.crop_tile)
        tiles.append(dict(name=name, sheet=sheet, bg=HOL_BG, crop=(cx, cy, cw, ch),
                          full_dest=(dx, dy), dest=(sx(dx), sy(dy)),
                          size=((cw + 1) // 2, (ch + 1) // 2)))
        if name in HOL_OFF:
            tiles.append(dict(name='OFF_' + name, sheet=HOL_BG, bg=HOL_BG,
                              crop=(dx, dy, cw, ch), full_dest=(dx, dy),
                              dest=(sx(dx), sy(dy)),
                              size=((cw + 1) // 2, (ch + 1) // 2)))
    pos, height = shelf_pack([t['size'] for t in tiles])
    for t, p in zip(tiles, pos):
        t['at'] = p
    return tiles, height


# ------------------------------------------------------------------- header

def tiles_header(out, tiles, atlas_h, name, comment):
    out.append(comment)
    for t in tiles:
        ax, ay = t['at']
        w, h = t['size']
        dx, dy = t['dest']
        out.append(f'#define B_{t["name"]}\t{{ {ax}, {ay}, {w}, {h}, {dx}, {dy} }}')
    out.append('')
    out.append(f'#define {name}\t{atlas_h}')
    out.append('')


def cmd_header(args):
    tiles, atlas_h = hol_atlas()
    ptiles, patlas_h = pron_atlas()
    out = ['/* generated by tools/layout.py: do not edit */',
           '#ifndef KLL_LAYOUT_H', '#define KLL_LAYOUT_H', '',
           f'#define SCREEN_W\t{SCREEN_W}',
           f'#define SCREEN_H\t{SCREEN_H}',
           f'#define SCREEN_TOP\t{TOP}', '']
    out.append('/* rectangle: x, y, width, height */')
    for name, r in (('R_HOL_IMG', HOL_IMG), ('R_HOL_KTEXT', HOL_KTEXT),
                    ('R_HOL_ETEXT', HOL_ETEXT), ('R_HOL_CATNAME', HOL_CATNAME),
                    ('R_HOL_POS', HOL_POS), ('R_PRON_IMG', PRON_IMG),
                    ('R_PRON_TEXT', PRON_TEXT)):
        out.append(f'#define {name}\t{{ {r[0]}, {r[1]}, {r[2]}, {r[3]} }}')
    out.append('')
    for name, r in sorted(HOT.items()):
        out.append(f'#define R_{name}\t{{ {r[0]}, {r[1]}, {r[2]}, {r[3]} }}')
    out.append('')
    out.append('/* menu rows: slanted columns, in 640x480 coordinates */')
    for side, m in MENU.items():
        for k, v in m.items():
            out.append(f'#define MENU_{side}_{k.upper()}\t{v}')
    out.append('')
    tiles_header(out, tiles, atlas_h, 'ATLAS_H',
                 "/* copies from the DATA/HOLBTN.PIC atlas: sx, sy, w, h, dx, dy */")
    tiles_header(out, ptiles, patlas_h, 'ATLAS_PRON_H',
                 "/* copies from the DATA/PRONBTN.PIC atlas */")

    dtiles, datlas_h = drill_atlas()
    tiles_header(out, dtiles, datlas_h, 'ATLAS_DRILL_H',
                 "/* copies from the DRILL/D<n>H.PIC atlas */")

    out.append('/* drills: answer boxes, the text inside them, the question */')
    for name, rects in (('DRILL_BOX', DRILL_BOX), ('DRILL_TEXT', DRILL_TEXT),
                        ('DRILL3_BOX', DRILL3_BOX), ('DRILL3_TEXT', DRILL3_TEXT)):
        vals = ', '.join('{ %d, %d, %d, %d }' % r for r in rects)
        out.append(f'#define {name}\t{{ {vals} }}')
    for name, r in (('R_DRILL_Q', DRILL_Q), ('R_DRILL3_REPEAT', DRILL3_REPEAT),
                    ('R_DRILL_CHOOSE', DRILL_CHOOSE), ('R_DRILL_HINT', DRILL_HINT),
                    ('R_DRILL_WRONG_N', DRILL_WRONG_N), ('R_DRILL_RIGHT_N', DRILL_RIGHT_N),
                    ('R_DRILL_METER', DRILL_METER), ('R_SCORE_TEXT', SCORE_TEXT),
                    ('R_SCORE_WRONG_N', SCORE_WRONG_N), ('R_SCORE_RIGHT_N', SCORE_RIGHT_N),
                    ('R_SCORE_CAT', SCORE_CAT)):
        out.append(f'#define {name}\t{{ {r[0]}, {r[1]}, {r[2]}, {r[3]} }}')
    out.append(f'#define DRILL_ANSWERS\t{len(DRILL_BOX)}')
    out.append(f'#define DRILL_QUESTIONS\t{DRILL_QUESTIONS}')
    clips = ', '.join('{ %d, "%s" }' % c for c in SCORE_CLIPS)
    out.append(f'#define SCORE_CLIPS\t{{ {clips} }}')
    out.append(f'#define NSCORE_CLIPS\t{len(SCORE_CLIPS)}')
    out.append('')

    out.append('/* the 34 phonemes: hot spot, lit cutout, unlit cutout */')
    out.append(f'#define NPHONEMES\t{len(PRON_BTN)}')
    hot = ', '.join('{ %d, %d, %d, %d }' % (sx(d[0]), sy(d[1]), (d[2] + 1) // 2, (d[3] + 1) // 2)
                    for _, d in PRON_BTN)
    out.append(f'#define PHON_HOT\t{{ {hot} }}')
    for on in (True, False):
        names = [('PB%02d' if on else 'OFF_PB%02d') % i for i in range(1, len(PRON_BTN) + 1)]
        vals = ', '.join('B_' + nm for nm in names)
        out.append(f'#define PHON_{"ON" if on else "OFF"}\t{{ {vals} }}')
    out.append('')
    out.append('/* lit menu panels, one per category: x, y */')
    pos = ', '.join('{ %d, %d }' % (sx(d[0]), sy(d[1])) for _, _, _, d in MAIN_CROPS)
    out.append(f'#define MAIN_OVL_POS\t{{ {pos} }}')
    out.append('')
    out.append('/* help texts, in the order of the HELP category of the DB */')
    for i, (name, _) in enumerate(HELP_TEXTS):
        out.append(f'#define HLP_{name}\t{i}')
    out.append('')
    out.append('/* colours reserved for the pointer sprite */')
    ptr = ', '.join('{ %d, 0x%03X }' % (k, v) for k, v in sorted(POINTER_COLORS.items()))
    out.append(f'#define POINTER_COLORS\t{{ {ptr} }}')
    out.append(f'#define NPOINTER_COLORS\t{len(POINTER_COLORS)}')
    out.append('')
    out.append(f'#define NCATEGORIES\t{len(CATEGORIES)}')
    out.append('')
    out.append('#endif')
    with open(args.out, 'w') as f:
        f.write('\n'.join(out) + '\n')
    print(f'{args.out}: {len(tiles)} cutouts, atlas {atlas_h} tall')


def reserve_args():
    return [f'--reserve {k}={v:03X}' for k, v in sorted(POINTER_COLORS.items())]


def cmd_reserve(args):
    print(' '.join(reserve_args()))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('header', help='write the constants for the C code')
    p.add_argument('out')
    p.set_defaults(func=cmd_header)

    p = sub.add_parser('reserve', help='--reserve options for the converters')
    p.set_defaults(func=cmd_reserve)
    args = ap.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
