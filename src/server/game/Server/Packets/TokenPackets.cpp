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

#include "TokenPackets.h"
#include "PacketOperators.h"

namespace WorldPackets::Token
{
void CommerceTokenGetLog::Read()
{
    _worldPacket >> ClientToken;
}

WorldPacket const* CommerceTokenGetLogResponse::Write()
{
    _worldPacket << ClientToken;
    _worldPacket << Result;
    _worldPacket << Size<uint32>(AuctionableTokens);
    for (AuctionableTokenInfo const& auctionableTokenAuctionable : AuctionableTokens)
    {
        _worldPacket << auctionableTokenAuctionable.Id;
        _worldPacket << auctionableTokenAuctionable.LastUpdate;
        _worldPacket << auctionableTokenAuctionable.Status;
        _worldPacket << auctionableTokenAuctionable.Price;
        _worldPacket << auctionableTokenAuctionable.DurationLeft;
    }

    return &_worldPacket;
}

void CommerceTokenGetMarketPrice::Read()
{
    _worldPacket >> ClientToken;
}

WorldPacket const* CommerceTokenGetMarketPriceResponse::Write()
{
    _worldPacket << ClientToken;
    _worldPacket << Result;
    _worldPacket << Price;
    _worldPacket << ExpectedSecondsUntilSold;

    return &_worldPacket;
}
}

