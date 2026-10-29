/**
 * \file borg-think.c
 * \brief The entry point for the borg thinking about what to do
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007-9 Andi Sidwell, Chris Carr, Ed Graham, Erik Osheim
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband License":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "borg-think.h"

#ifdef ALLOW_BORG

#include "../player-util.h"
#include "../ui-game.h"

#include "borg.h"
#include "borg-io.h"
#include "borg-magic.h"
#include "borg-power.h"
#include "borg-think-dungeon.h"
#include "borg-think-store.h"
#include "borg-trait.h"
#include "borg-update.h"

/*
 * Current shop index
 */
int16_t shop_num = -1;

/*
 * Strategy flags -- examine the world
 */
bool    borg_do_spell     = true; /* Acquire "spell" info */

/*
 * Abort the Borg, noting the reason
 */
void borg_oops(const char *what)
{
    /* Stop processing */
    borg.status.active = false;

    /* Give a warning */
    borg_note(format("# Aborting (%s).", what));

    /* Forget borg keys */
    borg_flush();
}

/*
 * Attempt to save the game
 */
static bool borg_save_game(void)
{
    /* Cancel everything */
    borg_keypress(ESCAPE);
    borg_keypress(ESCAPE);

    /* Save the game */
    borg_keypress('^');
    borg_keypress('S');

    /* Cancel everything */
    borg_keypress(ESCAPE);
    borg_keypress(ESCAPE);

    /* Success */
    return true;
}

/*
 * Check if the borg should save the game and do so if necessary
 */
static bool borg_check_save(void)
{
    int i;

    static char svSavefile[1024];
    static char svSavefile2[1024];
    static bool justSaved = false;

    /* save now */
    if (borg.status.save && borg_save_game()) {
        /* Log */
        borg_note("# Auto Save!");

        borg.status.save = false;

        /* Create a scum file */
        if (borg.trait[BI_CLEVEL] >= borg_cfg[BORG_DUMP_LEVEL]
            || strstr(player->died_from, "starvation")) {
            memcpy(svSavefile, savefile, sizeof(savefile));
            /* Process the player name */
            for (i = 0; player->full_name[i]; i++) {
                char c = player->full_name[i];

                /* No control characters */
                if (iscntrl((unsigned char)c)) {
                    /* Illegal characters */
                    quit_fmt("Illegal control char (0x%02X) in player name", c);
                }

                /* Convert all non-alphanumeric symbols */
                if (!isalpha((unsigned char)c) && !isdigit((unsigned char)c))
                    c = '_';

                /* Build "file_name" */
                svSavefile2[i] = c;
            }
            svSavefile2[i] = 0;

            path_build(savefile, 1024, ANGBAND_DIR_ARCHIVE, svSavefile2);

            justSaved = true;
        }
        return true;
    }
    if (justSaved) {
        memcpy(savefile, svSavefile, sizeof(savefile));
        borg_save_game();
        justSaved = false;
        return true;
    }

    return false;
}


/*
 * Think about the world and perform an action
 *
 * Check inventory/equipment/spells/panel once per "turn"
 *
 * Process "store" and other modes when necessary
 *
 * Note that the non-cheating "inventory" and "equipment" parsers
 * will get confused by a "weird" situation involving an ant ("a")
 * on line one of the screen, near the left, next to a shield, of
 * the same color, and using --(-- the ")" symbol, directly to the
 * right of the ant.  This is very rare, but perhaps not completely
 * impossible.  I ignore this situation.  :-)
 *
 * The handling of stores is a complete and total hack, but seems
 * to work remarkably well, considering... :-)  Note that while in
 * a store, time does not pass, and most actions are not available,
 * and a few new commands are available ("sell" and "purchase").
 *
 * Note the use of "cheat" functions to extract the current inventory,
 * the current equipment, the current panel, and the current spellbook
 * information.  These can be replaced by (very expensive) "parse"
 * functions, which cause an insane amount of "screen flashing".
 *
 * Technically, we should attempt to parse all the messages that
 * indicate that it is necessary to re-parse the equipment, the
 * inventory, or the books, and only set the appropriate flags
 * at that point.  This would not only reduce the potential
 * screen flashing, but would also optimize the code a lot,
 * since the "cheat_inven()" and "cheat_equip()" functions
 * are expensive.  For paranoia, we could always select items
 * and spells using capital letters, and keep a global verification
 * buffer, and induce failure and recheck the inventory/equipment
 * any time we get a mis-match.  We could even do some of the state
 * processing by hand, for example, charge reduction and such.  This
 * might also allow us to keep track of how long we have held objects,
 * especially if we attempt to do "item tracking" in the inventory
 * extraction code.
 */
bool borg_think(void)
{
    uint8_t t_a;

    char        buf[128];

    /* find all items currently in use */
    borg_find_all_items();

    /* check to see if we should save */
    if (borg_check_save())
        return true;

    /*** Process books/spells ***/
    if (borg_do_spell) {
        borg_cheat_spells();
        borg_do_spell = false;
    }

    /* Always revert shapechanged players to normal form.
     * !FIX !TODO: borg needs to know when to shapechange and how
     * to deal with being in a different form.
     */
    if (player_is_shapechanged(player)) {
        /* it looks like throw is a good command that checks */
        /* your form without a prerequisite check */
        borg_keypress('v');
        borg_keypress('r');
        return true;
    }

    /* Examine the equipment/inventory */
    borg_notice(true);

    /* Examine the screen */
    borg_update();

    /* Evaluate the current world */
    borg.power = borg_power();

    /*** Handle stores ***/

    /* Check for being in a store */
    if ((0 == borg_what_text(1, 3, 4, &t_a, buf))
        && (streq(buf, "Stor") || streq(buf, "Home")))
        return (borg_think_store());

    /* Check spells again later */
    borg_do_spell = true;

    /*** Analyze status ***/

    /* Track best level */
    if (borg.trait[BI_CLEVEL] > borg.trait[BI_MAXCLEVEL])
        borg.trait[BI_MAXCLEVEL] = borg.trait[BI_CLEVEL];
    if (borg.trait[BI_CDEPTH] > borg.trait[BI_MAXDEPTH]) {
        borg.trait[BI_MAXDEPTH] = borg.trait[BI_CDEPTH];
    }

    /* Increment the clock */
    borg.time.now++;

        /* If the clock overflowed, fix that  */
    if (borg.time.now > (BORG_TIME_MAX - 100)) {
        borg.time.now = 1;
        borg_warning("*****WARNING***** Borg Clock Overflow");
    }

    /* Increment our anti-bounce count to avoid loops */
    borg.antibounce_count++;

    /* Allow user abort */
    if (borg.status.cancel)
        return true;

    /* Do something */
    return (borg_think_dungeon());
}

#endif
