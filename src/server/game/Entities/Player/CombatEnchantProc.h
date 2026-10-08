/* This file is part of TrinityCore; licensed under GPL version 2 or later. */
#ifndef TRINITY_COMBAT_ENCHANT_PROC_H
#define TRINITY_COMBAT_ENCHANT_PROC_H

namespace CombatEnchantProc
{
template<class Roll, class Cast>
void Dispatch(float chance, Roll&& roll, Cast&& cast)
{
    if (roll(chance))
        cast();
}
}

#endif
