-- Classic 1.60.1.70009: the client picks its realm through the "super realm" address 70-1-70 (Region 70,
-- Battlegroup 1, realm id 70). Turn TrinityCores default realm (id 1) into that realm; set RealmID = 70 in
-- worldserver.conf to match. No-op when realm 1 does not exist (already converted).
UPDATE IGNORE `realmlist` SET `id`=70, `Region`=70, `Battlegroup`=1, `gamebuild`=70009 WHERE `id`=1;
