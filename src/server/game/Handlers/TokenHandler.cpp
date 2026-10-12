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

#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerDump.h"
#include "Random.h"
#include "RealmList.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "TokenPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <atomic>

void WorldSession::HandleCommerceTokenGetLog(WorldPackets::Token::CommerceTokenGetLog& commerceTokenGetLog)
{
    WorldPackets::Token::CommerceTokenGetLogResponse response;

    response.ClientToken = commerceTokenGetLog.ClientToken;
    response.Result = TOKEN_RESULT_ERROR_DISABLED;

    SendPacket(response.Write());
}

void WorldSession::HandleCommerceTokenGetMarketPrice(WorldPackets::Token::CommerceTokenGetMarketPrice& commerceTokenGetMarketPrice)
{
    WorldPackets::Token::CommerceTokenGetMarketPriceResponse response;

    response.Price = 0;
    response.ClientToken = commerceTokenGetMarketPrice.ClientToken;
    response.Result = TOKEN_RESULT_ERROR_DISABLED;

    SendPacket(response.Write());
}

// Classic 1.60 catalog shop: the client asks at login when it last fetched the catalog. CLASSIC_CATALOG_PROBE: layout not
// known yet (decoder captured while this reply arrives); send "never" as an int64 timestamp.
void WorldSession::HandleGetLastCatalogFetch(WorldPackets::Null& /*null*/)
{
    WorldPacket data(SMSG_LAST_CATALOG_FETCH_RESPONSE, 8);
    data << int64(0);
    SendPacket(&data);
}

// Classic 1.60 in-game Store, products from the world tables shop_group / shop_product / shop_product_action, plus the Gold
// Market tab built from the characters table shop_gold_listing (gold sold by players for points).
// Classic.Shop.Enabled = 0 answers like the official server (result 2, nothing for sale).
// Prices are shop points; the client shows fixed point / 10000 in its currency format.
static constexpr uint64 ShopPriceFixedPoint(uint32 points) { return uint64(points) * 10000; }

namespace
{
std::string_view CheckCharacterTransfer(uint32 accountId, ObjectGuid character, uint32 destinationRealmAddress);
bool BeginCharacterTransfer(uint32 accountId, ObjectGuid character, uint32 destinationRealmAddress);

constexpr uint32 GoldMarketGroupID = 0x40000000;
constexpr uint32 GoldMarketProductBase = 0x40000000;    // product id = base + listing id
constexpr uint32 GoldMarketMaxListings = 500;

struct GoldListing
{
    uint32 ID = 0;
    uint32 SellerAccount = 0;
    uint32 Gold = 0;
    uint32 Price = 0;
};

ShopProduct MakeGoldListingProduct(GoldListing const& listing)
{
    ShopProduct product;
    product.ID = GoldMarketProductBase + listing.ID;
    product.GroupID = GoldMarketGroupID;
    product.Name = Trinity::StringFormat("{} Gold", listing.Gold);
    product.Description = "Gold offered by another player, for this character.";
    product.IconFileDataID = 133784;    // inv_misc_coin_01
    product.Price = listing.Price;
    product.Actions.push_back({ .Action = ShopAction::Gold, .Param1 = int32(std::min<uint64>(uint64(listing.Gold) * GOLD, std::numeric_limits<int32>::max())) });
    product.InGameOnly = true;
    return product;
}

Optional<GoldListing> GetGoldListing(uint32 listingId)
{
    if (QueryResult result = CharacterDatabase.PQuery("SELECT ID, SellerAccount, Gold, Price FROM shop_gold_listing WHERE ID = {}", listingId))
    {
        Field* fields = result->Fetch();
        return GoldListing{ .ID = fields[0].GetUInt32(), .SellerAccount = fields[1].GetUInt32(), .Gold = fields[2].GetUInt32(), .Price = fields[3].GetUInt32() };
    }
    return {};
}

// a product from the world tables or a Gold Market listing (built into storage)
ShopProduct const* FindShopProduct(uint32 productId, Optional<ShopProduct>& storage, Optional<GoldListing>& listing)
{
    if (ShopProduct const* product = sObjectMgr->GetShopProduct(productId))
        return product;

    if (productId > GoldMarketProductBase)
    {
        listing = GetGoldListing(productId - GoldMarketProductBase);
        if (listing)
            return &storage.emplace(MakeGoldListingProduct(*listing));
    }
    return nullptr;
}
}

void WorldSession::HandleBattlePayGetProductList(WorldPackets::Null& /*null*/)
{
    SendShopProductList();
    SendShopDistributionList();
}

// boost / character service tokens of the account (also sent with the character list, see HandleCharEnum)
void WorldSession::SendShopDistributionList()
{
    WorldPackets::BattlePay::DistributionListResponse distributions;
    // Hardcore Character Transfer token on the character screen: on realms whose characters may leave once dead (transfer rules), while
    // the account has a dead character there (died before or after, like official); anywhere with Classic.Shop.VasTest
    if (sConfigMgr->GetBoolDefault("Classic.Shop.Enabled", false) && (sConfigMgr->GetBoolDefault("Classic.Shop.VasTest", false) ||
        (LoginDatabase.PQuery("SELECT 1 FROM character_transfer_rule WHERE SourceRealm = {} AND DeadOnly = 1", sRealmList->GetCurrentRealmId().Realm) &&
         CharacterDatabase.PQuery("SELECT 1 FROM characters WHERE account = {} AND deleteDate IS NULL AND (playerFlags & {} OR health = 0) LIMIT 1",
            GetAccountId(), uint32(PLAYER_FLAGS_GHOST)))))
    {
        WorldPackets::BattlePay::DistributionObject& distribution = distributions.Distributions.emplace_back();
        distribution.DistributionID = 1;
        distribution.DeliverableID = 900001;
        distribution.PurchaseID = 1;
        WorldPackets::BattlePay::Deliverable& token = distribution.Product.emplace();
        token.DeliverableID = 900001;
        token.Type = 39;                // HardcoreCharacterTransfer
        token.Flags = 0x400;            // character service token
        token.Name = "Hardcore Character Transfer";
    }

    // character boost tokens of the account (auth.account_boost_token): a boost button for each boost type on the character screen
    if (sConfigMgr->GetBoolDefault("Classic.Shop.Enabled", false))
        if (QueryResult boosts = LoginDatabase.PQuery("SELECT ID, BoostType FROM account_boost_token WHERE AccountID = {} AND UsedGuid = 0", GetAccountId()))
        {
            do
            {
                distributions.Distributions.push_back(MakeBoostDistribution((*boosts)[0].GetUInt32(), (*boosts)[1].GetUInt32(), 1));
            } while (boosts->NextRow());
        }

    SendPacket(distributions.Write());
}

