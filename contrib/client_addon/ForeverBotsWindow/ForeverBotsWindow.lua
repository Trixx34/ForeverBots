-- Forever Bots Window: add bots to the group and send them the chat commands from
-- docs/playerbots/feature-bot-chat-orders-20261008.md and feature-bot-control-commands-20261008.md.
-- Nothing here talks to the server in a special way: every button sends an ordinary chat line (party chat, or a whisper
-- to one bot), exactly what you would type. Open with /botwin.

local ADDON = ...
local Win = {}

-- Selectors the server understands (BotChat.h). "" = the whole group.
local SELECTORS = { "all", "tank", "healer", "dps", "g1", "g2" }

-- Pure helpers (also usable from a test): build the text of a command.
function Win.BuildLine(selector, verb, args)
    local parts = {}
    if selector and selector ~= "" then parts[#parts + 1] = selector end
    parts[#parts + 1] = verb
    if args and args ~= "" then parts[#parts + 1] = args end
    return table.concat(parts, " ")
end

function Win.CleanName(text)
    text = (text or ""):gsub("^%s+", ""):gsub("%s+$", "")
    if text == "" or text:find("%s") then return nil end
    return text
end

function Win.ParseYards(text)
    local n = tonumber(text)
    if not n or n ~= n or n < 3 or n > 40 then return nil end
    return n
end

-- Sending.
local state = { selector = "all", whisperTo = nil }

local function Send(verb, args)
    local line = Win.BuildLine(state.whisperTo and "" or state.selector, verb, args)
    if state.whisperTo then
        SendChatMessage(line, "WHISPER", nil, state.whisperTo)
    elseif IsInRaid() then
        SendChatMessage(line, "RAID")
    elseif IsInGroup() then
        SendChatMessage(line, "PARTY")
    else
        print("|cffff8800Bots:|r you are not in a group. Add a bot first.")
    end
end

local function Dot(cmd) SendChatMessage(cmd, "SAY") end

-- Window.
local frame = CreateFrame("Frame", "ForeverBotsWindowFrame", UIParent, "BasicFrameTemplateWithInset")
frame:SetSize(330, 470)
frame:SetPoint("CENTER")
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:Hide()
tinsert(UISpecialFrames, "ForeverBotsWindowFrame")
frame.TitleText:SetText("Bots")

local function Button(parent, text, x, y, w, onClick, tip)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    b:SetSize(w or 94, 22)
    b:SetPoint("TOPLEFT", x, y)
    b:SetText(text)
    b:SetScript("OnClick", onClick)
    if tip then
        b:SetScript("OnEnter", function(self) GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(tip, 1, 1, 1, 1, true); GameTooltip:Show() end)
        b:SetScript("OnLeave", GameTooltip_Hide)
    end
    return b
end

local function Label(text, y)
    local fs = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    fs:SetPoint("TOPLEFT", 14, y)
    fs:SetText(text)
    return fs
end

local function EditBox(x, y, w, default)
    local e = CreateFrame("EditBox", nil, frame, "InputBoxTemplate")
    e:SetSize(w, 20)
    e:SetPoint("TOPLEFT", x, y)
    e:SetAutoFocus(false)
    if default then e:SetText(default) end
    return e
end

-- 1. Group: add / remove bots.
Label("Group", -30)
local nameBox = EditBox(20, -50, 130)
Button(frame, "Invite", 160, -48, 70, function()
    local n = Win.CleanName(nameBox:GetText())
    if n then C_PartyInfo.InviteUnit(n) else print("|cffff8800Bots:|r type a bot name first.") end
end, "Invites the bot with that name. Bots accept (Bot.Invite.Enabled).")
Button(frame, "Kick", 236, -48, 70, function()
    local n = Win.CleanName(nameBox:GetText()) or (state.whisperTo)
    if n then C_PartyInfo.UninviteUnit(n) else print("|cffff8800Bots:|r type or pick a bot name first.") end
end)

Label("Find bots (needs Bot.LFG.Enabled)", -78)
local function Lfg(role)
    local n = Win.CleanName(nameBox:GetText())
    if not n then print("|cffff8800Bots:|r type the name of any bot first; it starts the search for the group."); return end
    SendChatMessage("lfg " .. role, "WHISPER", nil, n)
end
Button(frame, "I tank", 14, -98, 94, function() Lfg("tank") end)
Button(frame, "I heal", 118, -98, 94, function() Lfg("healer") end)
Button(frame, "I deal damage", 222, -98, 94, function() Lfg("dps") end)
Button(frame, "Search status", 14, -124, 148, function()
    local n = Win.CleanName(nameBox:GetText()); if n then SendChatMessage("lfg status", "WHISPER", nil, n) end
end)
Button(frame, "Stop search", 168, -124, 148, function()
    local n = Win.CleanName(nameBox:GetText()); if n then SendChatMessage("lfg off", "WHISPER", nil, n) end
end)

-- 2. Who gets the command.
Label("Orders go to", -156)
local target = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
target:SetPoint("TOPLEFT", 100, -156)
local function RefreshTarget()
    target:SetText(state.whisperTo and ("whisper to " .. state.whisperTo) or ("group: " .. state.selector))
end
for i, s in ipairs(SELECTORS) do
    Button(frame, s, 14 + ((i - 1) % 3) * 104, -176 - math.floor((i - 1) / 3) * 24, 94, function()
        state.selector, state.whisperTo = s, nil
        RefreshTarget()
    end, s == "all" and "Every bot in the group" or "Only that role / subgroup")
end
Button(frame, "Selected bot", 14, -226, 148, function()
    if UnitExists("target") and UnitIsPlayer("target") then
        state.whisperTo = UnitName("target"); RefreshTarget()
    else
        print("|cffff8800Bots:|r target a bot first.")
    end
end, "Whispers the orders to your current target only.")
RefreshTarget()

-- 3. Orders.
Label("Orders", -258)
local orders = {
    { "Follow", "follow" }, { "Stay", "stay" }, { "Stop", "stop" },
    { "Pull", "pull", nil, "Tanks pull your selected target." }, { "Heal", "heal", nil, "Healers heal your selected target." }, { "Revive", "revive" },
    { "Mount", "mount" }, { "Dismount", "dismount" }, { "Summon", "summon" },
    { "Release", "release" }, { "Rest", "rest" }, { "Status", "what are you doing?" },
}
for i, o in ipairs(orders) do
    Button(frame, o[1], 14 + ((i - 1) % 3) * 104, -278 - math.floor((i - 1) / 3) * 24, 94, function() Send(o[2], o[3]) end, o[4])
end

-- 4. Stance, role, distance, focus.
Label("Stance / role / focus", -380)
local row = {
    { "Aggressive", "stance", "aggressive" }, { "Defensive", "stance", "defensive" }, { "Passive", "stance", "passive" },
    { "Role: tank", "role", "tank" }, { "Role: healer", "role", "healer" }, { "Role: dps", "role", "dps" },
}
for i, o in ipairs(row) do
    Button(frame, o[1], 14 + ((i - 1) % 3) * 104, -400 - math.floor((i - 1) / 3) * 24, 94, function() Send(o[2], o[3]) end)
end
local dist = EditBox(20, -454, 40, "10")
Button(frame, "Distance", 70, -452, 80, function()
    local y = Win.ParseYards(dist:GetText())
    if y then Send("distance", tostring(y)) else print("|cffff8800Bots:|r distance is 3 to 40 yards.") end
end, "How far the bots stay from you (Bot.Chat.Control.Enabled).")
Button(frame, "Focus", 156, -452, 74, function() Send("focus") end, "Bots attack your selected target first.")
Button(frame, "Focus off", 236, -452, 74, function() Send("focus", "off") end)
frame:SetHeight(486)

SLASH_FOREVERBOTSWINDOW1 = "/botwin"
SlashCmdList["FOREVERBOTSWINDOW"] = function() frame:SetShown(not frame:IsShown()) end

ForeverBotsWindow = Win
