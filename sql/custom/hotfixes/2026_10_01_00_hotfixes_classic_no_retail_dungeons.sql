-- Classic 1.60: the retail LFGDungeons rows in the hotfix database (31 of them Midnight, expansion 11) were sent to the client
-- as hotfixes and showed up in the calendars dungeon/raid list. The clients own Forever data is the right list.
DELETE FROM `hotfix_data` WHERE `TableHash` = 2577119682 AND `RecordId` IN (SELECT `ID` FROM `lfg_dungeons`);
DELETE FROM `lfg_dungeons`;