// a character boost token as the client's distribution: deliverable Type 1, BoostID = boost type (GetUpgradeDistributions, 70058
// 0x242A220); status 1 = usable
WorldPackets::BattlePay::DistributionObject WorldSession::MakeBoostDistribution(uint32 tokenId, uint32 boostType, int32 status)
{
    WorldPackets::BattlePay::DistributionObject distribution;
    distribution.DistributionID = BoostDistributionIdBase + tokenId;
    distribution.Status = status;
    distribution.DeliverableID = 800000 + boostType;
    distribution.PurchaseID = distribution.DistributionID;
    WorldPackets::BattlePay::Deliverable& boost = distribution.Product.emplace();
    boost.DeliverableID = distribution.DeliverableID;
    boost.Type = 1;
    boost.BoostID = int32(boostType);
    boost.Name = "Character Boost";
    return distribution;
}

// boost button used: the character (offline, below the boost level) is raised to the boost level and gets the boost's items by mail
void WorldSession::HandleBattlePayDistributionAssignToTarget(WorldPackets::BattlePay::DistributionAssignToTarget& assign)
{
    WorldPackets::BattlePay::StartDistributionAssignToTargetResponse response;
    response.ClientToken = assign.ClientToken;
    response.DistributionID = assign.DistributionID;

    uint32 tokenId = assign.DistributionID > BoostDistributionIdBase ? uint32(assign.DistributionID - BoostDistributionIdBase) : 0;
    QueryResult token = tokenId ? LoginDatabase.PQuery("SELECT BoostType FROM account_boost_token WHERE ID = {} AND AccountID = {} AND UsedGuid = 0",
        tokenId, GetAccountId()) : nullptr;
    uint32 boostType = token ? (*token)[0].GetUInt32() : 0;
    uint8 boostLevel = boostType == 4 ? 50 : boostType == 8 ? 70 : boostType == 11 ? 80 : 60;   // CharacterServiceInfo boost levels

    std::string_view error;
    QueryResult character = CharacterDatabase.PQuery("SELECT level, class FROM characters WHERE guid = {} AND account = {} AND deleteDate IS NULL",
        assign.TargetCharacter.GetCounter(), GetAccountId());
    if (!token)
        error = "no such boost";
    else if (!character)
        error = "not your character";
    else if ((*character)[0].GetUInt8() >= boostLevel)
        error = "the character is already at that level";
    else if (ObjectAccessor::FindConnectedPlayer(assign.TargetCharacter))
        error = "log out the character first";

    TC_LOG_INFO("entities.player", "Shop: boost token {} (type {}) for {} by account {}, spec choice {}: {}", tokenId, boostType,
        assign.TargetCharacter.ToString(), GetAccountId(), assign.ProductChoice, error.empty() ? "ok" : error);

    if (!error.empty())
    {
        response.Result = 1;
        SendPacket(response.Write());
        WorldPackets::BattlePay::CharacterUpgradeState aborted(SMSG_CHARACTER_UPGRADE_ABORTED);
        aborted.Character = assign.TargetCharacter;
        SendPacket(aborted.Write());
        return;
    }

    uint8 classId = (*character)[1].GetUInt8();
    LoginDatabase.DirectPExecute("UPDATE account_boost_token SET UsedRealm = {}, UsedGuid = {}, UsedTime = NOW() WHERE ID = {}",
        sRealmList->GetCurrentRealmId().Realm, assign.TargetCharacter.GetCounter(), tokenId);

    SendPacket(response.Write());

    WorldPackets::BattlePay::DistributionUpdate used;
    used.Distribution = MakeBoostDistribution(tokenId, boostType, 2);
    used.Distribution.Revoked = true;
    SendPacket(used.Write());

    WorldPackets::BattlePay::CharacterUpgradeState started(SMSG_CHARACTER_UPGRADE_STARTED);
    started.Character = assign.TargetCharacter;
    SendPacket(started.Write());

    // the character is offline: level in the database (stats, talent points and spells follow at login; spells from trainers)
    CharacterDatabase.DirectPExecute("UPDATE characters SET level = {}, xp = 0 WHERE guid = {}", boostLevel, assign.TargetCharacter.GetCounter());
    sCharacterCache->UpdateCharacterLevel(assign.TargetCharacter, boostLevel);

    // the boost's items (world.shop_boost_item) and gold (Classic.Shop.BoostGold) by mail. RandomPropertyID = the "of the X" suffix
    // (an ItemBonusList); Item::CreateItem adds none, so it is set here (0 = a random one from the item's pool, if it has one)
    struct BoostStack { uint32 ItemId; uint32 Count; uint32 BonusList; };
    std::vector<BoostStack> stacks;
    if (QueryResult items = WorldDatabase.PQuery("SELECT ItemID, Count, RandomPropertyID FROM shop_boost_item WHERE BoostType = {} AND ClassID IN (0, {})", boostType, classId))
    {
        do
        {
            uint32 itemId = (*items)[0].GetUInt32();
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;
            uint32 maxStack = std::max<uint32>(proto->GetMaxStackSize(), 1);
            for (uint32 left = std::max<uint32>((*items)[1].GetUInt32(), 1); left > 0; left -= std::min(left, maxStack))
                stacks.push_back({ itemId, std::min(left, maxStack), (*items)[2].GetUInt32() });
        } while (items->NextRow());
    }

    uint64 gold = uint64(std::max(sConfigMgr->GetIntDefault("Classic.Shop.BoostGold", 250), 0)) * GOLD;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (std::size_t i = 0; i < stacks.size() || (i == 0 && gold); i += MAX_MAIL_ITEMS)
    {
        MailDraft draft("Character Boost", "Your character has been boosted. Here is your gear.");
        if (i == 0 && gold)
            draft.AddMoney(gold);
        for (std::size_t j = i; j < std::min(stacks.size(), i + MAX_MAIL_ITEMS); ++j)
            if (Item* item = Item::CreateItem(stacks[j].ItemId, stacks[j].Count, ItemContext::NONE, nullptr))
            {
                if (uint32 bonusList = stacks[j].BonusList ? stacks[j].BonusList : GenerateItemRandomBonusListId(stacks[j].ItemId))
                    item->SetItemRandomBonusList(bonusList);
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
        draft.SendMailTo(trans, MailReceiver(nullptr, assign.TargetCharacter.GetCounter()), MailSender(MAIL_NORMAL, UI64LIT(0), MAIL_STATIONERY_GM), MAIL_CHECK_MASK_NOT_RETURNABLE);
    }
    CharacterDatabase.CommitTransaction(trans);

    WorldPackets::BattlePay::CharacterUpgradeState complete(SMSG_CHARACTER_UPGRADE_COMPLETE);
    complete.Character = assign.TargetCharacter;
    SendPacket(complete.Write());
}

// Classic 1.60 character services, work in progress (Classic.Shop.VasTest). The Store's character service window (and the character
// screen's service token) ask for the account's characters and the target realms. For now: the characters of this realm.
void WorldSession::HandleGetVasAccountCharacterList(WorldPackets::BattlePay::GetVasAccountCharacterList& request)
{
    TC_LOG_DEBUG("network", "Shop: VAS character list request {} {}", request.Unknown1, request.Unknown2);
    WorldPackets::BattlePay::GetVasAccountCharacterListResult result;
    result.ClientToken = request.Unknown1;
    result.Unknown = request.Unknown2;

    uint32 realmAddress = sRealmList->GetCurrentRealmId().GetAddress();
    std::shared_ptr<Realm const> realm = sRealmList->GetCurrentRealm();
    if (QueryResult characters = CharacterDatabase.PQuery("SELECT guid, name, race, class, gender, level FROM characters WHERE account = {} AND deleteDate IS NULL ORDER BY name", GetAccountId()))
    {
        do
        {
            Field* fields = characters->Fetch();
            WorldPackets::BattlePay::VasCharacter& character = result.Characters.emplace_back();
            character.Guid = ObjectGuid::Create<HighGuid::Player>(fields[0].GetUInt64());
            character.WowAccountGuid = ObjectGuid::Create<HighGuid::WowAccount>(GetAccountId());
            character.VirtualRealmAddress = realmAddress;
            character.Name = fields[1].GetString();
            character.Race = int8(fields[2].GetUInt8());
            character.Class = int8(fields[3].GetUInt8());
            character.Sex = int8(fields[4].GetUInt8());
            character.Level = fields[5].GetUInt8();
            character.RealmName = realm ? realm->Name : "";
        } while (characters->NextRow());
    }
    SendPacket(result.Write());
}

void WorldSession::HandleGetVasTransferTargetRealmList(WorldPackets::BattlePay::GetVasTransferTargetRealmList& request)
{
    TC_LOG_DEBUG("network", "Shop: VAS target realm list request {} {}", request.Unknown1, request.Unknown2);
    WorldPackets::BattlePay::GetVasTransferTargetRealmListResult result;
    result.Unknown1 = request.Unknown1;     // client token
    result.Unknown2 = 0;                    // result, 0 = ok
    result.Unknown3 = request.Unknown2;     // the client sends it back in its character list request (15 = character transfer)
    Battlenet::RealmHandle current = sRealmList->GetCurrentRealmId();
    if (QueryResult realms = LoginDatabase.PQuery("SELECT id, name FROM realmlist WHERE id <> {} ORDER BY id", current.Realm))
    {
        do
        {
            WorldPackets::BattlePay::VasTransferTargetRealm& realm = result.Realms.emplace_back();
            realm.RealmAddress = Battlenet::RealmHandle(current.Region, current.Site, (*realms)[0].GetUInt32()).GetAddress();
            realm.Name = (*realms)[1].GetString();
        } while (realms->NextRow());
    }
    SendPacket(result.Write());
}

// target side of a realm transfer: load the dumps addressed to this realm onto the same account (a taken name loads with a
// temporary one and the character renames at login); called at start and every minute (classic_shop_commands.cpp)
void WorldSession::ProcessIncomingCharacterTransfers()
{
    uint32 realmId = sRealmList->GetCurrentRealmId().Realm;

    // the dump loader commits in the background: a load counts as done once the character is really there (status 3 -> 1);
    // a rejected one (e.g. a duplicate key) becomes failed, so a GM can retry or return it (the dump stays in the row)
    if (QueryResult loaded = LoginDatabase.PQuery("SELECT ID, TargetGuid, Name FROM character_transfer WHERE TargetRealm = {} AND Status = 3", realmId))
    {
        do
        {
            Field* fields = loaded->Fetch();
            bool arrived = CharacterDatabase.PQuery("SELECT 1 FROM characters WHERE guid = {}", fields[1].GetUInt64()) != nullptr;
            if (arrived)
                LoginDatabase.DirectPExecute("UPDATE character_transfer SET Status = 1 WHERE ID = {}", fields[0].GetUInt32());
            else
                LoginDatabase.DirectPExecute("UPDATE character_transfer SET Status = 2, Message = 'the character database rejected the character (see DBErrors.log)' WHERE ID = {}",
                    fields[0].GetUInt32());
            TC_LOG_INFO("entities.player", "Realm transfer {}: {} {}", fields[0].GetUInt32(), fields[2].GetString(), arrived ? "is in the database" : "was NOT saved");
        } while (loaded->NextRow());
    }

    QueryResult result = LoginDatabase.PQuery("SELECT ID, AccountID, Name, Dump FROM character_transfer WHERE TargetRealm = {} AND Status = 0 ORDER BY ID", realmId);
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetUInt32();
        uint32 accountId = fields[1].GetUInt32();
        std::string name = fields[2].GetString();
        std::string dump = fields[3].GetString();

        ObjectGuid::LowType newGuid = sObjectMgr->GetGenerator<HighGuid::Player>().GetNextAfterMaxUsed();

        // rows a deleted character left behind under the guid the loader picks would make its whole transaction fail (duplicate key)
        static constexpr std::string_view CharacterGuidTables[] =
        {
            "character_account_data", "character_achievement", "character_achievement_progress", "character_action", "character_aura",
            "character_aura_effect", "character_cuf_profiles", "character_declinedname", "character_favorite_auctions",
            "character_fishingsteps", "character_glyphs", "character_homebind", "character_inventory", "character_pvp_talent",
            "character_queststatus", "character_queststatus_daily", "character_queststatus_monthly", "character_queststatus_objectives",
            "character_queststatus_objectives_criteria", "character_queststatus_objectives_criteria_progress", "character_queststatus_rewarded",
            "character_queststatus_seasonal", "character_queststatus_weekly", "character_reputation", "character_select_screen_equipment_cache",
            "character_skills", "character_spell", "character_spell_charges", "character_spell_cooldown", "character_talent",
            "character_equipmentsets", "character_transmog_outfits",
        };
        if (!CharacterDatabase.PQuery("SELECT 1 FROM characters WHERE guid = {}", newGuid))
        {
            for (std::string_view table : CharacterGuidTables)
                CharacterDatabase.DirectPExecute("DELETE FROM `{}` WHERE guid = {}", table, newGuid);
            CharacterDatabase.DirectPExecute("DELETE FROM character_currency WHERE CharacterGuid = {}", newGuid);
        }

        PlayerDumpReader reader;
        DumpReturn loaded = reader.LoadDumpFromString(dump, accountId, name, 0);
        if (loaded == DUMP_SUCCESS)
        {
            LoginDatabase.DirectPExecute("UPDATE character_transfer SET Status = 3, TargetGuid = {}, Finished = NOW() WHERE ID = {}", newGuid, id);
            TC_LOG_INFO("entities.player", "Realm transfer {}: {} of account {} loaded as guid {}", id, name, accountId, newGuid);
        }
        else
        {
            std::string_view reason = loaded == DUMP_TOO_MANY_CHARS ? "the account has too many characters on this realm" : "the character data could not be loaded";
            LoginDatabase.DirectPExecute("UPDATE character_transfer SET Status = 2, Message = '{}', Finished = NOW() WHERE ID = {}", reason, id);
            TC_LOG_ERROR("entities.player", "Realm transfer {}: {} of account {} failed: {}", id, name, accountId, reason);
        }
    } while (result->NextRow());
}

