/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// In-game Store (TokenHandler.cpp) commands for the support site (contrib/support_site), run through Remote Access:
//   .shop reload                              reload shop_group / shop_product / shop_product_action
//   .shop goldlist <character> <gold> <points> take the gold from the character (online or not) and list it on the Gold Market
//   .shop goldcancel <listing id>             remove a listing and mail its gold back to the seller
// Output lines start with "OK" or "ERROR" so the site can read them.

#include "ScriptMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "StringFormat.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

namespace
{
constexpr uint32 GoldMarketMaxGold = 200000;    // the gold action is sent as int32 copper
constexpr uint32 GoldMarketMinPrice = 2;        // the fee is at least 1 point
}

class classic_shop_commandscript : public CommandScript
{
public:
    classic_shop_commandscript() : CommandScript("classic_shop_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable shopCommandTable =
        {
            { "reload",     HandleReloadCommand,     rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
            { "points",     HandlePointsCommand,     rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
            { "balance",    HandleBalanceCommand,    rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
            { "boost",      HandleBoostCommand,      rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
            { "goldlist",   HandleGoldListCommand,   rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
            { "goldcancel", HandleGoldCancelCommand, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "shop", shopCommandTable },
        };
        return commandTable;
    }

    static bool HandleReloadCommand(ChatHandler* handler)
    {
        sObjectMgr->LoadShop();
        handler->PSendSysMessage("OK shop reloaded: %u tabs, %u products", uint32(sObjectMgr->GetShopGroups().size()), uint32(sObjectMgr->GetShopProducts().size()));
        return true;
    }

    static uint32 GetPoints(uint32 accountId)
    {
        if (QueryResult result = LoginDatabase.PQuery("SELECT Points FROM account_shop_points WHERE AccountID = {}", accountId))
            return (*result)[0].GetUInt32();
        return 0;
    }

    // .shop points <amount> [character]: add shop points to the character's account (negative takes them, not below 0)
    static bool HandlePointsCommand(ChatHandler* handler, int32 amount, Optional<PlayerIdentifier> player)
    {
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
        {
            handler->SendSysMessage("Usage: .shop points <amount> [character] (or target a player)");
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(player->GetGUID());
        if (!accountId)
        {
            handler->SendSysMessage("ERROR unknown character");
            handler->SetSentErrorMessage(true);
            return false;
        }

        LoginDatabase.DirectPExecute("INSERT INTO account_shop_points (AccountID, Points) VALUES ({0}, {1}) "
            "ON DUPLICATE KEY UPDATE Points = GREATEST(0, CAST(Points AS SIGNED) + {2})", accountId, std::max(amount, 0), amount);
        uint32 points = GetPoints(accountId);

        handler->PSendSysMessage("OK %s: %d points, account %u now has %u.", player->GetName().c_str(), amount, accountId, points);
        if (Player* target = player->GetConnectedPlayer())
            if (target->GetSession() != handler->GetSession())
                ChatHandler(target->GetSession()).SendSysMessage(Trinity::StringFormat("Shop: you received {} points. You have {} points.", amount, points));
        TC_LOG_INFO("entities.player", "Shop: {} points for account {} ({}), now {}", amount, accountId, player->GetName(), points);
        return true;
    }

    // .shop boost [character] [type]: a character boost token for the character's account (type 6 = level 60)
    static bool HandleBoostCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, Optional<uint32> boostType)
    {
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
        {
            handler->SendSysMessage("Usage: .shop boost [character] [type] (or target a player)");
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(player->GetGUID());
        if (!accountId)
        {
            handler->SendSysMessage("ERROR unknown character");
            handler->SetSentErrorMessage(true);
            return false;
        }

        LoginDatabase.DirectPExecute("INSERT INTO account_boost_token (AccountID, BoostType, Source) VALUES ({}, {}, 'gm')", accountId, boostType.value_or(6));
        handler->PSendSysMessage("OK account %u of %s got a character boost token (type %u); it shows on the character screen.",
            accountId, player->GetName().c_str(), boostType.value_or(6));
        return true;
    }

    // .shop balance [character]
    static bool HandleBalanceCommand(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
        {
            handler->SendSysMessage("Usage: .shop balance [character] (or target a player)");
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(player->GetGUID());
        if (!accountId)
        {
            handler->SendSysMessage("ERROR unknown character");
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage("OK %s: account %u has %u shop points.", player->GetName().c_str(), accountId, GetPoints(accountId));
        return true;
    }

    static bool HandleGoldListCommand(ChatHandler* handler, std::string name, uint32 gold, uint32 price)
    {
        if (!normalizePlayerName(name))
        {
            handler->SendSysMessage("ERROR unknown character");
            return true;
        }

        if (gold < 1 || gold > GoldMarketMaxGold || price < GoldMarketMinPrice)
        {
            handler->PSendSysMessage("ERROR the gold must be 1 to %u and the price at least %u points", GoldMarketMaxGold, GoldMarketMinPrice);
            return true;
        }

        CharacterCacheEntry const* character = sCharacterCache->GetCharacterCacheByName(name);
        if (!character)
        {
            handler->SendSysMessage("ERROR unknown character");
            return true;
        }

        // the player save uses prepared statements, which only the async connections have (a Direct* commit asserts): gold through the
        // normal async save, the listing row synchronously so its id can be returned (commands run in the world thread, one at a time)
        uint64 copper = uint64(gold) * GOLD;
        if (Player* player = ObjectAccessor::FindConnectedPlayer(character->Guid))
        {
            if (!player->HasEnoughMoney(copper))
            {
                handler->SendSysMessage("ERROR the character does not have that much gold");
                return true;
            }
            player->ModifyMoney(-int64(copper));
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            player->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);
            ChatHandler(player->GetSession()).SendSysMessage(Trinity::StringFormat("Shop: {} gold was listed on the Gold Market.", gold));
        }
        else
        {
            QueryResult result = CharacterDatabase.PQuery("SELECT money FROM characters WHERE guid = {}", character->Guid.GetCounter());
            if (!result || (*result)[0].GetUInt64() < copper)
            {
                handler->SendSysMessage("ERROR the character does not have that much gold");
                return true;
            }
            CharacterDatabase.DirectPExecute("UPDATE characters SET money = money - {} WHERE guid = {}", copper, character->Guid.GetCounter());
        }

        CharacterDatabase.DirectPExecute("INSERT INTO shop_gold_listing (SellerAccount, SellerGuid, Gold, Price) VALUES ({}, {}, {}, {})",
            character->AccountId, character->Guid.GetCounter(), gold, price);

        uint32 listingId = 0;
        if (QueryResult result = CharacterDatabase.PQuery("SELECT MAX(ID) FROM shop_gold_listing WHERE SellerGuid = {}", character->Guid.GetCounter()))
            listingId = (*result)[0].GetUInt32();

        TC_LOG_INFO("entities.player", "Shop: {} ({}) listed {} gold for {} points (listing {})", name, character->Guid.ToString(), gold, price, listingId);
        handler->PSendSysMessage("OK %u", listingId);
        return true;
    }

    static bool HandleGoldCancelCommand(ChatHandler* handler, uint32 listingId)
    {
        QueryResult result = CharacterDatabase.PQuery("SELECT SellerGuid, Gold FROM shop_gold_listing WHERE ID = {}", listingId);
        if (!result)
        {
            handler->SendSysMessage("ERROR no such listing (sold or removed already)");
            return true;
        }

        uint64 sellerGuid = (*result)[0].GetUInt64();
        uint32 gold = (*result)[1].GetUInt32();

        // the listing goes first (synchronously), so a second cancel or a purchase can not pay the gold out twice; the mail uses prepared
        // statements and goes through the async connection
        CharacterDatabase.DirectPExecute("DELETE FROM shop_gold_listing WHERE ID = {}", listingId);
        if (sellerGuid)
        {
            ObjectGuid seller = ObjectGuid::Create<HighGuid::Player>(sellerGuid);
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft("Gold Market", Trinity::StringFormat("Your Gold Market listing of {} gold was removed. Here is your gold.", gold))
                .AddMoney(uint64(gold) * GOLD)
                .SendMailTo(trans, MailReceiver(ObjectAccessor::FindConnectedPlayer(seller), sellerGuid), MailSender(MAIL_NORMAL, UI64LIT(0), MAIL_STATIONERY_GM), MAIL_CHECK_MASK_NOT_RETURNABLE);
            CharacterDatabase.CommitTransaction(trans);
        }

        TC_LOG_INFO("entities.player", "Shop: Gold Market listing {} ({} gold) removed, gold mailed to {}", listingId, gold, sellerGuid);
        handler->PSendSysMessage("OK %u", listingId);
        return true;
    }
};

// realm transfers arriving at this realm: at start and every minute
class classic_character_transfer_world : public WorldScript
{
public:
    classic_character_transfer_world() : WorldScript("classic_character_transfer_world") { }

    void OnStartup() override
    {
        // pvp talent rows of characters that do not exist (an aborted creation or load left them): a new character that gets their
        // guid (creation, realm transfer) fails with a duplicate key. Character deletion itself removes them.
        CharacterDatabase.DirectExecute("DELETE p FROM character_pvp_talent p LEFT JOIN characters c ON c.guid = p.guid WHERE c.guid IS NULL");

        WorldSession::ProcessIncomingCharacterTransfers();
    }

    void OnUpdate(uint32 diff) override
    {
        _timer += diff;
        if (_timer < 60 * IN_MILLISECONDS)
            return;
        _timer = 0;
        WorldSession::ProcessIncomingCharacterTransfers();
    }

private:
    uint32 _timer = 0;
};

void AddSC_classic_shop_commands()
{
    new classic_shop_commandscript();
    new classic_character_transfer_world();
}