namespace WorldPackets::BattlePay
{
// strings are sent without terminator; bits are flushed at the end of each struct's bit block
static ByteBuffer& operator<<(ByteBuffer& data, DisplayInfo const& displayInfo)
{
    data << OptionalInit(displayInfo.FileDataID);
    data << OptionalInit(displayInfo.ModelSceneID);
    data << SizedString::BitsSize<10>(displayInfo.Name1);
    data << SizedString::BitsSize<10>(displayInfo.Name2);
    data << SizedString::BitsSize<13>(displayInfo.Name3);
    data << SizedString::BitsSize<13>(displayInfo.Tooltip);
    data << SizedString::BitsSize<13>(displayInfo.Instructions);
    data << OptionalInit(displayInfo.Flags);
    data << OptionalInit(displayInfo.OverrideTextColor);
    data << OptionalInit(displayInfo.OverrideTexture);
    data << OptionalInit(displayInfo.OverrideBackground);
    data << SizedString::BitsSize<13>(displayInfo.Disclaimer);
    data << SizedString::BitsSize<12>(displayInfo.NydusLink);
    data.FlushBits();

    data << Size<uint32>(displayInfo.Cards);
    data << int32(displayInfo.CardType);
    data << int32(displayInfo.BannerType);
    data << int32(displayInfo.ItemQuantity);
    if (displayInfo.FileDataID)
        data << uint32(*displayInfo.FileDataID);
    if (displayInfo.ModelSceneID)
        data << uint32(*displayInfo.ModelSceneID);
    data << SizedString::Data(displayInfo.Name1);
    data << SizedString::Data(displayInfo.Name2);
    data << SizedString::Data(displayInfo.Name3);
    data << SizedString::Data(displayInfo.Tooltip);
    data << SizedString::Data(displayInfo.Instructions);
    if (displayInfo.Flags)
        data << uint32(*displayInfo.Flags);
    if (displayInfo.OverrideTextColor)
        data << uint32(*displayInfo.OverrideTextColor);
    if (displayInfo.OverrideTexture)
        data << uint32(*displayInfo.OverrideTexture);
    if (displayInfo.OverrideBackground)
        data << uint32(*displayInfo.OverrideBackground);
    data << SizedString::Data(displayInfo.Disclaimer);
    data << SizedString::Data(displayInfo.NydusLink);

    for (DisplayCard const& card : displayInfo.Cards)
    {
        data << SizedString::BitsSize<10>(card.Title);
        data.FlushBits();
        data << uint32(card.CreatureDisplayInfoID);
        data << uint32(card.ModelSceneID);
        data << uint32(card.TransmogSetID);
        data << SizedString::Data(card.Title);
    }

    return data;
}

static ByteBuffer& operator<<(ByteBuffer& data, Deliverable const& deliverable)
{
    data << uint32(deliverable.DeliverableID);
    data << int32(deliverable.Type);
    data << uint32(deliverable.ItemID);
    data << uint32(deliverable.Quantity);
    data << uint32(deliverable.MountSpellID);
    data << uint32(deliverable.BattlePetCreatureID);
    data << int32(deliverable.BoostID);
    data << int32(deliverable.Flags);
    data << uint32(deliverable.TransmogItemModifiedAppearanceID);
    data << uint32(deliverable.TransmogSetID);
    data << uint32(deliverable.CharTitleID);
    data << uint32(deliverable.SpellItemEnchantmentID);
    data << uint32(deliverable.WarbandSceneID);
    data << SizedString::BitsSize<8>(deliverable.Name);
    data << Bits<1>(deliverable.AlreadyOwns);
    data << Bits<1>(false);             // HasPetResult
    data << Bits<7>(0);                 // Choices
    data << OptionalInit(deliverable.Display);
    data.FlushBits();

    data << SizedString::Data(deliverable.Name);
    if (deliverable.Display)
        data << *deliverable.Display;

    return data;
}

static ByteBuffer& operator<<(ByteBuffer& data, Product const& product)
{
    // reader 70338 0x91b440
    data << uint32(product.ProductID);
    data << uint64(product.NormalPriceFixedPoint);
    data << uint64(product.CurrentPriceFixedPoint);
    data << Size<uint32>(product.DeliverableIDs);
    data << int32(product.Type);
    data << int32(product.Flags);
    data << uint32(product.RequiredDeliverableID);
    data << Size<uint32>(product.BundledProductIDs);
    data << int32(product.Eligibility);
    data << uint64(product.PmtProductID);
    for (uint32 deliverableID : product.DeliverableIDs)
        data << uint32(deliverableID);
    for (uint32 productID : product.BundledProductIDs)
        data << uint32(productID);

    data << OptionalInit(product.Display);
    data.FlushBits();
    if (product.Display)
        data << *product.Display;

    return data;
}

static ByteBuffer& operator<<(ByteBuffer& data, ProductGroup const& group)
{
    data << uint32(group.GroupID);
    data << int32(group.IconFileDataID);
    data << uint8(group.DisplayType);
    data << int32(group.Ordering);
    data << int32(group.Flags);
    data << uint32(group.ParentGroupID);
    data << SizedString::BitsSize<8>(group.Name);
    data.WriteBits(group.DisabledDescription.empty() ? 0 : group.DisabledDescription.length() + 1, 24);
    data.FlushBits();

    data << SizedString::Data(group.Name);
    if (!group.DisabledDescription.empty())
    {
        data << SizedString::Data(group.DisabledDescription);
        data << uint8(0);
    }

    return data;
}

static ByteBuffer& operator<<(ByteBuffer& data, ShopEntry const& entry)
{
    data << uint32(entry.EntryID);
    data << uint32(entry.GroupID);
    data << uint32(entry.ProductID);
    data << int32(entry.Ordering);
    data << int32(entry.Flags);
    data << uint8(entry.BannerType);
    data << OptionalInit(entry.Display);
    data.FlushBits();

    if (entry.Display)
        data << *entry.Display;

    return data;
}

WorldPacket const* ProductListResponse::Write()
{
    _worldPacket << int32(Result);
    _worldPacket << uint32(CurrencyID);
    _worldPacket << Size<uint32>(Products);
    _worldPacket << Size<uint32>(Deliverables);
    _worldPacket << Size<uint32>(Groups);
    _worldPacket << Size<uint32>(Entries);

    for (Product const& product : Products)
        _worldPacket << product;
    for (Deliverable const& deliverable : Deliverables)
        _worldPacket << deliverable;
    for (ProductGroup const& group : Groups)
        _worldPacket << group;
    for (ShopEntry const& entry : Entries)
        _worldPacket << entry;

    return &_worldPacket;
}

// reader 70291 0x91e530
static ByteBuffer& operator<<(ByteBuffer& data, Purchase const& purchase)
{
    data << uint64(purchase.PurchaseID);
    data << int32(purchase.Status);
    data << int32(purchase.ResultCode);
    data << uint32(purchase.ProductID);
    data << uint64(purchase.BasePrice);
    data << uint64(purchase.UserPrice);
    data << int64(purchase.TimeCreated);
    data << SizedString::BitsSize<8>(purchase.WalletName);
    data.FlushBits();
    data << SizedString::Data(purchase.WalletName);

    return data;
}

WorldPacket const* PurchaseListResponse::Write()
{
    _worldPacket << int32(Result);
    _worldPacket << Size<uint32>(Purchases);
    for (Purchase const& purchase : Purchases)
        _worldPacket << purchase;

    return &_worldPacket;
}

WorldPacket const* PurchaseUpdate::Write()
{
    _worldPacket << Size<uint32>(Purchases);
    for (Purchase const& purchase : Purchases)
        _worldPacket << purchase;

    return &_worldPacket;
}

// writer 70058 0x8be640
void StartPurchase::Read()
{
    _worldPacket >> ClientToken;
    _worldPacket >> ProductID;
    _worldPacket >> TargetCharacter;
    _worldPacket >> Unknown;
    _worldPacket >> SizedString::BitsSize<6>(Unknown1);
    _worldPacket >> SizedString::BitsSize<12>(Unknown2);
    _worldPacket >> SizedString::BitsSize<7>(Unknown3);
    _worldPacket >> SizedString::Data(Unknown1);
    _worldPacket >> SizedString::Data(Unknown2);
    _worldPacket >> SizedString::Data(Unknown3);
}

// reader only (handler not captured): result and client token roles inferred from ACK_FAILED
WorldPacket const* StartPurchaseResponse::Write()
{
    _worldPacket << int32(PurchaseResult);
    _worldPacket << uint32(ClientToken);
    _worldPacket << uint64(PurchaseID);

    return &_worldPacket;
}

// handler 70291 0x2fb90f0: fires STORE_CONFIRM_PURCHASE when PurchaseID is the current purchase
WorldPacket const* ConfirmPurchase::Write()
{
    _worldPacket << uint64(PurchaseID);
    _worldPacket << uint32(ServerToken);

    return &_worldPacket;
}

// writer 70058 0x8bea40
void ConfirmPurchaseResponse::Read()
{
    _worldPacket >> ServerToken;
    _worldPacket >> ClientCurrentPriceFixedPoint;
    _worldPacket >> Bits<1>(ConfirmPurchase);
}

static ByteBuffer& operator<<(ByteBuffer& data, DistributionObject const& distribution)
{
    data << uint64(distribution.DistributionID);
    data << int32(distribution.Status);
    data << uint32(distribution.DeliverableID);
    data << distribution.LicenseGameAccountGUID;
    data << distribution.TargetPlayer;
    data << uint32(distribution.TargetNativeRealm);
    data << uint32(distribution.TargetVirtualRealm);
    data << uint64(distribution.PurchaseID);
    data << uint32(distribution.ManualReview);
    data << OptionalInit(distribution.Product);
    data << Bits<1>(distribution.Revoked);
    data.FlushBits();
    if (distribution.Product)
        data << *distribution.Product;

    return data;
}

WorldPacket const* DistributionListResponse::Write()
{
    _worldPacket << int32(Result);
    _worldPacket.WriteBits(Distributions.size(), 11);
    _worldPacket.FlushBits();

    for (DistributionObject const& distribution : Distributions)
        _worldPacket << distribution;

    return &_worldPacket;
}

void DistributionAssignToTarget::Read()
{
    _worldPacket >> ClientToken;
    _worldPacket >> DistributionID;
    _worldPacket >> TargetCharacter;
    _worldPacket >> ProductChoice;
}

WorldPacket const* StartDistributionAssignToTargetResponse::Write()
{
    _worldPacket << int32(Result);
    _worldPacket << uint32(ClientToken);
    _worldPacket << uint64(DistributionID);

    return &_worldPacket;
}

WorldPacket const* DistributionUpdate::Write()
{
    _worldPacket << Distribution;

    return &_worldPacket;
}

WorldPacket const* CharacterUpgradeState::Write()
{
    _worldPacket << Character;

    return &_worldPacket;
}

void GetVasAccountCharacterList::Read()
{
    _worldPacket >> Unknown1;
    _worldPacket >> Unknown2;
}

void GetVasTransferTargetRealmList::Read()
{
    _worldPacket >> Unknown1;
    _worldPacket >> Unknown2;
}

WorldPacket const* GetVasAccountCharacterListResult::Write()
{
    _worldPacket << uint32(ClientToken);
    _worldPacket << int32(Result);
    _worldPacket << int32(Unknown);
    _worldPacket << Size<uint32>(Characters);
    for (VasCharacter const& character : Characters)
    {
        // the client keys its character map by the second guid (70338 0x2E3E6B0)
        _worldPacket << character.WowAccountGuid;
        _worldPacket << character.Guid;
        _worldPacket << uint32(character.VirtualRealmAddress);
        _worldPacket << int8(character.Race);
        _worldPacket << int8(character.Class);
        _worldPacket << int8(character.Sex);
        _worldPacket << uint8(character.Level);
        _worldPacket << int64(0);
        _worldPacket << int32(0);
        _worldPacket << SizedString::BitsSize<6>(character.Name);
        _worldPacket << SizedString::BitsSize<9>(character.RealmName);
        _worldPacket.WriteBits(0, 9);           // unknown string, empty
        _worldPacket.FlushBits();
        _worldPacket << SizedString::Data(character.Name);
        _worldPacket << SizedString::Data(character.RealmName);
    }

    return &_worldPacket;
}

void StartVasPurchase::Read()
{
    _worldPacket >> ClientToken;
    _worldPacket >> ProductID;
    _worldPacket >> Character;
    _worldPacket >> CharacterRealmAddress;
    _worldPacket >> DestinationRealmAddress;
    for (ObjectGuid& guid : Guids)
        _worldPacket >> guid;
    _worldPacket >> SizedString::BitsSize<6>(NewName);
    _worldPacket >> SizedString::BitsSize<7>(GuildName1);
    _worldPacket >> SizedString::BitsSize<7>(GuildName2);
    _worldPacket >> SizedString::BitsSize<6>(Platform);
    _worldPacket >> SizedString::BitsSize<12>(Fingerprint);
    _worldPacket >> Bits<1>(Unknown);
    _worldPacket.ResetBitPos();
    _worldPacket >> SizedString::Data(NewName);
    _worldPacket >> SizedString::Data(GuildName1);
    _worldPacket >> SizedString::Data(GuildName2);
    _worldPacket >> SizedString::Data(Platform);
    _worldPacket >> SizedString::Data(Fingerprint);
}

WorldPacket const* VasPurchaseComplete::Write()
{
    _worldPacket << uint32(ProductID);
    _worldPacket << uint32(Unknown);
    _worldPacket << Character;
    _worldPacket << Guid2;
    _worldPacket << uint32(DestinationRealmAddress);
    _worldPacket << Guid3;
    _worldPacket << SizedString::BitsSize<6>(Name);
    _worldPacket << SizedString::BitsSize<9>(RealmName);
    _worldPacket.FlushBits();
    _worldPacket << SizedString::Data(Name);
    _worldPacket << SizedString::Data(RealmName);

    return &_worldPacket;
}

void RequestCharacterGuildFollowInfo::Read()
{
    _worldPacket >> Character;
    _worldPacket >> VirtualRealmAddress;
}

WorldPacket const* QueryGuildFollowInfoResponse::Write()
{
    _worldPacket << Character;
    _worldPacket << Bits<1>(HasInfo);         // with info: 9 bit realm name length, faction changed bit, u32, name (not sent)
    _worldPacket.FlushBits();

    return &_worldPacket;
}

WorldPacket const* GetVasTransferTargetRealmListResult::Write()
{
    _worldPacket << uint32(Unknown1);
    _worldPacket << int32(Unknown2);
    _worldPacket << int32(Unknown3);
    _worldPacket << Size<uint32>(Realms);
    for (VasTransferTargetRealm const& realm : Realms)
    {
        _worldPacket << uint32(realm.RealmAddress);
        for (int32 value : realm.Unknown)
            _worldPacket << int32(value);
        _worldPacket << SizedString::BitsSize<9>(realm.Name);
        _worldPacket.FlushBits();
        _worldPacket << SizedString::Data(realm.Name);
    }

    return &_worldPacket;
}

// writer 70058 0x8C6220 (order read from the writer, names guessed)
void DistributionAssignVas::Read()
{
    _worldPacket >> ClientToken;
    _worldPacket >> DistributionID;
    _worldPacket >> Character;
    _worldPacket >> SourceRealmAddress;
    _worldPacket >> Unknown1;
    _worldPacket >> DesiredSuperDistrictID;
    for (ObjectGuid& guid : Guids)
        _worldPacket >> guid;
    for (uint8& value : Unknown2)
        _worldPacket >> value;
    uint32 pairCount = 0;
    _worldPacket >> pairCount;
    _worldPacket >> Unknown3;
    if (pairCount > 100)
        throw ByteBufferException();
    Pairs.resize(pairCount);
    for (auto& [first, second] : Pairs)
        _worldPacket >> first >> second;
    _worldPacket >> SizedString::BitsSize<6>(Strings[0]);
    _worldPacket >> SizedString::BitsSize<9>(Strings[1]);
    _worldPacket >> SizedString::BitsSize<7>(Strings[2]);
    _worldPacket >> SizedString::BitsSize<7>(Strings[3]);
    for (bool& flag : Flags)
        _worldPacket >> Bits<1>(flag);
    _worldPacket.ResetBitPos();
    for (std::string& text : Strings)
        _worldPacket >> SizedString::Data(text);
}

WorldPacket const* DistributionAssignVasResponse::Write()
{
    _worldPacket << uint32(ClientToken);
    _worldPacket << int32(StoreError);
    _worldPacket << int32(VasPurchaseResult);

    return &_worldPacket;
}
}