void WorldSession::HandleRequestCharacterGuildFollowInfo(WorldPackets::BattlePay::RequestCharacterGuildFollowInfo& request)
{
    WorldPackets::BattlePay::QueryGuildFollowInfoResponse response;
    response.Character = request.Character;
    SendPacket(response.Write());
}

// character screen Hardcore Character Transfer token used: a free realm transfer of a dead character to the picked ruleset, under
// the same rules as the Store's transfer (auth.character_transfer_rule). The flow sends the same request twice, first to validate
// (which of its flag bits says so is not known): the first one is only checked, an identical second one moves the character.
void WorldSession::HandleBattlePayDistributionAssignVas(WorldPackets::BattlePay::DistributionAssignVas& assign)
{
    TC_LOG_INFO("network", "Shop: transfer token {} for {} to super district {} by account {} (flags {} {} {})", assign.DistributionID,
        assign.Character.ToString(), assign.DesiredSuperDistrictID, GetAccountId(), assign.Flags[0], assign.Flags[1], assign.Flags[2]);

    WorldPackets::BattlePay::DistributionAssignVasResponse response;
    response.ClientToken = assign.ClientToken;

    // destination ruleset -> its realm
    uint32 targetRealm = 0;
    if (uint32 contentSet = GetClassicContentSetForSuperDistrict(assign.DesiredSuperDistrictID))
        if (QueryResult realm = LoginDatabase.PQuery("SELECT id FROM realmlist WHERE contentSetId = {} LIMIT 1", contentSet))
            targetRealm = (*realm)[0].GetUInt32();
    Battlenet::RealmHandle current = sRealmList->GetCurrentRealmId();
    uint32 targetAddress = Battlenet::RealmHandle(current.Region, current.Site, targetRealm).GetAddress();

    std::string_view error;
    if (!sConfigMgr->GetBoolDefault("Classic.Shop.Enabled", false))
        error = "the shop is closed";
    else if (!targetRealm)
        error = "unknown destination";
    else if (!IsLegitCharacterForAccount(assign.Character))
        error = "not your character";
    else
        error = CheckCharacterTransfer(GetAccountId(), assign.Character, targetAddress);

    if (!error.empty())
    {
        TC_LOG_INFO("network", "Shop: transfer token refused: {}", error);
        _hardcoreTransferValidated.reset();
        response.StoreError = 1;
        SendPacket(response.Write());
        return;
    }

    std::pair<ObjectGuid, uint32> request(assign.Character, targetAddress);
    if (_hardcoreTransferValidated != request)
    {
        _hardcoreTransferValidated = request;          // validation step
        SendPacket(response.Write());
        return;
    }

    _hardcoreTransferValidated.reset();
    if (!BeginCharacterTransfer(GetAccountId(), assign.Character, targetAddress))
        response.StoreError = 1;
    SendPacket(response.Write());
}

// also sent after a purchase, so a sold Gold Market listing leaves the open Store at once
void WorldSession::SendShopProductList()
{
    bool const enabled = sConfigMgr->GetBoolDefault("Classic.Shop.Enabled", false);

    WorldPackets::BattlePay::ProductListResponse response;
    response.Result = enabled ? 0 : 2;
    response.CurrencyID = enabled ? 1 : 0;

    if (enabled)
    {
        for (auto const& [id, shopGroup] : sObjectMgr->GetShopGroups())
        {
            WorldPackets::BattlePay::ProductGroup& group = response.Groups.emplace_back();
            group.GroupID = shopGroup.ID;
            group.IconFileDataID = shopGroup.IconFileDataID;
            group.Ordering = shopGroup.Ordering;
            group.Name = shopGroup.Name;
        }

        uint32 deliverableId = 0;
        for (auto const& [id, shopProduct] : sObjectMgr->GetShopProducts())
        {
            WorldPackets::BattlePay::DisplayInfo display;
            display.FileDataID = shopProduct.IconFileDataID;
            display.Name1 = shopProduct.Name;
            display.Name3 = shopProduct.Description;

            WorldPackets::BattlePay::Product& product = response.Products.emplace_back();
            product.ProductID = shopProduct.ID;
            product.CurrentPriceFixedPoint = ShopPriceFixedPoint(shopProduct.Price);
            product.NormalPriceFixedPoint = ShopPriceFixedPoint(std::max(shopProduct.NormalPrice, shopProduct.Price));
            // 0x1/0x2 visible/buyable in one client context, 0x4/0x8 in the other (entry info builder 70291 0x2fb9520)
            product.Flags = 0x1 | 0x2 | 0x4 | 0x8;
            product.Display = display;
            // product Type -> character service (70338 0x2E378F0): 15 = CharacterTransfer, Buy Now opens the character / realm window
            if (std::ranges::any_of(shopProduct.Actions, [](ShopProductAction const& action) { return action.Action == ShopAction::RealmTransfer; }))
                product.Type = 15;

            for (ShopProductAction const& action : shopProduct.Actions)
            {
                if (action.Action != ShopAction::Item)
                    continue;

                WorldPackets::BattlePay::Deliverable& deliverable = response.Deliverables.emplace_back();
                deliverable.DeliverableID = ++deliverableId;
                deliverable.ItemID = action.Param1;
                deliverable.Quantity = action.Param2;
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(action.Param1))
                    deliverable.Name = proto->GetName(GetSessionDbcLocale());
                if (deliverable.Name.size() > 255)
                    deliverable.Name.resize(255);
                product.DeliverableIDs.push_back(deliverable.DeliverableID);
            }

            WorldPackets::BattlePay::ShopEntry& entry = response.Entries.emplace_back();
            entry.EntryID = shopProduct.ID;
            entry.GroupID = shopProduct.GroupID;
            entry.ProductID = shopProduct.ID;
            entry.Ordering = shopProduct.Ordering;
            entry.Display = display;
        }

        // Gold Market: most gold first
        if (QueryResult result = CharacterDatabase.PQuery("SELECT ID, SellerAccount, Gold, Price FROM shop_gold_listing ORDER BY Gold DESC, Price ASC, ID ASC LIMIT {}", GoldMarketMaxListings))
        {
            WorldPackets::BattlePay::ProductGroup& group = response.Groups.emplace_back();
            group.GroupID = GoldMarketGroupID;
            group.IconFileDataID = 133784;
            group.Ordering = 1000;
            group.Name = "Gold Market";

            int32 ordering = 0;
            do
            {
                Field* fields = result->Fetch();
                ShopProduct listingProduct = MakeGoldListingProduct({ .ID = fields[0].GetUInt32(), .SellerAccount = fields[1].GetUInt32(), .Gold = fields[2].GetUInt32(), .Price = fields[3].GetUInt32() });

                WorldPackets::BattlePay::DisplayInfo display;
                display.FileDataID = listingProduct.IconFileDataID;
                display.Name1 = listingProduct.Name;
                display.Name3 = listingProduct.Description;

                WorldPackets::BattlePay::Product& product = response.Products.emplace_back();
                product.ProductID = listingProduct.ID;
                product.CurrentPriceFixedPoint = ShopPriceFixedPoint(listingProduct.Price);
                product.NormalPriceFixedPoint = product.CurrentPriceFixedPoint;
                product.Flags = 0x1 | 0x2 | 0x4 | 0x8;
                product.Display = display;

                WorldPackets::BattlePay::ShopEntry& entry = response.Entries.emplace_back();
                entry.EntryID = listingProduct.ID;
                entry.GroupID = GoldMarketGroupID;
                entry.ProductID = listingProduct.ID;
                entry.Ordering = ++ordering;
                entry.Display = display;
            } while (result->NextRow());
        }
    }

    SendPacket(response.Write());
}

void WorldSession::HandleBattlePayGetPurchaseList(WorldPackets::Null& /*null*/)
{
    WorldPackets::BattlePay::PurchaseListResponse response;
    response.Result = sConfigMgr->GetBoolDefault("Classic.Shop.Enabled", false) ? 0 : 2;
    SendPacket(response.Write());
}

namespace
{
constexpr int32 ShopPurchaseStatusPending = 9;  // waiting for confirmation: C_StoreSecure.PurchaseProductConfirm (70338 0x19cfc70) requires it
constexpr int32 ShopPurchaseStatusFinished = 6; // HasPurchaseInProgress (70291 0x1af9ed0) treats 6 as done
constexpr int32 ShopPurchaseResultError = 1;

uint32 GetShopPoints(uint32 accountId)
{
    if (QueryResult result = LoginDatabase.PQuery("SELECT Points FROM account_shop_points WHERE AccountID = {}", accountId))
        return (*result)[0].GetUInt32();
    return 0;
}

WorldPackets::BattlePay::Purchase MakeShopPurchase(uint64 purchaseId, ShopProduct const& product, int32 status, int32 result, int64 timeCreated, uint32 points)
{
    WorldPackets::BattlePay::Purchase purchase;
    purchase.PurchaseID = purchaseId;
    purchase.Status = status;
    purchase.ResultCode = result;
    purchase.ProductID = product.ID;
    purchase.BasePrice = ShopPriceFixedPoint(product.Price);
    purchase.UserPrice = ShopPriceFixedPoint(product.Price);
    purchase.TimeCreated = timeCreated;
    purchase.WalletName = Trinity::StringFormat("Shop points: {}", points);
    return purchase;
}

// realm transfer rules (auth.character_transfer_rule, support site Shop admin -> Transfers): a transfer needs its row
std::string_view CheckCharacterTransfer(uint32 accountId, ObjectGuid character, uint32 destinationRealmAddress)
{
    uint32 sourceRealm = sRealmList->GetCurrentRealmId().Realm;
    uint32 targetRealm = Battlenet::RealmHandle(destinationRealmAddress).Realm;
    if (targetRealm == sourceRealm)
        return "the character is already on that realm";
    if (!LoginDatabase.PQuery("SELECT 1 FROM realmlist WHERE id = {}", targetRealm))
        return "unknown realm";
    if (ObjectAccessor::FindConnectedPlayer(character))
        return "log out the character first";

    {
        QueryResult rule = LoginDatabase.PQuery("SELECT DeadOnly FROM character_transfer_rule WHERE SourceRealm = {} AND TargetRealm = {}", sourceRealm, targetRealm);
        if (!rule)
            return "characters of this realm can not move to that realm";
        if ((*rule)[0].GetUInt8())
        {
            // dead: released (ghost) or still lying dead (no health)
            QueryResult state = CharacterDatabase.PQuery("SELECT playerFlags, health FROM characters WHERE guid = {}", character.GetCounter());
            if (!state || (!((*state)[0].GetUInt32() & PLAYER_FLAGS_GHOST) && (*state)[1].GetUInt32() != 0))
                return "only characters that died can move to that realm";
        }
    }

    if (LoginDatabase.PQuery("SELECT 1 FROM character_transfer WHERE AccountID = {} AND SourceRealm = {} AND SourceGuid = {} AND Status = 0", accountId, sourceRealm, character.GetCounter()))
        return "this character is already being transferred";
    return {};
}

// source side of a realm transfer: the character's dump goes to auth.character_transfer for the target realm, then the character
// is removed here. Returns false (and nothing changed) when the dump can not be written.
bool BeginCharacterTransfer(uint32 accountId, ObjectGuid character, uint32 destinationRealmAddress)
{
    std::string dump;
    PlayerDumpWriter writer;
    if (writer.WriteDumpToString(dump, character.GetCounter()) != DUMP_SUCCESS)
        return false;

    std::string name;
    sCharacterCache->GetCharacterNameByGuid(character, name);
    std::string escapedDump = dump;
    LoginDatabase.EscapeString(escapedDump);
    std::string escapedName = name;
    LoginDatabase.EscapeString(escapedName);
    LoginDatabase.DirectExecute(Trinity::StringFormat("INSERT INTO character_transfer (AccountID, SourceRealm, SourceGuid, TargetRealm, Name, Dump) "
        "VALUES ({}, {}, {}, {}, '{}', '{}')", accountId, sRealmList->GetCurrentRealmId().Realm, character.GetCounter(),
        Battlenet::RealmHandle(destinationRealmAddress).Realm, escapedName, escapedDump).c_str());

    Player::DeleteFromDB(character, accountId, true, true);
    TC_LOG_INFO("entities.player", "Realm transfer: {} ({}) of account {} left for realm address {:X}", name, character.ToString(), accountId, destinationRealmAddress);
    return true;
}

AtLoginFlags GetShopServiceFlag(int32 service)
{
    switch (ShopService(service))
    {
        case ShopService::Rename:        return AT_LOGIN_RENAME;
        case ShopService::Customize:     return AT_LOGIN_CUSTOMIZE;
        case ShopService::RaceChange:    return AT_LOGIN_CHANGE_RACE;
        case ShopService::FactionChange: return AT_LOGIN_CHANGE_FACTION;
        default:                         return AT_LOGIN_NONE;
    }
}

// checked before the confirmation, so nothing is charged for a product that cannot be applied; player is null at character select
std::string_view CheckShopProduct(ShopProduct const& product, Player const* player)
{
    if (product.InGameOnly && !player)
        return "log in with the character to buy this";

    if (!player)
        return {};

    for (ShopProductAction const& action : product.Actions)
    {
        switch (action.Action)
        {
            case ShopAction::Spell:
                if (player->HasSpell(action.Param1))
                    return "this character already knows it";
                break;
            case ShopAction::Service:
                if ((ShopService(action.Param1) == ShopService::RaceChange || ShopService(action.Param1) == ShopService::FactionChange) && player->GetLevel() < 10)
                    return "the character must be level 10";
                break;
            case ShopAction::Profession:
                // only raises a profession the character already has (primary or secondary), it never takes a profession slot
                if (!player->HasSkill(action.Param1))
                    return "the character must learn this profession first";
                break;
            case ShopAction::Level:
                if (player->GetLevel() >= action.Param1)
                    return "the character is already at that level";
                break;
            case ShopAction::Title:
                if (player->HasTitle(sCharTitlesStore.LookupEntry(action.Param1)))
                    return "this character already has the title";
                break;
            default:
                break;
        }
    }
    return {};
}

// items are mailed to target, boost tokens go to the account; everything else is applied to player (in game only, see CheckShopProduct)
void ApplyShopProduct(ShopProduct const& product, Player* player, ObjectGuid target, uint32 accountId)
{
    for (ShopProductAction const& action : product.Actions)
        if (action.Action == ShopAction::BoostToken)
            LoginDatabase.DirectPExecute("INSERT INTO account_boost_token (AccountID, BoostType, Source) VALUES ({}, {}, 'shop product {}')",
                accountId, action.Param1, product.ID);

    // items by mail, MAX_MAIL_ITEMS per mail
    std::vector<std::pair<uint32, uint32>> stacks;
    for (ShopProductAction const& action : product.Actions)
    {
        if (action.Action != ShopAction::Item)
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(action.Param1);
        uint32 maxStack = proto ? std::max<uint32>(proto->GetMaxStackSize(), 1) : 1;
        for (uint32 left = action.Param2; left > 0; left -= std::min(left, maxStack))
            stacks.emplace_back(action.Param1, std::min(left, maxStack));
    }

    if (!stacks.empty())
    {
        Player* receiver = ObjectAccessor::FindConnectedPlayer(target);
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (std::size_t i = 0; i < stacks.size(); i += MAX_MAIL_ITEMS)
        {
            MailDraft draft("Shop purchase", Trinity::StringFormat("Thank you for buying {}.", product.Name));
            for (std::size_t j = i; j < std::min(stacks.size(), i + MAX_MAIL_ITEMS); ++j)
            {
                if (Item* item = Item::CreateItem(stacks[j].first, stacks[j].second, ItemContext::NONE, receiver))
                {
                    if (uint32 bonusList = GenerateItemRandomBonusListId(stacks[j].first))   // "of the X" items get a suffix like a drop would
                        item->SetItemRandomBonusList(bonusList);
                    item->SaveToDB(trans);
                    draft.AddItem(item);
                }
            }
            draft.SendMailTo(trans, MailReceiver(receiver, target.GetCounter()), MailSender(MAIL_NORMAL, UI64LIT(0), MAIL_STATIONERY_GM), MAIL_CHECK_MASK_NOT_RETURNABLE);
        }
        CharacterDatabase.CommitTransaction(trans);
    }

    if (!player)
        return;

    for (ShopProductAction const& action : product.Actions)
    {
        switch (action.Action)
        {
            case ShopAction::Gold:
                player->ModifyMoney(action.Param1);
                break;
            case ShopAction::Spell:
                player->LearnSpell(action.Param1, false);
                break;
            case ShopAction::Service:
            {
                AtLoginFlags flag = GetShopServiceFlag(action.Param1);
                player->SetAtLoginFlag(flag);
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
                stmt->setUInt16(0, uint16(flag));
                stmt->setUInt64(1, player->GetGUID().GetCounter());
                CharacterDatabase.Execute(stmt);
                break;
            }
            case ShopAction::Profession:
            {
                // every recipe of the profession this race and class can learn (both specializations included)
                uint32 skill = action.Param1;
                if (std::vector<SkillLineAbilityEntry const*> const* abilities = sDB2Manager.GetSkillLineAbilitiesBySkill(skill))
                {
                    for (SkillLineAbilityEntry const* ability : *abilities)
                    {
                        if (ability->ClassMask && !(ability->ClassMask & player->GetClassMask()))
                            continue;
                        if (!ability->RaceMask.IsEmpty() && !ability->RaceMask.HasRace(player->GetRace()))
                            continue;
                        if (!sSpellMgr->GetSpellInfo(ability->Spell, DIFFICULTY_NONE) || player->HasSpell(ability->Spell))
                            continue;
                        player->LearnSpell(ability->Spell, false, skill, true);
                    }
                }
                uint16 value = uint16(action.Param2);
                player->SetSkill(skill, std::max<uint16>(player->GetSkillStep(skill), std::max<uint16>(value / 75, 1)), value, value);
                break;
            }
            case ShopAction::Level:
                player->GiveLevel(uint8(action.Param1));
                player->SetXP(0);
                break;
            case ShopAction::Title:
                player->SetTitle(sCharTitlesStore.LookupEntry(action.Param1));
                break;
            case ShopAction::Reputation:
                player->SetReputation(action.Param1, action.Param2);
                break;
            default:
                break;
        }
    }
}
}

// Buy Now: answer with a purchase id, the purchase itself (the confirmation reads product, price and wallet name from it) and
// the confirmation request. Points are checked here and taken in the confirmation response.
void WorldSession::HandleBattlePayStartPurchase(WorldPackets::BattlePay::StartPurchase& startPurchase)
{
    // the client sends no target (also at character select): in game the player, at character select the last played character
    ObjectGuid target = _player ? _player->GetGUID() : startPurchase.TargetCharacter;
    if (target.IsEmpty())
        if (QueryResult result = CharacterDatabase.PQuery("SELECT guid FROM characters WHERE account = {} AND deleteDate IS NULL ORDER BY logout_time DESC LIMIT 1", GetAccountId()))
            target = ObjectGuid::Create<HighGuid::Player>((*result)[0].GetUInt64());

    StartShopPurchase(startPurchase.ClientToken, startPurchase.ProductID, target, 0);
}

// the character service window (product Type 15, realm transfer): the same purchase steps, for the picked character and realm
void WorldSession::HandleBattlePayStartVasPurchase(WorldPackets::BattlePay::StartVasPurchase& startVasPurchase)
{
    TC_LOG_INFO("network", "Shop: account {} asks for product {} for {} to realm address {:X}", GetAccountId(), startVasPurchase.ProductID,
        startVasPurchase.Character.ToString(), startVasPurchase.DestinationRealmAddress);
    StartShopPurchase(startVasPurchase.ClientToken, startVasPurchase.ProductID, startVasPurchase.Character, startVasPurchase.DestinationRealmAddress);
}

void WorldSession::StartShopPurchase(uint32 clientToken, uint32 productId, ObjectGuid target, uint32 transferRealmAddress)
{
    static std::atomic<uint64> nextPurchaseId{ uint64(GameTime::GetGameTime()) << 16 };

    WorldPackets::BattlePay::StartPurchaseResponse response;
    response.ClientToken = clientToken;
    response.PurchaseID = ++nextPurchaseId;

    Optional<ShopProduct> listingProduct;
    Optional<GoldListing> listing;
    ShopProduct const* product = FindShopProduct(productId, listingProduct, listing);
    uint32 points = GetShopPoints(GetAccountId());
    bool const isTransfer = product && std::ranges::any_of(product->Actions, [](ShopProductAction const& action) { return action.Action == ShopAction::RealmTransfer; });

    std::string_view error;
    if (!sConfigMgr->GetBoolDefault("Classic.Shop.Enabled", false) || !product)
        error = "the product is not available";
    else if (target.IsEmpty() || !IsLegitCharacterForAccount(target))
        error = "no character selected";
    else if (points < product->Price)
        error = "not enough shop points";
    else if (listing && listing->SellerAccount == GetAccountId())
        error = "this is your own listing";
    else if (isTransfer != (transferRealmAddress != 0))
        error = "pick the character and the realm in the transfer window";
    else if (isTransfer)
        error = CheckCharacterTransfer(GetAccountId(), target, transferRealmAddress);
    else
        error = CheckShopProduct(*product, _player);

    if (!error.empty())
    {
        TC_LOG_DEBUG("network", "Shop: account {} could not buy product {}: {} (points {})", GetAccountId(), productId, error, points);
        if (_player)
            ChatHandler(this).SendSysMessage(Trinity::StringFormat("Shop: {} (you have {} points, the price is {}).", error, points, product ? product->Price : 0));
        // purchase id 0: the client raises the purchase error; any other id makes it wait for a confirmation ("Connecting to Shop")
        response.PurchaseResult = ShopPurchaseResultError;
        response.PurchaseID = 0;
        SendPacket(response.Write());
        return;
    }

    ShopPendingPurchase& pending = _shopPendingPurchase.emplace();
    pending.PurchaseID = response.PurchaseID;
    pending.ProductID = product->ID;
    pending.ServerToken = urand(1, std::numeric_limits<int32>::max());
    pending.Target = target;
    pending.TimeCreated = GameTime::GetGameTime();
    pending.TransferRealmAddress = transferRealmAddress;

    SendPacket(response.Write());

    WorldPackets::BattlePay::PurchaseUpdate update;
    update.Purchases.push_back(MakeShopPurchase(pending.PurchaseID, *product, ShopPurchaseStatusPending, 0, pending.TimeCreated, points));
    SendPacket(update.Write());

    WorldPackets::BattlePay::ConfirmPurchase confirm;
    confirm.PurchaseID = pending.PurchaseID;
    confirm.ServerToken = pending.ServerToken;
    SendPacket(confirm.Write());
}

void WorldSession::HandleBattlePayConfirmPurchaseResponse(WorldPackets::BattlePay::ConfirmPurchaseResponse& confirmPurchaseResponse)
{
    if (!_shopPendingPurchase || _shopPendingPurchase->ServerToken != confirmPurchaseResponse.ServerToken)
        return;

    ShopPendingPurchase pending = *_shopPendingPurchase;
    _shopPendingPurchase.reset();

    Optional<ShopProduct> listingProduct;
    Optional<GoldListing> listing;
    ShopProduct const* product = FindShopProduct(pending.ProductID, listingProduct, listing);

    uint32 points = GetShopPoints(GetAccountId());
    if (!product)
    {
        // removed meanwhile (a Gold Market listing someone else bought): end the purchase with an error
        ShopProduct gone;
        gone.ID = pending.ProductID;
        if (_player && confirmPurchaseResponse.ConfirmPurchase)
            ChatHandler(this).SendSysMessage("Shop: this offer is no longer available.");
        WorldPackets::BattlePay::PurchaseUpdate update;
        update.Purchases.push_back(MakeShopPurchase(pending.PurchaseID, gone, ShopPurchaseStatusFinished,
            confirmPurchaseResponse.ConfirmPurchase ? ShopPurchaseResultError : 0, pending.TimeCreated, points));
        SendPacket(update.Write());
        SendShopProductList();
        return;
    }

    int32 result = 0;
    if (confirmPurchaseResponse.ConfirmPurchase)
    {
        // the player can not have changed character (in game -> character select logs out), but recheck what can change meanwhile
        if (points < product->Price || !IsLegitCharacterForAccount(pending.Target)
            || !(pending.TransferRealmAddress ? CheckCharacterTransfer(GetAccountId(), pending.Target, pending.TransferRealmAddress) : CheckShopProduct(*product, _player)).empty())
            result = ShopPurchaseResultError;
        // the transfer goes first: if the character can not be written out, nothing is charged
        else if (pending.TransferRealmAddress && !BeginCharacterTransfer(GetAccountId(), pending.Target, pending.TransferRealmAddress))
            result = ShopPurchaseResultError;
        else
        {
            if (listing)
            {
                // Gold Market: the listing is gone once bought; the seller's account gets the price minus the fee
                CharacterDatabase.DirectPExecute("DELETE FROM shop_gold_listing WHERE ID = {}", listing->ID);
                // fee percent from auth.shop_config (support site GM part), else the config; at least 1 point while the percent is not 0
                int32 feePercent = sConfigMgr->GetIntDefault("Classic.Shop.GoldMarketFee", 10);
                if (QueryResult feeResult = LoginDatabase.Query("SELECT Value FROM shop_config WHERE Name = 'GoldMarketFee'"))
                    feePercent = Trinity::StringTo<int32>((*feeResult)[0].GetStringView()).value_or(feePercent);
                feePercent = std::clamp(feePercent, 0, 100);
                uint32 fee = uint32(uint64(listing->Price) * feePercent / 100);
                if (feePercent && !fee)
                    fee = 1;
                if (listing->SellerAccount && listing->Price > fee)
                    LoginDatabase.PExecute("INSERT INTO account_shop_points (AccountID, Points) VALUES ({0}, {1}) ON DUPLICATE KEY UPDATE Points = Points + {1}",
                        listing->SellerAccount, listing->Price - fee);
                TC_LOG_INFO("entities.player", "Shop: Gold Market listing {} ({} gold, {} points, fee {}) of account {} sold to account {}",
                    listing->ID, listing->Gold, listing->Price, fee, listing->SellerAccount, GetAccountId());
            }

            LoginDatabase.DirectPExecute("UPDATE account_shop_points SET Points = Points - {} WHERE AccountID = {} AND Points >= {}",
                product->Price, GetAccountId(), product->Price);
            LoginDatabase.PExecute("INSERT INTO shop_purchase_log (AccountID, ProductID, Price, CharacterGUID) VALUES ({}, {}, {}, {})",
                GetAccountId(), product->ID, product->Price, pending.Target.GetCounter());
            points -= product->Price;

            ApplyShopProduct(*product, _player, pending.Target, GetAccountId());
            if (std::ranges::any_of(product->Actions, [](ShopProductAction const& action) { return action.Action == ShopAction::BoostToken; }))
                SendShopDistributionList();     // the new boost button on the character screen

            TC_LOG_INFO("entities.player", "Shop: account {} bought product {} ({}) for {} points for {}", GetAccountId(), product->ID, product->Name, product->Price, pending.Target.ToString());
            if (_player)
                ChatHandler(this).SendSysMessage(Trinity::StringFormat("Shop: thank you for buying {}. You have {} points left.", product->Name, points));
        }
    }

    WorldPackets::BattlePay::PurchaseUpdate update;
    update.Purchases.push_back(MakeShopPurchase(pending.PurchaseID, *product, ShopPurchaseStatusFinished, result, pending.TimeCreated, points));
    SendPacket(update.Write());

    if (listing)
        SendShopProductList();

    // realm transfer done: the character service window's "complete" screen (one per purchase), and the character list without it
    if (pending.TransferRealmAddress && confirmPurchaseResponse.ConfirmPurchase && !result)
    {
        WorldPackets::BattlePay::VasPurchaseComplete complete;
        complete.ProductID = product->ID;
        complete.Character = pending.Target;
        complete.DestinationRealmAddress = pending.TransferRealmAddress;
        if (QueryResult realm = LoginDatabase.PQuery("SELECT name FROM realmlist WHERE id = {}", Battlenet::RealmHandle(pending.TransferRealmAddress).Realm))
            complete.RealmName = (*realm)[0].GetString();
        SendPacket(complete.Write());
    }
}
